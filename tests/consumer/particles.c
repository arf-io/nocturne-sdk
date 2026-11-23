#include <nocturne/particle.h>

#include <float.h>
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr,"particle consumer line %d: %s\n",__LINE__,#test); return 1; \
} } while (0)

static NtParticleEmitterOptions fixed(void)
{
    NtParticleEmitterOptions options = nt_particle_emitter_options_default();
    options.start_us = 1000000;
    options.period_us = 250000;
    options.emission_limit = 5;
    options.capacity = 2;
    options.lifetime_min_us = options.lifetime_max_us = 1000000;
    options.origin = (NtParticleRange){{1,3},{1,3}};
    options.velocity = (NtParticleRange){{5,-2},{5,-2}};
    options.acceleration = (NtPoint){2,2};
    return options;
}

static int schedule_and_motion(void)
{
    NtParticleEmitterOptions options = fixed();
    NtParticleEmitter *emitter;
    CHECK(nt_particle_emitter_create(NULL,&options,&emitter) == NT_RESULT_OK);
    NtParticleSample particles[2];
    size_t count;
    CHECK(nt_particle_emitter_sample(emitter,999999,particles,2,&count) == NT_RESULT_OK && count == 0);
    CHECK(nt_particle_emitter_sample(emitter,1000000,particles,2,&count) == NT_RESULT_OK && count == 1);
    CHECK(particles[0].id == 0 && particles[0].age_us == 0 && particles[0].position.x == 1);
    CHECK(nt_particle_emitter_sample(emitter,1500000,particles,2,&count) == NT_RESULT_OK && count == 2);
    CHECK(particles[0].id == 1 && particles[1].id == 2);
    CHECK(particles[0].position.x == 2.3125 && particles[0].position.y == 2.5625);
    CHECK(particles[0].velocity.x == 5.5 && particles[0].velocity.y == -1.5);
    CHECK(particles[0].born_us == 1250000 && particles[0].age_us == 250000 &&
          particles[0].lifetime_us == 1000000 && particles[0].progress == .25);
    NtParticleSample canary = {.id=77};
    count = 99;
    CHECK(nt_particle_emitter_sample(emitter,1500000,&canary,1,&count) == NT_RESULT_INSUFFICIENT_BUFFER);
    CHECK(canary.id == 77 && count == 2);
    CHECK(nt_particle_emitter_sample(emitter,1500000,NULL,0,&count) == NT_RESULT_OK && count == 2);
    bool alive;
    CHECK(nt_particle_emitter_particle(emitter,0,1999999,&alive,&particles[0]) == NT_RESULT_OK && alive);
    CHECK(nt_particle_emitter_particle(emitter,0,2000000,&alive,&canary) == NT_RESULT_OK && !alive);
    CHECK(canary.id == 77);

    NtParticleSample trail[4];
    CHECK(nt_particle_emitter_trail(emitter,1,1625000,(NtParticleTrailOptions){100000,4},
                                   trail,4,&count) == NT_RESULT_OK && count == 3);
    CHECK(trail[0].age_us == 75000 && trail[1].age_us == 175000 && trail[2].age_us == 275000);
    CHECK(trail[0].id == 1 && trail[2].position.x > trail[0].position.x);

    NtDeadline deadline;
    CHECK(nt_particle_emitter_deadline(emitter,999999,NT_MOTION_REDUCED,16000,&deadline) == NT_RESULT_OK &&
          deadline.scheduled && deadline.at == 1000000);
    CHECK(nt_particle_emitter_deadline(emitter,2000000,NT_MOTION_FULL,16000,&deadline) == NT_RESULT_OK &&
          deadline.scheduled && deadline.at == 2016000);
    CHECK(nt_particle_emitter_deadline(emitter,2999999,NT_MOTION_FULL,16000,&deadline) == NT_RESULT_OK &&
          deadline.scheduled && deadline.at == 3000000);
    CHECK(nt_particle_emitter_deadline(emitter,3000000,NT_MOTION_FULL,16000,&deadline) == NT_RESULT_OK &&
          !deadline.scheduled);
    nt_particle_emitter_destroy(emitter);
    return 0;
}

static int seeded(void)
{
    NtParticleEmitterOptions options = fixed();
    options.seed = 92;
    options.stream = 4;
    options.origin.max = (NtPoint){11,8};
    options.velocity.max = (NtPoint){9,1};
    options.lifetime_min_us = 750000;
    NtParticleEmitter *a, *b;
    CHECK(nt_particle_emitter_create(NULL,&options,&a) == NT_RESULT_OK);
    CHECK(nt_particle_emitter_create(NULL,&options,&b) == NT_RESULT_OK);
    options.stream = 7;
    NtParticleEmitter *separate;
    CHECK(nt_particle_emitter_create(NULL,&options,&separate) == NT_RESULT_OK);
    NtParticleSample before[2], after[2];
    size_t count;
    CHECK(nt_particle_emitter_sample(a,1625000,before,2,&count) == NT_RESULT_OK && count == 2);
    for (NtTimeUs at = 1000000; at <= 1625000; at += 31337)
        CHECK(nt_particle_emitter_sample(b,at,after,2,&count) == NT_RESULT_OK);
    CHECK(nt_particle_emitter_sample(b,2400000,after,2,&count) == NT_RESULT_OK);
    CHECK(nt_particle_emitter_sample(b,1625000,after,2,&count) == NT_RESULT_OK && count == 2);
    for (size_t i = 0; i < count; ++i) {
        CHECK(before[i].id == after[i].id && before[i].lifetime_us == after[i].lifetime_us);
        CHECK(before[i].position.x == after[i].position.x && before[i].position.y == after[i].position.y);
        CHECK(before[i].velocity.x == after[i].velocity.x && before[i].velocity.y == after[i].velocity.y);
    }
    CHECK(nt_particle_emitter_sample(separate,1625000,after,2,&count) == NT_RESULT_OK && count == 2);
    CHECK(before[0].position.x != after[0].position.x || before[0].lifetime_us != after[0].lifetime_us);
    nt_particle_emitter_destroy(separate);
    nt_particle_emitter_destroy(b);
    nt_particle_emitter_destroy(a);
    return 0;
}

static int limits(void)
{
    NtParticleEmitterOptions options = nt_particle_emitter_options_default();
    options.period_us = 1;
    options.capacity = 2;
    options.lifetime_min_us = options.lifetime_max_us = 2;
    NtParticleEmitter *emitter;
    CHECK(nt_particle_emitter_create(NULL,&options,&emitter) == NT_RESULT_OK);
    NtParticleSample samples[2];
    size_t count;
    CHECK(nt_particle_emitter_sample(emitter,UINT64_MAX,samples,2,&count) == NT_RESULT_OK && count == 2);
    CHECK(samples[0].id == UINT64_MAX-1 && samples[0].age_us == 1);
    CHECK(samples[1].id == UINT64_MAX && samples[1].born_us == UINT64_MAX && samples[1].age_us == 0);
    NtDeadline deadline;
    CHECK(nt_particle_emitter_deadline(emitter,UINT64_MAX,NT_MOTION_FULL,0,&deadline) == NT_RESULT_OK &&
          !deadline.scheduled);
    nt_particle_emitter_destroy(emitter);

    options.period_us = 0;
    options.births_per_tick = 4;
    options.emission_limit = 3;
    CHECK(nt_particle_emitter_create(NULL,&options,&emitter) == NT_RESULT_OK);
    CHECK(nt_particle_emitter_sample(emitter,1,samples,2,&count) == NT_RESULT_OK && count == 2);
    CHECK(samples[0].id == 1 && samples[1].id == 2 && !samples[1].born_us);
    CHECK(nt_particle_emitter_sample(emitter,2,samples,2,&count) == NT_RESULT_OK && count == 0);
    nt_particle_emitter_destroy(emitter);

    options.origin = (NtParticleRange){{DBL_MAX,0},{DBL_MAX,0}};
    options.velocity = (NtParticleRange){{DBL_MAX,0},{DBL_MAX,0}};
    options.lifetime_min_us = options.lifetime_max_us = 3000000;
    CHECK(nt_particle_emitter_create(NULL,&options,&emitter) == NT_RESULT_OK);
    samples[0].id = 999;
    count = 99;
    CHECK(nt_particle_emitter_sample(emitter,1000000,samples,2,&count) == NT_RESULT_OUT_OF_RANGE);
    CHECK(samples[0].id == 999 && count == 99);
    nt_particle_emitter_destroy(emitter);
    return 0;
}

static int mounted(void)
{
    NtParticleViewOptions view = nt_particle_view_options_default();
    view.emitter.origin = (NtParticleRange){{1,1},{1,1}};
    view.emitter.velocity = (NtParticleRange){{4,0},{4,0}};
    view.emitter.period_us = 0;
    view.emitter.lifetime_min_us = view.emitter.lifetime_max_us = 1000000;
    view.paint.trail = (NtParticleTrailOptions){250000,3};
    NtTree *tree;
    NtNodeId layer;
    CHECK(nt_tree_create(NULL,&tree) == NT_RESULT_OK);
    CHECK(nt_particle_view_mount(tree,(NtNodeId){0},(NtMountOptions){true,false,false},&view,&layer) == NT_RESULT_OK);
    CHECK(nt_tree_tick(tree,500000,NT_MOTION_FULL) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){10,3}) == NT_RESULT_OK);
    NtCellView cell;
    CHECK(nt_frame_cell(nt_tree_frame(tree),(NtCellPoint){3,1},&cell) == NT_RESULT_OK &&
          cell.owner == nt_node_owner(layer));
    CHECK(nt_frame_cell(nt_tree_frame(tree),(NtCellPoint){2,1},&cell) == NT_RESULT_OK &&
          cell.owner == nt_node_owner(layer));
    CHECK(nt_tree_tick(tree,600000,NT_MOTION_REDUCED) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){10,3}) == NT_RESULT_OK);
    CHECK(nt_frame_cell(nt_tree_frame(tree),(NtCellPoint){1,1},&cell) == NT_RESULT_OK &&
          cell.owner == nt_node_owner(layer));
    CHECK(nt_frame_cell(nt_tree_frame(tree),(NtCellPoint){2,1},&cell) == NT_RESULT_OK && !cell.owner);
    CHECK(nt_tree_tick(tree,1000000,NT_MOTION_REDUCED) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){10,3}) == NT_RESULT_OK);
    CHECK(!nt_tree_next_update(tree).scheduled);
    CHECK(nt_frame_cell(nt_tree_frame(tree),(NtCellPoint){1,1},&cell) == NT_RESULT_OK && !cell.owner);
    CHECK(nt_tree_remove(tree,layer) == NT_RESULT_OK);
    CHECK(nt_particle_view_set(tree,layer,&view) == NT_RESULT_STALE_REFERENCE);
    nt_tree_destroy(tree);
    return 0;
}

static int dropped_births_stay_dropped(void)
{
    NtParticleEmitterOptions options = nt_particle_emitter_options_default();
    options.period_us = 1;
    options.capacity = 1;
    options.emission_limit = 2;
    options.lifetime_min_us = 1;
    options.lifetime_max_us = 10;
    for (uint64_t seed = 0; seed < 128; ++seed) {
        options.seed = seed;
        NtParticleEmitter *emitter;
        CHECK(nt_particle_emitter_create(NULL,&options,&emitter) == NT_RESULT_OK);
        NtParticleSample first, second;
        bool alive;
        CHECK(nt_particle_emitter_particle(emitter,0,0,&alive,&first) == NT_RESULT_OK && alive);
        CHECK(nt_particle_emitter_particle(emitter,1,1,&alive,&second) == NT_RESULT_OK && alive);
        if (first.lifetime_us > 1+second.lifetime_us) {
            size_t count;
            const NtTimeUs newer_expired = 1+second.lifetime_us;
            CHECK(nt_particle_emitter_sample(emitter,newer_expired,NULL,0,&count) == NT_RESULT_OK && count == 0);
            CHECK(nt_particle_emitter_particle(emitter,0,newer_expired,&alive,&first) == NT_RESULT_OK && alive);
            nt_particle_emitter_destroy(emitter);
            return 0;
        }
        nt_particle_emitter_destroy(emitter);
    }
    CHECK(false);
}

typedef struct Heap { size_t calls, live, reject; } Heap;

static void *allocate(void *value, size_t bytes, size_t alignment)
{
    (void)alignment;
    Heap *heap = value;
    if (++heap->calls == heap->reject) return NULL;
    void *pointer = malloc(bytes);
    if (pointer) ++heap->live;
    return pointer;
}

static void release(void *value, void *pointer, size_t bytes, size_t alignment)
{
    (void)bytes;
    (void)alignment;
    Heap *heap = value;
    --heap->live;
    free(pointer);
}

static int allocation_lifecycle(void)
{
    Heap heap = {0};
    const NtAllocator allocator = {&heap,allocate,release};
    NtParticleViewOptions options = nt_particle_view_options_default();
    NtTree *tree;
    CHECK(nt_tree_create(&allocator,&tree) == NT_RESULT_OK);
    const size_t original = heap.live;
    const NtMountOptions display = {true,false,false};
    NtNodeId mount = {0};
    for (size_t site = 1; site <= 2; ++site) {
        heap.reject = heap.calls+site;
        CHECK(nt_particle_view_mount(tree,(NtNodeId){0},display,&options,&mount) == NT_RESULT_ALLOCATION_FAILED);
        CHECK(heap.live == original && !mount.tree);
    }
    heap.reject = 0;
    CHECK(nt_particle_view_mount(tree,(NtNodeId){0},display,&options,&mount) == NT_RESULT_OK);
    CHECK(nt_tree_tick(tree,100000,NT_MOTION_FULL) == NT_RESULT_OK && nt_tree_next_update(tree).scheduled);
    CHECK(nt_tree_publish(tree,(NtCellSize){12,8}) == NT_RESULT_OK);
    const NtFrame *prior = nt_tree_frame(tree);
    const size_t live = heap.live;
    NtParticleViewOptions invalid = options;
    invalid.paint.glyph = (NtByteView){"界",3};
    CHECK(nt_particle_view_set(tree,mount,&invalid) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(heap.live == live && nt_tree_frame(tree) == prior);
    CHECK(nt_tree_remove(tree,mount) == NT_RESULT_OK && !nt_tree_next_update(tree).scheduled);
    nt_tree_destroy(tree);
    CHECK(!heap.live);
    return 0;
}

static int motion_boundaries(void)
{
    NtParticleEmitterOptions options = nt_particle_emitter_options_default();
    options.period_us = 0;
    options.lifetime_min_us = options.lifetime_max_us = 2000000;
    const double unit = 0x1p1020;
    const double velocity = (DBL_MAX - 15 * unit) - 1.5 * unit;
    options.velocity.min = options.velocity.max = (NtPoint){velocity,-velocity};
    options.acceleration = (NtPoint){15*unit,-15*unit};
    NtParticleEmitter *emitter;
    CHECK(nt_particle_emitter_create(NULL,&options,&emitter) == NT_RESULT_OK);
    bool alive = false;
    NtParticleSample sample;
    CHECK(nt_particle_emitter_particle(emitter,0,1100000,&alive,&sample) == NT_RESULT_OK && alive);
    CHECK(sample.velocity.x == DBL_MAX && sample.velocity.y == -DBL_MAX);
    CHECK(nt_particle_emitter_particle(emitter,0,1100001,&alive,&sample) == NT_RESULT_OUT_OF_RANGE);
    CHECK(alive && sample.age_us == 1100000);
    nt_particle_emitter_destroy(emitter);

    NtParticleViewOptions view = nt_particle_view_options_default();
    view.emitter.period_us = 0;
    view.emitter.origin.min.x = view.emitter.origin.max.x = 1;
    view.emitter.velocity.min.x = view.emitter.velocity.max.x = -10;
    NtTree *tree;
    NtNodeId mount;
    CHECK(nt_tree_create(NULL,&tree) == NT_RESULT_OK);
    CHECK(nt_particle_view_mount(tree,(NtNodeId){0},(NtMountOptions){.enabled=true},&view,&mount) == NT_RESULT_OK);
    CHECK(nt_tree_tick(tree,100000,NT_MOTION_FULL) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){2,1}) == NT_RESULT_OK);
    NtCellView cell;
    CHECK(nt_frame_cell(nt_tree_frame(tree),(NtCellPoint){0,0},&cell) == NT_RESULT_OK);
    CHECK(cell.owner == nt_node_owner(mount) && cell.glyph.count);
    nt_tree_destroy(tree);
    return 0;
}

static int exact_motion_boundary(void)
{
    NtParticleViewOptions options = nt_particle_view_options_default();
    options.emitter.period_us = 0;
    options.emitter.lifetime_min_us = options.emitter.lifetime_max_us = 2000000;
    options.emitter.origin = (NtParticleRange){{1,0},{1,0}};
    options.emitter.velocity = (NtParticleRange){{-10,0},{-10,0}};
    NtParticleEmitter *emitter;
    CHECK(nt_particle_emitter_create(NULL,&options.emitter,&emitter) == NT_RESULT_OK);
    NtParticleSample sample;
    bool alive;
    CHECK(nt_particle_emitter_particle(emitter,0,100000,&alive,&sample) == NT_RESULT_OK && alive);
    CHECK(sample.position.x == 0);
    nt_particle_emitter_destroy(emitter);
    NtTree *tree;
    NtNodeId node;
    CHECK(nt_tree_create(NULL,&tree) == NT_RESULT_OK);
    CHECK(nt_particle_view_mount(tree,(NtNodeId){0},(NtMountOptions){true,false,false},&options,&node) == NT_RESULT_OK);
    CHECK(nt_tree_tick(tree,100000,NT_MOTION_FULL) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){2,1}) == NT_RESULT_OK);
    NtCellView cell;
    CHECK(nt_frame_cell(nt_tree_frame(tree),(NtCellPoint){0,0},&cell) == NT_RESULT_OK &&
          cell.owner == nt_node_owner(node));

    options.emitter.velocity = (NtParticleRange){{-0x1p-54,0},{-0x1p-54,0}};
    CHECK(nt_particle_view_set(tree,node,&options) == NT_RESULT_OK);
    CHECK(nt_tree_tick(tree,1000000,NT_MOTION_FULL) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){2,1}) == NT_RESULT_OK);
    CHECK(nt_frame_cell(nt_tree_frame(tree),(NtCellPoint){0,0},&cell) == NT_RESULT_OK &&
          cell.owner == nt_node_owner(node));
    CHECK(nt_frame_cell(nt_tree_frame(tree),(NtCellPoint){1,0},&cell) == NT_RESULT_OK && !cell.owner);
    nt_tree_destroy(tree);

    const double unit = 0x1p1020, initial = (DBL_MAX-15*unit)-1.5*unit;
    options.emitter.origin = (NtParticleRange){0};
    options.emitter.velocity = (NtParticleRange){{initial,-initial},{initial,-initial}};
    options.emitter.acceleration = (NtPoint){15*unit,-15*unit};
    CHECK(nt_particle_emitter_create(NULL,&options.emitter,&emitter) == NT_RESULT_OK);
    CHECK(nt_particle_emitter_particle(emitter,0,1100000,&alive,&sample) == NT_RESULT_OK && alive);
    CHECK(sample.velocity.x == DBL_MAX && sample.velocity.y == -DBL_MAX);
    nt_particle_emitter_destroy(emitter);
    return 0;
}

static int subnormal_paint_environment(void)
{
#if defined(FE_DFL_DISABLE_DENORMS_ENV)
    fenv_t saved;
    CHECK(fegetenv(&saved) == 0);
    CHECK(fesetenv(FE_DFL_DISABLE_DENORMS_ENV) == 0);
    const double tiny = 0x0.0000000000001p-1022;
    NtParticleViewOptions options = nt_particle_view_options_default();
    options.emitter.period_us = 0;
    options.emitter.origin.min.x = options.emitter.origin.max.x = -tiny;
    NtParticleEmitter *emitter = NULL;
    NtParticleEmitterOptions reversed = options.emitter;
    reversed.origin.min.x = 0;
    CHECK(nt_particle_emitter_create(NULL,&reversed,&emitter) == NT_RESULT_INVALID_ARGUMENT &&
          !emitter);
    NtTree *tree;
    NtNodeId layer = {0};
    CHECK(nt_tree_create(NULL,&tree) == NT_RESULT_OK);
    NtParticleViewOptions invalid = options;
    invalid.paint.trail_opacity = -tiny;
    const NtMountOptions draw = {.enabled=true};
    CHECK(nt_particle_view_mount(tree,(NtNodeId){0},draw,&invalid,&layer) == NT_RESULT_INVALID_ARGUMENT &&
          !layer.tree);
    CHECK(nt_particle_view_mount(tree,(NtNodeId){0},draw,&options,&layer) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){2,1}) == NT_RESULT_OK);
    NtCellView cell;
    CHECK(nt_frame_cell(nt_tree_frame(tree),(NtCellPoint){0,0},&cell) == NT_RESULT_OK && !cell.owner);
    options.emitter.origin = (NtParticleRange){0};
    options.emitter.velocity.min.x = options.emitter.velocity.max.x = -tiny;
    CHECK(nt_particle_view_set(tree,layer,&options) == NT_RESULT_OK);
    CHECK(nt_tree_tick(tree,1,NT_MOTION_FULL) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){2,1}) == NT_RESULT_OK);
    CHECK(nt_frame_cell(nt_tree_frame(tree),(NtCellPoint){0,0},&cell) == NT_RESULT_OK && !cell.owner);
    nt_tree_destroy(tree);
    CHECK(fesetenv(&saved) == 0);
#endif
    return 0;
}

int main(void)
{
    CHECK(schedule_and_motion() == 0);
    CHECK(seeded() == 0);
    CHECK(limits() == 0);
    CHECK(mounted() == 0);
    CHECK(dropped_births_stay_dropped() == 0);
    CHECK(allocation_lifecycle() == 0);
    CHECK(exact_motion_boundary() == 0);
    CHECK(subnormal_paint_environment() == 0);
    CHECK(motion_boundaries() == 0);
    return 0;
}
