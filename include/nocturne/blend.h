#ifndef NOCTURNE_BLEND_H
#define NOCTURNE_BLEND_H

#include <nocturne/color.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum NtBlendMode {
    NT_BLEND_NORMAL,
    NT_BLEND_MULTIPLY, NT_BLEND_SCREEN, NT_BLEND_OVERLAY,
    NT_BLEND_DARKEN, NT_BLEND_LIGHTEN, NT_BLEND_COLOR_DODGE, NT_BLEND_COLOR_BURN,
    NT_BLEND_HARD_LIGHT, NT_BLEND_SOFT_LIGHT, NT_BLEND_DIFFERENCE, NT_BLEND_EXCLUSION,
    NT_BLEND_HUE, NT_BLEND_SATURATION, NT_BLEND_COLOR, NT_BLEND_LUMINOSITY,
    NT_BLEND_ADD, NT_BLEND_SUBTRACT, NT_BLEND_DIVIDE, NT_BLEND_LINEAR_BURN,
    NT_BLEND_LINEAR_LIGHT, NT_BLEND_VIVID_LIGHT, NT_BLEND_PIN_LIGHT, NT_BLEND_HARD_MIX,
    NT_BLEND_DARKER_COLOR, NT_BLEND_LIGHTER_COLOR, NT_BLEND_DISSOLVE
} NtBlendMode;

typedef enum NtCompositeOperator {
    NT_COMPOSITE_SOURCE_OVER,
    NT_COMPOSITE_SOURCE, NT_COMPOSITE_DESTINATION, NT_COMPOSITE_CLEAR,
    NT_COMPOSITE_SOURCE_IN, NT_COMPOSITE_SOURCE_OUT, NT_COMPOSITE_SOURCE_ATOP,
    NT_COMPOSITE_DESTINATION_OVER, NT_COMPOSITE_DESTINATION_IN,
    NT_COMPOSITE_DESTINATION_OUT, NT_COMPOSITE_DESTINATION_ATOP,
    NT_COMPOSITE_XOR, NT_COMPOSITE_LIGHTER
} NtCompositeOperator;

typedef struct NtCompositeOptions {
    NtBlendMode blend;
    NtCompositeOperator operation;
    NtColorSpace space;         /* sRGB, linear sRGB, OKLab or OKLCH. */
    NtGamutPolicy gamut;
    NtHueInterpolation hue;
    uint64_t seed, sample_index; /* Used for deterministic dissolve. */
} NtCompositeOptions;

/* Lowercase canonical names, with "none" also accepted for normal blend.
 * Unknown names return NT_RESULT_UNSUPPORTED_CAPABILITY. */
NtResult nt_blend_mode_parse(const char *name, NtBlendMode *out);
NtResult nt_composite_operator_parse(const char *name, NtCompositeOperator *out);

/* Evaluate the blend in encoded sRGB and mix surviving contributions in
 * options.space. Lighter adds premultiplied encoded sRGB energy. Return
 * straight color in the chosen space, or quantize once to RGBA8. A NULL
 * options pointer selects normal, source-over, linear sRGB and gamut clip.
 * Errors preserve the output. */
NtResult nt_color_composite(NtColor backdrop, NtColor source, const NtCompositeOptions *options,
                            NtColor *out);
NtResult nt_rgba_composite(NtRgba backdrop, NtRgba source, const NtCompositeOptions *options,
                           NtRgba *out);

#ifdef __cplusplus
}
#endif
#endif
