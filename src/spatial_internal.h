#ifndef NOCTURNE_INTERNAL_SPATIAL_H
#define NOCTURNE_INTERNAL_SPATIAL_H

#include <nocturne/spatial.h>
#include "fp_environment_internal.h"
#include <math.h>

static inline bool nt_spatial_finite_vec(NtVec3 value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

#endif
