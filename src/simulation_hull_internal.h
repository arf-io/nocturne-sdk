#ifndef NOCTURNE_INTERNAL_SIMULATION_HULL_H
#define NOCTURNE_INTERNAL_SIMULATION_HULL_H

#include "simulation_internal.h"
#include "simulation_wide_internal.h"
#include "spatial_geometry_internal.h"
#include "spatial_internal.h"

struct NtHull {
    NtAllocator allocator;
    size_t vertex_count, triangle_count;
    NtVec3 vertices[NT_HULL_MAX_VERTICES];
    NtHullTriangle triangles[NT_HULL_MAX_TRIANGLES];
    NtVec3 origin; /* Exact shift applied before clipping. */
    NtSpatialExact six_volume;
    double volume;
};

typedef struct NtHullMeasure {
    NtSpatialExact six_volume, moment[3]; /* Six times volume, 24 times first moment. */
} NtHullMeasure;

typedef struct NtHullExactVertex {
    NtSpatialExact coordinate[3], divisor;
} NtHullExactVertex;

typedef struct NtHullTetraScratch {
    NtSimulationWide determinant, ab, cd, divisor;
    NtSimulationWide pair, first, second, sum;
} NtHullTetraScratch;

typedef struct NtHullInteger {
    uint32_t *limbs;
    size_t capacity;
    unsigned count;
    int offset;
} NtHullInteger;

typedef struct NtHullRefinement {
    NtAllocator allocator;
    NtResult status;
    NtHullInteger numerator, denominator, work[3];
} NtHullRefinement;

static inline void nt_hull_normal(NtVec3 a, NtVec3 b, NtVec3 c, NtSpatialExact out[3])
{
    NtSpatialExact ab[3], ac[3];
    nt_exact_displacement(b,a,ab);
    nt_exact_displacement(c,a,ac);
    nt_exact_cross(ab,ac,out);
}

NtResult nt_hull_body_transform(const NtFloatingBody *body, NtSpatialTransform *out);
void nt_hull_measure_tetra(NtHullMeasure *measure, NtVec3 base, NtVec3 a, NtVec3 b, NtVec3 c);
bool nt_hull_measure_exact(NtHullMeasure *measure, NtHullRefinement *refinement,
                            const NtHullExactVertex *vertices[4], NtHullTetraScratch *work);
bool nt_hull_refine_add(NtHullRefinement *refinement, const NtSpatialExact *volume);
bool nt_hull_refine_order(NtHullRefinement *refinement, const NtSpatialExact *threshold, int *order);
void nt_hull_refine_clear(NtHullRefinement *refinement);
int nt_hull_orientation_2d(NtPoint a, NtPoint b, NtPoint c);
bool nt_hull_overlap_2d(const NtPoint a[3], const NtPoint b[3]);

#endif
