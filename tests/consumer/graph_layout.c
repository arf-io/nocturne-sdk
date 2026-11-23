#include <nocturne/graph.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "graph layout line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)

typedef struct Allocation { size_t calls, live, refuse; } Allocation;
static void *allocate(void *context, size_t bytes, size_t alignment)
{
    Allocation *a = context;
    (void)alignment;
    if (++a->calls == a->refuse) return NULL;
    void *pointer = malloc(bytes);
    if (pointer) ++a->live;
    return pointer;
}

static void release(void *context, void *pointer, size_t bytes, size_t alignment)
{
    Allocation *a = context;
    (void)bytes;
    (void)alignment;
    --a->live;
    free(pointer);
}

int main(void)
{
    const NtGraphNode nodes[] = {{.id=92}, {.id=17}, {.id=41},
        {.id=100, .pinned=true, .position={.25,.75}}};
    const NtGraphNode reordered[] = {nodes[3], nodes[0], nodes[2], nodes[1]};
    const NtGraphEdge edges[] = {{643,41,92,true}, {23,17,41,true},
        {15,17,100,false}, {34,100,100,true}};
    const NtGraphEdge swapped[] = {edges[2], edges[1], edges[3], edges[0]};
    NtGraphData data = {nodes, 4, edges, 4};
    const NtGraphData permutation = {reordered, 4, swapped, 4};
    NtGraphLayout *layout = NULL, *other = NULL;
    for (NtGraphLayoutKind kind = NT_GRAPH_LAYERED; kind <= NT_GRAPH_FORCE; ++kind) {
        const NtGraphLayoutOptions options = {kind, 12};
        CHECK(nt_graph_layout_create(NULL, data, options, &layout) == NT_RESULT_OK);
        CHECK(nt_graph_layout_create(NULL, permutation, options, &other) == NT_RESULT_OK);
        size_t count = 0, other_count = 0;
        const NtGraphPlacement *positions = nt_graph_layout_nodes(layout, &count);
        const NtGraphPlacement *other_positions = nt_graph_layout_nodes(other, &other_count);
        CHECK(count == 4 && other_count == count);
        CHECK(positions[0].id == 17 && positions[1].id == 41 && positions[2].id == 92);
        CHECK(positions[3].id == 100 && positions[3].pinned);
        CHECK(positions[3].position.x == .25 && positions[3].position.y == .75);
        for (size_t i = 0; i < count; ++i) {
            const NtPoint point = positions[i].position;
            CHECK(isfinite(point.x) && point.x >= 0 && point.x <= 1);
            CHECK(isfinite(point.y) && point.y >= 0 && point.y <= 1);
            CHECK(other_positions[i].id == positions[i].id);
            CHECK(other_positions[i].position.x == point.x && other_positions[i].position.y == point.y);
        }
        if (kind == NT_GRAPH_LAYERED)
            CHECK(positions[0].position.x < positions[1].position.x &&
                  positions[1].position.x < positions[2].position.x);
        const NtGraphEdge *published = nt_graph_layout_edges(layout, &count);
        CHECK(count == 4 && published[0].id == 15 && !published[0].directed);
        CHECK(published[2].id == 34 && published[2].source == published[2].target);
        CHECK(published[3].source == 41 && published[3].target == 92 && published[3].directed);
        nt_graph_layout_destroy(layout);
        nt_graph_layout_destroy(other);
    }

    const NtGraphLayoutOptions options = {NT_GRAPH_LAYERED, 0};
    CHECK(nt_graph_layout_create(NULL, (NtGraphData){0}, options, &layout) == NT_RESULT_OK);
    size_t count = 1;
    CHECK(!nt_graph_layout_nodes(layout, &count) && count == 0);
    const NtGraphEdge dangling = {1,17,500,true};
    NtGraphLayout *canary = layout;
    data.edges = &dangling;
    data.edge_count = 1;
    CHECK(nt_graph_layout_create(NULL, data, options, &layout) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(layout == canary);
    data.edges = edges;
    data.edge_count = 4;
    data.node_count = NT_GRAPH_MAX_NODES + 1;
    CHECK(nt_graph_layout_create(NULL, data, options, &layout) == NT_RESULT_OUT_OF_RANGE);
    CHECK(layout == canary);
    data.node_count = 4;

    const NtGraphEdge cycle[] = {{1,17,41,true},{2,41,17,true},{3,17,41,true},
                                 {4,41,92,true}};
    data.edges = cycle;
    CHECK(nt_graph_layout_create(NULL,data,options,&other) == NT_RESULT_OK);
    const NtGraphPlacement *ordered = nt_graph_layout_nodes(other,&count);
    CHECK(count == 4 && ordered[0].position.x < ordered[1].position.x &&
          ordered[1].position.x < ordered[2].position.x);
    nt_graph_layout_destroy(other);
    const NtGraphNode duplicate[] = {{.id=17},{.id=17}};
    CHECK(nt_graph_layout_create(NULL,(NtGraphData){.nodes=duplicate,.node_count=2},
                                 options,&layout) == NT_RESULT_INVALID_ARGUMENT);
    const NtGraphNode invalid_pin = {.id=17,.pinned=true,.position={NAN,0}};
    CHECK(nt_graph_layout_create(NULL,(NtGraphData){.nodes=&invalid_pin,.node_count=1},
                                 options,&layout) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(nt_graph_layout_create(NULL,data,(NtGraphLayoutOptions){NT_GRAPH_FORCE,NT_GRAPH_MAX_ITERATIONS+1},
                                 &layout) == NT_RESULT_OUT_OF_RANGE);
    CHECK(layout == canary);

    Allocation observed = {0};
    NtAllocator allocator = {&observed, allocate, release};
    NtGraphLayout *candidate = NULL;
    CHECK(nt_graph_layout_create(&allocator, data, options, &candidate) == NT_RESULT_OK);
    const size_t allocations = observed.calls;
    nt_graph_layout_destroy(candidate);
    CHECK(observed.live == 0 && allocations > 1);
    for (size_t site = 1; site <= allocations; ++site) {
        observed = (Allocation){.refuse=site};
        CHECK(nt_graph_layout_create(&allocator, data, options, &layout) == NT_RESULT_ALLOCATION_FAILED);
        CHECK(layout == canary && observed.live == 0);
    }
    nt_graph_layout_destroy(layout);
    return 0;
}
