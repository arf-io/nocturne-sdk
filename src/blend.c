/* Mode inventory and formulas are informed by native/src/blend.c and
 * bindings/chromatic/chromatic-color.js, 53191655ec77. MIT, see LICENSE. */
#include "blend_internal.h"
#include "color_internal.h"
#include "wide_product_internal.h"

#include <string.h>

static const char *const blend_names[] = {
    "normal", "multiply", "screen", "overlay", "darken", "lighten", "color-dodge",
    "color-burn", "hard-light", "soft-light", "difference", "exclusion", "hue",
    "saturation", "color", "luminosity", "add", "subtract", "divide", "linear-burn",
    "linear-light", "vivid-light", "pin-light", "hard-mix", "darker-color", "lighter-color",
    "dissolve"
};

static const char *const operation_names[] = {
    "source-over", "source", "destination", "clear", "source-in", "source-out",
    "source-atop", "destination-over", "destination-in", "destination-out",
    "destination-atop", "xor", "lighter"
};

NtResult nt_blend_mode_parse(const char *name, NtBlendMode *out)
{
    if (!name || !out) return NT_RESULT_INVALID_ARGUMENT;
    if (!strcmp(name, "none")) {
        *out = NT_BLEND_NORMAL;
        return NT_RESULT_OK;
    }
    for (size_t i = 0; i < sizeof(blend_names) / sizeof(blend_names[0]); ++i) {
        if (!strcmp(name, blend_names[i])) {
            *out = (NtBlendMode)i;
            return NT_RESULT_OK;
        }
    }
    return NT_RESULT_UNSUPPORTED_CAPABILITY;
}

NtResult nt_composite_operator_parse(const char *name, NtCompositeOperator *out)
{
    if (!name || !out) return NT_RESULT_INVALID_ARGUMENT;
    for (size_t i = 0; i < sizeof(operation_names) / sizeof(operation_names[0]); ++i) {
        if (!strcmp(name, operation_names[i])) {
            *out = (NtCompositeOperator)i;
            return NT_RESULT_OK;
        }
    }
    return NT_RESULT_UNSUPPORTED_CAPABILITY;
}

bool nt_composite_options_valid(const NtCompositeOptions *options)
{
    return !options || (options->blend >= NT_BLEND_NORMAL && options->blend <= NT_BLEND_DISSOLVE &&
        options->operation >= NT_COMPOSITE_SOURCE_OVER && options->operation <= NT_COMPOSITE_LIGHTER &&
        (options->space == NT_COLOR_SRGB || options->space == NT_COLOR_LINEAR_SRGB ||
         options->space == NT_COLOR_OKLAB || options->space == NT_COLOR_OKLCH) &&
        nt_gamut_valid(options->gamut) && options->hue >= NT_HUE_SHORTER &&
        options->hue <= NT_HUE_DECREASING);
}

static double clip(double value)
{
    return fmin(1.0, fmax(0.0, value));
}

static double color_dodge(double b, double s)
{
    return !b ? 0.0 : s >= 1.0 ? 1.0 : fmin(1.0, b / (1.0 - s));
}

static double color_burn(double b, double s)
{
    return b >= 1.0 ? 1.0 : !s ? 0.0 : 1.0 - fmin(1.0, (1.0 - b) / s);
}

static double hard_light(double b, double s)
{
    return s <= 0.5 ? 2.0 * b * s : 1.0 - 2.0 * (1.0 - b) * (1.0 - s);
}

static double vivid_light(double b, double s)
{
    return s < 0.5 ? color_burn(b, 2.0 * s) : color_dodge(b, 2.0 * s - 1.0);
}

static double separable(NtBlendMode blend, double b, double s)
{
    switch (blend) {
    case NT_BLEND_MULTIPLY: return b * s;
    case NT_BLEND_SCREEN: return b + s - b * s;
    case NT_BLEND_OVERLAY: return hard_light(s, b);
    case NT_BLEND_DARKEN: return fmin(b, s);
    case NT_BLEND_LIGHTEN: return fmax(b, s);
    case NT_BLEND_COLOR_DODGE: return color_dodge(b, s);
    case NT_BLEND_COLOR_BURN: return color_burn(b, s);
    case NT_BLEND_HARD_LIGHT: return hard_light(b, s);
    case NT_BLEND_SOFT_LIGHT:
        if (s <= 0.5) return b - (1.0 - 2.0 * s) * b * (1.0 - b);
        return b + (2.0 * s - 1.0) *
            ((b <= 0.25 ? ((16.0 * b - 12.0) * b + 4.0) * b : sqrt(b)) - b);
    case NT_BLEND_DIFFERENCE: return fabs(b - s);
    case NT_BLEND_EXCLUSION: return b + s - 2.0 * b * s;
    case NT_BLEND_ADD: return fmin(1.0, b + s);
    case NT_BLEND_SUBTRACT: return fmax(0.0, b - s);
    case NT_BLEND_DIVIDE: return !s ? 1.0 : fmin(1.0, b / s);
    case NT_BLEND_LINEAR_BURN: return fmax(0.0, b + s - 1.0);
    case NT_BLEND_LINEAR_LIGHT: return clip(b + 2.0 * s - 1.0);
    case NT_BLEND_VIVID_LIGHT: return vivid_light(b, s);
    case NT_BLEND_PIN_LIGHT: return s < 0.5 ? fmin(b, 2.0 * s) : fmax(b, 2.0 * s - 1.0);
    case NT_BLEND_HARD_MIX: return fmin(b, s) >= 1.0 - fmax(b, s) ? 1.0 : 0.0;
    default: return s;
    }
}

static double luminosity(NtColor color)
{
    return 0.30 * color.channel[0] + 0.59 * color.channel[1] + 0.11 * color.channel[2];
}

static unsigned luminosity_bytes(const uint8_t rgb[3])
{
    return 30u * rgb[0] + 59u * rgb[1] + 11u * rgb[2];
}

static void expand_sum(double terms[12], unsigned *count, double value)
{
    unsigned kept = 0;
    for (unsigned i = 0; i < *count; ++i) {
        const double sum = value + terms[i];
        const double added = sum - value;
        const double tail = (value - (sum - added)) + (terms[i] - added);
        if (tail != 0.0) terms[kept++] = tail;
        value = sum;
    }
    if (value != 0.0) terms[kept++] = value;
    *count = kept;
}

static int luminosity_order(NtColor b, NtColor s, bool byte_channels)
{
    if (byte_channels) {
        uint8_t backdrop[3], source[3];
        for (unsigned i = 0; i < 3; ++i) {
            backdrop[i] = (uint8_t)round(255 * b.channel[i]);
            source[i] = (uint8_t)round(255 * s.channel[i]);
        }
        const unsigned a = luminosity_bytes(backdrop), c = luminosity_bytes(source);
        return (a > c) - (a < c);
    }
    /* Accumulate six exact products with error-free additions. The sign
     * of the largest surviving term is the sign of the luminosity delta. */
    static const double weights[] = {30, 59, 11};
    double terms[12];
    unsigned count = 0;
    for (unsigned i = 0; i < 3; ++i) {
        const double a = fma(b.channel[i], weights[i], 0.0);
        const double c = fma(s.channel[i], weights[i], 0.0);
        expand_sum(terms, &count, a);
        expand_sum(terms, &count, fma(b.channel[i], weights[i], -a));
        expand_sum(terms, &count, -c);
        expand_sum(terms, &count, -fma(s.channel[i], weights[i], -c));
    }
    return !count ? 0 : terms[count - 1] < 0.0 ? -1 : 1;
}

static double saturation(NtColor color)
{
    const double *c = color.channel;
    return fmax(c[0], fmax(c[1], c[2])) - fmin(c[0], fmin(c[1], c[2]));
}

static NtColor with_saturation(NtColor color, double amount)
{
    double *c = color.channel;
    const double low = fmin(c[0], fmin(c[1], c[2]));
    const double range = saturation(color);
    for (size_t i = 0; i < 3; ++i) c[i] = range ? ((c[i] - low) / range) * amount : 0.0;
    return color;
}

static NtColor with_luminosity(NtColor color, double target)
{
    double *c = color.channel;
    const double change = target - luminosity(color);
    for (size_t i = 0; i < 3; ++i) c[i] += change;
    const double low = fmin(c[0], fmin(c[1], c[2])), high = fmax(c[0], fmax(c[1], c[2]));
    for (size_t i = 0; i < 3; ++i) {
        if (low < 0.0) c[i] = target + (c[i] - target) * (target / (target - low));
        if (high > 1.0) c[i] = target + (c[i] - target) * ((1.0 - target) / (high - target));
        c[i] = clip(c[i]);
    }
    return color;
}

static NtColor blended_color(NtColor b, NtColor s, NtBlendMode mode, bool byte_channels)
{
    if (mode == NT_BLEND_HARD_MIX && byte_channels) {
        for (size_t i = 0; i < 3; ++i)
            s.channel[i] = round(255 * b.channel[i]) + round(255 * s.channel[i]) >= 255;
        return s;
    }
    switch (mode) {
    case NT_BLEND_HUE: return with_luminosity(with_saturation(s, saturation(b)), luminosity(b));
    case NT_BLEND_SATURATION: return with_luminosity(with_saturation(b, saturation(s)), luminosity(b));
    case NT_BLEND_COLOR: return with_luminosity(s, luminosity(b));
    case NT_BLEND_LUMINOSITY: return with_luminosity(b, luminosity(s));
    case NT_BLEND_DARKER_COLOR:
        return luminosity_order(b, s, byte_channels) <= 0 ? b : s;
    case NT_BLEND_LIGHTER_COLOR:
        return luminosity_order(b, s, byte_channels) >= 0 ? b : s;
    default:
        for (size_t i = 0; i < 3; ++i)
            s.channel[i] = clip(separable(mode, b.channel[i], s.channel[i]));
        return s;
    }
}

typedef struct AlphaFactors { double source, backdrop; } AlphaFactors;

static AlphaFactors factors(NtCompositeOperator operation, double b, double s, double unit)
{
    switch (operation) {
    case NT_COMPOSITE_SOURCE: return (AlphaFactors){unit, 0};
    case NT_COMPOSITE_DESTINATION: return (AlphaFactors){0, unit};
    case NT_COMPOSITE_CLEAR: return (AlphaFactors){0, 0};
    case NT_COMPOSITE_SOURCE_IN: return (AlphaFactors){b, 0};
    case NT_COMPOSITE_SOURCE_OUT: return (AlphaFactors){unit - b, 0};
    case NT_COMPOSITE_SOURCE_ATOP: return (AlphaFactors){b, unit - s};
    case NT_COMPOSITE_DESTINATION_OVER: return (AlphaFactors){unit - b, unit};
    case NT_COMPOSITE_DESTINATION_IN: return (AlphaFactors){0, s};
    case NT_COMPOSITE_DESTINATION_OUT: return (AlphaFactors){0, unit - s};
    case NT_COMPOSITE_DESTINATION_ATOP: return (AlphaFactors){unit - b, s};
    case NT_COMPOSITE_XOR: return (AlphaFactors){unit - b, unit - s};
    case NT_COMPOSITE_LIGHTER: return (AlphaFactors){unit, unit};
    default: return (AlphaFactors){unit, unit - s};
    }
}

static uint64_t dissolve_sample(uint64_t seed, uint64_t index)
{
    uint64_t x = seed + UINT64_C(0x9e3779b97f4a7c15) * (index + 1);
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return (x ^ (x >> 31)) >> 11;
}

static NtResult composite(NtColor backdrop, NtColor source, const NtCompositeOptions *options,
                          bool byte_channels, NtColor *out, AlphaFactors *contributions)
{
    const NtCompositeOptions defaults = {.space = NT_COLOR_LINEAR_SRGB};
    if (!options) options = &defaults;
    if (!nt_composite_options_valid(options)) return NT_RESULT_INVALID_ARGUMENT;
    NtColor b, s;
    NtResult result = nt_color_convert(backdrop, NT_COLOR_SRGB, options->gamut, &b);
    if (result != NT_RESULT_OK) return result;
    result = nt_color_convert(source, NT_COLOR_SRGB, options->gamut, &s);
    if (result != NT_RESULT_OK) return result;

    NtBlendMode mode = options->blend;
    if (mode == NT_BLEND_DISSOLVE) {
        const double sample = (double)dissolve_sample(options->seed, options->sample_index) * 0x1p-53;
        s.alpha = sample < s.alpha ? 1.0 : 0.0;
        mode = NT_BLEND_NORMAL;
    }
    const double ab = b.alpha, as = s.alpha;
    const AlphaFactors factor = factors(options->operation, ab, as, 1);
    const bool source_atop = options->operation == NT_COMPOSITE_SOURCE_ATOP;
    const bool destination_atop = options->operation == NT_COMPOSITE_DESTINATION_ATOP;
    const bool atop = source_atop || destination_atop;
    /* Cancel the shared alpha in atop before multiplying subnormal weights. */
    const double ws = source_atop ? as : destination_atop ? 1.0 - ab : as * factor.source;
    const double wb = source_atop ? 1.0 - as : destination_atop ? ab : ab * factor.backdrop;
    const double alpha = source_atop ? ab : destination_atop ? as : ws + wb;
    *contributions = alpha ? (AlphaFactors){ws, wb} : (AlphaFactors){0, 0};
    if (!alpha) {
        *out = (NtColor){.space = options->space};
        return NT_RESULT_OK;
    }

    if (ws && mode != NT_BLEND_NORMAL && ab) {
        const NtColor blended = blended_color(b, s, mode, byte_channels);
        for (size_t i = 0; i < 3; ++i)
            s.channel[i] = clip(s.channel[i] + ab * (blended.channel[i] - s.channel[i]));
    }
    if (!ws || !wb) {
        NtColor endpoint = ws ? s : b;
        endpoint.alpha = clip(alpha);
        return nt_color_convert(endpoint, options->space, options->gamut, out);
    }
    if (options->operation == NT_COMPOSITE_LIGHTER) {
        const double capped = clip(alpha);
        for (size_t i = 0; i < 3; ++i)
            s.channel[i] = clip((ws * s.channel[i] + wb * b.channel[i]) / capped);
        s.alpha = capped;
        return nt_color_convert(s, options->space, options->gamut, out);
    }
    b.alpha = s.alpha = clip(alpha);
    const NtColorInterpolation mix = {options->space, options->gamut, options->hue, NT_ALPHA_STRAIGHT};
    return nt_color_mix(b, s, atop ? ws : ws / alpha, mix, out);
}

NtResult nt_color_composite(NtColor backdrop, NtColor source, const NtCompositeOptions *options,
                            NtColor *out)
{
    if (!out) return NT_RESULT_INVALID_ARGUMENT;
    AlphaFactors contributions;
    return composite(backdrop, source, options, false, out, &contributions);
}

typedef struct ByteRatio { uint64_t numerator, denominator; } ByteRatio;

static void nonseparable_ratios(NtBlendMode mode, const uint8_t b[3], const uint8_t s[3],
                                 ByteRatio out[3])
{
    const bool from_backdrop = mode == NT_BLEND_SATURATION || mode == NT_BLEND_LUMINOSITY;
    const uint8_t *base = from_backdrop ? b : s;
    const uint8_t *chroma = from_backdrop ? s : b;
    const int64_t light = luminosity_bytes(mode == NT_BLEND_LUMINOSITY ? s : b);
    int64_t c[3] = {base[0], base[1], base[2]}, denominator = 1;
    if (mode == NT_BLEND_HUE || mode == NT_BLEND_SATURATION) {
        int64_t low = 255, high = 0, chroma_low = 255, chroma_high = 0;
        for (unsigned i = 0; i < 3; ++i) {
            if (base[i] < low) low = base[i];
            if (base[i] > high) high = base[i];
            if (chroma[i] < chroma_low) chroma_low = chroma[i];
            if (chroma[i] > chroma_high) chroma_high = chroma[i];
        }
        if (high != low) denominator = high - low;
        for (unsigned i = 0; i < 3; ++i) c[i] = (base[i] - low) * (chroma_high - chroma_low);
    }
    const int64_t target = light * denominator;
    const int64_t change = target - (30*c[0] + 59*c[1] + 11*c[2]);
    denominator *= 100;
    const int64_t cap = 255 * denominator;
    int64_t low = cap, high = 0;
    for (unsigned i = 0; i < 3; ++i) {
        c[i] = 100*c[i] + change;
        if (c[i] < low) low = c[i];
        if (c[i] > high) high = c[i];
    }
    for (unsigned i = 0; i < 3; ++i) {
        int64_t numerator = c[i], divisor = denominator;
        if (low < 0) {
            numerator = target * (c[i] - low);
            divisor *= target - low;
        } else if (high > cap) {
            numerator = target * (high - target) + (c[i] - target) * (cap - target);
            divisor *= high - target;
        }
        out[i] = (ByteRatio){(uint64_t)numerator, (uint64_t)divisor};
    }
}

static unsigned blend_denominator(NtBlendMode mode, uint8_t b, uint8_t s)
{
    switch (mode) {
    case NT_BLEND_MULTIPLY: case NT_BLEND_SCREEN: case NT_BLEND_OVERLAY:
    case NT_BLEND_HARD_LIGHT: case NT_BLEND_EXCLUSION: return 255;
    case NT_BLEND_COLOR_DODGE: return s == 255 ? 1 : 255u - s;
    case NT_BLEND_COLOR_BURN: case NT_BLEND_DIVIDE: return s ? s : 1;
    case NT_BLEND_VIVID_LIGHT:
        return s < 128 ? (s ? 2u*s : 1u) : (s == 255 ? 1u : 2u*(255u-s));
    case NT_BLEND_SOFT_LIGHT:
        if (s < 128) return 65025;
        return b <= 63 ? 16581375u : b == 255 ? 1u : 0u;
    default: return 1;
    }
}

static void blend_ratios(NtBlendMode mode, NtRgba b, NtRgba s, ByteRatio out[3])
{
    const uint8_t backdrop[] = {b.red,b.green,b.blue}, source[] = {s.red,s.green,s.blue};
    if (mode >= NT_BLEND_HUE && mode <= NT_BLEND_LUMINOSITY) {
        nonseparable_ratios(mode, backdrop, source, out);
        return;
    }
    const NtColor color = blended_color(nt_color_from_rgba(b), nt_color_from_rgba(s), mode, true);
    for (unsigned i = 0; i < 3; ++i) {
        const unsigned divisor = blend_denominator(mode, backdrop[i], source[i]);
        /* The rational denominators above are bounded by 255 cubed.
         * Recover their integer numerators without redoing the blend. */
        out[i] = (ByteRatio){(uint64_t)round(color.channel[i] * 255 * divisor), divisor};
    }
}

static bool product_at_least(uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
    const NtWideProduct x = nt_wide_product(a,b), y = nt_wide_product(c,d);
    return x.high != y.high ? x.high > y.high : x.low >= y.low;
}

static uint8_t packed_channel(uint8_t b, uint8_t s, uint8_t converted, NtCompositeOptions spec,
                               uint32_t wb, uint32_t ws, uint32_t total, uint8_t ab,
                               ByteRatio blend)
{
    const bool encoded = spec.operation == NT_COMPOSITE_LIGHTER || spec.space == NT_COLOR_SRGB;
    if (!encoded && spec.space != NT_COLOR_LINEAR_SRGB) return converted;
    if (spec.blend == NT_BLEND_NORMAL || !ab || !ws) {
        if (!encoded && (b > 10 || s > 10)) return converted;
        const uint32_t byte = (wb * b + ws * s + total / 2) / total;
        return byte > 255 ? 255 : (uint8_t)byte;
    }
    if (!blend.denominator) return converted;
    const uint64_t source = (uint64_t)(255u - ab) * s * blend.denominator + ab * blend.numerator;
    if (!encoded && (b > 10 || source > 2550 * blend.denominator)) return converted;

    const uint64_t baseline = (uint64_t)255u * wb * b + (uint64_t)ws * (255u - ab) * s;
    const uint64_t coefficient = ws * ab, denominator = total * 255u;
    const double byte = (baseline + coefficient * ((double)blend.numerator / blend.denominator)) /
                        denominator;
    if (byte >= 255.0) return 255;
    const unsigned lower = (unsigned)byte;
    const uint64_t midpoint = (2u*lower + 1u) * denominator;
    const bool round_up = midpoint <= 2 * baseline ||
        product_at_least(2 * coefficient, blend.numerator,
                         midpoint - 2 * baseline, blend.denominator);
    return (uint8_t)(lower + round_up);
}

NtResult nt_composite_pixel(NtRgba backdrop, NtRgba source, const NtCompositeOptions *options,
                             NtRgba *out, double *source_weight, double *backdrop_weight)
{
    if (!nt_composite_options_valid(options)) return NT_RESULT_INVALID_ARGUMENT;
    NtCompositeOptions spec = options ? *options : (NtCompositeOptions){.space = NT_COLOR_LINEAR_SRGB};
    if (spec.blend == NT_BLEND_DISSOLVE) {
        const uint64_t sample = dissolve_sample(spec.seed, spec.sample_index);
        source.alpha = sample * 255 < (uint64_t)source.alpha * (UINT64_C(1) << 53) ? 255 : 0;
        spec.blend = NT_BLEND_NORMAL;
    }
    NtColor color;
    AlphaFactors contributions;
    NtResult result = composite(nt_color_from_rgba(backdrop), nt_color_from_rgba(source),
                                &spec, true, &color, &contributions);
    if (result != NT_RESULT_OK) return result;
    NtRgba encoded;
    result = nt_color_to_rgba(color, spec.gamut, &encoded);
    if (result != NT_RESULT_OK) return result;
    /* Byte alpha factors have denominator 255. Keep the rational weights
     * through quantization where the encoded channel algebra is linear. */
    const AlphaFactors scale = factors(spec.operation, backdrop.alpha, source.alpha, 255);
    const uint32_t ws = (uint32_t)scale.source * source.alpha;
    const uint32_t wb = (uint32_t)scale.backdrop * backdrop.alpha;
    const uint32_t total = ws + wb > 65025u ? 65025u : ws + wb;
    encoded.alpha = (uint8_t)((total + 127u) / 255u);
    if (encoded.alpha) {
        ByteRatio blend[3] = {0};
        if (spec.blend != NT_BLEND_NORMAL && ws && backdrop.alpha)
            blend_ratios(spec.blend, backdrop, source, blend);
        encoded.red = packed_channel(backdrop.red, source.red, encoded.red, spec, wb, ws, total,
                                     backdrop.alpha, blend[0]);
        encoded.green = packed_channel(backdrop.green, source.green, encoded.green, spec, wb, ws, total,
                                       backdrop.alpha, blend[1]);
        encoded.blue = packed_channel(backdrop.blue, source.blue, encoded.blue, spec, wb, ws, total,
                                      backdrop.alpha, blend[2]);
    } else {
        encoded = (NtRgba){0};
    }
    *source_weight = contributions.source;
    *backdrop_weight = contributions.backdrop;
    *out = encoded;
    return NT_RESULT_OK;
}

NtResult nt_rgba_composite(NtRgba backdrop, NtRgba source, const NtCompositeOptions *options,
                           NtRgba *out)
{
    if (!out) return NT_RESULT_INVALID_ARGUMENT;
    double source_weight, backdrop_weight;
    return nt_composite_pixel(backdrop, source, options, out, &source_weight, &backdrop_weight);
}
