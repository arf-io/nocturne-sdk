#include "spatial_projection_internal.h"
#include "spatial_geometry_internal.h"
#include <nocturne/spatial_surface.h>

typedef struct ContactGeometry {
    NtSpatialFraction time;
    NtSpatialWeights weights;
    NtSpatialExact normal[3], scale;
} ContactGeometry;

static unsigned dominant_axis(const NtSpatialExact vector[3], NtSpatialExact *scale)
{
    unsigned axis = 3;
    nt_spatial_exact_number(scale,0,0);
    for (unsigned i = 0; i < 3; ++i) {
        NtSpatialExact size;
        nt_spatial_exact_copy(&size,&vector[i]);
        size.negative = false;
        if (nt_spatial_exact_compare(&size,scale) > 0) {
            nt_spatial_exact_copy(scale,&size);
            axis = i;
        }
    }
    return axis;
}

static void endpoint_weights(const NtSpatialExact ab[3], const NtSpatialExact ac[3],
                             const NtSpatialExact displacement[3], const NtSpatialExact *area,
                             unsigned axis, NtSpatialExact out[3])
{
    const unsigned x = (axis+1)%3, y = (axis+2)%3;
    nt_exact_determinant(&displacement[x],&displacement[y],&ac[x],&ac[y],&out[1]);
    nt_exact_determinant(&ab[x],&ab[y],&displacement[x],&displacement[y],&out[2]);
    NtSpatialExact sum;
    nt_spatial_exact_add(&sum,&out[1],&out[2]);
    nt_spatial_exact_subtract(&out[0],area,&sum);
    if (area->negative)
        for (unsigned i = 0; i < 3; ++i) nt_spatial_exact_negate(&out[i]);
}

static bool contact_time(const NtSpatialExact *first_plane, const NtSpatialExact *last_plane,
                          const NtSpatialExact first[3], const NtSpatialExact last[3],
                          NtSpatialFraction *out)
{
    if (!first_plane->count && !last_plane->count) {
        NtSpatialFraction end;
        nt_spatial_fraction_set(out,0);
        nt_spatial_fraction_set(&end,1);
        for (unsigned i = 0; i < 3; ++i)
            if (!nt_spatial_clip_fraction(out,&end,&first[i],&last[i])) return false;
        return true;
    }
    if (nt_spatial_exact_sign(first_plane) == nt_spatial_exact_sign(last_plane)) return false;
    nt_spatial_exact_copy(&out->numerator,first_plane);
    out->numerator.negative = false;
    nt_spatial_exact_subtract(&out->divisor,first_plane,last_plane);
    out->divisor.negative = false;
    return true;
}

static void moving_endpoint(NtVec3 point, NtVec3 origin, NtVec3 movement, NtSpatialExact out[3])
{
    NtSpatialExact relative[3];
    nt_exact_displacement(point,origin,relative);
    const double values[] = {movement.x,movement.y,movement.z};
    for (unsigned i = 0; i < 3; ++i) {
        NtSpatialExact offset;
        nt_spatial_exact_number(&offset,values[i],0);
        nt_spatial_exact_subtract(&out[i],&relative[i],&offset);
    }
}

static bool triangle_contact(NtVec3 first, NtVec3 last, const NtSpatialSweepTriangle *triangle,
                              ContactGeometry *out)
{
    const NtVec3 a = triangle->initial[0];
    NtSpatialExact ab[3], ac[3];
    nt_exact_displacement(triangle->initial[1],a,ab);
    nt_exact_displacement(triangle->initial[2],a,ac);
    nt_exact_cross(ab,ac,out->normal);
    const unsigned axis = dominant_axis(out->normal,&out->scale);
    if (axis == 3) return false;

    NtSpatialExact start[3], end[3], first_plane, last_plane;
    nt_exact_displacement(first,a,start);
    moving_endpoint(last,a,triangle->displacement,end);
    nt_exact_dot(out->normal,start,&first_plane);
    nt_exact_dot(out->normal,end,&last_plane);
    const NtSpatialExact *area = &out->normal[axis];
    NtSpatialExact first_weights[3], last_weights[3];
    endpoint_weights(ab,ac,start,area,axis,first_weights);
    endpoint_weights(ab,ac,end,area,axis,last_weights);
    if (!contact_time(&first_plane,&last_plane,first_weights,last_weights,&out->time)) return false;

    nt_spatial_exact_multiply(&out->weights.divisor,&out->scale,&out->time.divisor);
    for (unsigned i = 0; i < 3; ++i) {
        nt_exact_mix(&first_weights[i],&last_weights[i],&out->time,&out->weights.value[i]);
        if (out->weights.value[i].negative) return false;
    }
    return true;
}

static double trajectory_coordinate(double first, double last, const NtSpatialFraction *time)
{
    NtSpatialExact a, b, blended;
    nt_spatial_exact_number(&a,first,0);
    nt_spatial_exact_number(&b,last,0);
    nt_exact_mix(&a,&b,time,&blended);
    return nt_spatial_exact_quotient(&blended,&time->divisor);
}

static NtSpatialContact contact_value(NtVec3 first, NtVec3 last, const NtSpatialSweepTriangle *triangle,
                                      size_t index, const ContactGeometry *geometry)
{
    const NtSpatialFraction *time = &geometry->time;
    double components[3];
    for (unsigned i = 0; i < 3; ++i)
        components[i] = nt_spatial_exact_quotient(&geometry->normal[i],&geometry->scale);
    const double length = hypot(hypot(components[0],components[1]),components[2]);
    return (NtSpatialContact){
        .object_id=triangle->object_id, .primitive_id=triangle->primitive_id, .triangle_index=index,
        .fraction=nt_spatial_exact_quotient(&time->numerator,&time->divisor),
        .position={trajectory_coordinate(first.x,last.x,time),trajectory_coordinate(first.y,last.y,time),
                   trajectory_coordinate(first.z,last.z,time)},
        .weights=nt_spatial_weights_value(&geometry->weights),
        .normal={components[0]/length,components[1]/length,components[2]/length}
    };
}

static bool valid_triangles(const NtSpatialSweepTriangle *triangles, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (!nt_spatial_finite_vec(triangles[i].displacement)) return false;
        for (unsigned j = 0; j < 3; ++j)
            if (!nt_spatial_finite_vec(triangles[i].initial[j])) return false;
    }
    return true;
}

NtResult nt_spatial_sweep_point(NtVec3 first, NtVec3 last, const NtSpatialSweepTriangle *triangles,
                                size_t count, bool *found, NtSpatialContact *out)
{
    if ((count && !triangles) || !found || !out) return NT_RESULT_INVALID_ARGUMENT;
    if (count > SIZE_MAX/sizeof(triangles[0])) return NT_RESULT_OUT_OF_RANGE;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_OUT_OF_RANGE;
    if (!nt_spatial_finite_vec(first) || !nt_spatial_finite_vec(last) || !valid_triangles(triangles,count))
        return nt_spatial_end_projection(&saved,NT_RESULT_INVALID_ARGUMENT);

    NtSpatialFraction best;
    NtSpatialContact result;
    bool hit = false;
    for (size_t i = 0; i < count; ++i) {
        ContactGeometry geometry;
        if (!triangle_contact(first,last,&triangles[i],&geometry)) continue;
        if (hit && nt_spatial_fraction_order(&geometry.time,&best) >= 0) continue;
        nt_spatial_fraction_copy(&best,&geometry.time);
        result = contact_value(first,last,&triangles[i],i,&geometry);
        hit = true;
    }
    if (!nt_finish_standard_fp(&saved)) return NT_RESULT_OUT_OF_RANGE;
    *found = hit;
    if (hit) *out = result;
    return NT_RESULT_OK;
}
