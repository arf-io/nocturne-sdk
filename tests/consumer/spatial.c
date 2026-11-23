#include <nocturne/spatial.h>

#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "spatial line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)
#define OK(call) CHECK((call) == NT_RESULT_OK)

static const double quarter_turn = 0x1.921fb54442d18p+0;

static bool close_vec(NtVec3 value, NtVec3 expected)
{
    return fabs(value.x - expected.x) <= 2e-13 * fmax(1, fabs(expected.x)) &&
           fabs(value.y - expected.y) <= 2e-13 * fmax(1, fabs(expected.y)) &&
           fabs(value.z - expected.z) <= 2e-13 * fmax(1, fabs(expected.z));
}

static int vectors_and_affine(void)
{
    const NtVec3 x = {1, 0, 0}, y = {0, 1, 0}, z = {0, 0, 1};
    NtVec3 result;
    double scalar;
    OK(nt_spatial_vec_add(x, y, &result));
    CHECK(close_vec(result, (NtVec3){1, 1, 0}));
    OK(nt_spatial_vec_subtract(x, y, &result));
    CHECK(close_vec(result, (NtVec3){1, -1, 0}));
    OK(nt_spatial_vec_scale(y, -2, &result));
    CHECK(close_vec(result, (NtVec3){0, -2, 0}));
    OK(nt_spatial_vec_cross(x, y, &result));
    CHECK(close_vec(result, z));
    OK(nt_spatial_vec_dot((NtVec3){DBL_MAX, DBL_MAX, 3}, (NtVec3){2, -2, 2}, &scalar));
    CHECK(scalar == 6);
    OK(nt_spatial_vec_length((NtVec3){2, 3, 6}, &scalar));
    CHECK(scalar == 7);
    const double tiny = nextafter(0, 1);
    OK(nt_spatial_vec_normalize((NtVec3){tiny, tiny, 0}, &result));
    CHECK(close_vec(result, (NtVec3){sqrt(.5), sqrt(.5), 0}));
    OK(nt_spatial_vec_normalize((NtVec3){DBL_MAX, DBL_MAX, 0}, &result));
    CHECK(close_vec(result, (NtVec3){sqrt(.5), sqrt(.5), 0}));
    CHECK(nt_spatial_vec_length((NtVec3){DBL_MAX, DBL_MAX, 0}, &scalar) == NT_RESULT_OUT_OF_RANGE);
    CHECK(scalar == 7);
    const NtVec3 edge = {0x1.feff55d05f4e4p+1023, 0x1.ff41ddaceaf7fp+1019, 0x1.133451915936ep+1016};
    OK(nt_spatial_vec_length(edge, &scalar));
    CHECK(scalar == nextafter(DBL_MAX, 0));

    NtSpatialTransformOptions options = nt_spatial_transform_options_default();
    options.translation = (NtVec3){3, 4, 5};
    options.pivot = x;
    options.scale = (NtVec3){2, -3, 4};
    options.rotation_axis = z;
    options.rotation_radians = quarter_turn;
    NtSpatialTransform first;
    OK(nt_spatial_transform_create(&options, &first));
    OK(nt_spatial_transform_point(&first, (NtVec3){2, 1, 2}, &result));
    CHECK(close_vec(result, (NtVec3){7, 6, 13}));
    OK(nt_spatial_transform_direction(&first, x, &result));
    CHECK(close_vec(result, (NtVec3){0, 2, 0}));

    NtSpatialTransform second = nt_spatial_transform_identity();
    second.linear[1][0] = 2;
    second.translation = y;
    NtSpatialTransform combined = second;
    OK(nt_spatial_transform_compose(&combined, &first, &combined));
    OK(nt_spatial_transform_point(&combined, (NtVec3){2, 1, 2}, &result));
    CHECK(close_vec(result, (NtVec3){7, 21, 13}));
    OK(nt_spatial_transform_inverse(&combined, &combined));
    OK(nt_spatial_transform_point(&combined, result, &result));
    CHECK(close_vec(result, (NtVec3){2, 1, 2}));

    const NtSpatialTransform wide = {.linear = {{0x1p800, 0, 0}, {0, 0x1p-600, 0}, {0, 0, 0x1p800}}};
    OK(nt_spatial_transform_inverse(&wide, &combined));
    CHECK(combined.linear[0][0] == 0x1p-800 && combined.linear[1][1] == 0x1p600 &&
          combined.linear[2][2] == 0x1p-800);
    const NtSpatialTransform singular = {0};
    CHECK(nt_spatial_transform_inverse(&singular, &combined) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(combined.linear[0][0] == 0x1p-800);
    second.linear[0][0] = DBL_MAX;
    second.linear[0][1] = -DBL_MAX;
    second.translation = (NtVec3){7, 0, 0};
    OK(nt_spatial_transform_point(&second, (NtVec3){2, 2, 1}, &result));
    CHECK(close_vec(result, (NtVec3){7, 6, 1}));
    CHECK(nt_spatial_transform_point(&second, (NtVec3){2, 0, 0}, &result) == NT_RESULT_OUT_OF_RANGE);
    CHECK(result.x == 7);
    return 0;
}

typedef struct Budget { size_t size, alignment; unsigned calls, live; bool refuse; } Budget;

static void *reserve(void *context, size_t size, size_t alignment)
{
    Budget *budget = context;
    ++budget->calls;
    if (budget->refuse || budget->live) return NULL;
    void *memory = malloc(size);
    if (memory) {
        budget->size = size;
        budget->alignment = alignment;
        ++budget->live;
    }
    return memory;
}

static void release(void *context, void *memory, size_t size, size_t alignment)
{
    Budget *budget = context;
    if (budget->live != 1 || size != budget->size || alignment != budget->alignment) abort();
    --budget->live;
    free(memory);
}

static int paths(void)
{
    NtVec3 points[] = {{0, 0, 0}, {3, 4, 0}, {3, 4, 12}};
    NtSpatialPathOptions options = {points, 3, false, NT_PING_PONG};
    Budget budget = {.refuse = true};
    const NtAllocator allocator = {&budget, reserve, release};
    NtSpatialPath *path = NULL;
    CHECK(nt_spatial_path_create(&allocator, &options, &path) == NT_RESULT_ALLOCATION_FAILED && !path);
    budget.refuse = false;
    OK(nt_spatial_path_create(&allocator, &options, &path));
    CHECK(budget.calls == 2 && budget.live == 1);
    points[0].x = 999;
    budget.refuse = true;
    double length;
    OK(nt_spatial_path_length(path, &length));
    CHECK(length == 17);
    NtSpatialPathSample sample;
    OK(nt_spatial_path_sample(path, 2.5, &sample));
    CHECK(close_vec(sample.position, (NtVec3){1.5, 2, 0}));
    CHECK(close_vec(sample.tangent, (NtVec3){.6, .8, 0}));
    OK(nt_spatial_path_sample(path, 5, &sample));
    CHECK(close_vec(sample.tangent, (NtVec3){0, 0, 1}));
    OK(nt_spatial_path_sample(path, -5, &sample));
    CHECK(sample.distance == 5 && close_vec(sample.tangent, (NtVec3){-.6, -.8, 0}));
    OK(nt_spatial_path_sample(path, 17, &sample));
    CHECK(sample.position.z == 12 && sample.tangent.z == -1);
    OK(nt_spatial_path_sample(path, 34, &sample));
    CHECK(sample.distance == 0 && sample.position.x == 0 && sample.tangent.x > 0);
    CHECK(nt_spatial_path_sample(path, NAN, &sample) == NT_RESULT_INVALID_ARGUMENT && sample.distance == 0);
    nt_spatial_path_destroy(path);
    CHECK(!budget.live && budget.calls == 2);

    points[0].x = 0;
    options.closed = true;
    options.boundary = NT_LOOP;
    OK(nt_spatial_path_create(NULL, &options, &path));
    OK(nt_spatial_path_sample(path, -6.5, &sample));
    CHECK(sample.length == 30 && close_vec(sample.position, (NtVec3){1.5, 2, 6}));
    CHECK(close_vec(sample.tangent, (NtVec3){-3.0/13, -4.0/13, -12.0/13}));
    nt_spatial_path_destroy(path);

    points[2] = points[1];
    path = NULL;
    CHECK(nt_spatial_path_create(NULL, &options, &path) == NT_RESULT_INVALID_ARGUMENT && !path);
    options.count = SIZE_MAX;
    CHECK(nt_spatial_path_create(NULL, &options, &path) == NT_RESULT_OUT_OF_RANGE && !path);
    const NtVec3 wide[] = {{0, 0, 0}, {0x1p1023, 0, 0}};
    options = (NtSpatialPathOptions){wide, 2, false, NT_PING_PONG};
    OK(nt_spatial_path_create(NULL, &options, &path));
    OK(nt_spatial_path_sample(path, -0x1p1022, &sample));
    CHECK(sample.position.x == 0x1p1022 && sample.tangent.x == -1);
    for (int exponent = -1074; exponent < 1023; exponent += 73) {
        const double distance = scalbn(1, exponent);
        OK(nt_spatial_path_sample(path, distance, &sample));
        CHECK(sample.position.x == distance && sample.distance == distance && sample.tangent.x == 1);
        OK(nt_spatial_path_sample(path, -distance, &sample));
        CHECK(sample.position.x == distance && sample.distance == distance && sample.tangent.x == -1);
    }
    nt_spatial_path_destroy(path);
    const NtVec3 edge[] = {{0}, {0x1.feff55d05f4e4p+1023, 0x1.ff41ddaceaf7fp+1019, 0x1.133451915936ep+1016}};
    options = (NtSpatialPathOptions){edge, 2, false, NT_ONCE};
    OK(nt_spatial_path_create(NULL, &options, &path));
    OK(nt_spatial_path_length(path, &length));
    CHECK(length == nextafter(DBL_MAX, 0));
    nt_spatial_path_destroy(path);
    const NtVec3 cumulative[] = {{0}, {1, 0, 0}, {2, 0x1.8p-26, 0}};
    options = (NtSpatialPathOptions){cumulative, 3, false, NT_ONCE};
    OK(nt_spatial_path_create(NULL, &options, &path));
    OK(nt_spatial_path_length(path, &length));
    CHECK(length == 0x1.0000000000001p+1);
    nt_spatial_path_destroy(path);
    return 0;
}

static int cameras(void)
{
    const NtVec3 origin = {0}, up = {0, 1, 0}, eye = {0, 0, 5};
    NtSpatialCamera camera;
    NtVec3 view;
    OK(nt_spatial_camera_look_at(eye, origin, up, &camera));
    OK(nt_spatial_camera_to_view(&camera, (NtVec3){2, 3, 1}, &view));
    CHECK(close_vec(view, (NtVec3){2, 3, -4}));
    OK(nt_spatial_camera_fly(&camera, (NtVec3){1, 2, -3}, quarter_turn, 0, 0, &camera));
    CHECK(close_vec(camera.position, (NtVec3){1, 2, 2}));
    OK(nt_spatial_camera_to_view(&camera, (NtVec3){-2, 2, 2}, &view));
    CHECK(close_vec(view, (NtVec3){0, 0, -3}));
    OK(nt_spatial_camera_orbit(origin, 5, quarter_turn, quarter_turn, &camera));
    CHECK(close_vec(camera.position, (NtVec3){0, 5, 0}));
    OK(nt_spatial_camera_to_view(&camera, origin, &view));
    CHECK(close_vec(view, (NtVec3){0, 0, -5}));
    CHECK(nt_spatial_camera_orbit(origin, -1, 0, 0, &camera) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(nt_spatial_camera_look_at(origin, origin, up, &camera) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(nt_spatial_camera_look_at(eye, origin, eye, &camera) == NT_RESULT_INVALID_ARGUMENT);

    OK(nt_spatial_camera_look_at((NtVec3){DBL_MAX, 0, 0}, (NtVec3){-DBL_MAX, 0, 0},
                                 (NtVec3){DBL_MAX, nextafter(0, 1), 0}, &camera));
    CHECK(close_vec(camera.backward, (NtVec3){1, 0, 0}));
    OK(nt_spatial_camera_to_view(&camera, (NtVec3){DBL_MAX, 2, 3}, &view));
    CHECK(close_vec(view, (NtVec3){-3, 2, 0}));
    camera.up = camera.right;
    CHECK(nt_spatial_camera_to_view(&camera, origin, &view) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(view.x == -3);
    return 0;
}

static int numeric_environment(void)
{
#if !defined(__wasm__)
    fenv_t saved;
    CHECK(fegetenv(&saved) == 0);
    CHECK(fesetround(FE_DOWNWARD) == 0);
    CHECK(feclearexcept(FE_ALL_EXCEPT) == 0 && feraiseexcept(FE_DIVBYZERO) == 0);
    double dot;
    OK(nt_spatial_vec_dot((NtVec3){1, 0x1p-52, 0x1p-54}, (NtVec3){1, .5, 1}, &dot));
    CHECK(dot == 0x1.0000000000001p0);
    CHECK(fegetround() == FE_DOWNWARD && fetestexcept(FE_ALL_EXCEPT) == FE_DIVBYZERO);
    CHECK(fesetenv(&saved) == 0);
#endif
    return 0;
}

int main(void)
{
    return vectors_and_affine() || paths() || cameras() || numeric_environment();
}
