#ifndef NOCTURNE_SPATIAL_H
#define NOCTURNE_SPATIAL_H

#include <nocturne/animation.h>
#include <nocturne/base.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NtVec3 { double x, y, z; } NtVec3;

NtResult nt_spatial_vec_add(NtVec3 a, NtVec3 b, NtVec3 *out);
NtResult nt_spatial_vec_subtract(NtVec3 a, NtVec3 b, NtVec3 *out);
NtResult nt_spatial_vec_scale(NtVec3 vector, double factor, NtVec3 *out);
NtResult nt_spatial_vec_dot(NtVec3 a, NtVec3 b, double *out);
NtResult nt_spatial_vec_cross(NtVec3 a, NtVec3 b, NtVec3 *out);
NtResult nt_spatial_vec_length(NtVec3 vector, double *out);
NtResult nt_spatial_vec_normalize(NtVec3 vector, NtVec3 *out);

/* Right-handed Cartesian coordinates. The row-major linear matrix acts on
 * column vectors. Points receive translation and directions do not. */
typedef struct NtSpatialTransform {
    double linear[3][3];
    NtVec3 translation;
} NtSpatialTransform;

typedef struct NtSpatialTransformOptions {
    NtVec3 translation, pivot, scale, rotation_axis;
    double rotation_radians;
} NtSpatialTransformOptions;

NtSpatialTransform nt_spatial_transform_identity(void);
NtSpatialTransformOptions nt_spatial_transform_options_default(void);
/* Scale, then rotate about the pivot, then translate. */
NtResult nt_spatial_transform_create(const NtSpatialTransformOptions *options, NtSpatialTransform *out);
/* The result applies inner first and outer second. All outputs may alias inputs. */
NtResult nt_spatial_transform_compose(const NtSpatialTransform *outer,
                                      const NtSpatialTransform *inner, NtSpatialTransform *out);
NtResult nt_spatial_transform_inverse(const NtSpatialTransform *transform, NtSpatialTransform *out);
NtResult nt_spatial_transform_point(const NtSpatialTransform *transform, NtVec3 point, NtVec3 *out);
NtResult nt_spatial_transform_direction(const NtSpatialTransform *transform, NtVec3 direction, NtVec3 *out);

typedef struct NtSpatialPath NtSpatialPath;
typedef struct NtSpatialPathOptions {
    const NtVec3 *points;
    size_t count;
    bool closed;
    NtRepeat boundary;
} NtSpatialPathOptions;
typedef struct NtSpatialPathSample {
    NtVec3 position, tangent;
    double distance, length;
} NtSpatialPathSample;

/* Copy an immutable polyline. Samples use signed distance in world units. */
NtResult nt_spatial_path_create(const NtAllocator *allocator, const NtSpatialPathOptions *options,
                                 NtSpatialPath **out);
void nt_spatial_path_destroy(NtSpatialPath *path);
NtResult nt_spatial_path_length(const NtSpatialPath *path, double *out);
NtResult nt_spatial_path_sample(const NtSpatialPath *path, double distance, NtSpatialPathSample *out);

/* Camera pose in world space. right, up and backward are a right-handed
 * orthonormal basis. Visible points in front of the camera have negative
 * view z. Camera vectors stand apart from terminal rows and columns. */
typedef struct NtSpatialCamera { NtVec3 position, right, up, backward; } NtSpatialCamera;

NtResult nt_spatial_camera_look_at(NtVec3 position, NtVec3 target, NtVec3 up_hint, NtSpatialCamera *out);
/* Orbit world +Y. At zero azimuth and elevation the camera is on +Z. */
NtResult nt_spatial_camera_orbit(NtVec3 target, double radius, double azimuth,
                                double elevation, NtSpatialCamera *out);
/* Translate in the starting local basis, then rotate orientation by the
 * local matrix Ry(yaw) * Rx(pitch) * Rz(roll). Negative local z moves forward. */
NtResult nt_spatial_camera_fly(const NtSpatialCamera *camera, NtVec3 local_translation,
                              double yaw, double pitch, double roll, NtSpatialCamera *out);
NtResult nt_spatial_camera_to_view(const NtSpatialCamera *camera, NtVec3 world_point, NtVec3 *out);

typedef enum NtSpatialProjectionKind {
    NT_SPATIAL_PERSPECTIVE,
    NT_SPATIAL_ORTHOGRAPHIC
} NtSpatialProjectionKind;

/* In perspective mode the half extents are slopes at depth one. In
 * orthographic mode they are view-space distances. Depth is -view.z. */
typedef struct NtSpatialProjection {
    NtSpatialProjectionKind kind;
    double half_width, half_height;
    double near_depth, far_depth;
} NtSpatialProjection;

/* Normalized screen coordinates increase right and up, from -1 to +1. */
typedef struct NtSpatialScreen { double x, y; } NtSpatialScreen;
typedef struct NtSpatialProjected {
    NtSpatialScreen screen;
    double depth;
} NtSpatialProjected;

typedef struct NtSpatialClipVertex {
    NtVec3 view, weights;
    NtSpatialProjected projected;
} NtSpatialClipVertex;
typedef struct NtSpatialClippedSegment {
    NtSpatialClipVertex first, last;
} NtSpatialClippedSegment;
enum { NT_SPATIAL_CLIP_MAX_VERTICES = 9 };
typedef struct NtSpatialClippedPolygon {
    size_t count;
    NtSpatialClipVertex vertices[NT_SPATIAL_CLIP_MAX_VERTICES];
} NtSpatialClippedPolygon;

/* Project only points inside all six closed frustum planes. A successful
 * miss writes visible=false and leaves the projected value untouched. */
NtResult nt_spatial_project(const NtSpatialProjection *projection, NtVec3 view,
                            bool *visible, NtSpatialProjected *out);
/* The screen coordinate and any two in-range depths define a view ray.
 * Depth remains in view units and is never normalized. */
NtResult nt_spatial_unproject(const NtSpatialProjection *projection, NtSpatialScreen screen,
                              double depth, NtVec3 *out);
/* Segment weights refer to its first and last endpoints, with z zero. */
NtResult nt_spatial_clip_segment(const NtSpatialProjection *projection, NtVec3 first, NtVec3 last,
                                  bool *visible, NtSpatialClippedSegment *out);
/* Preserve the triangle's winding and return original-vertex weights.
 * An empty intersection or boundary contact with zero parameter area
 * publishes a polygon with count zero. */
NtResult nt_spatial_clip_triangle(const NtSpatialProjection *projection, const NtVec3 view[3],
                                   NtSpatialClippedPolygon *out);

typedef struct NtSpatialPickTriangle {
    NtVec3 view[3];
    uint64_t object_id, primitive_id;
} NtSpatialPickTriangle;
typedef struct NtSpatialHit {
    uint64_t object_id, primitive_id;
    size_t triangle_index;
    NtVec3 view, weights;
    double depth;
} NtSpatialHit;

/* Intersect the screen ray with caller-owned view-space triangles.
 * Return the nearest hit between near and far, in input order on exact
 * depth ties. Parallel or degenerate triangles contribute no hit. */
NtResult nt_spatial_pick(const NtSpatialProjection *projection, NtSpatialScreen screen,
                         const NtSpatialPickTriangle *triangles, size_t count,
                         bool *hit, NtSpatialHit *out);

#ifdef __cplusplus
}
#endif
#endif
