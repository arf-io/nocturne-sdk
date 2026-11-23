#ifndef NOCTURNE_INTERNAL_SPATIAL_PROJECTION_H
#define NOCTURNE_INTERNAL_SPATIAL_PROJECTION_H

#if defined(__clang__) && !defined(__wasm__)
#pragma STDC FENV_ACCESS ON
#elif defined(_MSC_VER)
#pragma fenv_access(on)
#endif

#include "spatial_internal.h"
#include "spatial_exact_internal.h"

typedef enum NtSpatialPlane {
    NT_SPATIAL_NEAR, NT_SPATIAL_FAR,
    NT_SPATIAL_LEFT, NT_SPATIAL_RIGHT, NT_SPATIAL_BOTTOM, NT_SPATIAL_TOP,
    NT_SPATIAL_WEIGHT_A, NT_SPATIAL_WEIGHT_B, NT_SPATIAL_WEIGHT_C
} NtSpatialPlane;

typedef struct NtSpatialWeights {
    NtSpatialExact value[3];
    NtSpatialExact divisor;
} NtSpatialWeights;

typedef struct NtSpatialScreenBounds {
    NtSpatialScreen minimum, maximum;
    bool empty;
} NtSpatialScreenBounds;

typedef struct NtSpatialScreenRatio {
    NtSpatialScreen numerator, denominator;
} NtSpatialScreenRatio;

NtResult nt_spatial_begin_projection(const NtSpatialProjection *projection, fenv_t *saved);
static inline NtResult nt_spatial_end_projection(const fenv_t *saved, NtResult status)
{
    return nt_finish_standard_fp(saved) ? status : NT_RESULT_OUT_OF_RANGE;
}
bool nt_spatial_screen_finite(NtSpatialScreen screen);
bool nt_spatial_screen_inside(NtSpatialScreen screen);
void nt_spatial_plane_score(const NtSpatialProjection *projection, NtSpatialPlane plane, NtVec3 view,
                             NtSpatialExact *out);
bool nt_spatial_intersect_weights(const NtSpatialExact first[3], const NtSpatialExact second[3],
                                  NtSpatialWeights *out);
void nt_spatial_weighted(const NtSpatialWeights *weights, double a, double b, double c,
                         NtSpatialExact *out);
NtVec3 nt_spatial_weights_value(const NtSpatialWeights *weights);
void nt_spatial_project_vertex(const NtSpatialProjection *projection, const NtSpatialExact *x,
                                const NtSpatialExact *y, const NtSpatialExact *depth,
                                const NtSpatialExact *divisor, NtSpatialProjected *out);
void nt_spatial_clipped_vertex(const NtSpatialProjection *projection, const NtVec3 view[3],
                                const NtSpatialWeights *weights, NtSpatialClipVertex *out);
bool nt_spatial_nearest_weights(const NtSpatialProjection *projection, NtSpatialScreenRatio screen,
                               const NtSpatialPickTriangle *triangles, const NtSpatialScreenBounds *bounds,
                               size_t count, size_t *index, NtSpatialWeights *selected,
                               NtSpatialExact *best_depth);
#endif
