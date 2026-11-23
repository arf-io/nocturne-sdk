#ifndef NOCTURNE_SPATIAL_SURFACE_H
#define NOCTURNE_SPATIAL_SURFACE_H

#include <nocturne/spatial.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NtSpatialSurfaceMotion {
    NtVec3 pivot;               /* World-space pivot at time zero. */
    NtVec3 linear_velocity;     /* World units per second. */
    NtVec3 angular_velocity;    /* World-axis radians per second. */
} NtSpatialSurfaceMotion;

typedef struct NtSpatialSurfaceSample {
    NtVec3 position, velocity;  /* Instantaneous world-space values. */
} NtSpatialSurfaceSample;

/* Map geometry from its time-zero world placement at a signed elapsed time. */
NtResult nt_spatial_surface_pose(const NtSpatialSurfaceMotion *motion, double seconds,
                                 NtSpatialTransform *out);
NtResult nt_spatial_surface_sample(const NtSpatialSurfaceMotion *motion, double seconds,
                                   NtVec3 initial_point, NtSpatialSurfaceSample *out);

typedef struct NtSpatialSweepTriangle {
    NtVec3 initial[3];
    NtVec3 displacement;        /* Common vertex movement over the interval. */
    uint64_t object_id, primitive_id;
} NtSpatialSweepTriangle;

typedef struct NtSpatialContact {
    uint64_t object_id, primitive_id;
    size_t triangle_index;
    double fraction;            /* Closed interval [0, 1]. */
    NtVec3 position, weights, normal;
} NtSpatialContact;

/* Sweep a world-space point from first to last against translating triangles.
 * Closed edges and coplanar contacts count. Exact time ties keep input order.
 * A successful miss writes only found=false. */
NtResult nt_spatial_sweep_point(NtVec3 first, NtVec3 last, const NtSpatialSweepTriangle *triangles,
                                size_t count, bool *found, NtSpatialContact *out);

#ifdef __cplusplus
}
#endif
#endif
