#include <nocturne/field.h>

#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stdio.h>

#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr,"field consumer line %d: %s\n",__LINE__,#test); return 1; \
} } while (0)

static int optical(void)
{
    double weight = -1;
    CHECK(nt_optical_falloff(NT_OPTICAL_LINEAR,.25,&weight) == NT_RESULT_OK && weight == .75);
    CHECK(nt_optical_falloff(NT_OPTICAL_LINEAR,1,&weight) == NT_RESULT_OK && weight == 0);
    CHECK(nt_optical_falloff(NT_OPTICAL_GAUSSIAN,2,&weight) == NT_RESULT_OK && fabs(weight-exp(-2)) < 1e-15);
    CHECK(nt_optical_falloff(NT_OPTICAL_GAUSSIAN,DBL_MAX,&weight) == NT_RESULT_OK && weight == 0);
    CHECK(nt_optical_falloff(NT_OPTICAL_INVERSE_QUADRATIC,2,&weight) == NT_RESULT_OK && weight == .2);
    CHECK(nt_optical_falloff(NT_OPTICAL_INVERSE_QUADRATIC,0x1p530,&weight) == NT_RESULT_OK && weight == 0x1p-1060);
    weight = -1;
    CHECK(nt_optical_falloff(NT_OPTICAL_LINEAR,-0x1p-1074,&weight) == NT_RESULT_INVALID_ARGUMENT && weight == -1);
    CHECK(nt_optical_falloff(NT_OPTICAL_LINEAR,INFINITY,&weight) == NT_RESULT_INVALID_ARGUMENT && weight == -1);
    return 0;
}

static int ripple_clock(void)
{
    NtField field = {.kind=NT_FIELD_RIPPLE,.as.ripple=nt_ripple_field_default()};
    NtRippleField *ripple = &field.as.ripple;
    ripple->origin = (NtPoint){10,10};
    ripple->aspect = (NtPoint){2,1};
    ripple->start_us = 100;
    ripple->lifetime_us = 100;
    ripple->spacing_us = 25;
    ripple->rings = 3;
    ripple->last_radius = 8;
    ripple->falloff = NT_OPTICAL_LINEAR;
    ripple->fade = NT_LINEAR;
    NtFieldQuery query = {{12,10},150,NT_MOTION_FULL};
    NtFieldSample sample;
    CHECK(nt_field_sample(&field,query,&sample) == NT_RESULT_OK && sample.value == .5 && sample.coverage == .5);
    query.position.x = 11;
    CHECK(nt_field_sample(&field,query,&sample) == NT_RESULT_OK && sample.value == .75);
    query.position.x = 10;
    CHECK(nt_field_sample(&field,query,&sample) == NT_RESULT_OK && sample.value == 1);
    query.now = 99;
    CHECK(nt_field_sample(&field,query,&sample) == NT_RESULT_OK && sample.coverage == 0);
    query.now = 249;
    query.motion = NT_MOTION_REDUCED;
    CHECK(nt_field_sample(&field,query,&sample) == NT_RESULT_OK && sample.coverage == 1);
    query.now = 250;
    CHECK(nt_field_sample(&field,query,&sample) == NT_RESULT_OK && sample.coverage == 0);
    NtDeadline next;
    CHECK(nt_field_deadline(&field,99,NT_MOTION_FULL,19,&next) == NT_RESULT_OK && next.scheduled && next.at == 100);
    CHECK(nt_field_deadline(&field,110,NT_MOTION_FULL,19,&next) == NT_RESULT_OK && next.scheduled && next.at == 125);
    CHECK(nt_field_deadline(&field,150,NT_MOTION_FULL,19,&next) == NT_RESULT_OK && next.scheduled && next.at == 169);
    CHECK(nt_field_deadline(&field,150,NT_MOTION_REDUCED,19,&next) == NT_RESULT_OK && next.scheduled && next.at == 200);
    CHECK(nt_field_deadline(&field,250,NT_MOTION_FULL,19,&next) == NT_RESULT_OK && !next.scheduled);

    ripple->start_us = UINT64_MAX-10;
    ripple->spacing_us = 10;
    CHECK(nt_field_deadline(&field,UINT64_MAX-1,NT_MOTION_REDUCED,1,&next) == NT_RESULT_OK &&
          next.scheduled && next.at == UINT64_MAX);
    query.now = UINT64_MAX;
    CHECK(nt_field_sample(&field,query,&sample) == NT_RESULT_OK && sample.coverage == 1);
    CHECK(nt_field_deadline(&field,UINT64_MAX,NT_MOTION_FULL,1,&next) == NT_RESULT_OK && !next.scheduled);
    ripple->rings = NT_FIELD_MAX_RINGS+1;
    sample.value = -41;
    CHECK(nt_field_sample(&field,query,&sample) == NT_RESULT_OUT_OF_RANGE && sample.value == -41);
    return 0;
}

static int ripple_precision(void)
{
    NtField field = {.kind=NT_FIELD_RIPPLE,.as.ripple=nt_ripple_field_default()};
    NtRippleField *ripple = &field.as.ripple;
    ripple->first_radius = ripple->last_radius = 5;
    ripple->falloff = NT_OPTICAL_LINEAR;
    NtFieldQuery query = {{3,4},0,NT_MOTION_FULL};
    NtFieldSample sample;
    CHECK(nt_field_sample(&field,query,&sample) == NT_RESULT_OK && sample.coverage == 1);
    query.position = (NtPoint){6,0};
    CHECK(nt_field_sample(&field,query,&sample) == NT_RESULT_OK && sample.coverage == 0);
    query.position.x = 4;
    CHECK(nt_field_sample(&field,query,&sample) == NT_RESULT_OK && sample.coverage == 0);
    query.position.x = 4.25;
    CHECK(nt_field_sample(&field,query,&sample) == NT_RESULT_OK && sample.coverage == .25);

    ripple->origin.x = -0x1p-54;
    ripple->width = 0x1p-54;
    ripple->first_radius = ripple->last_radius = 1;
    query.position.x = 1;
    CHECK(nt_field_sample(&field,query,&sample) == NT_RESULT_OK && sample.coverage == 0);
    ripple->origin.x = -nextafter(0x1p-54,0);
    CHECK(nt_field_sample(&field,query,&sample) == NT_RESULT_OK && sample.coverage == 0x1p-53);

    ripple->origin.x = 0;
    ripple->width = ripple->first_radius = ripple->last_radius = .5;
    query.position = (NtPoint){nextafter(1,0),0x1p-26};
    CHECK(nt_field_sample(&field,query,&sample) == NT_RESULT_OK && sample.coverage == 0);
    query.position.y = nextafter(0x1p-26,0);
    CHECK(nt_field_sample(&field,query,&sample) == NT_RESULT_OK && sample.coverage > 0 && sample.coverage < 1e-30);

    ripple->first_radius = 1;
    ripple->last_radius = nextafter(1,2);
    ripple->lifetime_us = UINT64_MAX;
    ripple->width = 0x1p-115;
    query.position = (NtPoint){1,0};
    query.now = 1;
    CHECK(nt_field_sample(&field,query,&sample) == NT_RESULT_OK && fabs(sample.coverage-.5) < 1e-15);

    ripple->origin = (NtPoint){-DBL_MAX,-DBL_MAX};
    ripple->aspect = (NtPoint){DBL_MAX,DBL_MAX};
    ripple->falloff = NT_OPTICAL_GAUSSIAN;
    ripple->width = DBL_MAX;
    query.position = (NtPoint){DBL_MAX,DBL_MAX};
    CHECK(nt_field_sample(&field,query,&sample) == NT_RESULT_OK && sample.coverage == 0);
    return 0;
}

static int wave_precision(void)
{
    NtField field = {.kind=NT_FIELD_WAVE,.as.wave=nt_wave_field_default()};
    NtWaveField *wave = &field.as.wave;
    wave->start_us = 25;
    wave->period_us = 100;
    NtFieldQuery query = {{2,0},25,NT_MOTION_FULL};
    NtFieldSample sample;
    CHECK(nt_field_sample(&field,query,&sample) == NT_RESULT_OK && sample.value == 1 && sample.coverage == 1);
    query.now = 50;
    CHECK(nt_field_sample(&field,query,&sample) == NT_RESULT_OK && sample.value == 0);
    query.now = 100;
    CHECK(nt_field_sample(&field,query,&sample) == NT_RESULT_OK && sample.value == 0);
    query.now = 75;
    CHECK(nt_field_sample(&field,query,&sample) == NT_RESULT_OK && sample.value == -1);
    query.motion = NT_MOTION_REDUCED;
    CHECK(nt_field_sample(&field,query,&sample) == NT_RESULT_OK && sample.value == 1);
    NtDeadline next;
    CHECK(nt_field_deadline(&field,24,NT_MOTION_REDUCED,0,&next) == NT_RESULT_OK && next.scheduled && next.at == 25);
    CHECK(nt_field_deadline(&field,25,NT_MOTION_REDUCED,0,&next) == NT_RESULT_OK && !next.scheduled);
    CHECK(nt_field_deadline(&field,25,NT_MOTION_FULL,0,&next) == NT_RESULT_OK && next.scheduled && next.at == 16025);

    wave->start_us = 0;
    wave->period_us = 0;
    wave->origin.x = -DBL_MAX;
    wave->cycles_per_unit = (NtPoint){DBL_MAX,1};
    query = (NtFieldQuery){{DBL_MAX,.25},0,NT_MOTION_FULL};
    CHECK(nt_field_sample(&field,query,&sample) == NT_RESULT_OK && sample.value == 1);
    wave->origin.x = 0;
    wave->cycles_per_unit.x = 0x1.0000000000001p-52;
    wave->phase_turns = -0x1p-51;
    query.position = (NtPoint){0x1.0000000000001p52,0};
    CHECK(nt_field_sample(&field,query,&sample) == NT_RESULT_OK && sample.value == 0x1.921fb54442d18p-102);

    wave->cycles_per_unit = (NtPoint){0,0};
    wave->phase_turns = .5;
    wave->period_us = UINT64_MAX;
    query.now = UINT64_MAX/2;
    CHECK(nt_field_sample(&field,query,&sample) == NT_RESULT_OK && sample.value > 0 && sample.value < 2e-19);
    const double positive = sample.value;
    ++query.now;
    CHECK(nt_field_sample(&field,query,&sample) == NT_RESULT_OK && sample.value == -positive);
#if !defined(__wasm__)
    fenv_t saved;
    CHECK(!fegetenv(&saved));
    CHECK(!fesetround(FE_DOWNWARD));
    CHECK(!feclearexcept(FE_ALL_EXCEPT) && !feraiseexcept(FE_DIVBYZERO));
    const NtResult result = nt_field_sample(&field,query,&sample);
    const bool preserved = fegetround() == FE_DOWNWARD && fetestexcept(FE_ALL_EXCEPT) == FE_DIVBYZERO;
    CHECK(!fesetenv(&saved));
    CHECK(result == NT_RESULT_OK && preserved && sample.value == -positive);
#endif
    return 0;
}

static int optical_tails(void)
{
    NtField field = {.kind=NT_FIELD_RIPPLE,.as.ripple=nt_ripple_field_default()};
    NtRippleField *ripple = &field.as.ripple;
    ripple->last_radius = 0;
    ripple->falloff = NT_OPTICAL_INVERSE_QUADRATIC;
    ripple->fade = NT_LINEAR;
    ripple->origin.x = 0x1.ep483;
    NtFieldQuery query = {{0x1.6a09e667f3bcdp+537,0},0,NT_MOTION_FULL};
    NtFieldSample sample;
    CHECK(nt_field_sample(&field,query,&sample) == NT_RESULT_OK && sample.coverage == 0x1p-1074);
    query.position.y = 1;
    CHECK(nt_field_sample(&field,query,&sample) == NT_RESULT_OK && sample.coverage == 0x1p-1074);
    ripple->origin.x = 0;
    CHECK(nt_field_sample(&field,query,&sample) == NT_RESULT_OK && sample.coverage == 0);
    ripple->first_radius = ripple->last_radius = 0x1.ep483;
    CHECK(nt_field_sample(&field,query,&sample) == NT_RESULT_OK && sample.coverage == 0x1p-1074);
    ripple->first_radius = ripple->last_radius = 0;
    ripple->lifetime_us = 2;
    ripple->peak = .75;
    query = (NtFieldQuery){{0x1.bp536,0},1,NT_MOTION_FULL};
    CHECK(nt_field_sample(&field,query,&sample) == NT_RESULT_OK && sample.coverage == 0x1p-1074);

    ripple->falloff = NT_OPTICAL_LINEAR;
    ripple->lifetime_us = UINT64_MAX;
    ripple->peak = 1;
    ripple->fade = NT_SINE;
    query = (NtFieldQuery){{0,0},UINT64_MAX-1,NT_MOTION_FULL};
    CHECK(nt_field_sample(&field,query,&sample) == NT_RESULT_OK &&
          fabs(sample.coverage/0x1.3bd3cc9be45dep-127-1) < 2e-15);
    ripple->fade = NT_EASE_OUT;
    CHECK(nt_field_sample(&field,query,&sample) == NT_RESULT_OK && sample.coverage == 0x1p-63);
    ++query.now;
    CHECK(nt_field_sample(&field,query,&sample) == NT_RESULT_OK && sample.coverage == 0);
    return 0;
}

typedef struct FieldFixture { NtField field; NtFieldInk ink; } FieldFixture;

static NtResult field_measure(const void *data, NtMeasureContext *context, NtConstraints limits, NtCellSize *out)
{
    (void)data;
    (void)context;
    (void)limits;
    *out = (NtCellSize){7,3};
    return NT_RESULT_OK;
}

static NtResult field_layout(const void *data, NtLayoutContext *context, NtCellSize assigned)
{
    (void)data;
    (void)context;
    (void)assigned;
    return NT_RESULT_OK;
}

static NtResult field_paint(const void *data, NtPaintContext *paint, NtCellSize assigned)
{
    (void)assigned;
    const FieldFixture *fixture = data;
    const NtResult result = nt_paint_glyph(paint,(NtCellPoint){1,1},(NtByteView){"x",1},1,
                                           (NtCellStyle){.foreground={255,255,255,255}});
    if (result != NT_RESULT_OK) return result;
    return nt_field_draw(&fixture->field,&fixture->ink,paint,(NtCellRect){{INT32_MIN,1},{UINT32_MAX,1}},0,
                         NT_MOTION_FULL);
}

static int palette_and_paint(void)
{
    const NtRgba levels[] = {{50,0,0,255},{0,180,0,255},{0,0,250,255}};
    const NtPaletteRamp ramp = {{levels,3},NT_PALETTE_STEP,NT_PAINT_PAD,
        {NT_COLOR_SRGB,NT_GAMUT_CLIP,NT_HUE_SHORTER,NT_ALPHA_STRAIGHT},NT_LINEAR};
    FieldFixture fixture = {
        .field={.kind=NT_FIELD_WAVE,.as.wave={.origin={.5,.5},.cycles_per_unit={.25,0}}},
        .ink={.palette=&ramp,.opacity=.5}
    };
    NtRgba color;
    CHECK(nt_field_color(&fixture.field,&fixture.ink,(NtFieldQuery){{3.5,1.5},0,NT_MOTION_FULL},&color) == NT_RESULT_OK);
    CHECK(color.red == 50 && color.green == 0 && color.alpha == 128);

    const NtComponentOps ops = {.measure=field_measure,.layout=field_layout,.paint=field_paint};
    NtTree *tree;
    NtNodeId node;
    CHECK(nt_tree_create(NULL,&tree) == NT_RESULT_OK);
    CHECK(nt_tree_mount(tree,(NtNodeId){0},&ops,&fixture,(NtMountOptions){true,false,false},&node) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){7,3}) == NT_RESULT_OK);
    NtCellBackground background;
    CHECK(nt_frame_background(nt_tree_frame(tree),(NtCellPoint){1,1},&background) == NT_RESULT_OK);
    CHECK(background.color.blue == 250 && background.color.alpha == 128 && background.owner == nt_node_owner(node));
    CHECK(nt_frame_background(nt_tree_frame(tree),(NtCellPoint){1,0},&background) == NT_RESULT_OK && !background.owner);
    NtCellView glyph;
    CHECK(nt_frame_cell(nt_tree_frame(tree),(NtCellPoint){1,1},&glyph) == NT_RESULT_OK && glyph.glyph.count == 1);
    CHECK(glyph.glyph.data[0] == 'x');
    fixture.ink.opacity = NAN;
    CHECK(nt_tree_invalidate(tree,node,NT_INVALIDATE_PAINT) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){7,3}) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(nt_frame_background(nt_tree_frame(tree),(NtCellPoint){1,1},&background) == NT_RESULT_OK && background.color.blue == 250);

    const NtRgba saturated[] = {{255,0,0,255},{0,0,255,255}};
    const NtPaletteRamp reject = {{saturated,2},NT_PALETTE_LINEAR,NT_PAINT_PAD,
        {NT_COLOR_OKLCH,NT_GAMUT_REJECT,NT_HUE_SHORTER,NT_ALPHA_STRAIGHT},NT_LINEAR};
    fixture.ink = (NtFieldInk){.palette=&reject,.opacity=1};
    fixture.field.as.wave.start_us = 1;
    CHECK(nt_field_color(&fixture.field,&fixture.ink,(NtFieldQuery){{.5,.5},0,NT_MOTION_FULL},&color) == NT_RESULT_OK);
    CHECK(color.red == 0 && color.green == 0 && color.blue == 0 && color.alpha == 0);
    CHECK(nt_tree_invalidate(tree,node,NT_INVALIDATE_PAINT) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){7,3}) == NT_RESULT_OK);
    CHECK(nt_frame_background(nt_tree_frame(tree),(NtCellPoint){1,1},&background) == NT_RESULT_OK && !background.owner);
    fixture.field.as.wave.start_us = 0;
    CHECK(nt_tree_invalidate(tree,node,NT_INVALIDATE_PAINT) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){7,3}) == NT_RESULT_OUT_OF_RANGE);
    CHECK(nt_frame_background(nt_tree_frame(tree),(NtCellPoint){1,1},&background) == NT_RESULT_OK && !background.owner);
    nt_tree_destroy(tree);
    return 0;
}

int main(void)
{
    return optical() || ripple_clock() || ripple_precision() || wave_precision() || optical_tails() || palette_and_paint();
}
