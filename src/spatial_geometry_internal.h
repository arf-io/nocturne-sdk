#ifndef NOCTURNE_INTERNAL_SPATIAL_GEOMETRY_H
#define NOCTURNE_INTERNAL_SPATIAL_GEOMETRY_H

#include "spatial_exact_internal.h"
#include <nocturne/spatial.h>

typedef struct NtSpatialFraction {
    NtSpatialExact numerator, divisor;
} NtSpatialFraction;

static inline void nt_spatial_fraction_set(NtSpatialFraction *out, double value)
{
    nt_spatial_exact_number(&out->numerator,value,0);
    nt_spatial_exact_number(&out->divisor,1,0);
}

static inline void nt_spatial_fraction_copy(NtSpatialFraction *out, const NtSpatialFraction *at)
{
    nt_spatial_exact_copy(&out->numerator,&at->numerator);
    nt_spatial_exact_copy(&out->divisor,&at->divisor);
}

static inline int nt_spatial_fraction_order(const NtSpatialFraction *a, const NtSpatialFraction *b)
{
    return nt_spatial_compare_ratios(&a->numerator,&a->divisor,&b->numerator,&b->divisor);
}

/* Intersect the current interval with a closed linear half-space given
 * by the signed scores at the original endpoints. */
static inline bool nt_spatial_clip_fraction(NtSpatialFraction *first, NtSpatialFraction *last,
                                            const NtSpatialExact *a, const NtSpatialExact *b)
{
    if (a->negative && b->negative) return false;
    if (a->negative == b->negative) return true;
    const bool entering = a->negative;
    NtSpatialFraction crossing;
    nt_spatial_exact_copy(&crossing.numerator,a);
    crossing.numerator.negative = false;
    nt_spatial_exact_subtract(&crossing.divisor,entering ? b : a,entering ? a : b);
    NtSpatialFraction *limit = entering ? first : last;
    const int order = nt_spatial_fraction_order(&crossing,limit);
    if (entering ? order > 0 : order < 0) nt_spatial_fraction_copy(limit,&crossing);
    return nt_spatial_fraction_order(first,last) <= 0;
}

static inline void nt_exact_difference(double a, double b, NtSpatialExact *out)
{
    NtSpatialExact first, last;
    nt_spatial_exact_number(&first,a,0);
    nt_spatial_exact_number(&last,b,0);
    nt_spatial_exact_subtract(out,&first,&last);
}

static inline void nt_exact_displacement(NtVec3 a, NtVec3 b, NtSpatialExact out[3])
{
    nt_exact_difference(a.x,b.x,&out[0]);
    nt_exact_difference(a.y,b.y,&out[1]);
    nt_exact_difference(a.z,b.z,&out[2]);
}

static inline void nt_exact_determinant(const NtSpatialExact *a, const NtSpatialExact *b,
                                        const NtSpatialExact *c, const NtSpatialExact *d,
                                        NtSpatialExact *out)
{
    NtSpatialExact ad, bc;
    nt_spatial_exact_multiply(&ad,a,d);
    nt_spatial_exact_multiply(&bc,b,c);
    nt_spatial_exact_subtract(out,&ad,&bc);
}

static inline void nt_exact_cross(const NtSpatialExact a[3], const NtSpatialExact b[3],
                                  NtSpatialExact out[3])
{
    for (unsigned i = 0; i < 3; ++i) {
        const unsigned j = (i+1)%3, k = (i+2)%3;
        nt_exact_determinant(&a[j],&a[k],&b[j],&b[k],&out[i]);
    }
}

static inline void nt_exact_dot(const NtSpatialExact a[3], const NtSpatialExact b[3],
                                NtSpatialExact *out)
{
    nt_spatial_exact_number(out,0,0);
    for (unsigned i = 0; i < 3; ++i) {
        NtSpatialExact product, sum;
        nt_spatial_exact_multiply(&product,&a[i],&b[i]);
        nt_spatial_exact_add(&sum,out,&product);
        nt_spatial_exact_copy(out,&sum);
    }
}

static inline void nt_exact_mix(const NtSpatialExact *a, const NtSpatialExact *b,
                                const NtSpatialFraction *at, NtSpatialExact *out)
{
    NtSpatialExact before, first, last;
    nt_spatial_exact_subtract(&before,&at->divisor,&at->numerator);
    nt_spatial_exact_multiply(&first,a,&before);
    nt_spatial_exact_multiply(&last,b,&at->numerator);
    nt_spatial_exact_add(out,&first,&last);
}

#endif
