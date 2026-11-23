#include "graph_internal.h"

#include <math.h>
#include <string.h>

NT_DECLARE_ALIGNMENT(NtGraphLayout);
NT_DECLARE_ALIGNMENT(NtGraphPlacement);
NT_DECLARE_ALIGNMENT(NtGraphEdge);
NT_DECLARE_ALIGNMENT(GraphArc);

static int order_nodes(const void *a, const void *b)
{
    const NtGraphPlacement *left = a, *right = b;
    return (left->id > right->id) - (left->id < right->id);
}

static int order_edges(const void *a, const void *b)
{
    const NtGraphEdge *left = a, *right = b;
    return (left->id > right->id) - (left->id < right->id);
}

size_t nt_graph_find_node(const NtGraphLayout *layout, NtGraphId id)
{
    size_t low = 0, high = layout->node_count;
    while (low < high) {
        const size_t middle = low + (high - low) / 2;
        if (layout->nodes[middle].id < id) low = middle + 1;
        else high = middle;
    }
    return low < layout->node_count && layout->nodes[low].id == id ? low : SIZE_MAX;
}

size_t nt_graph_find_edge(const NtGraphLayout *layout, NtGraphId id)
{
    size_t low = 0, high = layout->edge_count;
    while (low < high) {
        const size_t middle = low + (high - low) / 2;
        if (layout->edges[middle].id < id) low = middle + 1;
        else high = middle;
    }
    return low < layout->edge_count && layout->edges[low].id == id ? low : SIZE_MAX;
}

static bool unit_coordinate(double value)
{
    return value >= 0 && value <= 1;
}

static NtResult validate_records(NtGraphLayout *layout, NtGraphData data)
{
    for (size_t i = 0; i < data.node_count; ++i) {
        const NtGraphNode node = data.nodes[i];
        if (!node.id || (node.pinned && (!unit_coordinate(node.position.x) ||
                                         !unit_coordinate(node.position.y))))
            return NT_RESULT_INVALID_ARGUMENT;
        layout->nodes[i] = (NtGraphPlacement){node.id, node.pinned,
                                              node.pinned ? node.position : (NtPoint){0}};
    }
    if (data.node_count) qsort(layout->nodes, data.node_count, sizeof(*layout->nodes), order_nodes);
    for (size_t i = 1; i < data.node_count; ++i)
        if (layout->nodes[i - 1].id == layout->nodes[i].id) return NT_RESULT_INVALID_ARGUMENT;

    if (data.edge_count) {
        memcpy(layout->edges, data.edges, data.edge_count * sizeof(*data.edges));
        qsort(layout->edges, data.edge_count, sizeof(*layout->edges), order_edges);
    }
    for (size_t i = 0; i < data.edge_count; ++i) {
        const NtGraphEdge edge = layout->edges[i];
        if (!edge.id || (i && layout->edges[i - 1].id == edge.id))
            return NT_RESULT_INVALID_ARGUMENT;
        const size_t source = nt_graph_find_node(layout, edge.source);
        const size_t target = nt_graph_find_node(layout, edge.target);
        if (source == SIZE_MAX || target == SIZE_MAX) return NT_RESULT_INVALID_ARGUMENT;
        layout->arcs[i] = (GraphArc){source, target};
    }
    return NT_RESULT_OK;
}

/* Keep undirected links out of rank constraints. When a directed cycle
 * has no ready vertex, pick the lowest remaining ID and retain its rank. */
static void layered(NtGraphLayout *layout)
{
    uint16_t incoming[NT_GRAPH_MAX_NODES] = {0}, rank[NT_GRAPH_MAX_NODES] = {0};
    uint16_t rows[NT_GRAPH_MAX_NODES] = {0}, next_row[NT_GRAPH_MAX_NODES] = {0};
    bool placed[NT_GRAPH_MAX_NODES] = {0};
    size_t last_rank = 0;
    for (size_t i = 0; i < layout->edge_count; ++i) {
        const GraphArc arc = layout->arcs[i];
        if (layout->edges[i].directed && arc.source != arc.target) ++incoming[arc.target];
    }
    for (size_t placed_count = 0; placed_count < layout->node_count; ++placed_count) {
        size_t first = SIZE_MAX, ready = SIZE_MAX;
        for (size_t i = 0; i < layout->node_count; ++i) {
            if (placed[i]) continue;
            if (first == SIZE_MAX) first = i;
            if (!incoming[i]) {
                ready = i;
                break;
            }
        }
        const size_t node = ready != SIZE_MAX ? ready : first;
        placed[node] = true;
        if (last_rank < rank[node]) last_rank = rank[node];
        ++rows[rank[node]];
        for (size_t i = 0; i < layout->edge_count; ++i) {
            const GraphArc arc = layout->arcs[i];
            if (!layout->edges[i].directed || arc.source != node || placed[arc.target]) continue;
            --incoming[arc.target];
            if (rank[arc.target] <= rank[node]) rank[arc.target] = (uint16_t)(rank[node] + 1);
        }
    }
    for (size_t i = 0; i < layout->node_count; ++i) {
        const size_t r = rank[i], row = next_row[r]++;
        if (layout->nodes[i].pinned) continue;
        layout->nodes[i].position = (NtPoint){
            last_rank ? (double)r / last_rank : .5,
            rows[r] > 1 ? (double)row / (rows[r] - 1) : .5
        };
    }
}

static void radial(NtGraphLayout *layout)
{
    for (size_t i = 0; i < layout->node_count; ++i) {
        if (layout->nodes[i].pinned) continue;
        const double angle = 6.28318530717958647692 * (double)i / layout->node_count;
        const double radius = layout->node_count > 1 ? .45 : 0;
        layout->nodes[i].position = (NtPoint){.5 + radius * sin(angle), .5 - radius * cos(angle)};
    }
}

static double clamp(double value, double minimum, double maximum)
{
    return fmin(maximum, fmax(minimum, value));
}

static void add_pair(NtPoint *changes, GraphArc arc, double x, double y)
{
    changes[arc.source].x += x;
    changes[arc.source].y += y;
    changes[arc.target].x -= x;
    changes[arc.target].y -= y;
}

static void force_step(NtGraphLayout *layout, NtPoint *changes, double movement)
{
    for (size_t i = 0; i < layout->node_count; ++i) {
        const NtPoint a = layout->nodes[i].position;
        changes[i].x += (.5 - a.x) * .1;
        changes[i].y += (.5 - a.y) * .1;
        for (size_t j = i + 1; j < layout->node_count; ++j) {
            const NtPoint b = layout->nodes[j].position;
            const double x = a.x == b.x && a.y == b.y ? .01 : a.x - b.x;
            const double y = a.y - b.y;
            const double factor = .003 / fmax(.0001, x * x + y * y);
            add_pair(changes, (GraphArc){i, j}, x * factor, y * factor);
        }
    }
    for (size_t i = 0; i < layout->edge_count; ++i) {
        const GraphArc arc = layout->arcs[i];
        if (arc.source == arc.target) continue;
        const NtPoint a = layout->nodes[arc.source].position;
        const NtPoint b = layout->nodes[arc.target].position;
        add_pair(changes, arc, (b.x - a.x) * .18, (b.y - a.y) * .18);
    }
    for (size_t i = 0; i < layout->node_count; ++i) {
        NtGraphPlacement *node = &layout->nodes[i];
        if (node->pinned) continue;
        node->position.x = clamp(node->position.x + clamp(changes[i].x, -movement, movement), 0, 1);
        node->position.y = clamp(node->position.y + clamp(changes[i].y, -movement, movement), 0, 1);
    }
}

static void force(NtGraphLayout *layout, uint32_t steps)
{
    radial(layout);
    NtPoint changes[NT_GRAPH_MAX_NODES];
    for (uint32_t step = 0; step < steps; ++step) {
        memset(changes, 0, layout->node_count * sizeof(*changes));
        const double movement = .075 * (double)(steps - step) / steps;
        force_step(layout, changes, movement);
    }
}

void nt_graph_layout_retain(NtGraphLayout *layout)
{
    ++layout->references;
}

void nt_graph_layout_destroy(NtGraphLayout *layout)
{
    if (!layout || --layout->references) return;
    const NtAllocator allocator = layout->allocator;
    nt_release(&allocator, layout->arcs, layout->edge_count * sizeof(*layout->arcs), NT_ALIGNMENT(GraphArc));
    nt_release(&allocator, layout->edges, layout->edge_count * sizeof(*layout->edges), NT_ALIGNMENT(NtGraphEdge));
    nt_release(&allocator, layout->nodes, layout->node_count * sizeof(*layout->nodes), NT_ALIGNMENT(NtGraphPlacement));
    nt_release(&allocator, layout, sizeof(*layout), NT_ALIGNMENT(NtGraphLayout));
}

static NtResult allocate_records(NtGraphLayout *layout)
{
    const NtAllocator *a = &layout->allocator;
    if (layout->node_count) {
        layout->nodes = nt_allocate(a, layout->node_count * sizeof(*layout->nodes), NT_ALIGNMENT(NtGraphPlacement));
        if (!layout->nodes) return NT_RESULT_ALLOCATION_FAILED;
    }
    if (layout->edge_count) {
        layout->edges = nt_allocate(a, layout->edge_count * sizeof(*layout->edges), NT_ALIGNMENT(NtGraphEdge));
        if (!layout->edges) return NT_RESULT_ALLOCATION_FAILED;
        layout->arcs = nt_allocate(a, layout->edge_count * sizeof(*layout->arcs), NT_ALIGNMENT(GraphArc));
        if (!layout->arcs) return NT_RESULT_ALLOCATION_FAILED;
    }
    return NT_RESULT_OK;
}

NtResult nt_graph_layout_create(const NtAllocator *provided, NtGraphData data,
                                NtGraphLayoutOptions options, NtGraphLayout **out)
{
    NtAllocator allocator;
    if (!out || !nt_allocator_get(provided, &allocator) ||
        options.kind < NT_GRAPH_LAYERED || options.kind > NT_GRAPH_FORCE)
        return NT_RESULT_INVALID_ARGUMENT;
    if (data.node_count > NT_GRAPH_MAX_NODES || data.edge_count > NT_GRAPH_MAX_EDGES ||
        (options.kind == NT_GRAPH_FORCE && options.force_iterations > NT_GRAPH_MAX_ITERATIONS))
        return NT_RESULT_OUT_OF_RANGE;
    if ((data.node_count && !data.nodes) || (data.edge_count && !data.edges))
        return NT_RESULT_INVALID_ARGUMENT;

    NtGraphLayout *layout = nt_allocate(&allocator, sizeof(*layout), NT_ALIGNMENT(NtGraphLayout));
    if (!layout) return NT_RESULT_ALLOCATION_FAILED;
    *layout = (NtGraphLayout){.allocator=allocator, .node_count=data.node_count, .edge_count=data.edge_count,
                             .references=1};
    NtResult result = allocate_records(layout);
    if (result == NT_RESULT_OK) result = validate_records(layout, data);
    if (result != NT_RESULT_OK) {
        nt_graph_layout_destroy(layout);
        return result;
    }

    switch (options.kind) {
    case NT_GRAPH_LAYERED: layered(layout); break;
    case NT_GRAPH_RADIAL: radial(layout); break;
    case NT_GRAPH_FORCE: force(layout, options.force_iterations ? options.force_iterations : 24); break;
    }
    *out = layout;
    return NT_RESULT_OK;
}

const NtGraphPlacement *nt_graph_layout_nodes(const NtGraphLayout *layout, size_t *count)
{
    if (count) *count = layout ? layout->node_count : 0;
    return layout ? layout->nodes : NULL;
}

const NtGraphEdge *nt_graph_layout_edges(const NtGraphLayout *layout, size_t *count)
{
    if (count) *count = layout ? layout->edge_count : 0;
    return layout ? layout->edges : NULL;
}
