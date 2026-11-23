#ifndef NOCTURNE_COLOR_H
#define NOCTURNE_COLOR_H

#include <nocturne/base.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum NtColorSpace {
    NT_COLOR_SRGB,
    NT_COLOR_LINEAR_SRGB,
    NT_COLOR_HSL,
    NT_COLOR_HSV,
    NT_COLOR_OKLAB,
    NT_COLOR_OKLCH,
    NT_COLOR_CMYK
} NtColorSpace;

typedef struct NtColor {
    NtColorSpace space;
    double channel[4];
    double alpha;
} NtColor;

/* Channel order follows the space name. RGB, S/L/V, OK lightness, CMYK,
 * and straight alpha are in [0,1]. Hue is finite degrees modulo 360.
 * OKLab a/b are finite signed values and OKLCH chroma is finite and
 * nonnegative. The fourth channel is used only by CMYK. */

typedef enum NtGamutPolicy {
    NT_GAMUT_CLIP,
    NT_GAMUT_REDUCE_CHROMA,
    NT_GAMUT_REJECT
} NtGamutPolicy;

typedef enum NtHueInterpolation {
    NT_HUE_SHORTER,
    NT_HUE_LONGER,
    NT_HUE_INCREASING,
    NT_HUE_DECREASING
} NtHueInterpolation;

typedef enum NtAlphaInterpolation {
    NT_ALPHA_PREMULTIPLIED,
    NT_ALPHA_STRAIGHT
} NtAlphaInterpolation;

typedef struct NtColorInterpolation {
    NtColorSpace space;
    NtGamutPolicy gamut;
    NtHueInterpolation hue;
    NtAlphaInterpolation alpha;
} NtColorInterpolation;

/* Pure conversion. RGB-based destinations apply the gamut policy to the
 * linear-light RGB intermediate. CMYK uses a device-independent formula.
 * All result-pointer APIs preserve their outputs on error. */
NtResult nt_color_convert(NtColor source, NtColorSpace target, NtGamutPolicy gamut,
                          NtColor *out);
NtColor nt_color_from_rgba(NtRgba rgba);
NtResult nt_color_to_rgba(NtColor source, NtGamutPolicy gamut, NtRgba *out);

/* Both operands convert to interpolation.space. Progress is finite in
 * [0,1]. The result is straight alpha in that space. See the color
 * contract for powerless hue and transparent endpoint behavior. */
NtResult nt_color_mix(NtColor a, NtColor b, double progress,
                      NtColorInterpolation interpolation, NtColor *out);

typedef struct NtPaletteView {
    const NtRgba *colors;
    size_t count;
} NtPaletteView;

NtResult nt_palette_lookup(NtPaletteView palette, size_t index, NtRgba *out);

/* Nearest by squared distance in premultiplied linear sRGB plus alpha.
 * Equal distances select the lowest index. */
NtResult nt_palette_nearest(NtPaletteView palette, NtColor source, NtGamutPolicy gamut,
                            size_t *out_index);

#ifdef __cplusplus
}
#endif
#endif
