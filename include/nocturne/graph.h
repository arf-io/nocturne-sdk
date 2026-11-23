#ifndef NOCTURNE_GRAPH_H
#define NOCTURNE_GRAPH_H

#include <nocturne/event_rules.h>
#include <nocturne/motion.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t NtGraphId;       /* Zero is reserved. Node and edge IDs have separate namespaces. */
enum { NT_GRAPH_MAX_NODES = 512, NT_GRAPH_MAX_EDGES = 4096, NT_GRAPH_MAX_ITERATIONS = 64 };

typedef struct NtGraphNode {
    NtGraphId id;
    bool pinned;
    NtPoint position;            /* Pinned positions are in [0,1] on each axis, with y increasing down. */
} NtGraphNode;

typedef struct NtGraphEdge { NtGraphId id, source, target; bool directed; } NtGraphEdge;
typedef struct NtGraphData {
    const NtGraphNode *nodes;
    size_t node_count;
    const NtGraphEdge *edges;
    size_t edge_count;
} NtGraphData;

typedef enum NtGraphLayoutKind { NT_GRAPH_LAYERED, NT_GRAPH_RADIAL, NT_GRAPH_FORCE } NtGraphLayoutKind;
typedef struct NtGraphLayoutOptions {
    NtGraphLayoutKind kind;
    uint32_t force_iterations;   /* Zero selects 24 steps. Ignored by other layouts. */
} NtGraphLayoutOptions;

typedef NtGraphNode NtGraphPlacement;
typedef struct NtGraphLayout NtGraphLayout;

/* Builds an owned snapshot from the supplied records. Parallel edges,
 * self loops and cycles are valid. Positions and edge records are sorted
 * by ID; input order has no effect. Errors leave the output untouched. */
NtResult nt_graph_layout_create(const NtAllocator *allocator, NtGraphData data,
                                NtGraphLayoutOptions options, NtGraphLayout **out);
void nt_graph_layout_destroy(NtGraphLayout *layout);
/* Returned arrays borrow the layout and remain valid until destruction.
 * The optional count is set to zero for a NULL layout. */
const NtGraphPlacement *nt_graph_layout_nodes(const NtGraphLayout *layout, size_t *count);
const NtGraphEdge *nt_graph_layout_edges(const NtGraphLayout *layout, size_t *count);

enum { NT_GRAPH_MAX_LABEL_BYTES = 256, NT_GRAPH_MAX_LABEL_COLUMNS = 24 };
typedef struct NtGraphLabel { NtGraphId node; NtByteView text; } NtGraphLabel;
typedef struct NtGraphViewOptions {
    NtGraphData data;
    NtGraphLayoutOptions layout;
    const NtGraphLabel *labels;
    size_t label_count;
    NtWidthPolicy width;
    NtCellSize preferred;
    NtRgba node, pinned, edge, selected, background;
} NtGraphViewOptions;
typedef enum NtGraphHitKind { NT_GRAPH_HIT_NONE, NT_GRAPH_HIT_NODE, NT_GRAPH_HIT_EDGE } NtGraphHitKind;
typedef struct NtGraphHit { NtGraphHitKind kind; NtGraphId id; } NtGraphHit;
enum { NT_INTENT_GRAPH_SELECTION = 0x4e470001 };

NtGraphViewOptions nt_graph_view_options_default(void);
/* Mount a leaf. The graph and its counted UTF-8 labels are copied. A
 * replacement retains selection by kind and ID when the item still exists. */
NtResult nt_graph_view_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount,
                              const NtGraphViewOptions *options, NtNodeId *out);
NtResult nt_graph_view_set(NtTree *tree, NtNodeId node, const NtGraphViewOptions *options);
NtResult nt_graph_view_selection(const NtTree *tree, NtNodeId node, NtGraphHit *out);
NtResult nt_graph_view_select(NtTree *tree, NtNodeId node, NtGraphHit selection);
/* Resolve a node marker, label cell, or edge in the last published frame.
 * No hit is {NT_GRAPH_HIT_NONE, 0}. Positions use frame coordinates. */
NtResult nt_graph_view_hit(const NtTree *tree, NtNodeId node, NtCellPoint frame, NtGraphHit *out);
/* Primary pointer down selects the visible item. Arrow keys step through
 * visible nodes by ID. A changed user selection emits a typed intent. */
NtResult nt_graph_event_decode(NtEventIntentView intent, NtGraphHit *out);

#ifdef __cplusplus
}
#endif
#endif
