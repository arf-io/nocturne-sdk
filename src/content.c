#include <nocturne/content.h>
#include <nocturne/surface.h>

#include "canvas_internal.h"
#include "paint_internal.h"

NtResult nt_frame_from_cells(const NtAllocator *allocator, NtCellSize size,
                              const NtContentCell *cells, size_t count, NtFrame **out)
{
    if (!out || (count && !cells)) return NT_RESULT_INVALID_ARGUMENT;
    if (count > SIZE_MAX / sizeof(*cells)) return NT_RESULT_OUT_OF_RANGE;
    NtFrame *frame;
    NtResult result = nt_frame_create(allocator, size, &frame);
    if (result != NT_RESULT_OK) return result;
    NtSurfaceView view;
    result = nt_surface_root(frame, &view);
    for (size_t i = 0; i < count && result == NT_RESULT_OK; ++i) {
        const NtContentCell cell = cells[i];
        if (cell.columns) {
            result = nt_frame_set_glyph(frame, cell.position, cell.glyph, cell.columns,
                                         cell.style, cell.owner);
            continue;
        }
        const NtRgba fg = cell.style.foreground;
        if (cell.glyph.count || fg.red || fg.green || fg.blue || fg.alpha) {
            result = NT_RESULT_INVALID_ARGUMENT;
        } else if (cell.position.x < 0 || cell.position.y < 0 ||
                   (uint32_t)cell.position.x >= size.columns || (uint32_t)cell.position.y >= size.rows) {
            result = NT_RESULT_OUT_OF_RANGE;
        } else {
            result = nt_surface_fill(&view, (NtCellRect){cell.position, {1, 1}},
                                      cell.style.background, cell.owner);
        }
    }
    if (result == NT_RESULT_OK) *out = frame;
    else nt_frame_destroy(frame);
    return result;
}

static NtResult image_valid(const NtImageView *image, NtImageSampling sampling)
{
    if (!image || !image->pixels || !image->size.columns || !image->size.rows ||
        image->stride < image->size.columns ||
        (sampling.filter != NT_IMAGE_NEAREST && sampling.filter != NT_IMAGE_BILINEAR) ||
        !nt_address_valid(sampling.horizontal) || !nt_address_valid(sampling.vertical))
        return NT_RESULT_INVALID_ARGUMENT;
    const size_t limit = SIZE_MAX / sizeof(NtRgba);
    const size_t height = image->size.rows, width = image->size.columns;
    if (image->count > limit || width > limit || height - 1 > (limit - width) / image->stride)
        return NT_RESULT_OUT_OF_RANGE;
    return (height - 1) * image->stride + width <= image->count ?
           NT_RESULT_OK : NT_RESULT_INSUFFICIENT_BUFFER;
}

static uint32_t texel(int64_t coordinate, uint32_t size, NtPaintAddress address)
{
    if (coordinate < 0) return address == NT_PAINT_REPEAT ? size - 1 : 0;
    if (coordinate >= size) return address == NT_PAINT_REPEAT ? 0 : size - 1;
    return (uint32_t)coordinate;
}

static NtRgba image_pixel(const NtImageView *image, uint32_t x, uint32_t y)
{
    return image->pixels[(size_t)y * image->stride + x];
}

static double interpolate(double first, double last, double fraction)
{
    if (fraction == 0) return first;
    if (fraction == 1) return last;
    return fma(fraction, last - first, first);
}

static double interpolate_plane(const double values[4], double u, double v)
{
    /* If the row slopes cancel at this height, the entire sample has
     * the first column's value. Preserve that flat section through rounding. */
    if (u != 0 && v != 0 &&
        interpolate(values[1] - values[0], values[3] - values[2], v) == 0)
        return interpolate(values[0], values[2], v);
    return interpolate(interpolate(values[0], values[1], u),
                       interpolate(values[2], values[3], u), v);
}

static NtRgba bilinear(const NtImageView *image, NtImageSampling spec, double x, double y)
{
    const int64_t i = (int64_t)floor(x), j = (int64_t)floor(y);
    const uint32_t x0 = texel(i, image->size.columns, spec.horizontal);
    const uint32_t x1 = texel(i + 1, image->size.columns, spec.horizontal);
    const uint32_t y0 = texel(j, image->size.rows, spec.vertical);
    const uint32_t y1 = texel(j + 1, image->size.rows, spec.vertical);
    const double u = x0 == x1 ? 0 : x - (double)i;
    const double v = y0 == y1 ? 0 : y - (double)j;
    const NtRgba samples[] = {image_pixel(image, x0, y0), image_pixel(image, x1, y0),
                              image_pixel(image, x0, y1), image_pixel(image, x1, y1)};
    const bool active[] = {u < 1 && v < 1, u > 0 && v < 1, u < 1 && v > 0, u > 0 && v > 0};
    const double alphas[] = {samples[0].alpha, samples[1].alpha, samples[2].alpha, samples[3].alpha};
    const double alpha = interpolate_plane(alphas, u, v);
    const uint8_t opacity = (uint8_t)round(alpha);
    if (!opacity) return (NtRgba){0};
    double linear[3][4] = {{0}}, encoded[3][4] = {{0}};
    unsigned toe = 7;
    for (unsigned k = 0; k < 4; ++k) {
        const NtRgba color = samples[k];
        if (!active[k] || !color.alpha) continue;
        const uint8_t bytes[] = {color.red, color.green, color.blue};
        for (unsigned c = 0; c < 3; ++c) {
            if (bytes[c] > 10) toe &= ~(1u << c);
            linear[c][k] = color.alpha * nt_srgb_decode(bytes[c] / 255.0);
            encoded[c][k] = color.alpha * bytes[c];
        }
    }
    uint8_t rgb[3];
    for (unsigned c = 0; c < 3; ++c) {
        const double value = toe & (1u << c) ? interpolate_plane(encoded[c], u, v) / alpha :
                             255.0 * nt_srgb_encode(interpolate_plane(linear[c], u, v) / alpha);
        rgb[c] = (uint8_t)round(value);
    }
    return (NtRgba){rgb[0], rgb[1], rgb[2], opacity};
}

static NtRgba sample_image(const NtImageView *image, NtImageSampling spec, double u, double v)
{
    u = nt_address_coordinate(u, spec.horizontal);
    v = nt_address_coordinate(v, spec.vertical);
    const uint32_t width = image->size.columns, height = image->size.rows;
    if (spec.filter == NT_IMAGE_NEAREST) {
        const size_t i = nt_sample_interval(u, width), j = nt_sample_interval(v, height);
        return image_pixel(image, i < width ? (uint32_t)i : width - 1,
                           j < height ? (uint32_t)j : height - 1);
    }
    return bilinear(image, spec, u * width - 0.5, v * height - 0.5);
}

NtResult nt_image_sample(const NtImageView *image, NtImageSampling sampling,
                          double u, double v, NtRgba *out)
{
    if (!out || !isfinite(u) || !isfinite(v)) return NT_RESULT_INVALID_ARGUMENT;
    const NtResult result = image_valid(image, sampling);
    if (result == NT_RESULT_OK) *out = sample_image(image, sampling, u, v);
    return result;
}

static NtResult image_cell(NtFrame *frame, uint32_t x, uint32_t y, const NtImageView *image,
                            NtImageSampling spec, NtCellSize extent, NtSamplingMode mode,
                            NtOwnerToken owner)
{
    NtCellSamples samples = {0};
    for (unsigned i = 0; i < 8; ++i) {
        const uint64_t sx = 2*(uint64_t)x + i % 2, sy = 4*(uint64_t)y + i / 2;
        if (sx >= extent.columns || sy >= extent.rows) continue;
        const NtRgba color = sample_image(image, spec, ((double)sx + 0.5) / extent.columns,
                                                        ((double)sy + 0.5) / extent.rows);
        samples.pixels[i] = (NtCanvasPixel){color, owner, color.alpha != 0};
    }
    return nt_frame_write_samples(frame, (NtCellPoint){(int32_t)x, (int32_t)y}, &samples, mode);
}

NtResult nt_image_rasterize(const NtImageView *image, NtImageSampling sampling,
                             NtCellSize extent, NtSamplingMode mode, NtOwnerToken owner,
                             const NtAllocator *allocator, NtFrame **out)
{
    if (!out || !nt_sampling_valid(mode)) return NT_RESULT_INVALID_ARGUMENT;
    NtResult result = image_valid(image, sampling);
    if (result != NT_RESULT_OK) return result;
    const NtCellSize size = {extent.columns / 2 + (extent.columns % 2 != 0),
                             extent.rows / 4 + (extent.rows % 4 != 0)};
    NtFrame *frame;
    result = nt_frame_create(allocator, size, &frame);
    if (result != NT_RESULT_OK) return result;
    for (uint32_t y = 0; size.columns && y < size.rows; ++y) {
        for (uint32_t x = 0; x < size.columns; ++x) {
            result = image_cell(frame, x, y, image, sampling, extent, mode, owner);
            if (result != NT_RESULT_OK) {
                nt_frame_destroy(frame);
                return result;
            }
        }
    }
    *out = frame;
    return NT_RESULT_OK;
}
