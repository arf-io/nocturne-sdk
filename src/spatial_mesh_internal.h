#ifndef NOCTURNE_INTERNAL_SPATIAL_MESH_H
#define NOCTURNE_INTERNAL_SPATIAL_MESH_H

#include <nocturne/spatial_mesh.h>
#include "allocator.h"

struct NtSpatialMesh {
    NtAllocator allocator;
    NtSpatialVertex *vertices;
    NtSpatialFace *faces;
    size_t vertex_count, face_count;
};

bool nt_spatial_mesh_counts_valid(size_t vertices, size_t faces);
NtResult nt_spatial_mesh_allocate(const NtAllocator *allocator, size_t vertices, size_t faces,
                                   NtSpatialMesh **out);
bool nt_spatial_polygon_triangulate(NtSpatialMesh *mesh, size_t base, size_t count, size_t first_face);

#endif
