#include <nocturne/graph.h>

#include <stdio.h>
#include <stdlib.h>

#define VIEW(s) (NtByteView){s, sizeof(s)-1}
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "graph view line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)

static bool hit_is(NtTree *tree, NtNodeId graph, NtCellPoint point, NtGraphHitKind kind, NtGraphId id)
{
    NtGraphHit hit;
    return nt_graph_view_hit(tree, graph, point, &hit) == NT_RESULT_OK &&
           hit.kind == kind && hit.id == id;
}

typedef struct Heap { size_t calls, live, reject; } Heap;

static void *allocate(void *context, size_t bytes, size_t alignment)
{
    Heap *heap = context;
    (void)alignment;
    if (++heap->calls == heap->reject) return NULL;
    void *memory = malloc(bytes);
    if (memory) ++heap->live;
    return memory;
}

static void release(void *context, void *pointer, size_t bytes, size_t alignment)
{
    Heap *heap = context;
    (void)bytes;
    (void)alignment;
    --heap->live;
    free(pointer);
}

static int loop_direction(void)
{
    NtTree *tree;
    NtNodeId graph;
    CHECK(nt_tree_create(NULL,&tree) == NT_RESULT_OK);
    const NtGraphNode root = {.id=1,.pinned=true,.position={.5,.5}};
    NtGraphEdge edge = {98,1,1,true};
    NtGraphViewOptions view = nt_graph_view_options_default();
    view.data = (NtGraphData){&root,1,&edge,1};
    CHECK(nt_graph_view_mount(tree,(NtNodeId){0},(NtMountOptions){true,false,true},&view,&graph) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){7,7}) == NT_RESULT_OK);
    const NtCellPoint preceding = {3,2};
    NtCellView cell;
    CHECK(hit_is(tree,graph,preceding,NT_GRAPH_HIT_EDGE,98));
    CHECK(nt_frame_cell(nt_tree_frame(tree),preceding,&cell) == NT_RESULT_OK);
    CHECK(cell.glyph.count == 1 && cell.glyph.data[0] == 'v');
    CHECK(nt_tree_publish(tree,(NtCellSize){7,1}) == NT_RESULT_OK);
    const NtCellPoint horizontal = {2,0};
    CHECK(hit_is(tree,graph,horizontal,NT_GRAPH_HIT_EDGE,98));
    CHECK(nt_frame_cell(nt_tree_frame(tree),horizontal,&cell) == NT_RESULT_OK);
    CHECK(cell.glyph.count == 1 && cell.glyph.data[0] == '>');
    edge.directed = false;
    CHECK(nt_graph_view_set(tree,graph,&view) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){7,7}) == NT_RESULT_OK);
    CHECK(hit_is(tree,graph,preceding,NT_GRAPH_HIT_EDGE,98));
    CHECK(nt_frame_cell(nt_tree_frame(tree),preceding,&cell) == NT_RESULT_OK);
    CHECK(cell.glyph.count == 1 && cell.glyph.data[0] == '|');
    nt_tree_destroy(tree);
    return 0;
}

int main(void)
{
    CHECK(loop_direction() == 0);
    NtTree *tree;
    NtNodeId graph;
    Heap heap = {0};
    const NtAllocator allocator = {&heap,allocate,release};
    CHECK(nt_tree_create(&allocator, &tree) == NT_RESULT_OK);
    NtGraphViewOptions options = nt_graph_view_options_default();
    options.layout.kind = NT_GRAPH_LAYERED;
    NtGraphNode nodes[] = {{.id=17,.pinned=true,.position={.2,.5}},
                           {.id=98,.pinned=true,.position={.8,.5}}};
    NtGraphEdge edges[] = {{.id=31,.source=17,.target=98,.directed=true}};
    const NtGraphLabel labels[] = {{17, VIEW("café")}, {98, VIEW("水")}};
    options.data = (NtGraphData){nodes, 2, edges, 1};
    options.labels = labels;
    options.label_count = 2;
    CHECK(nt_graph_view_mount(tree, (NtNodeId){0}, (NtMountOptions){true,true,true},
                              &options, &graph) == NT_RESULT_OK);
    CHECK(hit_is(tree, graph, (NtCellPoint){4,2}, NT_GRAPH_HIT_NONE, 0));
    CHECK(nt_tree_publish(tree, (NtCellSize){21,5}) == NT_RESULT_OK);
    CHECK(hit_is(tree, graph, (NtCellPoint){4,2}, NT_GRAPH_HIT_NODE, 17));
    CHECK(hit_is(tree, graph, (NtCellPoint){16,2}, NT_GRAPH_HIT_NODE, 98));
    CHECK(hit_is(tree, graph, (NtCellPoint){6,2}, NT_GRAPH_HIT_NODE, 17));
    CHECK(hit_is(tree, graph, (NtCellPoint){18,2}, NT_GRAPH_HIT_NODE, 98));
    CHECK(hit_is(tree, graph, (NtCellPoint){19,2}, NT_GRAPH_HIT_NODE, 98));
    CHECK(hit_is(tree, graph, (NtCellPoint){12,2}, NT_GRAPH_HIT_EDGE, 31));
    CHECK(hit_is(tree, graph, (NtCellPoint){12,3}, NT_GRAPH_HIT_NONE, 0));
    NtCellView cell;
    CHECK(nt_frame_cell(nt_tree_frame(tree),(NtCellPoint){15,2},&cell) == NT_RESULT_OK);
    CHECK(cell.glyph.count == 1 && cell.glyph.data[0] == '>');

    const NtGraphLabel invalid_label = {17,(NtByteView){"\xff",1}};
    NtGraphViewOptions invalid = options;
    invalid.labels = &invalid_label;
    invalid.label_count = 1;
    const NtFrame *frame = nt_tree_frame(tree);
    const size_t old_live = heap.live;
    CHECK(nt_graph_view_set(tree,graph,&invalid) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(nt_tree_frame(tree) == frame && heap.live == old_live);
    heap.reject = heap.calls + 4;
    CHECK(nt_graph_view_set(tree,graph,&options) == NT_RESULT_ALLOCATION_FAILED);
    CHECK(nt_tree_frame(tree) == frame && heap.live == old_live);
    heap.reject = heap.calls + 4;
    CHECK(nt_tree_publish(tree,(NtCellSize){11,3}) == NT_RESULT_ALLOCATION_FAILED);
    CHECK(nt_tree_frame(tree) == frame && heap.live == old_live);
    CHECK(hit_is(tree, graph, (NtCellPoint){12,2}, NT_GRAPH_HIT_EDGE, 31));
    heap.reject = 0;

    NtDispatchResult dispatch;
    NtEvent event = {.kind=NT_EVENT_POINTER_DOWN, .at=1,
        .data.pointer={1,{12,2},NT_BUTTON_PRIMARY,NT_BUTTON_PRIMARY,true,0}};
    CHECK(nt_tree_dispatch(tree, &event,(NtByteView){0},NULL,&dispatch) == NT_RESULT_OK);
    CHECK(dispatch.consumed);
    NtGraphHit selection;
    CHECK(nt_graph_view_selection(tree, graph, &selection) == NT_RESULT_OK);
    CHECK(selection.kind == NT_GRAPH_HIT_EDGE && selection.id == 31);
    NtEventIntentView intent;
    CHECK(nt_tree_peek_event_intent(tree, &intent));
    CHECK(nt_graph_event_decode(intent, &selection) == NT_RESULT_OK && selection.id == 31);
    nt_tree_pop_intent(tree);

    const NtGraphNode reversed[] = {nodes[1], nodes[0]};
    options.data.nodes = reversed;
    CHECK(nt_graph_view_set(tree, graph, &options) == NT_RESULT_OK);
    CHECK(nt_graph_view_selection(tree, graph, &selection) == NT_RESULT_OK && selection.id == 31);
    CHECK(nt_tree_publish(tree, (NtCellSize){11,3}) == NT_RESULT_OK);
    CHECK(hit_is(tree, graph, (NtCellPoint){2,1}, NT_GRAPH_HIT_NODE, 17));
    CHECK(hit_is(tree, graph, (NtCellPoint){8,1}, NT_GRAPH_HIT_NODE, 98));
    CHECK(!nt_tree_peek_event_intent(tree, &intent));

    CHECK(nt_tree_publish(tree,(NtCellSize){21,5}) == NT_RESULT_OK);
    options.data.edge_count = 0;
    CHECK(nt_graph_view_set(tree, graph, &options) == NT_RESULT_OK);
    CHECK(nt_graph_view_selection(tree, graph, &selection) == NT_RESULT_OK && !selection.id);
    CHECK(hit_is(tree, graph, (NtCellPoint){12,2}, NT_GRAPH_HIT_EDGE, 31));
    CHECK(nt_tree_set_focus(tree,(NtNodeId){0}) == NT_RESULT_OK);
    event.at = 2;
    event.data.pointer.id = 2;
    CHECK(nt_tree_dispatch(tree,&event,(NtByteView){0},NULL,&dispatch) == NT_RESULT_OK);
    CHECK(!dispatch.consumed && !nt_tree_focus(tree).tree);
    CHECK(!nt_tree_peek_event_intent(tree,&intent));
    CHECK(nt_tree_publish(tree, (NtCellSize){21,5}) == NT_RESULT_OK);
    CHECK(hit_is(tree, graph, (NtCellPoint){12,2}, NT_GRAPH_HIT_NONE, 0));
    CHECK(nt_tree_remove(tree, graph) == NT_RESULT_OK);
    CHECK(nt_graph_view_selection(tree, graph, &selection) == NT_RESULT_STALE_REFERENCE);
    nt_tree_destroy(tree);
    CHECK(heap.live == 0);
    return 0;
}
