#include <nocturne/particle_deposits.h>

#include <float.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr,"particle deposits line %d: %s\n",__LINE__,#test); return 1; \
} } while (0)

typedef struct Budget { unsigned calls, live; size_t size, alignment; bool refuse; } Budget;

static void *reserve(void *context, size_t size, size_t alignment)
{
    Budget *budget = context;
    ++budget->calls;
    if (budget->refuse || budget->live) return NULL;
    void *allocation = malloc(size);
    if (allocation) {
        ++budget->live;
        budget->size = size;
        budget->alignment = alignment;
    }
    return allocation;
}

static void reclaim(void *context, void *address, size_t size, size_t alignment)
{
    Budget *budget = context;
    if (budget->size != size || budget->alignment != alignment || budget->live != 1) abort();
    --budget->live;
    free(address);
}

static NtParticleDepositsOptions options_default(void)
{
    NtParticleDepositsOptions options = nt_particle_deposits_options_default();
    options.hold_us = 10;
    options.capacity = 3;
    options.release.count = 2;
    options.release.lifetime_min_us = options.release.lifetime_max_us = 20;
    options.release.velocity = (NtParticleRange){{2,-3},{2,-3}};
    return options;
}

static int aggregate_and_release(void)
{
    Budget budget = {.refuse=true};
    const NtAllocator allocator = {&budget,reserve,reclaim};
    const NtParticleDepositsOptions options = options_default();
    NtParticleDeposits *deposits = NULL;
    CHECK(nt_particle_deposits_create(&allocator,&options,&deposits) == NT_RESULT_ALLOCATION_FAILED && !deposits);
    budget.refuse = false;
    CHECK(nt_particle_deposits_create(&allocator,&options,&deposits) == NT_RESULT_OK && budget.live == 1);
    budget.refuse = true;
    uint64_t id = UINT64_MAX;
    CHECK(nt_particle_deposits_add(deposits,(NtParticleContact){{2,3},1,1,{255,0,0,255}},&id) == NT_RESULT_OK && id == 0);
    CHECK(nt_particle_deposits_add(deposits,(NtParticleContact){{2,3},2,3,{0,0,255,255}},&id) == NT_RESULT_OK && id == 1);
    CHECK(nt_particle_deposits_add(deposits,(NtParticleContact){{-1,5},2,UINT32_MAX,{0,200,0,100}},&id) == NT_RESULT_OK && id == 2);
    CHECK(nt_particle_deposits_add(deposits,(NtParticleContact){{3,4},10,1,{1,0,0,255}},&id) == NT_RESULT_OUT_OF_RANGE && id == 2);

    size_t count = 92;
    NtParticlePile piles[2] = {{.mass=75}};
    CHECK(nt_particle_deposits_piles(deposits,2,piles,1,&count) == NT_RESULT_INSUFFICIENT_BUFFER && count == 2);
    CHECK(piles[0].mass == 75);
    CHECK(nt_particle_deposits_piles(deposits,2,piles,2,&count) == NT_RESULT_OK && count == 2);
    CHECK(piles[0].mass == 4 && piles[0].contacts == 2 && piles[0].cell.x == 2 && piles[0].cell.y == 3);
    CHECK(piles[0].color.red == 64 && piles[0].color.blue == 191 && piles[0].color.alpha == 255);
    CHECK(piles[1].mass == UINT32_MAX && piles[1].contacts == 1 && piles[1].color.green == 200);
    NtDeadline deadline;
    CHECK(nt_particle_deposits_deadline(deposits,10,NT_MOTION_FULL,1,&deadline) == NT_RESULT_OK && deadline.at == 11);
    CHECK(nt_particle_deposits_deadline(deposits,11,NT_MOTION_REDUCED,1,&deadline) == NT_RESULT_OK && deadline.at == 12);
    CHECK(nt_particle_deposits_deadline(deposits,12,NT_MOTION_REDUCED,1,&deadline) == NT_RESULT_OK && deadline.at == 31);
    CHECK(nt_particle_deposits_deadline(deposits,12,NT_MOTION_FULL,1,&deadline) == NT_RESULT_OK && deadline.at == 13);
    CHECK(nt_particle_deposits_piles(deposits,12,piles,2,&count) == NT_RESULT_OK && count == 0);
    CHECK(nt_particle_deposits_piles(deposits,11,piles,2,&count) == NT_RESULT_OK && count == 2 && piles[0].mass == 3);

    NtParticleRelease releases[6] = {{.deposit_id=77}};
    CHECK(nt_particle_deposits_releases(deposits,12,releases,1,&count) == NT_RESULT_INSUFFICIENT_BUFFER && count == 6);
    CHECK(releases[0].deposit_id == 77);
    CHECK(nt_particle_deposits_releases(deposits,11,releases,6,&count) == NT_RESULT_OK && count == 2);
    CHECK(releases[1].deposit_id == 0 && releases[1].particle.id == 1 && releases[1].particle.born_us == 11);
    CHECK(releases[1].particle.position.x == 2.5 && releases[1].particle.position.y == 3.5);
    CHECK(releases[1].particle.velocity.x == 2 && releases[1].particle.velocity.y == -3);
    CHECK(nt_particle_deposits_releases(deposits,32,NULL,0,&count) == NT_RESULT_OK && count == 0);
    CHECK(nt_particle_deposits_add(deposits,(NtParticleContact){{3,4},30,1,{1,0,0,255}},&id) == NT_RESULT_OUT_OF_RANGE && id == 2);
    CHECK(nt_particle_deposits_add(deposits,(NtParticleContact){{3,4},31,1,{1,0,0,255}},&id) == NT_RESULT_OK && id == 3);
    CHECK(nt_particle_deposits_advance(deposits,32) == NT_RESULT_OK);
    CHECK(nt_particle_deposits_piles(deposits,31,piles,2,&count) == NT_RESULT_INVALID_ARGUMENT && count == 0);
    CHECK(nt_particle_deposits_releases(deposits,60,NULL,0,&count) == NT_RESULT_OK && count == 2);
    CHECK(nt_particle_deposits_advance(deposits,61) == NT_RESULT_OK);
    CHECK(nt_particle_deposits_deadline(deposits,61,NT_MOTION_FULL,0,&deadline) == NT_RESULT_OK && !deadline.scheduled);
    nt_particle_deposits_destroy(deposits);
    CHECK(budget.live == 0 && budget.calls == 2);
    return 0;
}

static int capacity_and_clock(void)
{
    NtParticleDepositsOptions options = options_default();
    options.capacity = NT_PARTICLE_MAX_DEPOSITS;
    options.release.count = NT_PARTICLE_MAX_RELEASE;
    NtParticleDeposits *deposits;
    CHECK(nt_particle_deposits_create(NULL,&options,&deposits) == NT_RESULT_OK);
    const NtParticleContact contact = {{INT32_MIN,INT32_MAX},0,UINT32_MAX,{255,128,64,255}};
    for (uint64_t i = 0; i < NT_PARTICLE_MAX_DEPOSITS; ++i) {
        uint64_t id;
        CHECK(nt_particle_deposits_add(deposits,contact,&id) == NT_RESULT_OK && id == i);
    }
    CHECK(nt_particle_deposits_add(deposits,contact,NULL) == NT_RESULT_OUT_OF_RANGE);
    NtParticlePile pile;
    size_t count;
    CHECK(nt_particle_deposits_piles(deposits,9,&pile,1,&count) == NT_RESULT_OK && count == 1);
    CHECK(pile.contacts == NT_PARTICLE_MAX_DEPOSITS && pile.mass == (uint64_t)NT_PARTICLE_MAX_DEPOSITS*UINT32_MAX);
    CHECK(pile.color.red == 255 && pile.color.green == 128 && pile.color.blue == 64);
    CHECK(nt_particle_deposits_releases(deposits,10,NULL,0,&count) == NT_RESULT_OK &&
          count == NT_PARTICLE_MAX_DEPOSITS*NT_PARTICLE_MAX_RELEASE);
    CHECK(nt_particle_deposits_advance(deposits,30) == NT_RESULT_OK);
    CHECK(nt_particle_deposits_releases(deposits,30,NULL,0,&count) == NT_RESULT_OK && count == 0);
    nt_particle_deposits_destroy(deposits);

    options.release.count = 1;
    CHECK(nt_particle_deposits_create(NULL,&options,&deposits) == NT_RESULT_OK);
    CHECK(nt_particle_deposits_add(deposits,(NtParticleContact){{1,1},UINT64_MAX-10,1,{1,2,3,255}},NULL) == NT_RESULT_OK);
    CHECK(nt_particle_deposits_add(deposits,(NtParticleContact){{1,1},UINT64_MAX-9,3,{1,2,3,255}},NULL) == NT_RESULT_OK);
    NtDeadline next;
    CHECK(nt_particle_deposits_deadline(deposits,UINT64_MAX-1,NT_MOTION_REDUCED,1,&next) == NT_RESULT_OK &&
          next.scheduled && next.at == UINT64_MAX);
    CHECK(nt_particle_deposits_deadline(deposits,UINT64_MAX,NT_MOTION_FULL,1,&next) == NT_RESULT_OK && !next.scheduled);
    NtParticleRelease birth;
    CHECK(nt_particle_deposits_releases(deposits,UINT64_MAX,&birth,1,&count) == NT_RESULT_OK && count == 1);
    CHECK(birth.particle.born_us == UINT64_MAX && birth.particle.position.x == 1.5);
    CHECK(nt_particle_deposits_piles(deposits,UINT64_MAX,&pile,1,&count) == NT_RESULT_OK && count == 1 && pile.mass == 3);
    nt_particle_deposits_destroy(deposits);
    return 0;
}

static int cadence_and_range(void)
{
    NtParticleDepositsOptions options = options_default();
    options.release.velocity = (NtParticleRange){{-3,2},{5,9}};
    options.release.lifetime_min_us = 10;
    NtParticleDeposits *coarse, *fine;
    CHECK(nt_particle_deposits_create(NULL,&options,&coarse) == NT_RESULT_OK);
    CHECK(nt_particle_deposits_create(NULL,&options,&fine) == NT_RESULT_OK);
    for (NtTimeUs t = 0; t < 160; t += 7) {
        const NtParticleContact contact = {{1,2},t,1,{255,255,255,255}};
        uint64_t a = 777, b = 777;
        const NtResult first = nt_particle_deposits_add(coarse,contact,&a);
        const NtResult second = nt_particle_deposits_add(fine,contact,&b);
        CHECK(first == second && a == b);
        for (NtTimeUs tick = t; tick < t+6; ++tick) CHECK(nt_particle_deposits_advance(fine,tick) == NT_RESULT_OK);
        NtParticleRelease x[6], y[6];
        size_t nx, ny;
        CHECK(nt_particle_deposits_releases(coarse,t+6,x,6,&nx) == NT_RESULT_OK);
        CHECK(nt_particle_deposits_releases(fine,t+6,y,6,&ny) == NT_RESULT_OK && nx == ny);
        for (size_t i = 0; i < nx; ++i) {
            CHECK(x[i].deposit_id == y[i].deposit_id && x[i].particle.id == y[i].particle.id);
            CHECK(x[i].particle.lifetime_us == y[i].particle.lifetime_us);
            CHECK(x[i].particle.position.x == y[i].particle.position.x && x[i].particle.position.y == y[i].particle.position.y);
        }
    }
    nt_particle_deposits_destroy(fine);
    nt_particle_deposits_destroy(coarse);

    options.release.lifetime_min_us = options.release.lifetime_max_us = 3000000;
    options.release.velocity = (NtParticleRange){{DBL_MAX,0},{DBL_MAX,0}};
    CHECK(nt_particle_deposits_create(NULL,&options,&coarse) == NT_RESULT_OK);
    CHECK(nt_particle_deposits_add(coarse,(NtParticleContact){{0,0},0,1,{0,0,0,255}},NULL) == NT_RESULT_OK);
    NtParticleRelease sentinel = {.deposit_id=123};
    size_t count = 321;
    CHECK(nt_particle_deposits_releases(coarse,2000010,&sentinel,1,&count) == NT_RESULT_OUT_OF_RANGE);
    CHECK(count == 321 && sentinel.deposit_id == 123);
    CHECK(nt_particle_deposits_advance(coarse,3000010) == NT_RESULT_OK);
    CHECK(nt_particle_deposits_releases(coarse,3000010,NULL,0,&count) == NT_RESULT_OK && count == 0);
    nt_particle_deposits_destroy(coarse);
    return 0;
}

typedef struct PaintHeap { size_t calls, live, reject; } PaintHeap;
typedef struct DepositLeaf {
    NtParticleDeposits *store;
    NtTimeUs now;
    unsigned *paints;
    unsigned disposed;
} DepositLeaf;

static void *paint_allocate(void *context, size_t bytes, size_t alignment)
{
    (void)alignment;
    PaintHeap *heap = context;
    if (++heap->calls == heap->reject) return NULL;
    void *memory = malloc(bytes);
    if (memory) ++heap->live;
    return memory;
}

static void paint_release(void *context, void *address, size_t bytes, size_t alignment)
{
    (void)bytes;
    (void)alignment;
    --((PaintHeap *)context)->live;
    free(address);
}

static NtResult measure(const void *context, NtMeasureContext *measure, NtConstraints limits, NtCellSize *out)
{
    (void)context;
    (void)measure;
    (void)limits;
    *out = (NtCellSize){4,3};
    return NT_RESULT_OK;
}

static NtResult layout(const void *context, NtLayoutContext *layout, NtCellSize size)
{
    (void)context;
    (void)layout;
    (void)size;
    return NT_RESULT_OK;
}

static NtResult update(void *context, NtUpdateInfo info, NtUpdateReply *reply)
{
    DepositLeaf *leaf = context;
    leaf->now = info.now;
    reply->invalidate = NT_INVALIDATE_PAINT;
    const NtResult result = nt_particle_deposits_advance(leaf->store,info.now);
    return result == NT_RESULT_OK ? nt_particle_deposits_deadline(leaf->store,info.now,info.motion,1,&reply->next) : result;
}

static NtResult draw(const void *context, NtPaintContext *paint, NtCellSize size)
{
    (void)size;
    const DepositLeaf *leaf = context;
    ++*leaf->paints;
    NtParticleDepositPaint options = nt_particle_deposit_paint_default();
    options.pile_glyph = (NtByteView){"#",1};
    options.release.glyph = (NtByteView){"+",1};
    return nt_particle_deposits_draw(leaf->store,paint,leaf->now,NT_MOTION_FULL,&options);
}

static void dispose(void *context)
{
    DepositLeaf *leaf = context;
    nt_particle_deposits_destroy(leaf->store);
    leaf->store = NULL;
    ++leaf->disposed;
}

static int publication_and_removal(void)
{
    PaintHeap heap = {0};
    const NtAllocator allocator = {&heap,paint_allocate,paint_release};
    const NtParticleDepositsOptions options = options_default();
    unsigned paints = 0;
    DepositLeaf leaf = {.paints=&paints};
    CHECK(nt_particle_deposits_create(&allocator,&options,&leaf.store) == NT_RESULT_OK);
    CHECK(nt_particle_deposits_add(leaf.store,(NtParticleContact){{1,1},0,1,{255,64,0,255}},NULL) == NT_RESULT_OK);
    const NtComponentOps ops = {.measure=measure,.layout=layout,.update=update,.paint=draw,.dispose=dispose};
    NtTree *tree;
    NtNodeId mount;
    CHECK(nt_tree_create(&allocator,&tree) == NT_RESULT_OK);
    CHECK(nt_tree_mount(tree,(NtNodeId){0},&ops,&leaf,(NtMountOptions){.enabled=true},&mount) == NT_RESULT_OK);
    CHECK(nt_tree_tick(tree,0,NT_MOTION_FULL) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){4,3}) == NT_RESULT_OK);
    const NtFrame *prior = nt_tree_frame(tree);
    CHECK(nt_tree_tick(tree,10,NT_MOTION_FULL) == NT_RESULT_OK);

    bool shown = false, refused_paint = false;
    for (size_t offset = 1; offset <= 32; ++offset) {
        const unsigned attempts = paints;
        heap.reject = heap.calls+offset;
        const NtResult result = nt_tree_publish(tree,(NtCellSize){4,3});
        if (result == NT_RESULT_OK) { shown = true; break; }
        CHECK(result == NT_RESULT_ALLOCATION_FAILED && nt_tree_frame(tree) == prior);
        if (paints > attempts) refused_paint = true;
        NtCellView cell;
        CHECK(nt_frame_cell(prior,(NtCellPoint){1,1},&cell) == NT_RESULT_OK);
        CHECK(cell.glyph.count == 1 && *cell.glyph.data == '#' && cell.owner == nt_node_owner(mount));
    }
    CHECK(shown && refused_paint);
    heap.reject = 0;
    NtCellView cell;
    CHECK(nt_frame_cell(nt_tree_frame(tree),(NtCellPoint){1,1},&cell) == NT_RESULT_OK);
    CHECK(cell.glyph.count == 1 && *cell.glyph.data == '+' && cell.owner == nt_node_owner(mount));
    CHECK(nt_tree_next_update(tree).scheduled);
    CHECK(nt_tree_remove(tree,mount) == NT_RESULT_OK && !nt_tree_next_update(tree).scheduled);
    CHECK(leaf.disposed == 1 && !leaf.store);
    nt_tree_destroy(tree);
    CHECK(heap.live == 0);
    return 0;
}

int main(void)
{
    return aggregate_and_release() || capacity_and_clock() || cadence_and_range() || publication_and_removal();
}
