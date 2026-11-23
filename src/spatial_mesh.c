#include "spatial_projection_internal.h"
#include "spatial_mesh_internal.h"

#include <string.h>

NT_DECLARE_ALIGNMENT(NtSpatialMesh);
NT_DECLARE_ALIGNMENT(NtSpatialVertex);
NT_DECLARE_ALIGNMENT(NtSpatialFace);

bool nt_spatial_mesh_counts_valid(size_t vertices, size_t faces)
{
    return vertices <= UINT32_MAX && vertices <= SIZE_MAX/sizeof(NtSpatialVertex) &&
        faces <= SIZE_MAX/sizeof(NtSpatialFace);
}

NtResult nt_spatial_mesh_allocate(const NtAllocator *allocator, size_t vertices, size_t faces,
                                   NtSpatialMesh **out)
{
    if (!nt_spatial_mesh_counts_valid(vertices, faces)) return NT_RESULT_OUT_OF_RANGE;
    NtSpatialMesh *mesh = nt_allocate(allocator, sizeof(*mesh), NT_ALIGNMENT(NtSpatialMesh));
    if (!mesh) return NT_RESULT_ALLOCATION_FAILED;
    *mesh = (NtSpatialMesh){.allocator=*allocator, .vertex_count=vertices, .face_count=faces};
    if (vertices) mesh->vertices = nt_allocate(allocator, vertices*sizeof(mesh->vertices[0]),
                                                NT_ALIGNMENT(NtSpatialVertex));
    if (vertices && !mesh->vertices) {
        nt_spatial_mesh_destroy(mesh);
        return NT_RESULT_ALLOCATION_FAILED;
    }
    if (faces) mesh->faces = nt_allocate(allocator, faces*sizeof(mesh->faces[0]), NT_ALIGNMENT(NtSpatialFace));
    if (faces && !mesh->faces) {
        nt_spatial_mesh_destroy(mesh);
        return NT_RESULT_ALLOCATION_FAILED;
    }
    *out = mesh;
    return NT_RESULT_OK;
}

void nt_spatial_mesh_destroy(NtSpatialMesh *mesh)
{
    if (!mesh) return;
    const NtAllocator allocator = mesh->allocator;
    nt_release(&allocator, mesh->faces, mesh->face_count*sizeof(mesh->faces[0]), NT_ALIGNMENT(NtSpatialFace));
    nt_release(&allocator, mesh->vertices, mesh->vertex_count*sizeof(mesh->vertices[0]), NT_ALIGNMENT(NtSpatialVertex));
    nt_release(&allocator, mesh, sizeof(*mesh), NT_ALIGNMENT(NtSpatialMesh));
}

static bool valid_geometry(const NtSpatialMeshData *data)
{
    for (size_t i = 0; i < data->vertex_count; ++i) {
        const NtSpatialVertex v = data->vertices[i];
        if (!nt_spatial_finite_vec(v.position) || !isfinite(v.uv.x) || !isfinite(v.uv.y)) return false;
    }
    for (size_t i = 0; i < data->face_count; ++i)
        for (unsigned j = 0; j < 3; ++j)
            if (data->faces[i].indices[j] >= data->vertex_count) return false;
    return true;
}

NtResult nt_spatial_mesh_create(const NtAllocator *allocator, const NtSpatialMeshData *data,
                                 NtSpatialMesh **out)
{
    NtAllocator resolved;
    if (!data || !out || !nt_allocator_get(allocator, &resolved) ||
        (data->vertex_count && !data->vertices) || (data->face_count && !data->faces))
        return NT_RESULT_INVALID_ARGUMENT;
    if (!nt_spatial_mesh_counts_valid(data->vertex_count, data->face_count)) return NT_RESULT_OUT_OF_RANGE;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_OUT_OF_RANGE;
    const bool valid = valid_geometry(data);
    if (!nt_finish_standard_fp(&saved)) return NT_RESULT_OUT_OF_RANGE;
    if (!valid) return NT_RESULT_INVALID_ARGUMENT;
    NtSpatialMesh *mesh;
    const NtResult status = nt_spatial_mesh_allocate(&resolved, data->vertex_count, data->face_count, &mesh);
    if (status != NT_RESULT_OK) return status;
    if (data->vertex_count) memcpy(mesh->vertices, data->vertices, data->vertex_count*sizeof(data->vertices[0]));
    if (data->face_count) memcpy(mesh->faces, data->faces, data->face_count*sizeof(data->faces[0]));
    *out = mesh;
    return NT_RESULT_OK;
}

NtResult nt_spatial_mesh_data(const NtSpatialMesh *mesh, NtSpatialMeshData *out)
{
    if (!mesh || !out) return NT_RESULT_INVALID_ARGUMENT;
    *out = (NtSpatialMeshData){mesh->vertices, mesh->vertex_count, mesh->faces, mesh->face_count};
    return NT_RESULT_OK;
}
