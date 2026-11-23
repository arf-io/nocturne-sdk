#include <nocturne/spatial_surface.h>

#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr,"spatial surface line %d: %s\n",__LINE__,#condition); return 1; \
} } while (0)
#define OK(call) CHECK((call) == NT_RESULT_OK)

static bool close_vec(NtVec3 actual, NtVec3 expected)
{
    return fabs(actual.x-expected.x) <= 1e-14 && fabs(actual.y-expected.y) <= 1e-14 &&
        fabs(actual.z-expected.z) <= 1e-14;
}

static int pose_and_velocity(void)
{
    const NtSpatialSurfaceMotion motion = {{2,3,4},{2,-1,.5},{0,0,1}};
    const NtVec3 initial = {4,3,4};
    const double quarter = 0x1.921fb54442d18p0;
    NtSpatialSurfaceSample sample;
    OK(nt_spatial_surface_sample(&motion,quarter,initial,&sample));
    CHECK(close_vec(sample.position,(NtVec3){2+2*quarter,5-quarter,4+.5*quarter}));
    CHECK(close_vec(sample.velocity,(NtVec3){0,-1,.5}));
    NtSpatialTransform pose;
    OK(nt_spatial_surface_pose(&motion,quarter,&pose));
    NtVec3 transformed;
    OK(nt_spatial_transform_point(&pose,initial,&transformed));
    CHECK(close_vec(transformed,sample.position));

    OK(nt_spatial_surface_sample(&motion,-quarter,initial,&sample));
    CHECK(close_vec(sample.position,(NtVec3){2-2*quarter,1+quarter,4-.5*quarter}));
    CHECK(close_vec(sample.velocity,(NtVec3){4,-1,.5}));
    OK(nt_spatial_surface_sample(&motion,0,initial,&sample));
    CHECK(close_vec(sample.position,initial) && close_vec(sample.velocity,(NtVec3){2,1,.5}));

    const NtSpatialSurfaceMotion wide = {{-DBL_MAX,0,0},{DBL_MAX,0,0},{0,0,0}};
    OK(nt_spatial_surface_sample(&wide,2,(NtVec3){-DBL_MAX,0,0},&sample));
    CHECK(sample.position.x == DBL_MAX && sample.velocity.x == DBL_MAX);
    const NtSpatialTransform prior = pose;
    CHECK(nt_spatial_surface_pose(&wide,2,&pose) == NT_RESULT_OUT_OF_RANGE);
    CHECK(pose.translation.x == prior.translation.x && pose.linear[0][1] == prior.linear[0][1]);

    const NtSpatialSurfaceMotion fast = {{0,0,0},{0,0,0},{DBL_MAX,DBL_MAX,0}};
    OK(nt_spatial_surface_pose(&fast,DBL_MIN,&pose));
    OK(nt_spatial_transform_direction(&pose,(NtVec3){1,1,0},&transformed));
    CHECK(close_vec(transformed,(NtVec3){1,1,0}));
    CHECK(nt_spatial_surface_pose(&fast,2,&pose) == NT_RESULT_OUT_OF_RANGE);
    return 0;
}

static int contacts(void)
{
    const NtSpatialSweepTriangle triangles[] = {
        {{{0,0,0},{2,0,0},{0,2,0}},{0,0,0},11,101},
        {{{0,0,0},{2,0,0},{0,2,0}},{0,0,2},12,102},
        {{{0,0,0},{0,2,0},{2,0,0}},{0,0,2},13,103},
        {{{0,0,2},{0,0,2},{0,0,2}},{0,0,0},14,104}
    };
    NtSpatialContact hit;
    bool found;
    OK(nt_spatial_sweep_point((NtVec3){.5,.5,3},(NtVec3){.5,.5,-1},triangles,4,&found,&hit));
    CHECK(found && hit.fraction == .5 && hit.object_id == 12 && hit.primitive_id == 102 && hit.triangle_index == 1);
    CHECK(close_vec(hit.position,(NtVec3){.5,.5,1}) && close_vec(hit.weights,(NtVec3){.5,.25,.25}));
    CHECK(close_vec(hit.normal,(NtVec3){0,0,1}));

    OK(nt_spatial_sweep_point((NtVec3){-2,1,0},(NtVec3){2,1,0},triangles,1,&found,&hit));
    CHECK(found && hit.fraction == .5 && close_vec(hit.position,(NtVec3){0,1,0}));
    CHECK(close_vec(hit.weights,(NtVec3){.5,0,.5}));
    OK(nt_spatial_sweep_point((NtVec3){0,0,0},(NtVec3){-2,1,0},triangles,1,&found,&hit));
    CHECK(found && hit.fraction == 0 && close_vec(hit.weights,(NtVec3){1,0,0}));
    OK(nt_spatial_sweep_point((NtVec3){0,2,3},(NtVec3){0,2,0},triangles,1,&found,&hit));
    CHECK(found && hit.fraction == 1 && close_vec(hit.weights,(NtVec3){0,0,1}));
    OK(nt_spatial_sweep_point((NtVec3){0,0,0},(NtVec3){0,0,0},triangles+2,1,&found,&hit));
    CHECK(found && hit.fraction == 0 && close_vec(hit.normal,(NtVec3){0,0,-1}));

    OK(nt_spatial_sweep_point((NtVec3){-1,0,0},(NtVec3){0,-1,0},triangles,1,&found,&hit));
    CHECK(!found && hit.primitive_id == 103);
    OK(nt_spatial_sweep_point((NtVec3){0},(NtVec3){0},NULL,0,&found,&hit));
    CHECK(!found && hit.primitive_id == 103);
    return 0;
}

static int wide_range_and_rollback(void)
{
    NtSpatialSweepTriangle triangles[] = {
        {{{-DBL_MAX,0,0},{DBL_MAX,0,0},{0,2,0}},{0,0,0},1,20},
        {{{-DBL_MAX,0,0x1p-1074},{DBL_MAX,0,0x1p-1074},{0,2,0x1p-1074}},{0,0,0},2,21}
    };
    const NtVec3 first = {0,0,DBL_MAX}, last = {0,0,-DBL_MAX};
    NtSpatialContact hit = {0};
    bool found = false;
    OK(nt_spatial_sweep_point(first,last,triangles,2,&found,&hit));
    CHECK(found && hit.object_id == 2 && hit.fraction == .5 && hit.position.z == 0x1p-1074);
    CHECK(close_vec(hit.weights,(NtVec3){.5,.5,0}));

    const uint64_t signaling_nan = UINT64_C(0x7ff0000000000001);
    memcpy(&triangles[1].displacement.x,&signaling_nan,sizeof(signaling_nan));
#if !defined(__wasm__)
    fenv_t saved;
    CHECK(fegetenv(&saved) == 0);
    CHECK(fesetround(FE_UPWARD) == 0 && feclearexcept(FE_ALL_EXCEPT) == 0 && feraiseexcept(FE_DIVBYZERO) == 0);
#endif
    CHECK(nt_spatial_sweep_point(first,last,triangles,2,&found,&hit) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(found && hit.object_id == 2);
    const NtSpatialSurfaceMotion invalid = {{0,0,0},triangles[1].displacement,{0,0,0}};
    NtSpatialSurfaceSample sample = {{1,2,3},{4,5,6}};
    CHECK(nt_spatial_surface_sample(&invalid,0,(NtVec3){0},&sample) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(sample.position.x == 1 && sample.velocity.z == 6);
#if !defined(__wasm__)
    CHECK(fegetround() == FE_UPWARD && fetestexcept(FE_ALL_EXCEPT) == FE_DIVBYZERO);
    CHECK(fesetenv(&saved) == 0);
#endif
    return 0;
}

int main(void)
{
    return pose_and_velocity() || contacts() || wide_range_and_rollback();
}
