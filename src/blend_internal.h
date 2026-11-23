#ifndef NOCTURNE_INTERNAL_BLEND_H
#define NOCTURNE_INTERNAL_BLEND_H

#include <nocturne/blend.h>

bool nt_composite_options_valid(const NtCompositeOptions *options);
NtResult nt_composite_pixel(NtRgba backdrop, NtRgba source, const NtCompositeOptions *options,
                             NtRgba *out, double *source_weight, double *backdrop_weight);

#endif
