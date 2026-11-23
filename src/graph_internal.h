#ifndef NOCTURNE_INTERNAL_GRAPH_H
#define NOCTURNE_INTERNAL_GRAPH_H

#include <nocturne/graph.h>

#include "allocator.h"

typedef struct GraphArc { size_t source, target; } GraphArc;

struct NtGraphLayout {
    NtAllocator allocator;
    NtGraphPlacement *nodes;
    NtGraphEdge *edges;
    GraphArc *arcs;
    size_t node_count, edge_count, references;
};

void nt_graph_layout_retain(NtGraphLayout *layout);
size_t nt_graph_find_node(const NtGraphLayout *layout, NtGraphId id);
size_t nt_graph_find_edge(const NtGraphLayout *layout, NtGraphId id);

#endif
