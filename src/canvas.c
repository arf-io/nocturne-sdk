#include "canvas_internal.h"
#include "allocator.h"
#include "blend_internal.h"
#include "line_raster.h"

struct NtCanvas {
    NtAllocator allocator;
    NtCellSize size;
    NtCanvasPixel *pixels;
    NtRgba background;
};

NT_DECLARE_ALIGNMENT(NtCanvas);
NT_DECLARE_ALIGNMENT(NtCanvasPixel);

static size_t canvas_count(const NtCanvas *canvas)
{
    return (size_t)canvas->size.columns * canvas->size.rows;
}

NtResult nt_canvas_create(const NtAllocator *allocator, NtCellSize samples, NtCanvas **out)
{
    NtAllocator resolved;
    if (!out || !nt_allocator_get(allocator, &resolved)) return NT_RESULT_INVALID_ARGUMENT;
    const uint32_t limit = (uint32_t)INT32_MAX + 1;
    if (samples.columns > limit || samples.rows > limit ||
        (samples.columns && samples.rows > (SIZE_MAX / sizeof(NtCanvasPixel)) / samples.columns))
        return NT_RESULT_OUT_OF_RANGE;
    NtCanvas *canvas = nt_allocate(&resolved, sizeof(*canvas), NT_ALIGNMENT(NtCanvas));
    if (!canvas) return NT_RESULT_ALLOCATION_FAILED;
    *canvas = (NtCanvas){.allocator = resolved, .size = samples};
    const size_t count = canvas_count(canvas);
    if (count) {
        canvas->pixels = nt_allocate(&resolved, count * sizeof(*canvas->pixels),
                                     NT_ALIGNMENT(NtCanvasPixel));
        if (!canvas->pixels) {
            nt_canvas_destroy(canvas);
            return NT_RESULT_ALLOCATION_FAILED;
        }
        for (size_t i = 0; i < count; ++i) canvas->pixels[i] = (NtCanvasPixel){0};
    }
    *out = canvas;
    return NT_RESULT_OK;
}

void nt_canvas_destroy(NtCanvas *canvas)
{
    if (!canvas) return;
    const NtAllocator allocator = canvas->allocator;
    nt_release(&allocator, canvas->pixels, canvas_count(canvas) * sizeof(*canvas->pixels),
               NT_ALIGNMENT(NtCanvasPixel));
    nt_release(&allocator, canvas, sizeof(*canvas), NT_ALIGNMENT(NtCanvas));
}

NtCellSize nt_canvas_size(const NtCanvas *canvas)
{
    return canvas ? canvas->size : (NtCellSize){0};
}

NtResult nt_canvas_clear(NtCanvas *canvas, NtRgba background)
{
    if (!canvas) return NT_RESULT_INVALID_ARGUMENT;
    canvas->background = background;
    const size_t count = canvas_count(canvas);
    for (size_t i = 0; i < count; ++i) canvas->pixels[i] = (NtCanvasPixel){.color = background};
    return NT_RESULT_OK;
}

static bool on_canvas(const NtCanvas *canvas, int64_t x, int64_t y)
{
    return x >= 0 && x < canvas->size.columns && y >= 0 && y < canvas->size.rows;
}

static size_t pixel_index(const NtCanvas *canvas, int64_t x, int64_t y)
{
    return (size_t)y * canvas->size.columns + (size_t)x;
}

NtResult nt_canvas_pixel(const NtCanvas *canvas, NtCellPoint position, NtCanvasPixel *out)
{
    if (!canvas || !out) return NT_RESULT_INVALID_ARGUMENT;
    if (!on_canvas(canvas, position.x, position.y)) return NT_RESULT_OUT_OF_RANGE;
    *out = canvas->pixels[pixel_index(canvas, position.x, position.y)];
    return NT_RESULT_OK;
}

typedef struct CanvasDraw {
    NtCanvas *canvas;
    NtCanvasInk ink;
    bool check_only;
} CanvasDraw;

static NtResult draw_pixel(const CanvasDraw *draw, int64_t x, int64_t y)
{
    if (!on_canvas(draw->canvas, x, y)) return NT_RESULT_OK;
    const size_t index = pixel_index(draw->canvas, x, y);
    NtCanvasPixel *pixel = &draw->canvas->pixels[index];
    NtCompositeOptions options;
    const NtCompositeOptions *spec = draw->ink.composite;
    if (spec && spec->blend == NT_BLEND_DISSOLVE) {
        options = *spec;
        options.sample_index += (uint64_t)index;
        spec = &options;
    }
    NtRgba color;
    double source_weight, backdrop_weight;
    const NtResult result = nt_composite_pixel(pixel->color, draw->ink.color, spec, &color,
                                               &source_weight, &backdrop_weight);
    if (result != NT_RESULT_OK || draw->check_only) return result;
    const bool source = color.alpha && source_weight > 0.0;
    const bool backdrop = color.alpha && backdrop_weight > 0.0 && pixel->covered;
    const NtOwnerToken owner = source ? draw->ink.owner : backdrop ? pixel->owner : 0;
    *pixel = (NtCanvasPixel){color, owner, source || backdrop};
    return NT_RESULT_OK;
}

static int64_t min_i64(int64_t a, int64_t b)
{
    return a < b ? a : b;
}

static int64_t max_i64(int64_t a, int64_t b)
{
    return a > b ? a : b;
}

static NtResult line_pixel(const void *context, int64_t x, int64_t y)
{
    return draw_pixel(context, x, y);
}

static NtResult draw_line(const CanvasDraw *draw, NtCellPoint a, NtCellPoint b)
{
    return nt_line_raster(a, b, (NtCellRect){{0,0}, draw->canvas->size}, line_pixel, draw);
}

static int edge(NtCellPoint a, NtCellPoint b, NtCellPoint p)
{
    const int64_t ax = (int64_t)b.x - a.x, ay = (int64_t)b.y - a.y;
    const int64_t bx = (int64_t)p.x - a.x, by = (int64_t)p.y - a.y;
    /* Each product fits in uint64_t. Compare signs and magnitudes without
     * subtracting products whose exact difference could exceed 64 bits. */
    const uint64_t first = nt_line_magnitude(ax) * nt_line_magnitude(by);
    const uint64_t second = nt_line_magnitude(ay) * nt_line_magnitude(bx);
    const bool first_negative = first && ((ax < 0) != (by < 0));
    const bool second_negative = second && ((ay < 0) != (bx < 0));
    if (first_negative != second_negative) return first_negative ? -1 : 1;
    if (first == second) return 0;
    return (first > second) != first_negative ? 1 : -1;
}

static bool point_before(NtCellPoint a, NtCellPoint b)
{
    return a.x < b.x || (a.x == b.x && a.y < b.y);
}

static NtResult draw_triangle(const CanvasDraw *draw, NtCellPoint a, NtCellPoint b, NtCellPoint c)
{
    const int winding = edge(a, b, c);
    if (!winding) {
        NtCellPoint start = point_before(a, b) ? a : b;
        NtCellPoint end = point_before(a, b) ? b : a;
        if (point_before(c, start)) start = c;
        if (point_before(end, c)) end = c;
        return draw_line(draw, start, end);
    }
    const int64_t x0 = max_i64(0, min_i64(a.x, min_i64(b.x, c.x)));
    const int64_t y0 = max_i64(0, min_i64(a.y, min_i64(b.y, c.y)));
    const int64_t x1 = min_i64((int64_t)draw->canvas->size.columns - 1, max_i64(a.x, max_i64(b.x, c.x)));
    const int64_t y1 = min_i64((int64_t)draw->canvas->size.rows - 1, max_i64(a.y, max_i64(b.y, c.y)));
    for (int64_t y = y0; y <= y1; ++y) {
        for (int64_t x = x0; x <= x1; ++x) {
            const NtCellPoint p = {(int32_t)x, (int32_t)y};
            if (winding * edge(a, b, p) < 0 || winding * edge(b, c, p) < 0 ||
                winding * edge(c, a, p) < 0) continue;
            const NtResult result = draw_pixel(draw, x, y);
            if (result != NT_RESULT_OK) return result;
        }
    }
    return NT_RESULT_OK;
}

static NtResult draw_rectangle(const CanvasDraw *draw, NtCellRect rect, bool border)
{
    if (!rect.size.columns || !rect.size.rows) return NT_RESULT_OK;
    const int64_t left = rect.origin.x, top = rect.origin.y;
    const int64_t right = left + rect.size.columns - 1, bottom = top + rect.size.rows - 1;
    const int64_t x0 = max_i64(0, left), x1 = min_i64((int64_t)draw->canvas->size.columns - 1, right);
    const int64_t y0 = max_i64(0, top), y1 = min_i64((int64_t)draw->canvas->size.rows - 1, bottom);
    if (x0 > x1) return NT_RESULT_OK;
    for (int64_t y = y0; y <= y1; ++y) {
        if (border && y != top && y != bottom) {
            NtResult result = draw_pixel(draw, left, y);
            if (result != NT_RESULT_OK) return result;
            if (right != left) {
                result = draw_pixel(draw, right, y);
                if (result != NT_RESULT_OK) return result;
            }
        } else {
            for (int64_t x = x0; x <= x1; ++x) {
                const NtResult result = draw_pixel(draw, x, y);
                if (result != NT_RESULT_OK) return result;
            }
        }
    }
    return NT_RESULT_OK;
}

typedef enum ShapeKind { POINT, LINE, TRIANGLE, FILL, BORDER } ShapeKind;
typedef struct CanvasShape {
    ShapeKind kind;
    NtCellPoint a, b, c;
    NtCellRect rect;
} CanvasShape;

static NtResult render_shape(const CanvasDraw *draw, CanvasShape shape)
{
    switch (shape.kind) {
    case POINT: return draw_pixel(draw, shape.a.x, shape.a.y);
    case LINE: return draw_line(draw, shape.a, shape.b);
    case TRIANGLE: return draw_triangle(draw, shape.a, shape.b, shape.c);
    default: return draw_rectangle(draw, shape.rect, shape.kind == BORDER);
    }
}

static NtResult draw_shape(NtCanvas *canvas, CanvasShape shape, NtCanvasInk ink)
{
    if (!canvas || !nt_composite_options_valid(ink.composite)) return NT_RESULT_INVALID_ARGUMENT;
    if (!canvas_count(canvas)) return NT_RESULT_OK;
    CanvasDraw draw = {canvas, ink, false};
    if (ink.composite && ink.composite->gamut == NT_GAMUT_REJECT) {
        /* Rasterizers visit each sample at most once. Check fallible
         * conversion against the original state before mutating it. */
        draw.check_only = true;
        const NtResult result = render_shape(&draw, shape);
        if (result != NT_RESULT_OK) return result;
        draw.check_only = false;
    }
    return render_shape(&draw, shape);
}

NtResult nt_canvas_point(NtCanvas *canvas, NtCellPoint point, NtCanvasInk ink)
{
    return draw_shape(canvas, (CanvasShape){.kind = POINT, .a = point}, ink);
}

NtResult nt_canvas_line(NtCanvas *canvas, NtCellPoint a, NtCellPoint b, NtCanvasInk ink)
{
    return draw_shape(canvas, (CanvasShape){.kind = LINE, .a = a, .b = b}, ink);
}

NtResult nt_canvas_triangle(NtCanvas *canvas, NtCellPoint a, NtCellPoint b, NtCellPoint c,
                             NtCanvasInk ink)
{
    return draw_shape(canvas, (CanvasShape){.kind = TRIANGLE, .a = a, .b = b, .c = c}, ink);
}

NtResult nt_canvas_fill(NtCanvas *canvas, NtCellRect rect, NtCanvasInk ink)
{
    return draw_shape(canvas, (CanvasShape){.kind = FILL, .rect = rect}, ink);
}

NtResult nt_canvas_border(NtCanvas *canvas, NtCellRect rect, NtCanvasInk ink)
{
    return draw_shape(canvas, (CanvasShape){.kind = BORDER, .rect = rect}, ink);
}

static NtResult rasterize_cell(const NtCanvas *canvas, NtFrame *frame, uint32_t x, uint32_t y,
                               NtSamplingMode mode)
{
    NtCellSamples samples;
    for (unsigned i = 0; i < 8; ++i) {
        const int64_t px = 2 * (int64_t)x + i % 2, py = 4 * (int64_t)y + i / 2;
        samples.background[i] = canvas->background;
        samples.pixels[i] = on_canvas(canvas, px, py) ?
            canvas->pixels[pixel_index(canvas, px, py)] : (NtCanvasPixel){.color = canvas->background};
    }
    return nt_frame_write_samples(frame, (NtCellPoint){(int32_t)x, (int32_t)y}, &samples, mode);
}

NtResult nt_canvas_render(const NtCanvas *canvas, NtSamplingMode mode,
                           const NtAllocator *allocator, NtFrame **out)
{
    if (!canvas || !out || !nt_sampling_valid(mode)) return NT_RESULT_INVALID_ARGUMENT;
    const NtCellSize size = {(canvas->size.columns + 1) / 2, (canvas->size.rows + 3) / 4};
    NtFrame *frame;
    NtResult result = nt_frame_create(allocator, size, &frame);
    if (result != NT_RESULT_OK) return result;
    for (uint32_t y = 0; size.columns && y < size.rows; ++y) {
        for (uint32_t x = 0; x < size.columns; ++x) {
            result = rasterize_cell(canvas, frame, x, y, mode);
            if (result != NT_RESULT_OK) {
                nt_frame_destroy(frame);
                return result;
            }
        }
    }
    *out = frame;
    return NT_RESULT_OK;
}
