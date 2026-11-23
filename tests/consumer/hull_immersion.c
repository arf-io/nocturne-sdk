#include <nocturne/simulation.h>
#include <fenv.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "simulation_worker.h"

#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr,"hull immersion line %d: %s\n",__LINE__,#test); return 1; \
} } while (0)
#define OK(call) CHECK((call)==NT_RESULT_OK)

static const NtVec3 cube[] = {
    {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
    {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}
};
static const NtHullTriangle cube_faces[] = {
    {{0,2,1}},{{0,3,2}},{{4,5,6}},{{4,6,7}},{{0,1,5}},{{0,5,4}},
    {{3,7,6}},{{3,6,2}},{{0,4,7}},{{0,7,3}},{{1,2,6}},{{1,6,5}}
};
static const NtHullGeometry box = {cube,8,cube_faces,12};
static const NtFloatingBody still = {.mass=3,.principal_inertia={2,2,2}};

static bool close_value(double a, double b)
{
    return fabs(a-b)<=3e-12*fmax(1,fabs(b));
}

static bool close_point(NtVec3 a, NtVec3 b)
{
    return close_value(a.x,b.x) && close_value(a.y,b.y) && close_value(a.z,b.z);
}

static int planes(void)
{
    NtHull *hull;
    OK(nt_hull_create(NULL,&box,&hull));
    NtWaterPlane plane = {{0,0,0},{0,1,0}};
    NtHullImmersion wet;
    for (int height=-2;height<=2;++height) {
        plane.point.y = height;
        OK(nt_hull_immerse_plane(hull,&still,&plane,&wet));
        CHECK(wet.full_volume==8 && wet.displaced_volume==4*fmin(2,fmax(0,height+1)));
        CHECK(close_point(wet.center,(NtVec3){0,height==0 ? -.5 : 0,0}));
    }

    plane = (NtWaterPlane){{0,.25,0},{-.125,1,.125}};
    OK(nt_hull_immerse_plane(hull,&still,&plane,&wet));
    CHECK(close_value(wet.displaced_volume,5));
    CHECK(close_point(wet.center,(NtVec3){1.0/30,-89.0/240,-1.0/30}));
    NtBuoyancy force;
    OK(nt_hull_buoyancy(&wet,&still,1,10,&force));
    CHECK(close_point(force.lift,(NtVec3){0,50,0}));
    CHECK(close_point(force.weight,(NtVec3){0,-30,0}));
    CHECK(close_point(force.force,(NtVec3){0,20,0}));
    CHECK(close_point(force.torque,(NtVec3){5.0/3,0,5.0/3}));
    /* Positive normal scaling, including a subnormal normal, preserves the cut. */
    plane.normal = (NtVec3){-0x1p-1074,0x1p-1071,0x1p-1074};
    OK(nt_hull_immerse_plane(hull,&still,&plane,&wet));
    CHECK(close_value(wet.displaced_volume,5));
    CHECK(close_point(wet.center,(NtVec3){1.0/30,-89.0/240,-1.0/30}));

    NtFloatingBody translated = still;
    translated.position = (NtVec3){8,9,10};
    translated.rotation_axis = (NtVec3){0,0,1};
    translated.rotation_radians = acos(0.0);
    NtSpatialTransform pose;
    NtVec3 origin;
    OK(nt_floating_body_transform(&translated,&pose));
    OK(nt_spatial_transform_point(&pose,(NtVec3){0},&origin));
    CHECK(close_point(origin,translated.position));
    plane = (NtWaterPlane){translated.position,{0,1,0}};
    OK(nt_hull_immerse_plane(hull,&translated,&plane,&wet));
    CHECK(close_value(wet.displaced_volume,4));
    CHECK(close_point(wet.center,(NtVec3){8,8.5,10}));
    CHECK(close_point(wet.center_offset,(NtVec3){0,-.5,0}));
    plane.point.y -= 2;
    OK(nt_hull_immerse_plane(hull,&translated,&plane,&wet));
    OK(nt_hull_buoyancy(&wet,&translated,1,10,&force));
    CHECK(wet.displaced_volume==0 && close_point(force.force,(NtVec3){0,-30,0}));
    CHECK(close_point(force.torque,(NtVec3){0}));
    nt_hull_destroy(hull);
    return 0;
}

static int tetrahedron(void)
{
    const NtVec3 vertices[] = {{0,0,0},{1,0,0},{0,1,0},{0,0,1}};
    const NtHullTriangle faces[] = {{{0,2,1}},{{0,1,3}},{{0,3,2}},{{1,2,3}}};
    const NtHullGeometry geometry = {vertices,4,faces,4};
    NtHull *hull;
    OK(nt_hull_create(NULL,&geometry,&hull));
    NtHullImmersion wet;
    const NtWaterPlane surface = {{0,.5,0},{0,1,0}};
    OK(nt_hull_immerse_plane(hull,&still,&surface,&wet));
    CHECK(wet.full_volume==1.0/6 && close_value(wet.displaced_volume,7.0/48));
    CHECK(close_point(wet.center,(NtVec3){15.0/56,11.0/56,15.0/56}));
    nt_hull_destroy(hull);

    /* The thin body would collapse if each vertex were translated first. */
    NtVec3 thin[8];
    for (size_t i=0;i<8;++i) thin[i] = (NtVec3){cube[i].x,cube[i].y*0x1p-40,cube[i].z};
    const NtHullGeometry sheet = {thin,8,cube_faces,12};
    OK(nt_hull_create(NULL,&sheet,&hull));
    NtFloatingBody body = still;
    body.position.y = 999999;
    const NtWaterPlane level = {{0,999999,0},{0,1,0}};
    OK(nt_hull_immerse_plane(hull,&body,&level,&wet));
    CHECK(wet.full_volume==0x1p-37 && wet.displaced_volume==0x1p-38);
    CHECK(wet.center.y==body.position.y && wet.center_offset.y==-0x1p-41);
    nt_hull_destroy(hull);

    /* The cut lies between adjacent authored coordinates in the local mesh. */
    for (size_t i=0;i<8;++i)
        thin[i].y = cube[i].y<0 ? nextafter(1e6,0) : 1e6;
    OK(nt_hull_create(NULL,&sheet,&hull));
    body.position.y = -1e6;
    const NtWaterPlane between = {{0,-0x1p-34,0},{0,1,0}};
    OK(nt_hull_immerse_plane(hull,&body,&between,&wet));
    CHECK(wet.full_volume==0x1p-31 && wet.displaced_volume==0x1p-32);
    CHECK(wet.center.y==-3*0x1p-35);
    nt_hull_destroy(hull);
    return 0;
}

static int water_patches(void)
{
    NtVec3 points[8];
    for (size_t i=0;i<8;++i)
        points[i] = (NtVec3){(cube[i].x+1)/2,(cube[i].y+1)/2,(cube[i].z+1)/2};
    const NtHullGeometry unit = {points,8,cube_faces,12};
    NtHull *hull;
    OK(nt_hull_create(NULL,&unit,&hull));
    /* Projected diagonal partitions the field y=min(x,z). */
    NtWaterTriangle water[] = {{{{0,0,0},{1,0,0},{1,1,1}}},
                               {{{0,0,0},{0,0,1},{1,1,1}}}};
    NtHullImmersion wet;
    OK(nt_hull_immerse_water(hull,&still,water,2,&wet));
    CHECK(wet.full_volume==1 && close_value(wet.displaced_volume,1.0/3));
    CHECK(close_point(wet.center,(NtVec3){.625,.25,.625}));
    OK(nt_hull_immerse_water(hull,&still,water,1,&wet));
    CHECK(close_value(wet.displaced_volume,1.0/6));
    CHECK(close_point(wet.center,(NtVec3){.75,.25,.5}));
    for (unsigned i=0;i<2;++i)
        for (unsigned j=0;j<3;++j) water[i].vertices[j].y = .5;
    OK(nt_hull_immerse_water(hull,&still,water,2,&wet));
    CHECK(close_value(wet.displaced_volume,.5));
    CHECK(close_point(wet.center,(NtVec3){.5,.25,.5}));
    OK(nt_hull_immerse_water(hull,&still,NULL,0,&wet));
    CHECK(wet.full_volume==1 && wet.displaced_volume==0 && close_point(wet.center,(NtVec3){0}));
    CHECK(nt_hull_immerse_water(hull,&still,water,NT_WATER_MAX_TRIANGLES+1,&wet)==NT_RESULT_OUT_OF_RANGE);
    CHECK(wet.full_volume==1 && wet.displaced_volume==0);
    OK(nt_hull_immerse_water(hull,&still,water,1,&wet));
    const NtHullImmersion prior = wet;
    water[1] = water[0];
    CHECK(nt_hull_immerse_water(hull,&still,water,2,&wet)==NT_RESULT_INVALID_ARGUMENT);
    CHECK(wet.displaced_volume==prior.displaced_volume && close_point(wet.center,prior.center));
    water[0].vertices[1].x = 0;
    CHECK(nt_hull_immerse_water(hull,&still,water,1,&wet)==NT_RESULT_INVALID_ARGUMENT);
    CHECK(wet.displaced_volume==prior.displaced_volume);
    nt_hull_destroy(hull);
    return 0;
}

static int coplanar_patch(void)
{
    const double scale = 100000;
    NtVec3 points[8];
    for (size_t i=0;i<8;++i) {
        const double x = 1.5*scale*(cube[i].x+1);
        points[i] = (NtVec3){x,3*x+.5*scale*(cube[i].y+1),1.5*scale*(cube[i].z+1)};
    }
    const NtHullGeometry geometry = {points,8,cube_faces,12};
    NtHull *hull;
    OK(nt_hull_create(NULL,&geometry,&hull));
    NtWaterTriangle water = {{{scale,3*scale,-scale},{3*scale,9*scale,2*scale},{0,0,2*scale}}};
    const NtWaterPlane plane = {{0,0,0},{-3,1,0}};
    NtHullImmersion wet;
    OK(nt_hull_immerse_plane(hull,&still,&plane,&wet));
    CHECK(wet.displaced_volume==0);
    OK(nt_hull_immerse_water(hull,&still,&water,1,&wet));
    CHECK(wet.displaced_volume==0 && close_point(wet.center,(NtVec3){0}));
    for (unsigned i=0;i<3;++i) water.vertices[i].y += 100;
    OK(nt_hull_immerse_water(hull,&still,&water,1,&wet));
    CHECK(close_value(wet.displaced_volume,400*scale*scale));
    nt_hull_destroy(hull);
    return 0;
}

static int narrow_immersion(void)
{
    double edge = 999999;
    const double height = 0x1p-33;
    NtVec3 points[8];
    for (size_t i=0;i<8;++i) {
        const double x = .5*edge*(cube[i].x+1);
        points[i] = (NtVec3){x,x+.5*height*(cube[i].y+1),.5*edge*(cube[i].z+1)};
    }
    const NtHullGeometry geometry = {points,8,cube_faces,12};
    NtHull *hull;
    OK(nt_hull_create(NULL,&geometry,&hull));
    NtWaterPlane plane = {{0,.5*height,0},{-1,1,0}};
    NtHullImmersion wet;
    OK(nt_hull_immerse_plane(hull,&still,&plane,&wet));
    CHECK(wet.full_volume==edge*edge*height);
    CHECK(close_value(wet.displaced_volume,wet.full_volume/2));
    CHECK(close_point(wet.center,(NtVec3){edge/2,edge/2+height/4,edge/2}));
    nt_hull_destroy(hull);

    edge = 1e6;
    for (size_t i=0;i<8;++i)
        points[i] = (NtVec3){.5*edge*(cube[i].x+1),cube[i].y,.5*edge*(cube[i].z+1)};
    OK(nt_hull_create(NULL,&geometry,&hull));
    const double slope = 0x1p-75;
    plane = (NtWaterPlane){{0,-1,0},{-slope,1,0}};
    OK(nt_hull_immerse_plane(hull,&still,&plane,&wet));
    const double expected = edge*edge*edge*slope/2;
    CHECK(fabs(wet.displaced_volume-expected)<=3e-12*expected);
    CHECK(close_point(wet.center,(NtVec3){2*edge/3,-1,edge/2}));
    NtBuoyancy force;
    OK(nt_hull_buoyancy(&wet,&still,1e6,1e6,&force));
    CHECK(close_value(force.lift.y,expected*1e12));
    nt_hull_destroy(hull);
    return 0;
}

typedef struct RefinementMemory { size_t live, attempts, fail_at; } RefinementMemory;

static void *allocate_refinement(void *context, size_t bytes, size_t alignment)
{
    (void)alignment;
    RefinementMemory *memory = context;
    if (memory->attempts++==memory->fail_at) return NULL;
    void *pointer = malloc(bytes);
    if (pointer) ++memory->live;
    return pointer;
}

static void release_refinement(void *context, void *pointer, size_t bytes, size_t alignment)
{
    (void)bytes;
    (void)alignment;
    RefinementMemory *memory = context;
    --memory->live;
    free(pointer);
}

static int finite_water_midpoint(void)
{
    NtVec3 points[8];
    for (size_t i=0;i<8;++i)
        points[i] = (NtVec3){.5*cube[i].x,.5*cube[i].y,.5*cube[i].z};
    const NtHullGeometry geometry = {points,8,cube_faces,12};
    RefinementMemory memory = {.fail_at=SIZE_MAX};
    const NtAllocator allocator = {&memory,allocate_refinement,release_refinement};
    NtHull *hull;
    OK(nt_hull_create(&allocator,&geometry,&hull));
    NtWaterTriangle patch[32];
    size_t count = 0;
    for (int x=-2;x<2;++x) {
        for (int z=-2;z<2;++z) {
            const NtVec3 a = {x,0,z}, b = {x+1,0,z}, c = {x+1,0,z+1}, d = {x,0,z+1};
            patch[count++] = (NtWaterTriangle){{a,b,c}};
            patch[count++] = (NtWaterTriangle){{a,c,d}};
        }
    }
    NtFloatingBody body = still;
    body.position.y = -0x1p-54;
    const size_t attempts = memory.attempts;
    NtHullImmersion wet;
    /* The exact displaced volume is the midpoint above one half. */
    OK(nt_hull_immerse_water(hull,&body,patch,count,&wet));
    CHECK(wet.full_volume==1 && wet.displaced_volume==.5);
    CHECK(memory.attempts>attempts && memory.live==1);
    memory.fail_at = memory.attempts;
    wet = (NtHullImmersion){.full_volume=3};
    CHECK(nt_hull_immerse_water(hull,&body,patch,count,&wet)==NT_RESULT_ALLOCATION_FAILED);
    CHECK(wet.full_volume==3 && memory.live==1);
    nt_hull_destroy(hull);
    CHECK(memory.live==0);
    return 0;
}

static int rounding_and_symmetry(void)
{
    NtVec3 points[8];
    for (size_t i=0;i<8;++i)
        points[i] = (NtVec3){1e6*cube[i].x,1e6*cube[i].y,1e6*cube[i].z};
    const NtHullGeometry geometry = {points,8,cube_faces,12};
    NtHull *hull;
    OK(nt_hull_create(NULL,&geometry,&hull));
    NtHullImmersion wet;
    NtWaterPlane plane = {{0,0,0},{0,3,-1}};
    OK(nt_hull_immerse_plane(hull,&still,&plane,&wet));
    NtBuoyancy force;
    OK(nt_hull_buoyancy(&wet,&still,1e6,1e6,&force));
    CHECK(wet.displaced_volume==4e18 && fabs(force.torque.z)<=3e-12);
    nt_hull_destroy(hull);

    const double tiny = 0x1p-1074;
    for (size_t i=0;i<8;++i)
        points[i] = (NtVec3){(cube[i].x+1)/2,(cube[i].y+1)/2,cube[i].z<0 ? 0 : tiny};
    RefinementMemory memory = {.fail_at=SIZE_MAX};
    const NtAllocator allocator = {&memory,allocate_refinement,release_refinement};
    OK(nt_hull_create(&allocator,&geometry,&hull));
    for (unsigned i=0;i<3;++i) {
        plane = (NtWaterPlane){{.5,.5,i==0 ? tiny : 0},{-1,3,i==2 ? 0 : -1}};
        OK(nt_hull_immerse_plane(hull,&still,&plane,&wet));
        CHECK(wet.full_volume==tiny && wet.displaced_volume==(i==1 ? tiny : 0));
        CHECK(memory.live==1);
    }
    size_t failures = 0;
    for (size_t i=0;i<8;++i) {
        memory.fail_at = memory.attempts+i;
        wet = (NtHullImmersion){.full_volume=3};
        const NtResult status = nt_hull_immerse_plane(hull,&still,&plane,&wet);
        CHECK(memory.live==1);
        if (status==NT_RESULT_ALLOCATION_FAILED) {
            ++failures;
            CHECK(wet.full_volume==3);
        } else CHECK(status==NT_RESULT_OK && wet.displaced_volume==0);
    }
    CHECK(failures>=2);
    nt_hull_destroy(hull);
    CHECK(memory.live==0);
    return 0;
}

typedef struct Memory { bool fail; size_t bytes, alignment; unsigned allocated, released; } Memory;
static void *allocate(void *context, size_t bytes, size_t alignment)
{
    Memory *memory = context;
    if (memory->fail) return NULL;
    memory->bytes = bytes;
    memory->alignment = alignment;
    ++memory->allocated;
    return malloc(bytes);
}

static void release(void *context, void *pointer, size_t bytes, size_t alignment)
{
    Memory *memory = context;
    if (memory->bytes==bytes && memory->alignment==alignment) ++memory->released;
    free(pointer);
}

static int failure_and_ownership(void)
{
    Memory memory = {.fail=true};
    const NtAllocator allocator = {&memory,allocate,release};
    NtHull *hull = NULL;
    CHECK(nt_hull_create(&allocator,&box,&hull)==NT_RESULT_ALLOCATION_FAILED && !hull);
    memory.fail = false;
    NtHullTriangle faces[12];
    NtVec3 vertices[8];
    for (size_t i=0;i<12;++i) faces[i] = cube_faces[i];
    for (size_t i=0;i<8;++i) vertices[i] = cube[i];
    NtHullGeometry mesh = {vertices,8,faces,11};
    CHECK(nt_hull_create(&allocator,&mesh,&hull)==NT_RESULT_INVALID_ARGUMENT && !hull);
    mesh.triangle_count = 12;
    faces[0] = (NtHullTriangle){{0,1,2}};
    CHECK(nt_hull_create(&allocator,&mesh,&hull)==NT_RESULT_INVALID_ARGUMENT && !hull);
    faces[0] = cube_faces[0];
    vertices[6] = vertices[5];
    CHECK(nt_hull_create(&allocator,&mesh,&hull)==NT_RESULT_INVALID_ARGUMENT && !hull);
    vertices[6] = (NtVec3){.5,.5,.5};
    CHECK(nt_hull_create(&allocator,&mesh,&hull)==NT_RESULT_INVALID_ARGUMENT && !hull);
    vertices[6] = cube[6];
    OK(nt_hull_create(&allocator,&mesh,&hull));
    CHECK(memory.allocated==1 && memory.released==0);
    vertices[0].y = NAN;
    faces[0].indices[0] = 999;
    NtHullImmersion result = {.full_volume=7};
    NtWaterPlane plane = {{0,0,0},{0,-1,0}};
    CHECK(nt_hull_immerse_plane(hull,&still,&plane,&result)==NT_RESULT_INVALID_ARGUMENT);
    CHECK(result.full_volume==7);
    plane.normal.y = 1;
    const int before = fegetround();
    CHECK(fesetround(FE_UPWARD)==0);
    OK(nt_hull_immerse_plane(hull,&still,&plane,&result));
    CHECK(fegetround()==FE_UPWARD && fesetround(before)==0);
    CHECK(result.full_volume==8 && result.displaced_volume==4);
    NtFloatingBody body = still;
    body.rotation_radians = 1;
    NtSpatialTransform untouched = {.translation={1,2,3}};
    CHECK(nt_floating_body_transform(&body,&untouched)==NT_RESULT_INVALID_ARGUMENT);
    CHECK(close_point(untouched.translation,(NtVec3){1,2,3}));
    NtBuoyancy force = {.lift={0,5,0}};
    CHECK(nt_hull_buoyancy(&result,&still,-1,10,&force)==NT_RESULT_INVALID_ARGUMENT);
    CHECK(force.lift.y==5);
    nt_hull_destroy(hull);
    nt_hull_destroy(NULL);
    CHECK(memory.allocated==1 && memory.released==1);
    return 0;
}

static int maximum_hull(void)
{
    enum { RING = NT_HULL_MAX_VERTICES/2 };
    NtVec3 vertices[NT_HULL_MAX_VERTICES];
    NtHullTriangle triangles[NT_HULL_MAX_TRIANGLES];
    size_t count = 0;
    const double angle = 2*acos(-1.0)/RING;
    for (unsigned i=0;i<RING;++i) {
        vertices[i] = (NtVec3){cos(i*angle),-1,sin(i*angle)};
        vertices[RING+i] = (NtVec3){vertices[i].x,1,vertices[i].z};
        const unsigned j = (i+1)%RING;
        triangles[count++] = (NtHullTriangle){{i,RING+i,RING+j}};
        triangles[count++] = (NtHullTriangle){{i,RING+j,j}};
    }
    for (unsigned i=1;i+1<RING;++i) {
        triangles[count++] = (NtHullTriangle){{0,i,i+1}};
        triangles[count++] = (NtHullTriangle){{RING,RING+i+1,RING+i}};
    }
    CHECK(count==NT_HULL_MAX_TRIANGLES);
    const NtHullGeometry geometry = {vertices,NT_HULL_MAX_VERTICES,triangles,count};
    NtHull *hull;
    OK(nt_hull_create(NULL,&geometry,&hull));
    const NtWaterPlane plane = {{0,0,0},{0,1,0}};
    NtHullImmersion wet;
    OK(nt_hull_immerse_plane(hull,&still,&plane,&wet));
    const double area = RING*sin(angle)/2;
    CHECK(close_value(wet.full_volume,2*area) && close_value(wet.displaced_volume,area));
    CHECK(close_point(wet.center,(NtVec3){0,-.5,0}));
    nt_hull_destroy(hull);
    return 0;
}

static int run_contract(void)
{
    return planes() || tetrahedron() || water_patches() || coplanar_patch() ||
           narrow_immersion() || rounding_and_symmetry() || finite_water_midpoint() ||
           failure_and_ownership() || maximum_hull();
}
