#ifndef NOCTURNE_INTERNAL_COMPONENT_H
#define NOCTURNE_INTERNAL_COMPONENT_H

#include <nocturne/component.h>
#include <nocturne/surface.h>
#include <nocturne/text_paint.h>

#include "allocator.h"

#define NT_NO_SLOT UINT32_MAX

typedef struct NtNode {
    NtComponentOps ops;
    NtMountOptions options;
    void *state;
    uint32_t generation, parent, first, last, previous, next;
    size_t children;
    NtDeadline deadline;
    NtTimeUs updated_at;
    bool live, updated;
    const struct NtPropertyDescriptor *properties;
    struct NtPropertyProviderTable *property_provider;
    size_t property_count;
    bool owns_properties;
} NtNode;

typedef struct NtPlacement {
    NtNodeId id, parent;
    NtSurfaceView surface;
    NtCellSize preferred;
    void *paint_data;
    void (*dispose_paint_data)(void *);
    size_t measured_by, measurement;
    uint32_t paint_first, paint_next, traversal_next;
    int32_t z;
    bool placed, enabled;
} NtPlacement;

struct NtPaintContext {
    const NtSurfaceView *surface;
    NtOwnerToken owner;
    NtPlacement *placement;
};

typedef struct NtPaintOrder {
    uint32_t slot, parent;
    int32_t z;
    size_t insertion;
} NtPaintOrder;

typedef struct NtSnapshot {
    NtFrame *frame;
    NtPlacement *placements;
    NtPaintOrder *order;
    size_t slots, count;
} NtSnapshot;

typedef struct NtIntent NtIntent;
typedef struct NtGesture NtGesture;
typedef struct NtSchedule NtSchedule;
typedef struct NtLocalPosition { int64_t x, y; } NtLocalPosition;
typedef struct NtTreeAttachment {
    NtTree *tree;
    struct NtTreeAttachment *next;
} NtTreeAttachment;

struct NtTree {
    NtAllocator allocator;
    NtNode *nodes;
    size_t slots, capacity;
    uint32_t root, vacancy;
    NtSnapshot committed;
    NtTimeUs watermark;
    NtInvalidation dirty;
    bool busy;
    NtNodeId focus;
    NtGesture *gestures;
    NtIntent *intent_first, *intent_last;
    NtTreeAttachment *attachments;
    NtSchedule *schedule;
};

static inline bool nt_node_same(NtNodeId a, NtNodeId b)
{
    return a.tree == b.tree && a.slot == b.slot && a.generation == b.generation;
}

static inline bool nt_node_null(NtNodeId id)
{
    return !id.tree && !id.slot && !id.generation;
}

static inline NtNodeId nt_node_ref(const NtTree *tree, uint32_t slot)
{
    return slot == NT_NO_SLOT ? (NtNodeId){0} :
           (NtNodeId){tree, slot, tree->nodes[slot].generation};
}

static inline bool nt_node_live(const NtTree *tree, NtNodeId id)
{
    return id.tree == tree && id.slot < tree->slots && id.generation &&
           tree->nodes[id.slot].live && id.generation == tree->nodes[id.slot].generation;
}

static inline NtResult nt_node_validate(const NtTree *tree, NtNodeId id)
{
    if (id.tree != tree || !id.generation) return NT_RESULT_INVALID_ARGUMENT;
    return nt_node_live(tree, id) ? NT_RESULT_OK : NT_RESULT_STALE_REFERENCE;
}

static inline void nt_tree_dirty(NtTree *tree, NtInvalidation flags)
{
    if (flags > tree->dirty) tree->dirty = flags;
}

/* Preorder successor within the given subtree. */
uint32_t nt_tree_preorder(const NtTree *tree, uint32_t root, uint32_t slot, bool descend);
void nt_snapshot_destroy(const NtTree *tree, NtSnapshot *snapshot);
NtResult nt_component_paint_text(NtPaintContext *paint, NtCellPoint position,
                                  const NtTextLayout *layout, const NtTextPaintOptions *options);
/* A stock leaf can attach one owned value to the frame it is painting. */
void nt_component_attach_paint_data(NtPaintContext *paint, void *data, void (*dispose)(void *));

/* Input-state seam. Both entry points run while the mutation guard is held.
 * A retiring node keeps its descriptor until pruning and notifications finish. */
const NtPlacement *nt_tree_placement(const NtTree *tree, NtNodeId node);
void nt_tree_prune_input(NtTree *tree);
void nt_tree_release_intents(NtTree *tree);
void nt_tree_drop_intent(NtTree *tree);
void nt_tree_prune_schedule(NtTree *tree);
void nt_tree_release_schedule(NtTree *tree);
void nt_tree_release_gestures(NtTree *tree);
bool nt_component_local_position(const NtEventContext *context, NtLocalPosition *out);
void nt_component_discard_pending_intents(NtEventContext *context);

#endif
