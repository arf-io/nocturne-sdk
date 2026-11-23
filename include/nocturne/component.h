#ifndef NOCTURNE_COMPONENT_H
#define NOCTURNE_COMPONENT_H

#include <nocturne/event.h>
#include <nocturne/paint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NtMeasureContext NtMeasureContext;
typedef struct NtLayoutContext NtLayoutContext;

typedef struct NtMountOptions { bool enabled, focusable, hit_testable; } NtMountOptions;
typedef struct NtRowSpan { NtCellPoint first; uint32_t columns; } NtRowSpan;
/* Child-local spans. Non-NULL with zero spans gives no coverage. */
typedef struct NtClipMask { const NtRowSpan *spans; size_t count; } NtClipMask;

typedef struct NtDeadline { bool scheduled; NtTimeUs at; } NtDeadline;
typedef struct NtUpdateInfo { NtTimeUs now, elapsed; NtMotionPolicy motion; } NtUpdateInfo;
typedef struct NtUpdateReply { NtInvalidation invalidate; NtDeadline next; } NtUpdateReply;

typedef struct NtComponentOps {
    NtResult (*measure)(const void *state, NtMeasureContext *context,
                        NtConstraints constraints, NtCellSize *preferred);
    NtResult (*layout)(const void *state, NtLayoutContext *context, NtCellSize assigned);
    NtResult (*update)(void *state, NtUpdateInfo info, NtUpdateReply *reply);
    NtResult (*paint)(const void *state, NtPaintContext *paint, NtCellSize assigned);
    NtResult (*event)(void *state, NtEventContext *context, const NtEvent *event,
                      NtEventReply *reply);
    void (*dispose)(void *state);
} NtComponentOps;

size_t nt_measure_child_count(const NtMeasureContext *context);
NtResult nt_measure_child_at(const NtMeasureContext *context, size_t index, NtNodeId *child);
/* Use a zero reference to start an insertion-order walk. A successful
 * result with a zero child marks the end. */
NtResult nt_measure_child_after(const NtMeasureContext *context, NtNodeId previous,
                               NtNodeId *child);
/* At most 128 synchronous child measures may be nested beneath the root
 * measure. Exceeding that depth fails the pending frame with OUT_OF_RANGE. */
NtResult nt_measure_child(NtMeasureContext *context, NtNodeId child,
                         NtConstraints constraints, NtCellSize *preferred);
size_t nt_layout_child_count(const NtLayoutContext *context);
NtResult nt_layout_child_at(const NtLayoutContext *context, size_t index, NtNodeId *child);
NtResult nt_layout_child_after(const NtLayoutContext *context, NtNodeId previous,
                              NtNodeId *child);
NtResult nt_layout_child_size(const NtLayoutContext *context, NtNodeId child, NtCellSize *preferred);
NtResult nt_layout_place(NtLayoutContext *context, NtNodeId child, NtCellRect bounds,
                         int32_t z_index, const NtClipMask *mask);
/* Bounds and clip are both parent-local. Child geometry keeps the assigned
 * bounds while paint and hits are restricted to the intersected clip. */
NtResult nt_layout_place_clipped(NtLayoutContext *context, NtNodeId child, NtCellRect bounds,
                                 int32_t z_index, NtCellRect clip);

/* Zero-initialize an NtNodeId to address the root mount or clear focus.
 * A tree is serialized by its caller. Mutations during callbacks fail with
 * WRONG_PHASE. A destroy invoked during a callback has no effect. */
NtResult nt_tree_create(const NtAllocator *allocator, NtTree **out);
void nt_tree_destroy(NtTree *tree);
NtResult nt_tree_mount(NtTree *tree, NtNodeId parent, const NtComponentOps *ops,
                       void *state, NtMountOptions options, NtNodeId *out);
NtResult nt_tree_move(NtTree *tree, NtNodeId node, NtNodeId new_parent);
NtResult nt_tree_remove(NtTree *tree, NtNodeId node);
NtResult nt_tree_invalidate(NtTree *tree, NtNodeId node, NtInvalidation flags);
NtResult nt_tree_tick(NtTree *tree, NtTimeUs now, NtMotionPolicy motion);
NtDeadline nt_tree_next_update(const NtTree *tree);
NtResult nt_tree_publish(NtTree *tree, NtCellSize size);
const NtFrame *nt_tree_frame(const NtTree *tree);
NtResult nt_tree_set_focus(NtTree *tree, NtNodeId node);
NtNodeId nt_tree_focus(const NtTree *tree);
NtResult nt_tree_focus_advance(NtTree *tree, bool reverse);
NtNodeId nt_tree_capture(const NtTree *tree, NtPointerId pointer);

/* Route one normalized event using committed geometry. A key can carry
 * associated UTF-8 text. Reserved keys omit both routes. NULL policy
 * selects no reserved chords or default traversal. Invalid input leaves
 * out unchanged. After admission, out describes delivery even on failure. */
NtResult nt_tree_dispatch(NtTree *tree, const NtEvent *event, NtByteView key_text,
                           const NtDispatchPolicy *policy, NtDispatchResult *out);

typedef struct NtNodeGeometry {
    NtNodeId parent;
    NtCellRect bounds, clip; /* Frame coordinates. The rectangular clip may contain mask holes. */
    bool placed, enabled;
} NtNodeGeometry;

/* Queries borrow the last successful snapshot, even during a callback. */
NtResult nt_tree_geometry(const NtTree *tree, NtNodeId node, NtNodeGeometry *out);
NtResult nt_tree_hit(const NtTree *tree, NtCellPoint frame_position, NtNodeId *out);
NtInvalidation nt_tree_pending(const NtTree *tree);
NtOwnerToken nt_node_owner(NtNodeId node);

/* A peeked payload remains live until pop or tree destruction. Typed
 * intents have an empty byte view. See <nocturne/event_rules.h>. */
bool nt_tree_peek_intent(const NtTree *tree, NtIntentView *out);
void nt_tree_pop_intent(NtTree *tree);

#ifdef __cplusplus
}
#endif
#endif
