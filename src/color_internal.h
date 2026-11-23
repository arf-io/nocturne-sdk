#ifndef NOCTURNE_INTERNAL_COLOR_H
#define NOCTURNE_INTERNAL_COLOR_H

#include <nocturne/color.h>
#include <math.h>

static inline double nt_srgb_decode(double channel)
{
    return channel <= 0.04045 ? channel / 12.92 : pow((channel + 0.055) / 1.055, 2.4);
}

static inline double nt_srgb_encode(double channel)
{
    if (channel >= 1.0) return 1.0;
    return channel <= 0.0031308 ? 12.92 * channel : 1.055 * pow(channel, 1.0 / 2.4) - 0.055;
}

static inline bool nt_unit_interval(double value)
{
    return value >= 0.0 && value <= 1.0;
}

static inline bool nt_color_space_valid(NtColorSpace space)
{
    return space >= NT_COLOR_SRGB && space <= NT_COLOR_CMYK;
}

static inline bool nt_gamut_valid(NtGamutPolicy gamut)
{
    return gamut >= NT_GAMUT_CLIP && gamut <= NT_GAMUT_REJECT;
}

static inline bool nt_color_interpolation_valid(NtColorInterpolation spec)
{
    return nt_color_space_valid(spec.space) && nt_gamut_valid(spec.gamut) &&
           spec.hue >= NT_HUE_SHORTER && spec.hue <= NT_HUE_DECREASING &&
           (spec.alpha == NT_ALPHA_PREMULTIPLIED || spec.alpha == NT_ALPHA_STRAIGHT);
}

static inline double nt_wrap_positive(double value, double period)
{
    double result = fmod(value, period);
    if (result < 0.0) {
        result += period;
        /* Very small negative remainders can round up to the period. */
        if (result == period) result = nextafter(period, 0.0);
    }
    return result == 0.0 ? 0.0 : result;
}

#endif
