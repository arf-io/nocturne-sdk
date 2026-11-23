#ifndef NOCTURNE_INTERNAL_PAINT_H
#define NOCTURNE_INTERNAL_PAINT_H

#include <nocturne/paint_sample.h>

#include "color_internal.h"

NtResult nt_palette_validate(const NtPaletteRamp *ramp);

static inline bool nt_address_valid(NtPaintAddress mode)
{
    return mode >= NT_PAINT_PAD && mode <= NT_PAINT_REFLECT;
}

static inline double nt_address_coordinate(double coordinate, NtPaintAddress mode)
{
    if (mode == NT_PAINT_PAD) return fmin(fmax(coordinate, 0.0), 1.0);
    if (mode == NT_PAINT_REPEAT) return nt_wrap_positive(coordinate, 1.0);
    const double position = fabs(fmod(coordinate, 2.0));
    return position <= 1.0 ? position : 2.0 - position;
}

static inline size_t nt_sample_interval(double t, size_t segments)
{
    const double intervals = (double)segments;
    size_t index = (size_t)(t * intervals);
    /* Test the representable boundary if multiplication rounds across it. */
    if (t < (double)index / intervals)
        --index;
    else if (index < segments && t >= (double)(index + 1) / intervals)
        ++index;
    return index;
}

#endif
