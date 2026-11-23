/* Dot mapping and coverage/fallback policy are informed by native/src/sampling.c,
 * 53191655ec77. MIT, see LICENSE. */
#include "canvas_internal.h"
#include "color_internal.h"

#include <string.h>

NtResult nt_sampling_mode_parse(const char *name, NtSamplingMode *out)
{
    if (!name || !out) return NT_RESULT_INVALID_ARGUMENT;
    static const char *const names[] = {"cell", "half-block", "braille", "hybrid"};
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (!strcmp(name, names[i])) {
            *out = (NtSamplingMode)i;
            return NT_RESULT_OK;
        }
    }
    return NT_RESULT_UNSUPPORTED_CAPABILITY;
}

static NtRgba average(const NtRgba colors[8], unsigned selected)
{
    unsigned count = 0, alpha = 0;
    double linear[3] = {0};
    unsigned encoded[3] = {0}, linear_segment = 7;
    for (unsigned i = 0; i < 8; ++i) {
        if (!(selected & (1u << i))) continue;
        const NtRgba color = colors[i];
        const uint8_t channels[] = {color.red, color.green, color.blue};
        for (unsigned j = 0; j < 3; ++j) {
            if (color.alpha && channels[j] > 10) linear_segment &= ~(1u << j);
            encoded[j] += color.alpha * channels[j];
            linear[j] += color.alpha * nt_srgb_decode(channels[j] / 255.0);
        }
        alpha += color.alpha;
        ++count;
    }
    if (!alpha) return (NtRgba){0};
    uint8_t rgb[3];
    for (unsigned j = 0; j < 3; ++j) {
        rgb[j] = linear_segment & (1u << j) ? (uint8_t)((encoded[j] + alpha / 2) / alpha) :
                 (uint8_t)round(255.0 * nt_srgb_encode(linear[j] / alpha));
    }
    return (NtRgba){rgb[0], rgb[1], rgb[2], (uint8_t)((alpha + count / 2) / count)};
}

static unsigned contrast_mask(const NtRgba colors[8])
{
    enum { LUMINANCE_SCALE = 25500 };
    uint32_t values[8], sum = 0, deviation = 0;
    for (unsigned i = 0; i < 8; ++i) {
        const NtRgba c = colors[i];
        values[i] = (30u*c.red + 59u*c.green + 11u*c.blue) * c.alpha;
        sum += values[i];
    }
    unsigned mask = 0;
    for (unsigned i = 0; i < 8; ++i) {
        const uint32_t above_mean = 8 * values[i];
        deviation += above_mean > sum ? above_mean - sum : sum - above_mean;
        if (above_mean > sum + 8u * LUMINANCE_SCALE) mask |= 1u << i;
    }
    return deviation > 12u * 8u * LUMINANCE_SCALE && mask != 255 ? mask : 0;
}

static void set_glyph(NtSampledCell *cell, uint32_t scalar)
{
    if (scalar < 128) {
        cell->glyph[0] = (char)scalar;
        cell->glyph_size = 1;
    } else {
        cell->glyph[0] = (char)(0xe0u | (scalar >> 12));
        cell->glyph[1] = (char)(0x80u | ((scalar >> 6) & 0x3fu));
        cell->glyph[2] = (char)(0x80u | (scalar & 0x3fu));
        cell->glyph_size = 3;
    }
}

NtResult nt_cell_sample(const NtCellSamples *samples, NtSamplingMode mode, NtSampledCell *out)
{
    if (!samples || !out || !nt_sampling_valid(mode)) return NT_RESULT_INVALID_ARGUMENT;
    NtSampledCell cell = {0};
    NtRgba pixels[8];
    unsigned geometry = 0;
    for (unsigned i = 0; i < 8; ++i) {
        const NtCanvasPixel pixel = samples->pixels[i];
        pixels[i] = pixel.color;
        if (pixel.covered) {
            geometry |= 1u << i;
            if (!cell.owner && pixel.color.alpha) cell.owner = pixel.owner;
        }
    }
    unsigned dots = 0;
    if (mode == NT_SAMPLE_BRAILLE || mode == NT_SAMPLE_HYBRID)
        dots = geometry ? geometry : contrast_mask(pixels);
    if (dots) {
        static const unsigned braille_bits[] = {1, 8, 2, 16, 4, 32, 64, 128};
        unsigned braille = 0;
        for (unsigned i = 0; i < 8; ++i) {
            if (dots & (1u << i)) braille |= braille_bits[i];
        }
        set_glyph(&cell, UINT32_C(0x2800) + braille);
        cell.style.foreground = average(pixels, dots);
        cell.style.background = geometry ? average(samples->background, 255) :
                                           average(pixels, 255 ^ dots);
    } else if (mode == NT_SAMPLE_HALF_BLOCK || mode == NT_SAMPLE_HYBRID) {
        set_glyph(&cell, UINT32_C(0x2580));
        cell.style.foreground = average(pixels, 15);
        cell.style.background = average(pixels, 240);
    } else {
        set_glyph(&cell, ' ');
        cell.style.background = average(pixels, 255);
    }
    *out = cell;
    return NT_RESULT_OK;
}

NtResult nt_frame_write_sampled_cell(NtFrame *frame, NtCellPoint position, const NtSampledCell *cell)
{
    if (!cell->style.foreground.alpha && !cell->style.background.alpha) return NT_RESULT_OK;
    return nt_frame_set_glyph(frame, position, (NtByteView){cell->glyph, cell->glyph_size},
                              1, cell->style, cell->owner);
}

NtResult nt_frame_write_samples(NtFrame *frame, NtCellPoint position, const NtCellSamples *samples,
                                 NtSamplingMode mode)
{
    NtSampledCell cell;
    const NtResult result = nt_cell_sample(samples, mode, &cell);
    return result == NT_RESULT_OK ? nt_frame_write_sampled_cell(frame, position, &cell) : result;
}
