#include <nocturne/component.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef NOCTURNE_TEST_PTHREAD
#include <pthread.h>
#endif

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #condition); exit(EXIT_FAILURE); \
} } while (0)
#define ID_EMPTY ((NtNodeId){0})
#define SIZE(w,h) ((NtCellSize){w,h})
#define POINT(x,y) ((NtCellPoint){x,y})
#define RECT(x,y,w,h) ((NtCellRect){{x,y},{w,h}})
#define COUNT(a) (sizeof(a)/sizeof(*(a)))

static const NtMountOptions enabled = {true,true,true};
static const NtCellStyle ink = {{255,255,255,255},{16,24,32,255}};

typedef struct Heap {
    size_t calls, fail_at, live, bytes;
} Heap;

static void *allocate(void *context, size_t bytes, size_t alignment)
{
    Heap *heap = context;
    CHECK(bytes && alignment);
    if (++heap->calls == heap->fail_at) return NULL;
    void *memory = malloc(bytes);
    if (memory) {
        CHECK((uintptr_t)memory % alignment == 0);
        ++heap->live;
        heap->bytes += bytes;
    }
    return memory;
}

static void release(void *context, void *address, size_t bytes, size_t alignment)
{
    Heap *heap = context;
    CHECK(address && bytes && alignment && heap->live && heap->bytes >= bytes);
    --heap->live;
    heap->bytes -= bytes;
    free(address);
}

typedef struct Log {
    int values[32];
    size_t count;
} Log;

typedef struct Node {
    NtTree *tree;
    NtNodeId id;
    NtCellRect placement;
    const NtRowSpan *spans;
    size_t span_count;
    NtByteView glyph;
    NtCellPoint glyph_at;
    NtCellRect observed_clip;
    NtTimeUs elapsed, next;
    unsigned measurements, layouts, paints, updates, gains, losses, disposals;
    int32_t z;
    int tag;
    Log *log;
    bool omit, mask, bad_limits, fail_paint, fail_update, reenter, emit;
    uint8_t columns;
} Node;

static bool same(NtNodeId a, NtNodeId b)
{
    return a.tree == b.tree && a.slot == b.slot && a.generation == b.generation;
}

static void record(Node *node, int value)
{
    if (!node->log) return;
    CHECK(node->log->count < COUNT(node->log->values));
    node->log->values[node->log->count++] = value;
}

static Node *lookup(const void *state, NtNodeId id);
static Node *registry[32];

static Node *lookup(const void *state, NtNodeId id)
{
    (void)state;
    for (size_t i = 0; i < COUNT(registry); ++i)
        if (registry[i] && same(registry[i]->id, id)) return registry[i];
    CHECK(false);
    return NULL;
}

static NtResult measure(const void *state, NtMeasureContext *context,
                        NtConstraints limits, NtCellSize *out)
{
    Node *node = (Node *)state;
    ++node->measurements;
    for (size_t i = 0; i < nt_measure_child_count(context); ++i) {
        NtNodeId child;
        NtResult result = nt_measure_child_at(context, i, &child);
        if (result != NT_RESULT_OK) return result;
        NtCellSize size = SIZE(100,200);
        if (node->bad_limits) {
            CHECK(nt_measure_child(context, child, (NtConstraints){{2,1,true},{0,0,false}}, &size)
                  == NT_RESULT_INVALID_ARGUMENT);
            CHECK(size.columns == 100 && size.rows == 200);
            return NT_RESULT_INVALID_ARGUMENT;
        }
        result = nt_measure_child(context, child, limits, &size);
        if (result != NT_RESULT_OK) return result;
        CHECK(size.columns == limits.columns.min && size.rows == limits.rows.min);
    }
    *out = SIZE(limits.columns.min,limits.rows.min);
    return NT_RESULT_OK;
}

static NtResult layout(const void *state, NtLayoutContext *context, NtCellSize size)
{
    Node *node = (Node *)state;
    (void)size;
    ++node->layouts;
    for (size_t i = 0; i < nt_layout_child_count(context); ++i) {
        NtNodeId child;
        NtResult result = nt_layout_child_at(context, i, &child);
        if (result != NT_RESULT_OK) return result;
        Node *config = lookup(state, child);
        NtCellSize preferred;
        CHECK(nt_layout_child_size(context, child, &preferred) == NT_RESULT_OK);
        if (config->omit) continue;
        const NtClipMask mask = {config->spans, config->span_count};
        result = nt_layout_place(context, child, config->placement, config->z,
                                 config->mask ? &mask : NULL);
        if (result != NT_RESULT_OK) return result;
        CHECK(nt_layout_place(context, child, config->placement, 0, NULL) == NT_RESULT_WRONG_PHASE);
    }
    return NT_RESULT_OK;
}

static NtResult paint(const void *state, NtPaintContext *context, NtCellSize size)
{
    Node *node = (Node *)state;
    (void)size;
    ++node->paints;
    node->observed_clip = nt_paint_clip(context);
    if (node->reenter) {
        CHECK(nt_tree_remove(node->tree, node->id) == NT_RESULT_WRONG_PHASE);
        CHECK(nt_tree_publish(node->tree, size) == NT_RESULT_WRONG_PHASE);
        CHECK(nt_tree_tick(node->tree, 1000000, NT_MOTION_REDUCED) == NT_RESULT_WRONG_PHASE);
        nt_tree_destroy(node->tree);
    }
    if (node->glyph.count) {
        const NtResult result = nt_paint_glyph(context, node->glyph_at, node->glyph, node->columns, ink);
        if (result != NT_RESULT_OK) return result;
    }
    return node->fail_paint ? NT_RESULT_INVALID_ARGUMENT : NT_RESULT_OK;
}

static NtResult update(void *state, NtUpdateInfo info, NtUpdateReply *out)
{
    Node *node = state;
    CHECK(info.motion == NT_MOTION_FULL || info.motion == NT_MOTION_REDUCED);
    ++node->updates;
    node->elapsed = info.elapsed;
    record(node, node->tag);
    if (node->fail_update) return NT_RESULT_ALLOCATION_FAILED;
    *out = (NtUpdateReply){NT_INVALIDATE_PAINT,{node->next != 0,node->next}};
    return NT_RESULT_OK;
}

static NtResult event(void *state, NtEventContext *context, const NtEvent *event, NtEventReply *reply)
{
    Node *node = state;
    CHECK(nt_event_phase(context) == NT_EVENT_TARGET);
    NtCellPoint position = POINT(13,17);
    CHECK(!nt_event_local_position(context, &position) && position.x == 13 && position.y == 17);
    if (event->kind == NT_EVENT_FOCUS_GAIN) {
        CHECK(same(nt_tree_focus(node->tree), node->id));
        ++node->gains;
    } else {
        CHECK(event->kind == NT_EVENT_FOCUS_LOSS && !same(nt_tree_focus(node->tree), node->id));
        ++node->losses;
        record(node, -node->tag);
    }
    CHECK(nt_tree_set_focus(node->tree, ID_EMPTY) == NT_RESULT_WRONG_PHASE);
    reply->invalidate = NT_INVALIDATE_PAINT;
    if (node->emit) {
        char data[] = {'a','\0','z'};
        CHECK(nt_event_emit(context, 42, (NtByteView){data,sizeof(data)}) == NT_RESULT_OK);
        data[0] = '_';
        return nt_event_emit(context, 99, (NtByteView){data,sizeof(data)});
    }
    return NT_RESULT_OK;
}

static void dispose(void *state)
{
    Node *node = state;
    NtNodeGeometry geometry;
    CHECK(nt_tree_geometry(node->tree, node->id, &geometry) == NT_RESULT_STALE_REFERENCE);
    ++node->disposals;
    record(node, node->tag);
}

static const NtComponentOps ops = {measure,layout,update,paint,event,dispose};

static void mount(NtTree *tree, NtNodeId parent, Node *node, NtMountOptions options)
{
    node->tree = tree;
    CHECK(nt_tree_mount(tree, parent, &ops, node, options, &node->id) == NT_RESULT_OK);
    for (size_t i = 0; i < COUNT(registry); ++i) if (!registry[i]) {
        registry[i] = node;
        return;
    }
    CHECK(false);
}

static void reset_registry(void)
{
    memset(registry, 0, sizeof(registry));
}

static void hit(NtTree *tree, int32_t x, int32_t y, NtNodeId expect)
{
    NtNodeId actual;
    CHECK(nt_tree_hit(tree, POINT(x,y), &actual) == NT_RESULT_OK && same(actual, expect));
}

static NtCellView cell(const NtFrame *frame, int32_t x, int32_t y)
{
    NtCellView cell;
    CHECK(nt_frame_cell(frame, POINT(x,y), &cell) == NT_RESULT_OK);
    return cell;
}

static void partial_tick_keeps_monotonic_watermark(void)
{
    reset_registry();
    NtTree *tree;
    CHECK(nt_tree_create(NULL, &tree) == NT_RESULT_OK);
    Log log = {0};
    Node p = {.log=&log,.tag=1,.next=20000};
    Node c = {.log=&log,.tag=2,.next=20000,.placement=RECT(0,0,1,1)};
    mount(tree, ID_EMPTY, &p, enabled);
    mount(tree, p.id, &c, enabled);
    CHECK(nt_tree_next_update(tree).scheduled && nt_tree_next_update(tree).at == 0);
    CHECK(nt_tree_tick(tree, 10000, NT_MOTION_FULL) == NT_RESULT_OK);
    CHECK(!p.elapsed && !c.elapsed);
    CHECK(nt_tree_publish(tree, SIZE(1,1)) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree, SIZE(1,1)) == NT_RESULT_OK);
    CHECK(p.updates == 1 && c.updates == 1 && p.paints == 2 && c.paints == 2);
    p.next = 30000;
    c.fail_update = true;
    CHECK(nt_tree_tick(tree, 20000, NT_MOTION_FULL) == NT_RESULT_ALLOCATION_FAILED);
    CHECK(log.count == 4 && log.values[2] == 1 && log.values[3] == 2);
    CHECK(nt_tree_tick(tree, 19000, NT_MOTION_FULL) == NT_RESULT_OUT_OF_RANGE);
    CHECK(p.updates == 2 && c.updates == 2 && nt_tree_next_update(tree).at == 20000);
    c.fail_update = false;
    c.next = 40000;
    CHECK(nt_tree_tick(tree, 20000, NT_MOTION_REDUCED) == NT_RESULT_OK);
    CHECK(p.updates == 2 && c.updates == 3 && c.elapsed == 10000);
    CHECK(nt_tree_next_update(tree).at == 30000);
    CHECK(nt_tree_remove(tree, p.id) == NT_RESULT_OK);
    CHECK(log.count == 7 && log.values[5] == 2 && log.values[6] == 1);
    CHECK(p.disposals == 1 && c.disposals == 1 && !nt_tree_next_update(tree).scheduled);
    Node recycled = {0};
    mount(tree, ID_EMPTY, &recycled, enabled);
    CHECK(recycled.id.slot == p.id.slot && recycled.id.generation != p.id.generation);
    CHECK(nt_tree_remove(tree, p.id) == NT_RESULT_STALE_REFERENCE);
    CHECK(nt_tree_tick(tree, 100000, NT_MOTION_FULL) == NT_RESULT_OK);
    CHECK(p.updates == 2 && c.updates == 3 && recycled.updates == 1);
    nt_tree_destroy(tree);
    CHECK(recycled.disposals == 1);
}

static void mount_validation_and_allocation_failure_are_atomic(void)
{
    reset_registry();
    Heap heap = {0};
    NtAllocator allocator = {&heap,allocate,release};
    NtTree *tree;
    CHECK(nt_tree_create(&allocator, &tree) == NT_RESULT_OK);
    Node root = {0};
    const NtNodeId keep = {tree,123,567};
    root.id = keep;
    NtComponentOps invalid = ops;
    invalid.paint = NULL;
    CHECK(nt_tree_mount(tree, ID_EMPTY, &invalid, &root, enabled, &root.id) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(nt_tree_mount(tree, keep, &ops, &root, enabled, &root.id) == NT_RESULT_STALE_REFERENCE);
    heap.fail_at = heap.calls + 1;
    CHECK(nt_tree_mount(tree, ID_EMPTY, &ops, &root, enabled, &root.id) == NT_RESULT_ALLOCATION_FAILED);
    CHECK(same(root.id, keep) && root.disposals == 0 && heap.live == 1 && !nt_tree_frame(tree));
    mount(tree, ID_EMPTY, &root, enabled);
    CHECK(!same(root.id, keep) && heap.live == 2);
    NtTree *other;
    CHECK(nt_tree_create(NULL, &other) == NT_RESULT_OK);
    CHECK(nt_tree_remove(other, root.id) == NT_RESULT_INVALID_ARGUMENT);
    nt_tree_destroy(other);
    nt_tree_destroy(tree);
    CHECK(root.disposals == 1 && !heap.live && !heap.bytes);
}

static void publication_preserves_frame_and_paths(void)
{
    reset_registry();
    Heap heap = {0};
    NtAllocator allocator = {&heap,allocate,release};
    NtTree *tree;
    CHECK(nt_tree_create(&allocator, &tree) == NT_RESULT_OK);
    Log log = {0};
    Node root = {.reenter=true};
    Node a = {.tag=1,.log=&log,.placement=RECT(1,0,4,1)};
    Node b = {.tag=2,.log=&log,.placement=RECT(5,0,3,1)};
    Node c = {.tag=3,.log=&log,.placement=RECT(0,0,3,1),.glyph={"界",3},.columns=2};
    mount(tree, ID_EMPTY, &root, (NtMountOptions){true,false,false});
    mount(tree, root.id, &a, (NtMountOptions){true,false,false});
    mount(tree, root.id, &b, (NtMountOptions){true,false,false});
    mount(tree, a.id, &c, enabled);
    CHECK(nt_tree_publish(tree, SIZE(8,1)) == NT_RESULT_OK);
    CHECK(nt_tree_pending(tree) == NT_INVALIDATE_NONE);
    CHECK(nt_tree_set_focus(tree, c.id) == NT_RESULT_OK && c.gains == 1);
    hit(tree, 2,0,c.id);
    const NtFrame *old_frame = nt_tree_frame(tree);
    const NtCellView old_cell = cell(old_frame,2,0);
    CHECK(old_cell.owner == nt_node_owner(c.id) && old_cell.lead.x == 1);
    CHECK(nt_tree_move(tree, a.id, c.id) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(nt_tree_move(tree, c.id, b.id) == NT_RESULT_OK);
    c.fail_paint = true;
    CHECK(nt_tree_publish(tree, SIZE(6,1)) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(nt_tree_frame(tree) == old_frame && cell(old_frame,2,0).glyph.data == old_cell.glyph.data);
    CHECK(nt_tree_pending(tree) == NT_INVALIDATE_LAYOUT && same(nt_tree_focus(tree), c.id));
    NtNodeGeometry geometry;
    CHECK(nt_tree_geometry(tree, c.id, &geometry) == NT_RESULT_OK && same(geometry.parent, a.id));
    hit(tree,2,0,c.id);
    hit(tree,5,0,ID_EMPTY);
    c.fail_paint = false;
    CHECK(nt_tree_publish(tree, SIZE(6,1)) == NT_RESULT_OK);
    CHECK(nt_tree_geometry(tree, c.id, &geometry) == NT_RESULT_OK && same(geometry.parent, b.id));
    CHECK(geometry.bounds.origin.x == 5 && geometry.clip.size.columns == 1);
    hit(tree,2,0,ID_EMPTY);
    hit(tree,5,0,c.id);
    CHECK(!cell(nt_tree_frame(tree),5,0).glyph_columns && same(nt_tree_focus(tree), c.id));
    root.bad_limits = true;
    const unsigned before = a.measurements;
    old_frame = nt_tree_frame(tree);
    CHECK(nt_tree_publish(tree, SIZE(8,1)) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(a.measurements == before && nt_tree_frame(tree) == old_frame);
    root.bad_limits = false;
    b.placement = RECT(INT32_MAX,0,3,1);
    c.placement.origin.x = 1;
    CHECK(nt_tree_publish(tree, SIZE(8,1)) == NT_RESULT_OUT_OF_RANGE && nt_tree_frame(tree) == old_frame);
    b.placement = RECT(5,0,3,1);
    c.placement.origin.x = 0;
    CHECK(nt_tree_move(tree, c.id, a.id) == NT_RESULT_OK);
    CHECK(nt_tree_remove(tree, b.id) == NT_RESULT_OK);
    CHECK(log.count == 2 && log.values[0] == -3 && log.values[1] == 2);
    CHECK(c.losses == 1 && !nt_tree_focus(tree).tree && !c.disposals);
    CHECK(nt_tree_geometry(tree, c.id, &geometry) == NT_RESULT_OK && !geometry.placed);
    hit(tree,5,0,ID_EMPTY);
    CHECK(nt_tree_publish(tree, SIZE(8,1)) == NT_RESULT_OK);
    hit(tree,1,0,c.id);
    CHECK(nt_tree_set_focus(tree, c.id) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree, SIZE(0,1)) == NT_RESULT_OK && c.losses == 2);
    hit(tree,1,0,ID_EMPTY);
    nt_tree_destroy(tree);
    CHECK(!heap.live && !heap.bytes);
}

static void masked_order_and_allocation_rollback(void)
{
    reset_registry();
    Heap heap = {0};
    NtAllocator allocator = {&heap,allocate,release};
    NtTree *tree;
    CHECK(nt_tree_create(&allocator, &tree) == NT_RESULT_OK);
    NtRowSpan spans[] = {{POINT(1,0),1},{POINT(3,0),2}};
    Node root = {0};
    Node back = {.placement=RECT(1,0,4,1),.glyph={"B",1},.columns=1};
    Node front = {.placement=RECT(0,0,5,1),.z=1,.mask=true,.spans=spans,.span_count=COUNT(spans),
                  .glyph={"界",3},.columns=2,.glyph_at=POINT(3,0)};
    mount(tree, ID_EMPTY, &root, (NtMountOptions){true,false,false});
    mount(tree, root.id, &back, enabled);
    mount(tree, root.id, &front, enabled);
    CHECK(nt_tree_publish(tree, SIZE(5,1)) == NT_RESULT_OK);
    hit(tree,1,0,front.id);
    hit(tree,2,0,back.id);
    hit(tree,4,0,front.id);
    const NtFrame *old = nt_tree_frame(tree);
    const NtCellView borrow = cell(old,4,0);
    CHECK(borrow.owner == nt_node_owner(front.id) && borrow.lead.x == 3);
    CHECK(front.observed_clip.origin.x == 1 && front.observed_clip.size.columns == 4);
    spans[0].columns = 0;
    hit(tree,1,0,front.id);
    CHECK(nt_tree_invalidate(tree, front.id, NT_INVALIDATE_LAYOUT) == NT_RESULT_OK);
    const size_t bytes = heap.bytes, live = heap.live;
    unsigned failures = 0;
    for (unsigned i = 1; i < 16; ++i) {
        heap.fail_at = heap.calls + i;
        const NtResult result = nt_tree_publish(tree, SIZE(5,1));
        if (result == NT_RESULT_OK) break;
        ++failures;
        CHECK(result == NT_RESULT_ALLOCATION_FAILED);
        CHECK(nt_tree_pending(tree) == NT_INVALIDATE_LAYOUT && nt_tree_frame(tree) == old);
        CHECK(cell(old,4,0).glyph.data == borrow.glyph.data && heap.bytes == bytes && heap.live == live);
        hit(tree,1,0,front.id);
    }
    CHECK(failures == 7 && nt_tree_pending(tree) == NT_INVALIDATE_NONE);
    heap.fail_at = 0;
    hit(tree,1,0,back.id);
    front.omit = true;
    CHECK(nt_tree_publish(tree, SIZE(5,1)) == NT_RESULT_OK);
    hit(tree,4,0,back.id);
    nt_tree_destroy(tree);
    CHECK(!heap.bytes && !heap.live);
}

static void focus_intents_copy_and_stage_atomically(void)
{
    reset_registry();
    Heap heap = {0};
    NtAllocator allocator = {&heap,allocate,release};
    NtTree *tree;
    CHECK(nt_tree_create(&allocator, &tree) == NT_RESULT_OK);
    Node root = {.emit=true};
    mount(tree, ID_EMPTY, &root, enabled);
    CHECK(nt_tree_set_focus(tree, root.id) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(nt_tree_publish(tree, SIZE(1,1)) == NT_RESULT_OK);
    heap.fail_at = heap.calls + 2;
    CHECK(nt_tree_set_focus(tree, root.id) == NT_RESULT_OK && root.gains == 1);
    NtIntentView intent;
    CHECK(!nt_tree_peek_intent(tree, &intent) && nt_tree_pending(tree) == NT_INVALIDATE_NONE);
    CHECK(nt_tree_set_focus(tree, ID_EMPTY) == NT_RESULT_OK && root.losses == 1);
    CHECK(nt_tree_peek_intent(tree, &intent) && intent.kind == 42 && same(intent.source, root.id));
    CHECK(intent.payload.count == 3 && !memcmp(intent.payload.data, "a\0z", 3));
    nt_tree_pop_intent(tree);
    CHECK(nt_tree_peek_intent(tree, &intent) && intent.kind == 99 && intent.payload.data[0] == '_');
    nt_tree_pop_intent(tree);
    CHECK(!nt_tree_peek_intent(tree, &intent) && nt_tree_pending(tree) == NT_INVALIDATE_PAINT);
    nt_tree_destroy(tree);
    CHECK(!heap.live && !heap.bytes);
}

static NtResult chain_layout(const void *state, NtLayoutContext *context, NtCellSize size)
{
    (void)state;
    if (!nt_layout_child_count(context)) return NT_RESULT_OK;
    NtNodeId child;
    CHECK(nt_layout_child_at(context, 0, &child) == NT_RESULT_OK);
    return nt_layout_place(context, child, (NtCellRect){{0,0},size}, 0, NULL);
}

static void *rejects_excessive_measure_depth_without_publishing(void *unused)
{
    (void)unused;
    enum { CHILD_DEPTH = 128, MEASURE_COUNT = 1 + CHILD_DEPTH };
    NtTree *tree;
    CHECK(nt_tree_create(NULL, &tree) == NT_RESULT_OK);
    Node state = {0};
    const NtComponentOps chain = {measure,chain_layout,NULL,paint,NULL,NULL};
    NtNodeId root;
    CHECK(nt_tree_mount(tree, ID_EMPTY, &chain, &state, enabled, &root) == NT_RESULT_OK);
    NtNodeId leaf = root;
    for (unsigned i = 0; i < CHILD_DEPTH; ++i)
        CHECK(nt_tree_mount(tree, leaf, &chain, &state, enabled, &leaf) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree, SIZE(1,1)) == NT_RESULT_OK && state.measurements == MEASURE_COUNT);
    NtNodeId deepest;
    CHECK(nt_tree_mount(tree, leaf, &chain, &state, enabled, &deepest) == NT_RESULT_OK);
    const NtFrame *prior = nt_tree_frame(tree);
    CHECK(nt_tree_publish(tree, SIZE(2,1)) == NT_RESULT_OUT_OF_RANGE);
    CHECK(nt_tree_frame(tree) == prior && state.measurements == 2*MEASURE_COUNT);
    CHECK(nt_tree_pending(tree) == NT_INVALIDATE_LAYOUT);
    hit(tree,0,0,leaf);
    CHECK(nt_tree_remove(tree, deepest) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree, SIZE(2,1)) == NT_RESULT_OK);
    CHECK(nt_frame_size(nt_tree_frame(tree)).columns == 2);
    nt_tree_destroy(tree);
    return NULL;
}

int main(void)
{
    partial_tick_keeps_monotonic_watermark();
    mount_validation_and_allocation_failure_are_atomic();
    publication_preserves_frame_and_paths();
    masked_order_and_allocation_rollback();
    focus_intents_copy_and_stage_atomically();
    rejects_excessive_measure_depth_without_publishing(NULL);
#ifdef NOCTURNE_TEST_PTHREAD
    pthread_t worker;
    CHECK(pthread_create(&worker, NULL, rejects_excessive_measure_depth_without_publishing, NULL) == 0);
    CHECK(pthread_join(worker, NULL) == 0);
#endif
    puts("Component lifecycle, clipping, monotonic time, publication and allocation rollback passed.");
    return EXIT_SUCCESS;
}
