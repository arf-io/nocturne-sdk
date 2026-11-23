#include <nocturne/simulation.h>

#include <fenv.h>
#include <math.h>
#include <stdio.h>

#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr,"surface flow line %d: %s\n",__LINE__,#test); return 1; \
} } while (0)
#define OK(call) CHECK((call) == NT_RESULT_OK)
#define US(t) (UINT64_C(1000000)*(t))

static bool near(double actual, double reference)
{
    return fabs(actual-reference) <= 3e-13*fmax(fabs(reference),1.0);
}

static int waves(void)
{
    NtWaterWaveSample sample;
    OK(nt_water_wave_sample(NULL,0,(NtPoint){3,5},-7,&sample));
    CHECK(sample.height == 0 && sample.normal.y == 1 && sample.laplacian == 0);
    const NtWaterWave wave = {2,{.5,.25},.125,.25};
    const double pi = 0x1.921fb54442d18p+1;
    OK(nt_water_wave_sample(&wave,1,(NtPoint){0,0},0,&sample));
    CHECK(near(sample.height,0) && near(sample.gradient.x,-2*pi) && near(sample.gradient.y,-pi));
    CHECK(near(sample.vertical_velocity,pi/2) && near(sample.laplacian,0));
    CHECK(near(sample.normal.x,2*pi/sqrt(1+5*pi*pi)));
    OK(nt_water_wave_sample(&wave,1,(NtPoint){0,0},-2,&sample));
    CHECK(near(sample.height,-2) && near(sample.laplacian,2*1.25*pi*pi));
    CHECK(nt_water_wave_sample(&wave,1,(NtPoint){INFINITY,0},0,&sample) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(near(sample.height,-2));
    const NtWaterWave cancellation = {1e6,{999999,1},1,0};
    OK(nt_water_wave_sample(&cancellation,1,(NtPoint){999999.5,0x1p-56},.5,&sample));
    const double gradient = -1e6*sin(2*pi*0x1p-56)*(2*pi*999999);
    CHECK(fabs(sample.gradient.x-gradient) < 1e-16);
    const NtWaterWave quarter = {1e6,{1,1},0,.25};
    OK(nt_water_wave_sample(&quarter,1,(NtPoint){0,0},0,&sample));
    CHECK(sample.height == 0 && sample.laplacian == 0);

    NtWaterWave modes[] = {{1e6,{1e6,-1e6},1e6,.25},
                           {1e-5,{1,2},3,.25},
                           {-1e6,{1e6,-1e6},1e6,.25}};
    for (unsigned i = 0; i < 2; ++i) {
        NtWaterWaveSample small;
        OK(nt_water_wave_sample(&modes[1],1,(NtPoint){0,0},0,&small));
        OK(nt_water_wave_sample(modes,3,(NtPoint){0,0},0,&sample));
        CHECK(sample.height == small.height && sample.laplacian == small.laplacian);
        CHECK(sample.gradient.x == small.gradient.x && sample.gradient.y == small.gradient.y);
        CHECK(sample.vertical_velocity == small.vertical_velocity);
        CHECK(sample.normal.x == small.normal.x && sample.normal.y == small.normal.y &&
              sample.normal.z == small.normal.z);
        modes[0].phase_turns = modes[1].phase_turns = modes[2].phase_turns = 0;
    }

    const NtWaterWave residual[] = {{1e6,{1000.5,0},0,0},{-999999.999,{1000.5,0},0,0}};
    OK(nt_water_wave_sample(residual,2,(NtPoint){.1,0},0,&sample));
    CHECK(near(sample.normal.y,.4576946852791475));
    return 0;
}

static int flow(void)
{
    const NtFlowStage stages[] = {{US(2),0},{US(1),US(1)},{0,US(1)}};
    const NtFlowInput inputs[] = {{US(1),US(4),6},{US(5),US(6),2}};
    const NtFlow channel = {stages,3,inputs,2};
    NtFlowSample sample;
    OK(nt_flow_sample(&channel,US(3),&sample));
    CHECK(sample.stage_count==3 && sample.ports[0].volume==12 && sample.ports[0].rate==6);
    CHECK(sample.ports[1].volume==0 && sample.ports[1].rate==6 && sample.retained[0]==12);
    CHECK(sample.ports[2].volume==0 && sample.ports[2].rate==0);
    OK(nt_flow_sample(&channel,US(6),&sample));
    const double e = exp(-2);
    CHECK(near(sample.ports[2].rate,6*(1-e)) && near(sample.ports[2].volume,6*(1+e)));
    CHECK(near(sample.ports[3].rate,6*(1-3*e)) && near(sample.ports[3].volume,24*e));
    for (size_t i = 0; i < sample.stage_count; ++i)
        CHECK(sample.ports[i+1].volume >= 0 && sample.retained[i] >= 0 &&
              near(sample.ports[i+1].volume+sample.retained[i],sample.ports[i].volume));

    /* At t=7 the first pulse just shut off at both delayed reservoirs. */
    OK(nt_flow_sample(&channel,US(7),&sample));
    CHECK(near(sample.ports[2].rate,6*(1-exp(-3))));
    CHECK(near(sample.ports[3].rate,6*(1-4*exp(-3))));
    OK(nt_flow_sample(&channel,UINT64_MAX,&sample));
    CHECK(near(sample.ports[3].volume,20) && sample.ports[3].rate==0);
    CHECK(sample.ports[0].volume==20 && sample.retained[0]==0);
    const NtFlowStage disparate[] = {{0,1},{0,US(86400)}};
    OK(nt_flow_sample(&(NtFlow){disparate,2,inputs,2},UINT64_MAX,&sample));
    CHECK(near(sample.ports[2].volume,20) && sample.ports[2].rate==0);
    const NtFlowInput overlaps[] = {{0,US(2),1},{US(1),US(3),1}};
    CHECK(nt_flow_sample(&(NtFlow){NULL,0,overlaps,2},0,&sample)==NT_RESULT_INVALID_ARGUMENT);
    CHECK(sample.stage_count==2 && near(sample.ports[2].volume,20));
    return 0;
}

static int section_and_spray(void)
{
    const NtFlowSection section = {4,2,.5};
    NtFlowSectionSample channel;
    OK(nt_flow_section_sample(&section,6,&channel));
    CHECK(channel.depth==.5 && channel.carried_rate==4 && channel.excess_rate==2);
    NtVec3 p;
    OK(nt_flow_section_point((NtVec3){2,3,4},(NtVec3){0,1,0},channel.depth,.5,&p));
    CHECK(p.x==2 && p.y==3.25 && p.z==4);

    NtFlowSectionSample thin;
    OK(nt_flow_section_sample(&(NtFlowSection){1e-6,1e-6,0x1p-1040},0x1p-1074,&thin));
    CHECK(thin.depth == 0x1p-1040 && thin.carried_rate == 0 && thin.excess_rate == 0x1p-1074);
    NtVec3 offset;
    OK(nt_flow_section_point((NtVec3){-1,0,0},(NtVec3){1,0,0},0x1.0000000000001p+0,
                             0x1.ffffffffffffep-1,&offset));
    CHECK(offset.x == -0x1p-104);
    const NtVec3 outside = {1,0x1.7ba0041887017p-20,0};
    CHECK(nt_flow_section_point((NtVec3){0},outside,1,1,&offset)==NT_RESULT_INVALID_ARGUMENT);
    CHECK(offset.x == -0x1p-104);
    const NtVec3 inside = {1,0x1.7ba0041887016p-20,0};
    OK(nt_flow_section_point((NtVec3){0},inside,1,1,&offset));
    CHECK(offset.x == 1 && offset.y == inside.y);

    const NtSprayOptions options = {
        .origin={p.x,p.y,p.z},.velocity={3,1,0},.acceleration={0,-2,0},.lifetime_us=US(2),
        .droplet_volume=.125,.seed=73,.stream=49
    };
    NtSprayBatch batch;
    OK(nt_spray_emit(&options,channel.excess_rate,5,401,US(2),&batch));
    CHECK(batch.count==5 && batch.emitted_volume==.625 && batch.remaining_volume==1.375);
    NtSpraySample drop = {.id=100};
    bool active = true;
    OK(nt_spray_sample(&batch,3,US(1),&active,&drop));
    CHECK(!active && drop.id==100);
    OK(nt_spray_sample(&batch,3,US(3),&active,&drop));
    CHECK(active && drop.id==404 && drop.position.x==5 && drop.position.y==3.25);
    CHECK(drop.velocity.y==-1 && drop.progress==.5 && drop.volume==.125);
    OK(nt_spray_sample(&batch,3,US(4),&active,&drop));
    CHECK(!active && drop.id==404);
    CHECK(nt_spray_emit(&options,1,2,UINT64_MAX,0,&batch)==NT_RESULT_OUT_OF_RANGE);
    CHECK(batch.count==5 && batch.first_id==401);
    OK(nt_spray_emit(&options,nextafter(.25,0),5,UINT64_MAX,0,&batch));
    CHECK(batch.count==1 && batch.remaining_volume < .125);
    OK(nt_spray_emit(&options,1,0,1,0,&batch));
    CHECK(batch.count==0 && batch.remaining_volume==1);
    return 0;
}

static int fp_environment(void)
{
#if !defined(__wasm__)
    fenv_t saved;
    CHECK(fegetenv(&saved)==0 && fesetround(FE_DOWNWARD)==0 &&
          feclearexcept(FE_ALL_EXCEPT)==0 && feraiseexcept(FE_DIVBYZERO)==0);
    const int before = fetestexcept(FE_ALL_EXCEPT);
    NtFlowSample sample;
    const NtFlowStage stage = {0,123456};
    const NtFlowInput input = {0,US(1),.13};
    OK(nt_flow_sample(&(NtFlow){&stage,1,&input,1},321123,&sample));
    const int mode = fegetround(), after = fetestexcept(FE_ALL_EXCEPT);
    CHECK(fesetenv(&saved)==0 && mode==FE_DOWNWARD && after==before);
#endif
    return 0;
}

int main(void)
{
    return waves() || flow() || section_and_spray() || fp_environment();
}
