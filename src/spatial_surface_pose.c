#include "spatial_projection_internal.h"
#include "spatial_geometry_internal.h"
#include "spatial_norm_internal.h"
#include <nocturne/spatial_surface.h>

static bool motion_finite(const NtSpatialSurfaceMotion *motion, double seconds)
{
    return nt_spatial_finite_vec(motion->pivot) && nt_spatial_finite_vec(motion->linear_velocity) &&
        nt_spatial_finite_vec(motion->angular_velocity) && isfinite(seconds);
}

static NtResult motion_rotation(const NtSpatialSurfaceMotion *motion, double seconds, NtSpatialTransform *out)
{
    const NtVec3 omega = motion->angular_velocity;
    const double rates[3] = {omega.x,omega.y,omega.z};
    NtScaledSum swept[3] = {0};
    const NtScaledTerm time = nt_scaled_term(seconds,0);
    for (unsigned i = 0; i < 3; ++i)
        nt_scaled_add_product(&swept[i],nt_scaled_term(rates[i],0),time);
    const NtScaledSum squared = nt_spatial_norm_squared(swept);
    const double angle = nt_spatial_round_length(&squared,0);
    if (!isfinite(angle)) return NT_RESULT_OUT_OF_RANGE;
    NtSpatialTransformOptions options = nt_spatial_transform_options_default();
    options.rotation_axis = omega;
    options.rotation_radians = copysign(angle,seconds);
    return nt_spatial_transform_create(&options,out);
}

static void exact_vec(NtVec3 point, NtSpatialExact out[3])
{
    nt_spatial_exact_number(&out[0],point.x,0);
    nt_spatial_exact_number(&out[1],point.y,0);
    nt_spatial_exact_number(&out[2],point.z,0);
}

static NtVec3 position_at(const NtSpatialSurfaceMotion *motion, const NtSpatialTransform *rotation,
                          double seconds, NtVec3 initial, NtSpatialExact rotated[3])
{
    const NtVec3 pivot = motion->pivot, linear = motion->linear_velocity;
    const double origins[] = {pivot.x,pivot.y,pivot.z}, rates[] = {linear.x,linear.y,linear.z};
    NtSpatialExact relative[3], time, one;
    nt_exact_displacement(initial,pivot,relative);
    nt_spatial_exact_number(&time,seconds,0);
    nt_spatial_exact_number(&one,1,0);
    double position[3];
    for (unsigned i = 0; i < 3; ++i) {
        const double *row = rotation->linear[i];
        NtSpatialExact coefficients[3], rate, offset, origin, moved, result;
        exact_vec((NtVec3){row[0],row[1],row[2]},coefficients);
        nt_exact_dot(coefficients,relative,&rotated[i]);
        nt_spatial_exact_number(&rate,rates[i],0);
        nt_spatial_exact_multiply(&offset,&rate,&time);
        nt_spatial_exact_number(&origin,origins[i],0);
        nt_spatial_exact_add(&moved,&origin,&offset);
        nt_spatial_exact_add(&result,&moved,&rotated[i]);
        position[i] = nt_spatial_exact_quotient(&result,&one);
    }
    return (NtVec3){position[0],position[1],position[2]};
}

static NtVec3 velocity_at(const NtSpatialSurfaceMotion *motion, const NtSpatialExact rotated[3])
{
    NtSpatialExact omega[3], linear[3], tangent[3], one;
    exact_vec(motion->angular_velocity,omega);
    exact_vec(motion->linear_velocity,linear);
    nt_exact_cross(omega,rotated,tangent);
    nt_spatial_exact_number(&one,1,0);
    double velocity[3];
    for (unsigned i = 0; i < 3; ++i) {
        NtSpatialExact sum;
        nt_spatial_exact_add(&sum,&linear[i],&tangent[i]);
        velocity[i] = nt_spatial_exact_quotient(&sum,&one);
    }
    return (NtVec3){velocity[0],velocity[1],velocity[2]};
}

NtResult nt_spatial_surface_pose(const NtSpatialSurfaceMotion *motion, double seconds,
                                 NtSpatialTransform *out)
{
    if (!motion || !out) return NT_RESULT_INVALID_ARGUMENT;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_OUT_OF_RANGE;
    if (!motion_finite(motion,seconds)) return nt_spatial_end_projection(&saved,NT_RESULT_INVALID_ARGUMENT);
    NtSpatialTransform value;
    NtResult status = motion_rotation(motion,seconds,&value);
    if (status == NT_RESULT_OK) {
        NtSpatialExact rotated[3];
        value.translation = position_at(motion,&value,seconds,(NtVec3){0},rotated);
        if (!nt_spatial_finite_vec(value.translation)) status = NT_RESULT_OUT_OF_RANGE;
    }
    status = nt_spatial_end_projection(&saved,status);
    if (status == NT_RESULT_OK) *out = value;
    return status;
}

NtResult nt_spatial_surface_sample(const NtSpatialSurfaceMotion *motion, double seconds,
                                   NtVec3 initial_point, NtSpatialSurfaceSample *out)
{
    if (!motion || !out) return NT_RESULT_INVALID_ARGUMENT;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_OUT_OF_RANGE;
    if (!motion_finite(motion,seconds) || !nt_spatial_finite_vec(initial_point))
        return nt_spatial_end_projection(&saved,NT_RESULT_INVALID_ARGUMENT);
    NtSpatialTransform rotation;
    NtSpatialSurfaceSample value;
    NtResult status = motion_rotation(motion,seconds,&rotation);
    if (status == NT_RESULT_OK) {
        NtSpatialExact rotated[3];
        value.position = position_at(motion,&rotation,seconds,initial_point,rotated);
        value.velocity = velocity_at(motion,rotated);
        if (!nt_spatial_finite_vec(value.position) || !nt_spatial_finite_vec(value.velocity))
            status = NT_RESULT_OUT_OF_RANGE;
    }
    status = nt_spatial_end_projection(&saved,status);
    if (status == NT_RESULT_OK) *out = value;
    return status;
}
