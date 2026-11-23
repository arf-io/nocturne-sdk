#include <nocturne/spatial.h>

#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "spatial projection line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)
#define OK(call) CHECK((call) == NT_RESULT_OK)

static const NtSpatialProjection perspective = {NT_SPATIAL_PERSPECTIVE, 2, 1, 1, 10};
static const NtSpatialProjection orthographic = {NT_SPATIAL_ORTHOGRAPHIC, 4, 2, 1, 10};

static int projection(void)
{
    bool visible = false;
    NtSpatialProjected sample = {0};
    NtVec3 view;
    OK(nt_spatial_project(&perspective, (NtVec3){6, -1.5, -3}, &visible, &sample));
    CHECK(visible && sample.screen.x == 1 && sample.screen.y == -.5 && sample.depth == 3);
    OK(nt_spatial_unproject(&perspective, sample.screen, sample.depth, &view));
    CHECK(view.x == 6 && view.y == -1.5 && view.z == -3);
    OK(nt_spatial_project(&perspective, (NtVec3){nextafter(6, INFINITY), -1.5, -3}, &visible, &sample));
    CHECK(!visible && sample.depth == 3);
    OK(nt_spatial_project(&orthographic, (NtVec3){4, -1, -10}, &visible, &sample));
    CHECK(visible && sample.screen.x == 1 && sample.screen.y == -.5 && sample.depth == 10);
    OK(nt_spatial_unproject(&orthographic, sample.screen, 2, &view));
    CHECK(view.x == 4 && view.y == -1 && view.z == -2);
    CHECK(nt_spatial_unproject(&perspective, (NtSpatialScreen){2, 0}, 2, &view) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(view.x == 4);
    CHECK(nt_spatial_project(&perspective, (NtVec3){NAN, 0, 0}, &visible, &sample) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(visible && sample.depth == 10);
    const NtSpatialProjection wide = {NT_SPATIAL_PERSPECTIVE, DBL_MAX, 1, 1, DBL_MAX};
    OK(nt_spatial_project(&wide, (NtVec3){DBL_MAX, 1, -2}, &visible, &sample));
    CHECK(visible && sample.screen.x == .5 && sample.screen.y == .5);
    OK(nt_spatial_unproject(&wide, sample.screen, 2, &view));
    CHECK(view.x == DBL_MAX);
    CHECK(nt_spatial_unproject(&wide, (NtSpatialScreen){1, 0}, 2, &view) == NT_RESULT_OUT_OF_RANGE);
    const NtSpatialProjection skinny = {NT_SPATIAL_PERSPECTIVE, 0x1p-1074, 1, 1, DBL_MAX};
    OK(nt_spatial_unproject(&skinny, (NtSpatialScreen){0x1p-50, 0}, 0x1p1023, &view));
    CHECK(view.x == 0x1p-101);
    return 0;
}

static int clipping(void)
{
    bool visible = false;
    NtSpatialClippedSegment segment;
    OK(nt_spatial_clip_segment(&orthographic, (NtVec3){-12, 0, -2}, (NtVec3){20, 0, -2},
                               &visible, &segment));
    CHECK(visible && segment.first.view.x == -4 && segment.last.view.x == 4);
    CHECK(segment.first.weights.y == .25 && segment.last.weights.y == .5);
    CHECK(segment.first.projected.screen.x == -1 && segment.last.projected.screen.x == 1);
    OK(nt_spatial_clip_segment(&perspective, (NtVec3){0, 0, 1}, (NtVec3){0, 0, -15},
                               &visible, &segment));
    CHECK(visible && segment.first.view.z == -1 && segment.last.view.z == -10);
    CHECK(segment.first.weights.y == .125 && segment.last.weights.y == .6875);
    OK(nt_spatial_clip_segment(&orthographic, (NtVec3){-DBL_MAX, 0, -5}, (NtVec3){DBL_MAX, 0, -5},
                               &visible, &segment));
    CHECK(visible && segment.first.view.x == -4 && segment.last.view.x == 4);
    CHECK(segment.first.weights.y == .5 && segment.last.weights.y == .5);
    OK(nt_spatial_clip_segment(&orthographic, (NtVec3){4, 3, -2}, (NtVec3){5, 2, -2},
                               &visible, &segment));
    CHECK(!visible && segment.first.view.x == -4);
    OK(nt_spatial_clip_segment(&orthographic, (NtVec3){3, 3, -2}, (NtVec3){5, 1, -2},
                               &visible, &segment));
    CHECK(visible && segment.first.view.x == 4 && segment.first.view.y == 2);
    CHECK(segment.last.view.x == 4 && segment.last.view.y == 2);

    const NtVec3 triangle[] = {{-8,-1,-2},{2,-1,-2},{2,1,-2}};
    NtSpatialClippedPolygon polygon;
    OK(nt_spatial_clip_triangle(&orthographic, triangle, &polygon));
    CHECK(polygon.count == 4);
    double area = 0;
    size_t boundary = 0;
    for (size_t i = 0; i < polygon.count; ++i) {
        const NtSpatialClipVertex a = polygon.vertices[i], b = polygon.vertices[(i+1)%polygon.count];
        CHECK(a.projected.depth == 2 && a.weights.x >= 0 && a.weights.y >= 0 && a.weights.z >= 0);
        CHECK(fabs(a.weights.x + a.weights.y + a.weights.z - 1) <= 1e-14);
        CHECK(fabs(a.view.x - (-8*a.weights.x + 2*a.weights.y + 2*a.weights.z)) <= 1e-14);
        if (a.projected.screen.x == -1) ++boundary;
        area += a.view.x*b.view.y - a.view.y*b.view.x;
    }
    CHECK(boundary == 2 && area > 0);
    const NtVec3 corner[] = {{4,2,-2},{4,3,-2},{5,2,-2}};
    OK(nt_spatial_clip_triangle(&orthographic, corner, &polygon));
    CHECK(polygon.count == 0);
    return 0;
}

static int picking(void)
{
    NtSpatialPickTriangle geometry[] = {
        {{{-2,-2,-6},{2,-2,-6},{0,2,-6}}, 41, 7},
        {{{-2,-2,-3},{0,2,-3},{2,-2,-3}}, 42, 9},
        {{{-2,-2,-3},{2,-2,-3},{0,2,-3}}, 1, 1},
        {{{-2,-2,1},{2,-2,1},{0,2,1}}, 0, 0}
    };
    bool found = false;
    NtSpatialHit hit;
    const NtSpatialScreen center = {0};
    OK(nt_spatial_pick(&perspective, center, geometry, 4, &found, &hit));
    CHECK(found && hit.object_id == 42 && hit.primitive_id == 9 && hit.triangle_index == 1);
    CHECK(hit.depth == 3 && hit.view.x == 0 && hit.view.y == 0 && hit.view.z == -3);
    CHECK(hit.weights.x == .25 && hit.weights.y == .5 && hit.weights.z == .25);
    OK(nt_spatial_pick(&orthographic, (NtSpatialScreen){.5,-1}, geometry, 4, &found, &hit));
    CHECK(found && hit.weights.z == 1);
    OK(nt_spatial_pick(&orthographic, (NtSpatialScreen){1, 1}, geometry, 4, &found, &hit));
    CHECK(!found && hit.object_id == 42);
    OK(nt_spatial_pick(&perspective, center, NULL, 0, &found, &hit));
    CHECK(!found && hit.object_id == 42);
    geometry[3].view[0].z = NAN;
    CHECK(nt_spatial_pick(&perspective, center, geometry, 4, &found, &hit) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(!found && hit.object_id == 42);

    NtSpatialPickTriangle close[] = {
        {{{-1,-1,-3},{1,-1,-3},{0,1,-3}}, 11, 0},
        {{{-1,-1,-3},{1,-1,-3},{0,1,nextafter(-3, 0)}}, 12, 0}
    };
    OK(nt_spatial_pick(&perspective, (NtSpatialScreen){0,-.25}, close, 2, &found, &hit));
    CHECK(found && hit.depth == 3 && hit.object_id == 12);
    return 0;
}

static int numeric_environment(void)
{
#if !defined(__wasm__)
    fenv_t saved;
    CHECK(fegetenv(&saved) == 0 && fesetround(FE_DOWNWARD) == 0);
    CHECK(feclearexcept(FE_ALL_EXCEPT) == 0 && feraiseexcept(FE_DIVBYZERO) == 0);
    const NtSpatialProjection p = {NT_SPATIAL_ORTHOGRAPHIC, 10, 1, 0x1p-1074, 1};
    bool visible = false;
    NtSpatialProjected value;
    OK(nt_spatial_project(&p, (NtVec3){1, 0, -0x1p-1074}, &visible, &value));
    CHECK(visible && value.screen.x == 0x1.999999999999ap-4);
    CHECK(fegetround() == FE_DOWNWARD && fetestexcept(FE_ALL_EXCEPT) == FE_DIVBYZERO);
    const uint64_t signaling_bits = UINT64_C(0x7ff0000000000001);
    double signaling_nan;
    memcpy(&signaling_nan, &signaling_bits, sizeof(signaling_nan));
    CHECK(nt_spatial_project(&p, (NtVec3){signaling_nan,0,-1}, &visible, &value) == NT_RESULT_INVALID_ARGUMENT);
    NtVec3 view = {17,18,19};
    CHECK(nt_spatial_unproject(&p, (NtSpatialScreen){0,0}, signaling_nan, &view) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(visible && value.screen.x == 0x1.999999999999ap-4 && view.x == 17);
    CHECK(fegetround() == FE_DOWNWARD && fetestexcept(FE_ALL_EXCEPT) == FE_DIVBYZERO);
    CHECK(fesetenv(&saved) == 0);
#endif
    return 0;
}

int main(void)
{
    return projection() || clipping() || picking() || numeric_environment();
}
