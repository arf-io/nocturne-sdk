#include <nocturne/particle_effect.h>

#include <float.h>
#include <math.h>
#include <stdio.h>

#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr,"particle effect consumer line %d: %s\n",__LINE__,#test); return 1; \
} } while (0)

static bool same_cell(NtCellPoint a, NtCellPoint b)
{
    return a.x == b.x && a.y == b.y;
}

static NtParticleEffectOptions single_effect(void)
{
    NtParticleEffectOptions options = {.view=nt_particle_view_options_default()};
    options.view.emitter.period_us = 0;
    options.view.emitter.lifetime_min_us = options.view.emitter.lifetime_max_us = 3000000;
    options.view.emitter.end_color = options.view.emitter.birth_color;
    options.view.emitter.origin = (NtParticleRange){{3,2},{3,2}};
    return options;
}

static int radial_and_sway(void)
{
    NtParticleEffectOptions options = single_effect();
    options.motion = (NtParticleEffectMotion){.radial=true,.direction_turns=.25,.speed_min=4,.speed_max=4};
    options.view.emitter.acceleration = (NtPoint){0,-2};
    NtParticleEmitter *emitter;
    CHECK(nt_particle_effect_emitter_create(NULL,&options,&emitter) == NT_RESULT_OK);
    options.motion.direction_turns = .75;
    NtParticleSample sample;
    size_t count;
    CHECK(nt_particle_emitter_sample(emitter,1000000,&sample,1,&count) == NT_RESULT_OK && count == 1);
    CHECK(sample.position.x == 3 && sample.position.y == 5 && sample.velocity.x == 0 && sample.velocity.y == 2);
    NtParticleSample trail[2];
    CHECK(nt_particle_emitter_trail(emitter,0,1000000,(NtParticleTrailOptions){500000,2},trail,2,&count) == NT_RESULT_OK);
    CHECK(count == 2 && trail[0].position.y == 2 && trail[1].position.y == 3.75);
    nt_particle_emitter_destroy(emitter);

    options.motion.direction_turns = .25;
    options.motion.sweep_turns = 0x1p-55;
    options.motion.speed_min = options.motion.speed_max = 0x1p60;
    CHECK(nt_particle_effect_emitter_create(NULL,&options,&emitter) == NT_RESULT_OK);
    CHECK(nt_particle_emitter_sample(emitter,0,&sample,1,&count) == NT_RESULT_OK && count == 1);
    CHECK(sample.velocity.x < 0 && sample.velocity.y == 0x1p60);
    nt_particle_emitter_destroy(emitter);

    options.view.emitter.seed = 1;
    options.motion.direction_turns = 0;
    options.motion.sweep_turns = 0x1p-1074;
    options.motion.speed_min = options.motion.speed_max = DBL_MAX;
    CHECK(nt_particle_effect_emitter_create(NULL,&options,&emitter) == NT_RESULT_OK);
    CHECK(nt_particle_emitter_sample(emitter,0,&sample,1,&count) == NT_RESULT_OK && count == 1);
    CHECK(sample.velocity.x == DBL_MAX && sample.velocity.y == 0x1p-1073*DBL_MAX);
    nt_particle_emitter_destroy(emitter);

    options = single_effect();
    options.motion.sway_amplitude = (NtPoint){2,-4};
    options.motion.sway_period_us = 1000000;
    CHECK(nt_particle_effect_emitter_create(NULL,&options,&emitter) == NT_RESULT_OK);
    NtParticleSample birth, quarter, half, cycle;
    bool alive;
    CHECK(nt_particle_emitter_particle(emitter,0,1000000,&alive,&cycle) == NT_RESULT_OK && alive);
    CHECK(nt_particle_emitter_particle(emitter,0,500000,&alive,&half) == NT_RESULT_OK && alive);
    CHECK(nt_particle_emitter_particle(emitter,0,0,&alive,&birth) == NT_RESULT_OK && alive);
    CHECK(nt_particle_emitter_particle(emitter,0,250000,&alive,&quarter) == NT_RESULT_OK && alive);
    CHECK(cycle.position.x == 3 && cycle.position.y == 2 && cycle.velocity.x == birth.velocity.x);
    CHECK(fabs(quarter.position.y-2 + 2*(quarter.position.x-3)) < 1e-13);
    CHECK(fabs((quarter.position.x-3) - (half.position.x-3)/2 - birth.velocity.x/0x1.921fb54442d18p+2) < 1e-13);
    nt_particle_emitter_destroy(emitter);

    const NtTimeUs period = UINT64_C(1)<<63;
    options.view.emitter.lifetime_min_us = options.view.emitter.lifetime_max_us = UINT64_MAX;
    options.motion.sway_amplitude = (NtPoint){(double)period,0};
    options.motion.sway_period_us = period;
    CHECK(nt_particle_effect_emitter_create(NULL,&options,&emitter) == NT_RESULT_OK);
    CHECK(nt_particle_emitter_particle(emitter,0,period-1,&alive,&half) == NT_RESULT_OK && alive);
    CHECK(nt_particle_emitter_particle(emitter,0,0,&alive,&birth) == NT_RESULT_OK && alive);
    CHECK(nt_particle_emitter_particle(emitter,0,1,&alive,&quarter) == NT_RESULT_OK && alive);
    CHECK(nt_particle_emitter_particle(emitter,0,period+1,&alive,&cycle) == NT_RESULT_OK && alive);
    CHECK(fabs(quarter.position.x-3 - birth.velocity.x/1e6) < 1e-13);
    CHECK(fabs(half.position.x-3 + birth.velocity.x/1e6) < 1e-13);
    CHECK(cycle.position.x == quarter.position.x && cycle.velocity.x == quarter.velocity.x);
    nt_particle_emitter_destroy(emitter);

    options.view.emitter.seed = UINT64_C(12743223581622469569);
    options.motion.sway_amplitude = (NtPoint){0x1.767e07a77ff4ap+1010,-0x1.767e07a77ff4ap+1010};
    options.motion.sway_period_us = 561;
    CHECK(nt_particle_effect_emitter_create(NULL,&options,&emitter) == NT_RESULT_OK);
    CHECK(nt_particle_emitter_particle(emitter,0,0,&alive,&birth) == NT_RESULT_OK && alive);
    CHECK(birth.velocity.x == DBL_MAX && birth.velocity.y == -DBL_MAX);
    nt_particle_emitter_destroy(emitter);

    options.motion.sway_period_us = 5;
    options.motion.sway_amplitude = (NtPoint){0x1.ab3a71b007403p+1003,0};
    CHECK(nt_particle_effect_emitter_create(NULL,&options,&emitter) == NT_RESULT_OK);
    CHECK(nt_particle_emitter_particle(emitter,0,0,&alive,&birth) == NT_RESULT_OUT_OF_RANGE);
    CHECK(alive && birth.velocity.x == DBL_MAX);
    nt_particle_emitter_destroy(emitter);

    options.motion.sway_amplitude.x = DBL_MAX;
    options.motion.sway_period_us = 1;
    CHECK(nt_particle_effect_emitter_create(NULL,&options,&emitter) == NT_RESULT_OK);
    sample.id = 123;
    count = 321;
    CHECK(nt_particle_emitter_sample(emitter,0,&sample,1,&count) == NT_RESULT_OUT_OF_RANGE);
    CHECK(sample.id == 123 && count == 321);
    nt_particle_emitter_destroy(emitter);
    return 0;
}

static int glitter_and_recipes(void)
{
    NtParticleEffectOptions options = single_effect();
    options.motion.glitter_period_us = 10;
    options.motion.glitter_min_alpha = 80;
    NtParticleEmitter *emitter;
    CHECK(nt_particle_effect_emitter_create(NULL,&options,&emitter) == NT_RESULT_OK);
    unsigned variation = 0, previous = 0;
    for (NtTimeUs epoch = 0; epoch < 100; ++epoch) {
        NtParticleSample first, last;
        bool alive;
        CHECK(nt_particle_emitter_particle(emitter,0,epoch*10+9,&alive,&last) == NT_RESULT_OK && alive);
        CHECK(nt_particle_emitter_particle(emitter,0,epoch*10,&alive,&first) == NT_RESULT_OK && alive);
        CHECK(first.color.alpha >= 80 && first.color.alpha == last.color.alpha);
        if (epoch && previous != first.color.alpha) ++variation;
        previous = first.color.alpha;
    }
    CHECK(variation > 70);
    NtDeadline deadline;
    CHECK(nt_particle_emitter_deadline(emitter,19,NT_MOTION_FULL,100,&deadline) == NT_RESULT_OK && deadline.at == 20);
    CHECK(nt_particle_emitter_deadline(emitter,19,NT_MOTION_REDUCED,100,&deadline) == NT_RESULT_OK && deadline.at == 3000000);
    nt_particle_emitter_destroy(emitter);

    for (NtParticleRecipe recipe = NT_PARTICLE_RECIPE_BURST; recipe <= NT_PARTICLE_RECIPE_FIREWORK; ++recipe) {
        CHECK(nt_particle_effect_recipe(recipe,&options) == NT_RESULT_OK);
        options.view.emitter.seed = 671;
        options.view.emitter.start_us = 1000;
        CHECK(nt_particle_effect_emitter_create(NULL,&options,&emitter) == NT_RESULT_OK);
        NtParticleSample samples[64];
        size_t count;
        CHECK(nt_particle_emitter_sample(emitter,999,samples,64,&count) == NT_RESULT_OK && !count);
        CHECK(nt_particle_emitter_sample(emitter,1000,samples,64,&count) == NT_RESULT_OK);
        CHECK(count == options.view.emitter.births_per_tick);
        if (options.motion.radial && count > 2) {
            CHECK(samples[0].velocity.x != samples[1].velocity.x);
            for (size_t i = 0; i < count; ++i) {
                const double speed = hypot(samples[i].velocity.x,samples[i].velocity.y);
                CHECK(speed >= options.motion.speed_min-1e-12 && speed <= options.motion.speed_max+1e-12);
            }
        }
        nt_particle_emitter_destroy(emitter);
        NtTree *tree;
        NtNodeId id;
        CHECK(nt_tree_create(NULL,&tree) == NT_RESULT_OK);
        CHECK(nt_particle_effect_mount(tree,(NtNodeId){0},(NtMountOptions){true,false,false},&options,&id) == NT_RESULT_OK);
        CHECK(nt_tree_tick(tree,1000,NT_MOTION_FULL) == NT_RESULT_OK);
        CHECK(nt_tree_publish(tree,(NtCellSize){32,9}) == NT_RESULT_OK);
        nt_tree_destroy(tree);
    }
    options.view.emitter.seed = 673;
    CHECK(nt_particle_effect_recipe((NtParticleRecipe)-1,&options) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(options.view.emitter.seed == 673);
    return 0;
}

static int effect_mount(void)
{
    NtParticleEffectOptions options = single_effect();
    options.view.frame_interval_us = 1000;
    options.motion = (NtParticleEffectMotion){.radial=true,.speed_min=4,.speed_max=4};
    char glyph = 'o';
    options.view.paint.glyph = (NtByteView){&glyph,1};
    NtTree *tree;
    NtNodeId id;
    CHECK(nt_tree_create(NULL,&tree) == NT_RESULT_OK);
    CHECK(nt_particle_effect_mount(tree,(NtNodeId){0},(NtMountOptions){true,false,false},&options,&id) == NT_RESULT_OK);
    glyph = 'x';
    NtCellView cell;
    CHECK(nt_tree_tick(tree,500000,NT_MOTION_FULL) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){10,4}) == NT_RESULT_OK);
    CHECK(nt_frame_cell(nt_tree_frame(tree),(NtCellPoint){5,2},&cell) == NT_RESULT_OK && cell.owner == nt_node_owner(id));
    CHECK(cell.glyph.count == 1 && *cell.glyph.data == 'o');
    options.motion.speed_min = -1;
    CHECK(nt_particle_effect_set(tree,id,&options) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(nt_tree_tick(tree,750000,NT_MOTION_REDUCED) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){10,4}) == NT_RESULT_OK);
    CHECK(nt_frame_cell(nt_tree_frame(tree),(NtCellPoint){3,2},&cell) == NT_RESULT_OK && cell.owner == nt_node_owner(id));
    CHECK(cell.glyph.count == 1 && *cell.glyph.data == 'o');
    CHECK(nt_particle_view_set(tree,id,&options.view) == NT_RESULT_OK);
    CHECK(nt_tree_tick(tree,800000,NT_MOTION_FULL) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){10,4}) == NT_RESULT_OK);
    CHECK(nt_frame_cell(nt_tree_frame(tree),(NtCellPoint){3,2},&cell) == NT_RESULT_OK && cell.owner == nt_node_owner(id));
    CHECK(cell.glyph.count == 1 && *cell.glyph.data == 'x');
    CHECK(nt_tree_remove(tree,id) == NT_RESULT_OK && !nt_tree_next_update(tree).scheduled);
    nt_tree_destroy(tree);
    return 0;
}

static int branch_timing(void)
{
    NtVisualBranchOptions options = nt_visual_branch_options_default();
    options.start_us = 100;
    options.lifetime_us = 120;
    options.reveal_us = 5;
    options.flicker_us = 0;
    NtVisualBranchSegment segments[NT_VISUAL_BRANCH_MAX_SEGMENTS];
    NtVisualBranchSegment head = {.id=999};
    size_t count = 123;
    CHECK(nt_visual_branch_sample(&options,99,NT_MOTION_FULL,&head,1,&count) == NT_RESULT_OK && !count && head.id == 999);
    CHECK(nt_visual_branch_sample(&options,105,NT_MOTION_FULL,&head,1,&count) == NT_RESULT_INSUFFICIENT_BUFFER);
    CHECK(count == 2 && head.id == 999);
    CHECK(nt_visual_branch_sample(&options,160,NT_MOTION_FULL,segments,NT_VISUAL_BRANCH_MAX_SEGMENTS,&count) == NT_RESULT_OK);
    CHECK(count == 13 && segments[0].color.alpha == 128 && segments[0].parent_id == UINT32_MAX);
    for (size_t i = 1; i < count; ++i) {
        CHECK(segments[i].id == i && segments[i].parent_id < i);
        CHECK(same_cell(segments[i].start,segments[segments[i].parent_id].end));
    }
    CHECK(nt_visual_branch_sample(&options,100,NT_MOTION_REDUCED,segments,NT_VISUAL_BRANCH_MAX_SEGMENTS,&count) == NT_RESULT_OK);
    CHECK(count == 19 && segments[0].color.alpha == 255 && same_cell(segments[6].end,options.end));
    NtDeadline deadline;
    CHECK(nt_visual_branch_deadline(&options,99,NT_MOTION_FULL,60,&deadline) == NT_RESULT_OK && deadline.at == 100);
    CHECK(nt_visual_branch_deadline(&options,103,NT_MOTION_FULL,60,&deadline) == NT_RESULT_OK && deadline.at == 105);
    CHECK(nt_visual_branch_deadline(&options,103,NT_MOTION_REDUCED,60,&deadline) == NT_RESULT_OK && deadline.at == 220);
    CHECK(nt_visual_branch_sample(&options,220,NT_MOTION_FULL,NULL,0,&count) == NT_RESULT_OK && !count);
    CHECK(nt_visual_branch_deadline(&options,220,NT_MOTION_FULL,60,&deadline) == NT_RESULT_OK && !deadline.scheduled);

    options.start_us = UINT64_MAX-1;
    options.reveal_us = UINT64_MAX;
    CHECK(nt_visual_branch_sample(&options,UINT64_MAX,NT_MOTION_FULL,NULL,0,&count) == NT_RESULT_OK && count == 1);
    CHECK(nt_visual_branch_deadline(&options,UINT64_MAX,NT_MOTION_FULL,0,&deadline) == NT_RESULT_OK && !deadline.scheduled);
    options.trunk_segments = 256;
    options.branch_count = 0;
    CHECK(nt_visual_branch_sample(&options,UINT64_MAX,NT_MOTION_REDUCED,NULL,0,&count) == NT_RESULT_OK && count == 256);
    options.branch_count = 1;
    CHECK(nt_visual_branch_sample(&options,UINT64_MAX,NT_MOTION_REDUCED,&head,1,&count) == NT_RESULT_OUT_OF_RANGE);
    CHECK(head.id == 999 && count == 256);

    options.trunk_segments = 2;
    options.branch_count = 1;
    options.start = (NtCellPoint){1,1};
    options.end = (NtCellPoint){21,1};
    options.jitter_cells = 0;
    options.branch_reach_cells = UINT32_MAX;
    CHECK(nt_visual_branch_sample(&options,UINT64_MAX,NT_MOTION_REDUCED,&head,1,&count) == NT_RESULT_OUT_OF_RANGE);
    CHECK(head.id == 999 && count == 256);
    return 0;
}

typedef struct BranchFixture {
    NtVisualBranchOptions options;
    NtByteView glyph;
} BranchFixture;

static NtResult branch_paint(const void *state, NtPaintContext *context, NtCellSize assigned)
{
    (void)assigned;
    const BranchFixture *fixture = state;
    return nt_visual_branch_draw(&fixture->options,context,0,NT_MOTION_REDUCED,fixture->glyph,
                                 (NtWidthPolicy){NT_WIDTH_ONE,NT_WIDTH_TWO});
}

static NtResult branch_measure(const void *state, NtMeasureContext *context, NtConstraints constraints, NtCellSize *out)
{
    (void)state;
    (void)context;
    (void)constraints;
    *out = (NtCellSize){6,3};
    return NT_RESULT_OK;
}

static NtResult branch_layout(const void *state, NtLayoutContext *context, NtCellSize assigned)
{
    (void)state;
    (void)context;
    (void)assigned;
    return NT_RESULT_OK;
}

static int branch_draw(void)
{
    BranchFixture fixture = {nt_visual_branch_options_default(),{"*",1}};
    fixture.options.start = (NtCellPoint){INT32_MIN,1};
    fixture.options.end = (NtCellPoint){INT32_MAX,1};
    fixture.options.trunk_segments = 2;
    fixture.options.branch_count = fixture.options.jitter_cells = 0;
    fixture.options.color.alpha = 127;
    const NtComponentOps ops = {.measure=branch_measure,.layout=branch_layout,.paint=branch_paint};
    NtTree *tree;
    NtNodeId id;
    CHECK(nt_tree_create(NULL,&tree) == NT_RESULT_OK);
    CHECK(nt_tree_mount(tree,(NtNodeId){0},&ops,&fixture,(NtMountOptions){true,false,false},&id) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){6,3}) == NT_RESULT_OK);
    for (int x = 0; x < 6; ++x) {
        NtCellView cell;
        CHECK(nt_frame_cell(nt_tree_frame(tree),(NtCellPoint){x,1},&cell) == NT_RESULT_OK);
        CHECK(cell.owner == nt_node_owner(id) && cell.style.foreground.alpha == 127);
        CHECK(nt_frame_cell(nt_tree_frame(tree),(NtCellPoint){x,0},&cell) == NT_RESULT_OK && !cell.owner);
    }
    char glyph[129] = {'a'};
    for (size_t i = 1; i < sizeof glyph; i += 2) {
        glyph[i] = (char)0xcc;
        glyph[i+1] = (char)0x81;
    }
    fixture.glyph = (NtByteView){glyph,sizeof glyph};
    CHECK(nt_tree_invalidate(tree,id,NT_INVALIDATE_PAINT) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){6,3}) == NT_RESULT_OK);
    NtCellView cell;
    CHECK(nt_frame_cell(nt_tree_frame(tree),(NtCellPoint){0,1},&cell) == NT_RESULT_OK);
    CHECK(cell.owner == nt_node_owner(id) && cell.glyph.count == sizeof glyph);
    nt_tree_destroy(tree);
    return 0;
}

int main(void)
{
    return radial_and_sway() || glitter_and_recipes() || effect_mount() || branch_timing() || branch_draw();
}
