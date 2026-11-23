#include <nocturne/simulation.h>
#include <nocturne/spatial_surface.h>
#include <fenv.h>
#include <math.h>
#include <stdio.h>
#include "simulation_worker.h"

#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr,"fluid coupling line %d: %s\n",__LINE__,#test); return 1; \
} } while (0)
#define OK(call) CHECK((call)==NT_RESULT_OK)
#define SECOND UINT64_C(1000000)

static bool near(double a, double b)
{
    return fabs(a-b)<=5e-12*fmax(1,fabs(b));
}

static bool near_vec(NtVec3 a, NtVec3 b)
{
    return near(a.x,b.x) && near(a.y,b.y) && near(a.z,b.z);
}

static bool same_body(NtFloatingBody a, NtFloatingBody b)
{
    return near_vec(a.position,b.position) && near_vec(a.linear_velocity,b.linear_velocity) &&
        near_vec(a.angular_velocity,b.angular_velocity) && near_vec(a.rotation_axis,b.rotation_axis) &&
        a.rotation_radians==b.rotation_radians && a.mass==b.mass && near_vec(a.principal_inertia,b.principal_inertia);
}

static int exchanges(void)
{
    const double ln2 = 0x1.62e42fefa39efp-1;
    NtLinearPair pair = {{1,{4,-1,0}},{1,{-2,-1,0}}};
    NtDragResult result;
    OK(nt_linear_drag_exchange(&pair,ln2/2,SECOND,&result));
    CHECK(near_vec(result.first_velocity,(NtVec3){2.5,-1,0}));
    CHECK(near_vec(result.second_velocity,(NtVec3){-.5,-1,0}));
    CHECK(near_vec(result.impulse_on_second,(NtVec3){1.5,0,0}));
    CHECK(near(result.dissipated_energy,6.75));
    OK(nt_linear_drag_exchange(&pair,3,0,&result));
    CHECK(near_vec(result.first_velocity,pair.first.velocity) &&
          near_vec(result.second_velocity,pair.second.velocity) && result.dissipated_energy==0);
    OK(nt_linear_drag_exchange(&pair,0,SECOND,&result));
    CHECK(near_vec(result.first_velocity,pair.first.velocity) &&
          near_vec(result.second_velocity,pair.second.velocity) && result.dissipated_energy==0);

    const NtAngularPair angular = {{{1,2,4},{4,8,-3}},{{1,2,4},{-2,0,1}}};
    OK(nt_angular_drag_exchange(&angular,(NtVec3){ln2/2,ln2,0},SECOND,&result));
    CHECK(near_vec(result.first_velocity,(NtVec3){2.5,6,-3}));
    CHECK(near_vec(result.second_velocity,(NtVec3){-.5,2,1}));
    CHECK(near_vec(result.impulse_on_second,(NtVec3){1.5,4,0}));
    CHECK(near(result.dissipated_energy,30.75));

    pair = (NtLinearPair){{1e-6,{1e6,0,0}},{1e6,{-1e6,0,0}}};
    OK(nt_linear_drag_exchange(&pair,0x1p-1074,1,&result));
    CHECK(result.impulse_on_second.x==0x1p-1073);
    CHECK(result.dissipated_energy==0x0.00000003d09p-1022);
    pair = (NtLinearPair){{1,{1e6,0,0}},{1,{-1e6,0,0}}};
    OK(nt_linear_drag_exchange(&pair,375,SECOND,&result));
    const double tail = (1e6*exp(-375))*exp(-375);
    CHECK(result.first_velocity.x==tail && result.second_velocity.x==-tail);
    CHECK(result.impulse_on_second.x==1e6 && result.dissipated_energy==1e12);

    result.dissipated_energy = 7;
    CHECK(nt_linear_drag_exchange(&pair,1,SECOND+1,&result)==NT_RESULT_OUT_OF_RANGE);
    CHECK(result.dissipated_energy==7);
    CHECK(nt_linear_drag_exchange(&pair,-1,0,&result)==NT_RESULT_INVALID_ARGUMENT);
    CHECK(result.dissipated_energy==7);
    return 0;
}

static int invariants(void)
{
    NtLinearPair pair = {{12,{1,4,-2}},{5,{7,-2,3}}};
    const NtLinearPair original = pair;
    const NtVec3 initial_momentum = {47,38,-9};
    const double initial_energy = (12*(1+16+4)+5*(49+4+9))/2.0;
    double dissipated = 0;
    for (unsigned i=0;i<128;++i) {
        NtDragResult step;
        OK(nt_linear_drag_exchange(&pair,2.5,15000,&step));
        CHECK(step.dissipated_energy>=0);
        dissipated += step.dissipated_energy;
        pair.first.velocity = step.first_velocity;
        pair.second.velocity = step.second_velocity;
    }
    const NtVec3 a = pair.first.velocity, b = pair.second.velocity;
    CHECK(near_vec((NtVec3){12*a.x+5*b.x,12*a.y+5*b.y,12*a.z+5*b.z},initial_momentum));
    const double energy = (12*(a.x*a.x+a.y*a.y+a.z*a.z)+5*(b.x*b.x+b.y*b.y+b.z*b.z))/2;
    CHECK(near(initial_energy,energy+dissipated));

    pair = original;
    for (unsigned i=0;i<128;++i) {
        NtDragResult step;
        OK(nt_linear_drag_exchange(&pair,2.5,15000,&step));
        pair.first.velocity = step.first_velocity;
        pair.second.velocity = step.second_velocity;
    }
    CHECK(pair.first.velocity.x==a.x && pair.first.velocity.y==a.y && pair.first.velocity.z==a.z);
    CHECK(pair.second.velocity.x==b.x && pair.second.velocity.y==b.y && pair.second.velocity.z==b.z);
    return 0;
}

static int pose_step(void)
{
    NtFloatingBody body = {.position={3,0,0},.mass=2,.principal_inertia={2,2,2},.linear_velocity={4,0,0}};
    NtBodyLoad load = {{-6,0,0},{0,0,0}};
    NtFloatingBody next;
    OK(nt_floating_body_step(&body,&load,SECOND/2,&next));
    CHECK(next.position.x==37.0/8 && next.linear_velocity.x==2.5);

    body.rotation_axis = (NtVec3){1,0,0};
    body.rotation_radians = acos(0.0);
    load = (NtBodyLoad){{0,0,0},{0,0,2}};
    OK(nt_floating_body_step(&body,&load,SECOND/4,&next));
    CHECK(near_vec(next.angular_velocity,(NtVec3){0,0,.25}));
    NtSpatialTransform pose;
    NtVec3 direction;
    OK(nt_floating_body_transform(&next,&pose));
    OK(nt_spatial_transform_direction(&pose,(NtVec3){1,0,0},&direction));
    CHECK(near_vec(direction,(NtVec3){cos(.03125),sin(.03125),0}));
    OK(nt_spatial_transform_direction(&pose,(NtVec3){0,1,0},&direction));
    CHECK(near_vec(direction,(NtVec3){0,0,1}));

    body = (NtFloatingBody){.mass=1,.principal_inertia={2,3,4},.angular_velocity={1,2,3}};
    load = (NtBodyLoad){0};
    OK(nt_floating_body_step(&body,&load,10000,&next));
    CHECK(near_vec(next.angular_velocity,(NtVec3){.97,2.02,2.995}));

    body = (NtFloatingBody){.mass=1,.principal_inertia={1,1,1},.angular_velocity={0x1p-1074,0x1p-1074,0x1p-1074}};
    OK(nt_floating_body_step(&body,&load,SECOND,&next));
    CHECK(next.rotation_radians==0x1p-1073);
    CHECK(near_vec(next.rotation_axis,(NtVec3){1/sqrt(3),1/sqrt(3),1/sqrt(3)}));

    NtFloatingBody small = body;
    small.rotation_radians = 0x1p-1074;
    small.rotation_axis = (NtVec3){3,4,0};
    small.angular_velocity = (NtVec3){-0x1p-1074,0,0};
    NtFloatingBody composed;
    OK(nt_floating_body_step(&small,&load,SECOND,&composed));
    CHECK(composed.rotation_radians==0x1p-1074);
    CHECK(near_vec(composed.rotation_axis,(NtVec3){-1/sqrt(5),2/sqrt(5),0}));

    small.rotation_axis = (NtVec3){1,0,0};
    small.principal_inertia = (NtVec3){0x1p64,0x1p64,0x1p64};
    const double least = 0x1p-1074;
    for (int sign=-1;sign<=1;++sign) {
        const NtBodyLoad residual = {.torque={sign*least,0,0}};
        OK(nt_floating_body_step(&small,&residual,SECOND/2,&composed));
        CHECK(composed.rotation_radians==(sign>0 ? least : 0));
    }
    small.rotation_radians = 2*least;
    small.angular_velocity = (NtVec3){0,3*least,0};
    OK(nt_floating_body_step(&small,&load,SECOND/2,&composed));
    CHECK(composed.rotation_radians==2*least);
    CHECK(near_vec(composed.rotation_axis,(NtVec3){.8,.6,0}));
    small.rotation_axis = (NtVec3){3,4,0};
    small.angular_velocity = (NtVec3){0,0,3*least};
    OK(nt_floating_body_step(&small,&load,SECOND/2,&composed));
    CHECK(composed.rotation_radians==2*least);

    small.rotation_radians = 5*least;
    small.angular_velocity = (NtVec3){-3*least,-4*least,0};
    OK(nt_floating_body_step(&small,&load,SECOND/2,&composed));
    CHECK(composed.rotation_radians==2*least);
    CHECK(near_vec(composed.rotation_axis,(NtVec3){.6,.8,0}));

    small.rotation_axis = (NtVec3){-1,0,0};
    small.rotation_radians = .125;
    small.angular_velocity = (NtVec3){.125,0,0};
    small.principal_inertia = (NtVec3){1,1,1};
    const NtBodyLoad tiny_torque = {.torque={0x1p-100,0,0}};
    OK(nt_floating_body_step(&small,&tiny_torque,SECOND,&composed));
    CHECK(composed.rotation_radians==0x1p-101 && composed.rotation_axis.x==1);
    const NtBodyLoad near_opposite = {.torque={0x1p-100,0x1p-100,0}};
    OK(nt_floating_body_step(&small,&near_opposite,SECOND,&composed));
    const double y = sin(.125)/.125, z = (1-cos(.125))/.125;
    const double norm = hypot(1,hypot(y,z));
    CHECK(near(composed.rotation_radians/0x1p-101,norm));
    CHECK(near_vec(composed.rotation_axis,(NtVec3){1/norm,y/norm,z/norm}));

    const NtFloatingBody boundary = {.rotation_axis={-1,0,0},.rotation_radians=0x1.fffffffffffffp-1022,
        .angular_velocity={0x1p-1021,0,0},.mass=1,.principal_inertia={2,2,2}};
    const NtBodyLoad mixed = {.torque={-least,5*least,0}};
    OK(nt_floating_body_step(&boundary,&mixed,SECOND,&composed));
    const double length = hypot(.75,1.25);
    CHECK(composed.rotation_radians==least);
    CHECK(near_vec(composed.rotation_axis,(NtVec3){.75/length,1.25/length,0}));

    const double normal = 0x1p-1022;
    const NtFloatingBody oblique = {.rotation_axis={3,4,0},.rotation_radians=normal,
        .mass=1,.principal_inertia={2.5,2.5,2.5}};
    const NtBodyLoad transverse = {.torque={-3*normal,-4*normal,4*least}};
    OK(nt_floating_body_step(&oblique,&transverse,SECOND,&composed));
    CHECK(composed.rotation_radians==least);
    CHECK(near_vec(composed.rotation_axis,(NtVec3){0,0,1}));

    small.rotation_radians = 0;
    small.angular_velocity = (NtVec3){.25,0,0};
    OK(nt_floating_body_step(&small,&load,SECOND,&composed));
    CHECK(composed.rotation_radians==.25);
    CHECK(nt_floating_body_step(&small,&tiny_torque,SECOND,&composed)==NT_RESULT_OUT_OF_RANGE);
    small.angular_velocity.y = 0x1p-1074;
    CHECK(nt_floating_body_step(&small,&load,SECOND,&composed)==NT_RESULT_OUT_OF_RANGE);
    CHECK(composed.rotation_radians==.25);

    body.position.x = 1e6;
    body.linear_velocity.x = 0x1p-1074;
    const NtFloatingBody unchanged = body;
    CHECK(nt_floating_body_step(&body,&load,1,&body)==NT_RESULT_OUT_OF_RANGE);
    CHECK(same_body(body,unchanged));
    body.position.x = 0;
    body.angular_velocity = (NtVec3){0,1,0};
    CHECK(nt_floating_body_step(&body,&load,SECOND,&next)==NT_RESULT_OUT_OF_RANGE);
    CHECK(next.rotation_radians==0x1p-1073);

    const int before = fegetround();
    CHECK(fesetround(FE_UPWARD)==0);
    OK(nt_floating_body_step(&body,&load,0,&next));
    CHECK(fegetround()==FE_UPWARD && fesetround(before)==0);
    CHECK(same_body(body,next));
    return 0;
}

static int coupled_pose(void)
{
    const NtVec3 vertices[] = {{0,0,0},{1,0,0},{0,1,0},{0,0,1}};
    const NtHullTriangle faces[] = {{{0,2,1}},{{0,1,3}},{{0,3,2}},{{1,2,3}}};
    const NtHullGeometry mesh = {vertices,4,faces,4};
    NtHull *hull;
    OK(nt_hull_create(NULL,&mesh,&hull));
    NtFloatingBody body = {.mass=2,.principal_inertia={8,8,8}};
    const NtWaterPlane plane = {{0,.5,0},{0,1,0}};
    NtHullImmersion wet;
    NtBuoyancy buoyancy;
    OK(nt_hull_immerse_plane(hull,&body,&plane,&wet));
    OK(nt_hull_buoyancy(&wet,&body,30,10,&buoyancy));
    nt_hull_destroy(hull);
    const NtBodyLoad load = {buoyancy.force,buoyancy.torque};
    const NtFloatingBody snapshot = body;
    OK(nt_floating_body_step(&body,&load,SECOND/10,&body));
    CHECK(body.position.y>0 && body.linear_velocity.y>0 && body.rotation_radians>0);
    NtFloatingBody replay;
    OK(nt_floating_body_step(&snapshot,&load,SECOND/10,&replay));
    CHECK(same_body(body,replay));

    const NtSpatialSweepTriangle surface = {
        .initial={{-1,.01,-1},{1,.01,-1},{0,.01,1}},
        .object_id=14,.primitive_id=8
    };
    NtSpatialContact contact;
    bool found;
    OK(nt_spatial_sweep_point(snapshot.position,body.position,&surface,1,&found,&contact));
    CHECK(found && contact.object_id==14 && contact.primitive_id==8);
    CHECK(near(contact.position.y,.01) && contact.fraction>0 && contact.fraction<1);
    return 0;
}

static int periodic_pose(void)
{
    /* The nearest binary64 value of 2*pi lies below the full turn.
     * The complementary increment overshoots by this residual. */
    const double turn = 0x1.921fb54442d18p+2;
    const double complement = 0x1.1a62633145c07p-52;
    const double residual = 0x1.f1976b7ed8fbcp-108;
    const NtBodyLoad load = {0};
    for (int sign=-1;sign<=1;sign+=2) {
        NtFloatingBody body = {.mass=1,.principal_inertia={1,1,1},.rotation_axis={1,0,0},
            .rotation_radians=sign*turn,.angular_velocity={sign*complement,0,0}};
        NtFloatingBody next;
        OK(nt_floating_body_step(&body,&load,SECOND,&next));
        CHECK(next.rotation_radians==residual && next.rotation_axis.x==sign);
        body.angular_velocity.y = residual;
        OK(nt_floating_body_step(&body,&load,SECOND,&next));
        CHECK(near(next.rotation_radians/residual,sqrt(2)));
        CHECK(near_vec(next.rotation_axis,(NtVec3){sign/sqrt(2),1/sqrt(2),0}));
    }
    return 0;
}

static int run_contract(void)
{
    return exchanges() || invariants() || pose_step() || periodic_pose() || coupled_pose();
}
