#include "simulation_hull_internal.h"
#include "allocator.h"
#include <string.h>

NT_DECLARE_ALIGNMENT(NtHull);

int nt_hull_orientation_2d(NtPoint a, NtPoint b, NtPoint c)
{
    NtSpatialExact abx, aby, acx, acy, determinant;
    nt_exact_difference(b.x,a.x,&abx);
    nt_exact_difference(b.y,a.y,&aby);
    nt_exact_difference(c.x,a.x,&acx);
    nt_exact_difference(c.y,a.y,&acy);
    nt_exact_determinant(&abx,&aby,&acx,&acy,&determinant);
    return nt_spatial_exact_sign(&determinant);
}

static bool separated(const NtPoint a[3], const NtPoint b[3])
{
    const int inside = nt_hull_orientation_2d(a[0],a[1],a[2]);
    for (unsigned i=0;i<3;++i) {
        bool outside = true;
        for (unsigned j=0;j<3;++j) {
            if (nt_hull_orientation_2d(a[i],a[(i+1)%3],b[j])*inside>0) {
                outside = false;
                break;
            }
        }
        if (outside) return true;
    }
    return false;
}

bool nt_hull_overlap_2d(const NtPoint a[3], const NtPoint b[3])
{
    return !separated(a,b) && !separated(b,a);
}

static NtPoint project(NtVec3 v, unsigned component)
{
    const double coordinates[3] = {v.x,v.y,v.z};
    return (NtPoint){coordinates[(component+1)%3],coordinates[(component+2)%3]};
}

static bool supporting_face(const NtHullGeometry *mesh, size_t face)
{
    const uint32_t *indices = mesh->triangles[face].indices;
    const NtVec3 a = mesh->vertices[indices[0]], b = mesh->vertices[indices[1]];
    const NtVec3 c = mesh->vertices[indices[2]];
    NtSpatialExact normal[3];
    nt_hull_normal(a,b,c,normal);
    bool planar[NT_HULL_MAX_VERTICES];
    bool has_inside = false;
    for (size_t i=0;i<mesh->vertex_count;++i) {
        NtSpatialExact offset[3], score;
        nt_exact_displacement(mesh->vertices[i],a,offset);
        nt_exact_dot(normal,offset,&score);
        const int sign = nt_spatial_exact_sign(&score);
        if (sign>0) return false;
        planar[i] = sign==0;
        has_inside = has_inside || sign<0;
    }
    if (!has_inside) return false;
    unsigned axis = 0;
    while (!normal[axis].count) ++axis;
    const NtPoint projection[3] = {project(a,axis),project(b,axis),project(c,axis)};
    for (size_t i=0;i<face;++i) {
        const uint32_t *v = mesh->triangles[i].indices;
        if (!planar[v[0]] || !planar[v[1]] || !planar[v[2]]) continue;
        const NtPoint other[3] = {
            project(mesh->vertices[v[0]],axis),project(mesh->vertices[v[1]],axis),
            project(mesh->vertices[v[2]],axis)
        };
        if (nt_hull_overlap_2d(projection,other)) return false;
    }
    return true;
}

static bool closed_boundary(const NtHullGeometry *mesh)
{
    /* Each winding must traverse an edge once in each direction. */
    uint8_t edges[NT_HULL_MAX_VERTICES][NT_HULL_MAX_VERTICES] = {{0}};
    bool used[NT_HULL_MAX_VERTICES] = {false};
    for (size_t i=0;i<mesh->triangle_count;++i) {
        const uint32_t *v = mesh->triangles[i].indices;
        for (unsigned j=0;j<3;++j) {
            const uint32_t a = v[j], b = v[(j+1)%3];
            if (a>=mesh->vertex_count || b>=mesh->vertex_count || a==b || edges[a][b]) return false;
            edges[a][b] = 1;
            used[a] = true;
        }
    }
    for (size_t i=0;i<mesh->vertex_count;++i) {
        if (!used[i]) return false;
        for (size_t j=0;j<i;++j)
            if (edges[i][j]!=edges[j][i]) return false;
    }
    return true;
}

void nt_hull_measure_tetra(NtHullMeasure *measure, NtVec3 base, NtVec3 a, NtVec3 b, NtVec3 c)
{
    NtSpatialExact ab[3], ac[3], ad[3], cross[3], volume;
    nt_exact_displacement(a,base,ab);
    nt_exact_displacement(b,base,ac);
    nt_exact_displacement(c,base,ad);
    nt_exact_cross(ac,ad,cross);
    nt_exact_dot(ab,cross,&volume);
    if (!volume.count) return;
    nt_simulation_accumulate(&measure->six_volume,&volume);
    const double points[3][4] = {{base.x,a.x,b.x,c.x},{base.y,a.y,b.y,c.y},{base.z,a.z,b.z,c.z}};
    for (unsigned i=0;i<3;++i) {
        NtSpatialExact sum, coordinate, moment;
        nt_spatial_exact_number(&sum,points[i][0],0);
        for (unsigned j=1;j<4;++j) {
            nt_spatial_exact_number(&coordinate,points[i][j],0);
            nt_simulation_accumulate(&sum,&coordinate);
        }
        nt_spatial_exact_multiply(&moment,&sum,&volume);
        nt_simulation_accumulate(&measure->moment[i],&moment);
    }
}

static double centering_axis(double min, double max)
{
    /* Use only shifts whose subtraction is exact throughout the interval. */
    if (min>0 && max<=2*min) return min;
    if (max<0 && min>=2*max) return max;
    return 0;
}

static NtResult measure_geometry(const NtHullGeometry *mesh, NtHullMeasure *measure,
                                 NtVec3 *origin)
{
    NtVec3 min = mesh->vertices[0], max = min;
    for (size_t i=0;i<mesh->vertex_count;++i) {
        const NtVec3 a = mesh->vertices[i];
        if (!nt_simulation_vector(a)) return NT_RESULT_INVALID_ARGUMENT;
        min = (NtVec3){fmin(min.x,a.x),fmin(min.y,a.y),fmin(min.z,a.z)};
        max = (NtVec3){fmax(max.x,a.x),fmax(max.y,a.y),fmax(max.z,a.z)};
        for (size_t j=0;j<i;++j) {
            const NtVec3 b = mesh->vertices[j];
            if (a.x==b.x && a.y==b.y && a.z==b.z) return NT_RESULT_INVALID_ARGUMENT;
        }
    }
    if (!closed_boundary(mesh)) return NT_RESULT_INVALID_ARGUMENT;
    for (size_t i=0;i<mesh->triangle_count;++i) {
        if (!supporting_face(mesh,i)) return NT_RESULT_INVALID_ARGUMENT;
        const uint32_t *v = mesh->triangles[i].indices;
        nt_hull_measure_tetra(measure,mesh->vertices[0],mesh->vertices[v[0]],
                              mesh->vertices[v[1]],mesh->vertices[v[2]]);
    }
    *origin = (NtVec3){centering_axis(min.x,max.x),centering_axis(min.y,max.y),centering_axis(min.z,max.z)};
    return nt_spatial_exact_sign(&measure->six_volume)>0 ? NT_RESULT_OK : NT_RESULT_INVALID_ARGUMENT;
}

NtResult nt_hull_create(const NtAllocator *allocator, const NtHullGeometry *geometry, NtHull **out)
{
    NtAllocator resolved;
    if (!geometry || !out || !geometry->vertices || !geometry->triangles ||
        !nt_allocator_get(allocator,&resolved)) return NT_RESULT_INVALID_ARGUMENT;
    if (geometry->vertex_count>NT_HULL_MAX_VERTICES || geometry->triangle_count>NT_HULL_MAX_TRIANGLES)
        return NT_RESULT_OUT_OF_RANGE;
    if (geometry->vertex_count<4 || geometry->triangle_count<4) return NT_RESULT_INVALID_ARGUMENT;

    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    NtHullMeasure measure = {0};
    NtVec3 origin = {0}, points[NT_HULL_MAX_VERTICES];
    double volume = 0;
    NtResult status = measure_geometry(geometry,&measure,&origin);
    if (status == NT_RESULT_OK) {
        NtSpatialExact six;
        nt_spatial_exact_number(&six,6,0);
        volume = nt_spatial_exact_quotient(&measure.six_volume,&six);
        if (!volume) status = NT_RESULT_OUT_OF_RANGE;
        for (size_t i=0;i<geometry->vertex_count;++i) {
            const NtVec3 p = geometry->vertices[i];
            points[i] = (NtVec3){p.x-origin.x,p.y-origin.y,p.z-origin.z};
        }
    }
    status = nt_simulation_finish(&saved,status);
    if (status != NT_RESULT_OK) return status;

    NtHull *hull = nt_allocate(&resolved,sizeof(*hull),NT_ALIGNMENT(NtHull));
    if (!hull) return NT_RESULT_ALLOCATION_FAILED;
    hull->allocator = resolved;
    hull->vertex_count = geometry->vertex_count;
    hull->triangle_count = geometry->triangle_count;
    hull->volume = volume;
    hull->origin = origin;
    nt_spatial_exact_copy(&hull->six_volume,&measure.six_volume);
    memcpy(hull->vertices,points,hull->vertex_count*sizeof(hull->vertices[0]));
    memcpy(hull->triangles,geometry->triangles,hull->triangle_count*sizeof(hull->triangles[0]));
    *out = hull;
    return NT_RESULT_OK;
}

void nt_hull_destroy(NtHull *hull)
{
    if (!hull) return;
    const NtAllocator allocator = hull->allocator;
    nt_release(&allocator,hull,sizeof(*hull),NT_ALIGNMENT(NtHull));
}

bool nt_simulation_valid_body(const NtFloatingBody *body)
{
    const NtVec3 inertia = body->principal_inertia;
    return nt_simulation_vector(body->position) && nt_simulation_vector(body->rotation_axis) &&
        nt_simulation_bounded(body->rotation_radians) && body->mass>0 && body->mass<=1e6 &&
        inertia.x>0 && inertia.x<=1e24 && inertia.y>0 && inertia.y<=1e24 &&
        inertia.z>0 && inertia.z<=1e24 && nt_simulation_vector(body->linear_velocity) &&
        nt_simulation_vector(body->angular_velocity) &&
        (body->rotation_radians==0 || body->rotation_axis.x!=0 || body->rotation_axis.y!=0 ||
         body->rotation_axis.z!=0);
}

NtResult nt_hull_body_transform(const NtFloatingBody *body, NtSpatialTransform *out)
{
    if (!nt_simulation_valid_body(body)) return NT_RESULT_INVALID_ARGUMENT;
    NtSpatialTransformOptions options = nt_spatial_transform_options_default();
    options.translation = body->position;
    options.rotation_axis = body->rotation_axis;
    options.rotation_radians = body->rotation_radians;
    return nt_spatial_transform_create(&options,out);
}

NtResult nt_floating_body_transform(const NtFloatingBody *body, NtSpatialTransform *out)
{
    if (!body || !out) return NT_RESULT_INVALID_ARGUMENT;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    NtSpatialTransform result;
    NtResult status = nt_hull_body_transform(body,&result);
    status = nt_simulation_finish(&saved,status);
    if (status == NT_RESULT_OK) *out = result;
    return status;
}

NtResult nt_hull_buoyancy(const NtHullImmersion *immersion, const NtFloatingBody *body,
                          double density, double gravity, NtBuoyancy *out)
{
    if (!immersion || !body || !out) return NT_RESULT_INVALID_ARGUMENT;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    const NtVec3 arm = immersion->center_offset;
    if (!nt_simulation_valid_body(body) || !(density>=0 && density<=1e6) || !(gravity>=0 && gravity<=1e6) ||
        !(immersion->full_volume>0 && immersion->full_volume<=8e18) ||
        !(immersion->displaced_volume>=0 && immersion->displaced_volume<=immersion->full_volume) ||
        !nt_spatial_finite_vec(immersion->center) || !nt_spatial_finite_vec(arm) ||
        fabs(arm.x)>2e6 || fabs(arm.y)>2e6 || fabs(arm.z)>2e6)
        return nt_simulation_finish(&saved,NT_RESULT_INVALID_ARGUMENT);

    NtSpatialExact weighted, g, lift, weight, net, one;
    nt_simulation_product(&weighted,density,immersion->displaced_volume);
    nt_spatial_exact_number(&g,gravity,0);
    nt_spatial_exact_multiply(&lift,&weighted,&g);
    nt_simulation_product(&weight,-body->mass,gravity);
    nt_spatial_exact_add(&net,&lift,&weight);
    nt_spatial_exact_number(&one,1,0);

    NtSpatialExact lever, torque_x, torque_z;
    nt_spatial_exact_number(&lever,-arm.z,0);
    nt_spatial_exact_multiply(&torque_x,&lever,&lift);
    nt_spatial_exact_number(&lever,arm.x,0);
    nt_spatial_exact_multiply(&torque_z,&lever,&lift);
    const NtBuoyancy result = {
        .lift={0,nt_spatial_exact_quotient(&lift,&one),0},
        .weight={0,nt_spatial_exact_quotient(&weight,&one),0},
        .force={0,nt_spatial_exact_quotient(&net,&one),0},
        .torque={nt_spatial_exact_quotient(&torque_x,&one),0,nt_spatial_exact_quotient(&torque_z,&one)}
    };
    const NtResult status = nt_simulation_finish(&saved,NT_RESULT_OK);
    if (status == NT_RESULT_OK) *out = result;
    return status;
}
