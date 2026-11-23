#ifndef NOCTURNE_SPATIAL_MESH_H
#define NOCTURNE_SPATIAL_MESH_H

#include <nocturne/content.h>
#include <nocturne/motion.h>
#include <nocturne/paint.h>
#include <nocturne/spatial.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NtSpatialMesh NtSpatialMesh;
typedef struct NtSpatialRaster NtSpatialRaster;

typedef struct NtSpatialVertex {
    NtVec3 position;
    NtPoint uv;
} NtSpatialVertex;

typedef struct NtSpatialFace {
    uint32_t indices[3];
    uint64_t primitive_id;
} NtSpatialFace;

typedef struct NtSpatialMeshData {
    const NtSpatialVertex *vertices;
    size_t vertex_count;
    const NtSpatialFace *faces;
    size_t face_count;
} NtSpatialMeshData;

/* Own copies of finite vertices and indexed triangles. Borrowed mesh data
 * stays valid until destruction. */
NtResult nt_spatial_mesh_create(const NtAllocator *allocator, const NtSpatialMeshData *data,
                                 NtSpatialMesh **out);
void nt_spatial_mesh_destroy(NtSpatialMesh *mesh);
NtResult nt_spatial_mesh_data(const NtSpatialMesh *mesh, NtSpatialMeshData *out);

enum { NT_SPATIAL_OBJ_MAX_FACE_VERTICES = 256, NT_SPATIAL_OBJ_MAX_NUMBER_BYTES = 128 };

/* Decode the polygonal Wavefront OBJ subset described in the contracts.
 * The parser reads only the supplied bytes. */
NtResult nt_spatial_mesh_decode_obj(const NtAllocator *allocator, NtByteView source, NtSpatialMesh **out);

typedef struct NtSpatialMeshInstance {
    const NtSpatialMesh *mesh;
    const NtSpatialTransform *transform;       /* NULL selects identity. */
    uint64_t object_id;
    NtOwnerToken owner;
    NtRgba tint;
    const NtImageView *texture;                /* Optional normalized UV texture. */
    NtImageSampling sampling;
} NtSpatialMeshInstance;

typedef struct NtSpatialRasterOptions {
    NtSpatialProjection projection;
    const NtSpatialCamera *camera;             /* NULL treats transformed coordinates as view space. */
    NtCellSize samples;
    NtRgba background;
    size_t sample_limit, triangle_limit;      /* Zero selects the representable limit. */
} NtSpatialRasterOptions;

typedef struct NtSpatialRasterSample {
    NtRgba color;
    NtOwnerToken owner;
    bool covered;
    uint64_t object_id, primitive_id;
    double depth;
    NtPoint uv;
} NtSpatialRasterSample;

/* Shade the nearest closed triangle at each sample center. The snapshot
 * owns its samples and needs no scene, camera or texture after return. */
NtResult nt_spatial_rasterize(const NtAllocator *allocator, const NtSpatialRasterOptions *options,
                              const NtSpatialMeshInstance *instances, size_t count, NtSpatialRaster **out);
void nt_spatial_raster_destroy(NtSpatialRaster *raster);
NtCellSize nt_spatial_raster_size(const NtSpatialRaster *raster);
NtResult nt_spatial_raster_sample(const NtSpatialRaster *raster, NtCellPoint position,
                                  NtSpatialRasterSample *out);
/* Reduce 2 by 4 sample tiles into an ordinary owned terminal frame. */
NtResult nt_spatial_raster_frame(const NtSpatialRaster *raster, NtSamplingMode mode,
                                 const NtAllocator *allocator, NtFrame **out);

typedef struct NtSpatialCellHit {
    NtCellPoint sample_position;
    NtSpatialRasterSample sample;
} NtSpatialCellHit;

/* Cell dimensions for the same 2 by 4 reduction used by frame and paint. */
NtCellSize nt_spatial_raster_cell_size(const NtSpatialRaster *raster);
/* Pick the covered sample closest to the cell center, with row then column
 * order on ties. A successful miss writes only found=false. */
NtResult nt_spatial_raster_cell_hit(const NtSpatialRaster *raster, NtCellPoint cell,
                                    bool *found, NtSpatialCellHit *out);
/* Paint at a component-local cell origin through the caller's clipped context. */
NtResult nt_spatial_raster_paint(const NtSpatialRaster *raster, NtPaintContext *paint,
                                 NtCellPoint origin, NtSamplingMode mode);

#ifdef __cplusplus
}
#endif
#endif
