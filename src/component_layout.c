#include "component_internal.h"
#include "panel_transform_internal.h"
#include "surface_internal.h"
#include "text_paint_internal.h"

#include <string.h>

NT_DECLARE_ALIGNMENT(NtPlacement);
NT_DECLARE_ALIGNMENT(NtPaintOrder);

/* Child measurement calls consumer code synchronously. Bound the number of
 * nested SDK callbacks before consuming another native stack frame. */
enum { MAX_MEASURE_DEPTH = 1 + 128 }; /* Root plus child measures. */

struct NtMeasureContext {
    NtTree *tree;
    NtSnapshot *snapshot;
    uint32_t slot;
    unsigned depth;
    size_t invocation, *sequence;
    NtResult failure;
};

struct NtLayoutContext {
    NtTree *tree;
    NtSnapshot *snapshot;
    uint32_t slot;
};

static bool inside(NtExtentRange limits, uint32_t value)
{
    return value >= limits.min && (!limits.bounded || value <= limits.max);
}

static NtResult measure(NtMeasureContext *caller, uint32_t slot, NtConstraints constraints,
                         NtCellSize *out)
{
    if (*caller->sequence == SIZE_MAX) return NT_RESULT_OUT_OF_RANGE;
    NtMeasureContext context = *caller;
    context.slot = slot;
    context.depth = caller->depth + 1;
    context.invocation = ++*caller->sequence;
    context.failure = NT_RESULT_OK;
    const NtNode *node = &context.tree->nodes[slot];
    NtCellSize preferred = {0};
    NtResult result = node->ops.measure(node->state, &context, constraints, &preferred);
    if (result == NT_RESULT_OK) result = context.failure;
    if (result != NT_RESULT_OK) return result;
    if (!inside(constraints.columns, preferred.columns) || !inside(constraints.rows, preferred.rows))
        return NT_RESULT_OUT_OF_RANGE;
    NtPlacement *place = &context.snapshot->placements[slot];
    place->measurement = context.invocation;
    place->measured_by = caller->invocation;
    place->preferred = *out = preferred;
    return NT_RESULT_OK;
}

static NtResult child_at(const NtTree *tree, uint32_t slot, size_t index, NtNodeId *out)
{
    if (!out) return NT_RESULT_INVALID_ARGUMENT;
    if (index >= tree->nodes[slot].children) return NT_RESULT_OUT_OF_RANGE;
    uint32_t child = tree->nodes[slot].first;
    while (index--) child = tree->nodes[child].next;
    *out = nt_node_ref(tree, child);
    return NT_RESULT_OK;
}

static NtResult validate_child(const NtTree *tree, uint32_t slot, NtNodeId child)
{
    const NtResult result = nt_node_validate(tree, child);
    if (result != NT_RESULT_OK) return result;
    return tree->nodes[child.slot].parent == slot ? NT_RESULT_OK : NT_RESULT_INVALID_ARGUMENT;
}

static NtResult child_after(const NtTree *tree, uint32_t slot, NtNodeId previous, NtNodeId *out)
{
    if (!out) return NT_RESULT_INVALID_ARGUMENT;
    if (nt_node_null(previous)) {
        *out = nt_node_ref(tree, tree->nodes[slot].first);
        return NT_RESULT_OK;
    }
    const NtResult result = validate_child(tree, slot, previous);
    if (result == NT_RESULT_OK) *out = nt_node_ref(tree, tree->nodes[previous.slot].next);
    return result;
}

size_t nt_measure_child_count(const NtMeasureContext *context)
{
    return context ? context->tree->nodes[context->slot].children : 0;
}

NtResult nt_measure_child_at(const NtMeasureContext *context, size_t index, NtNodeId *out)
{
    return context ? child_at(context->tree, context->slot, index, out) : NT_RESULT_INVALID_ARGUMENT;
}

NtResult nt_measure_child_after(const NtMeasureContext *context, NtNodeId previous, NtNodeId *out)
{
    return context ? child_after(context->tree, context->slot, previous, out) :
           NT_RESULT_INVALID_ARGUMENT;
}

NtResult nt_measure_child(NtMeasureContext *context, NtNodeId child,
                          NtConstraints constraints, NtCellSize *out)
{
    if (!context || !out || !inside(constraints.columns, constraints.columns.min) ||
        !inside(constraints.rows, constraints.rows.min)) return NT_RESULT_INVALID_ARGUMENT;
    if (context->failure != NT_RESULT_OK) return context->failure;
    NtResult result = validate_child(context->tree, context->slot, child);
    if (result != NT_RESULT_OK) return result;
    if (context->depth == MAX_MEASURE_DEPTH)
        return context->failure = NT_RESULT_OUT_OF_RANGE;
    result = measure(context, child.slot, constraints, out);
    if (result != NT_RESULT_OK) context->failure = result;
    return result;
}

size_t nt_layout_child_count(const NtLayoutContext *context)
{
    return context ? context->tree->nodes[context->slot].children : 0;
}

NtResult nt_layout_child_at(const NtLayoutContext *context, size_t index, NtNodeId *out)
{
    return context ? child_at(context->tree, context->slot, index, out) : NT_RESULT_INVALID_ARGUMENT;
}

NtResult nt_layout_child_after(const NtLayoutContext *context, NtNodeId previous, NtNodeId *out)
{
    return context ? child_after(context->tree, context->slot, previous, out) :
           NT_RESULT_INVALID_ARGUMENT;
}

static NtResult measured_child(const NtLayoutContext *context, NtNodeId child)
{
    const NtResult result = validate_child(context->tree, context->slot, child);
    if (result != NT_RESULT_OK) return result;
    const NtPlacement *placements = context->snapshot->placements;
    if (placements[child.slot].measured_by != placements[context->slot].measurement)
        return NT_RESULT_WRONG_PHASE;
    return NT_RESULT_OK;
}

NtResult nt_layout_child_size(const NtLayoutContext *context, NtNodeId child, NtCellSize *out)
{
    if (!context || !out) return NT_RESULT_INVALID_ARGUMENT;
    const NtResult result = measured_child(context, child);
    if (result == NT_RESULT_OK) *out = context->snapshot->placements[child.slot].preferred;
    return result;
}

static NtCellRect visible_span(NtRowSpan span, const NtSurfaceView *view)
{
    const NtCellRect clip = view->clip;
    const int64_t col = (int64_t)view->bounds.origin.x + span.first.x;
    const int64_t row = (int64_t)view->bounds.origin.y + span.first.y;
    if (row < clip.origin.y || row >= (int64_t)clip.origin.y + clip.size.rows)
        return (NtCellRect){0};
    const int64_t right = (int64_t)clip.origin.x + clip.size.columns;
    const int64_t first = col > clip.origin.x ? col : clip.origin.x;
    const int64_t after = col + span.columns < right ? col + span.columns : right;
    if (first >= after) return (NtCellRect){0};
    return (NtCellRect){{(int32_t)first,(int32_t)row},{(uint32_t)(after-first),1}};
}

static NtResult apply_spans(const NtTree *tree, NtSurfaceView *view, const NtClipMask *mask)
{
    if (mask->count && !mask->spans) return NT_RESULT_INVALID_ARGUMENT;
    if (mask->count > SIZE_MAX / sizeof(*mask->spans)) return NT_RESULT_OUT_OF_RANGE;
    int64_t left = INT64_MAX, top = INT64_MAX, right = 0, bottom = 0;
    for (size_t i = 0; i < mask->count; ++i) {
        const NtCellRect span = visible_span(mask->spans[i], view);
        if (!span.size.columns) continue;
        const int64_t x = span.origin.x, y = span.origin.y;
        if (x < left) left = x;
        if (y < top) top = y;
        if (x + span.size.columns > right) right = x + span.size.columns;
        if (y + 1 > bottom) bottom = y + 1;
    }
    if (left == INT64_MAX)
        return nt_surface_attach_mask(view, &(NtSurfaceMask){.kind = NT_MASK_RECTANGLE});
    const size_t columns = (size_t)(right-left), rows = (size_t)(bottom-top);
    if (rows > SIZE_MAX / columns) return NT_RESULT_OUT_OF_RANGE;
    const size_t bytes = rows * columns;
    uint8_t *pixels = nt_allocate(&tree->allocator, bytes, 1);
    if (!pixels) return NT_RESULT_ALLOCATION_FAILED;
    memset(pixels, 0, bytes);
    for (size_t i = 0; i < mask->count; ++i) {
        const NtCellRect span = visible_span(mask->spans[i], view);
        if (!span.size.columns) continue;
        const size_t index = (size_t)(span.origin.y-top) * columns + (size_t)(span.origin.x-left);
        memset(pixels + index, 255, span.size.columns);
    }
    const NtSurfaceMask shape = {
        .kind = NT_MASK_ALPHA,
        .bounds = {{(int32_t)left, (int32_t)top}, {(uint32_t)columns, (uint32_t)rows}},
        .alpha = pixels, .stride = columns, .count = bytes
    };
    const NtResult result = nt_surface_attach_mask(view, &shape);
    if (result != NT_RESULT_OK) nt_release(&tree->allocator, pixels, bytes, 1);
    return result;
}

static NtResult place_child(NtLayoutContext *context, NtNodeId child, NtCellRect bounds,
                            int32_t z_index, const NtClipMask *mask, const NtCellRect *clip)
{
    if (!context) return NT_RESULT_INVALID_ARGUMENT;
    NtResult result = measured_child(context, child);
    if (result != NT_RESULT_OK) return result;
    NtPlacement *placements = context->snapshot->placements;
    NtPlacement *place = &placements[child.slot];
    if (place->placed) return NT_RESULT_WRONG_PHASE;
    const NtPlacement *parent = &placements[context->slot];
    NtSurfaceView surface;
    result = nt_surface_child(&parent->surface, bounds, NULL, &surface);
    if (result != NT_RESULT_OK) return result;
    if (clip) {
        result = nt_surface_limit(&surface, parent->surface.bounds.origin, *clip);
        if (result != NT_RESULT_OK) return result;
    }
    if (mask) {
        result = apply_spans(context->tree, &surface, mask);
        if (result != NT_RESULT_OK) return result;
    }
    place->surface = surface;
    place->id = child;
    place->parent = parent->id;
    place->z = z_index;
    place->placed = true;
    place->enabled = parent->enabled && context->tree->nodes[child.slot].options.enabled;
    return NT_RESULT_OK;
}

NtResult nt_layout_place(NtLayoutContext *context, NtNodeId child, NtCellRect bounds,
                         int32_t z_index, const NtClipMask *mask)
{
    return place_child(context, child, bounds, z_index, mask, NULL);
}

NtResult nt_layout_place_clipped(NtLayoutContext *context, NtNodeId child, NtCellRect bounds,
                                 int32_t z_index, NtCellRect clip)
{
    return place_child(context, child, bounds, z_index, NULL, &clip);
}

NtCellRect nt_paint_clip(const NtPaintContext *paint)
{
    if (!paint) return (NtCellRect){0};
    const NtSurfaceView *surface = paint->surface;
    NtCellRect clip = surface->clip;
    const int64_t x = (int64_t)clip.origin.x - surface->bounds.origin.x;
    const int64_t y = (int64_t)clip.origin.y - surface->bounds.origin.y;
    if (!clip.size.columns || !clip.size.rows || x > INT32_MAX || y > INT32_MAX)
        return (NtCellRect){0};
    clip.origin = (NtCellPoint){(int32_t)x,(int32_t)y};
    return clip;
}

NtResult nt_paint_glyph(NtPaintContext *paint, NtCellPoint lead, NtByteView glyph,
                       uint8_t columns, NtCellStyle style)
{
    return paint ? nt_surface_glyph(paint->surface, lead, glyph, columns, style, paint->owner) :
           NT_RESULT_INVALID_ARGUMENT;
}

NtResult nt_paint_fill(NtPaintContext *paint, NtCellRect bounds, NtRgba color)
{
    return paint ? nt_surface_fill(paint->surface, bounds, color, paint->owner) : NT_RESULT_INVALID_ARGUMENT;
}

NtResult nt_paint_erase_glyph(NtPaintContext *paint, NtCellPoint lead)
{
    return paint ? nt_surface_erase_glyph(paint->surface, lead) : NT_RESULT_INVALID_ARGUMENT;
}

NtResult nt_paint_project_panels(NtPaintContext *paint, const NtPanelLayer *layers, size_t count)
{
    return paint ? nt_panel_project_owned(paint->surface,layers,count,paint->owner) :
           NT_RESULT_INVALID_ARGUMENT;
}

NtResult nt_component_paint_text(NtPaintContext *paint, NtCellPoint position,
                                  const NtTextLayout *layout, const NtTextPaintOptions *options)
{
    return nt_text_paint_candidate(paint->surface, position, layout, options, paint->owner);
}

NtResult nt_paint_text_effect(NtPaintContext *paint, NtCellPoint position,
                              const NtTextLayout *layout, const NtTextPaintOptions *options,
                              const NtTextEffect *effect)
{
    return paint ? nt_text_effect_paint_owner(paint->surface,position,layout,options,effect,paint->owner)
                 : NT_RESULT_INVALID_ARGUMENT;
}

void nt_component_attach_paint_data(NtPaintContext *paint, void *data, void (*dispose)(void *))
{
    paint->placement->paint_data = data;
    paint->placement->dispose_paint_data = dispose;
}

void nt_snapshot_destroy(const NtTree *tree, NtSnapshot *snapshot)
{
    if (snapshot->placements) for (size_t i = 0; i < snapshot->slots; ++i) {
        const NtPlacement *place = &snapshot->placements[i];
        if (place->dispose_paint_data) place->dispose_paint_data(place->paint_data);
        const NtSurfaceMask *mask = &place->surface.mask;
        nt_release(&tree->allocator, (void *)mask->alpha, mask->count, 1);
    }
    nt_release(&tree->allocator, snapshot->placements,
               snapshot->slots * sizeof(*snapshot->placements), NT_ALIGNMENT(NtPlacement));
    nt_release(&tree->allocator, snapshot->order,
               snapshot->slots * sizeof(*snapshot->order), NT_ALIGNMENT(NtPaintOrder));
    nt_frame_destroy(snapshot->frame);
    *snapshot = (NtSnapshot){0};
}

static NtResult begin_snapshot(NtTree *tree, NtCellSize size, NtSnapshot *snapshot)
{
    const NtAllocator *allocator = tree->allocator.allocate ? &tree->allocator : NULL;
    NtResult result = nt_frame_create(allocator, size, &snapshot->frame);
    if (result != NT_RESULT_OK || tree->root == NT_NO_SLOT) return result;
    const size_t slots = tree->slots;
    if (slots > SIZE_MAX / sizeof(NtPlacement) || slots > SIZE_MAX / sizeof(NtPaintOrder))
        return NT_RESULT_OUT_OF_RANGE;
    snapshot->slots = slots;
    const size_t placements_size = slots * sizeof(NtPlacement);
    snapshot->placements = nt_allocate(&tree->allocator, placements_size, NT_ALIGNMENT(NtPlacement));
    if (!snapshot->placements) return NT_RESULT_ALLOCATION_FAILED;
    memset(snapshot->placements, 0, placements_size);
    snapshot->order = nt_allocate(&tree->allocator, slots * sizeof(NtPaintOrder), NT_ALIGNMENT(NtPaintOrder));
    return snapshot->order ? NT_RESULT_OK : NT_RESULT_ALLOCATION_FAILED;
}

static int compare_order(const void *a, const void *b)
{
    const NtPaintOrder *left = a, *right = b;
    if (left->parent != right->parent) return left->parent < right->parent ? -1 : 1;
    if (left->z != right->z) return left->z < right->z ? -1 : 1;
    return (left->insertion > right->insertion) - (left->insertion < right->insertion);
}

static NtResult arrange(NtTree *tree, NtSnapshot *snapshot, NtCellSize size)
{
    const uint32_t root = tree->root;
    NtPlacement *placements = snapshot->placements;
    NtPlacement *root_place = &placements[root];
    root_place->id = nt_node_ref(tree, root);
    root_place->placed = true;
    root_place->enabled = tree->nodes[root].options.enabled;
    NtResult result = nt_surface_root(snapshot->frame, &root_place->surface);
    if (result != NT_RESULT_OK) return result;
    size_t sequence = 0;
    NtMeasureContext measurement = {.tree = tree, .snapshot = snapshot, .sequence = &sequence};
    const NtConstraints constraints = {{0, size.columns, true}, {0, size.rows, true}};
    NtCellSize preferred;
    result = measure(&measurement, root, constraints, &preferred);
    if (result != NT_RESULT_OK) return result;

    size_t count = 0;
    uint32_t previous = NT_NO_SLOT;
    for (uint32_t slot = root; slot != NT_NO_SLOT;
         slot = nt_tree_preorder(tree, root, slot, placements[slot].placed)) {
        NtPlacement *place = &placements[slot];
        if (!place->placed) continue;
        NtLayoutContext context = {tree, snapshot, slot};
        const NtNode *node = &tree->nodes[slot];
        result = node->ops.layout(node->state, &context, place->surface.bounds.size);
        if (result != NT_RESULT_OK) return result;
        place->paint_first = place->paint_next = place->traversal_next = NT_NO_SLOT;
        if (previous != NT_NO_SLOT) placements[previous].traversal_next = slot;
        previous = slot;
        if (slot == root) continue;
        snapshot->order[count] = (NtPaintOrder){slot, place->parent.slot, place->z, count};
        ++count;
    }
    if (count > 1) qsort(snapshot->order, count, sizeof(*snapshot->order), compare_order);
    while (count) {
        const NtPaintOrder entry = snapshot->order[--count];
        placements[entry.slot].paint_next = placements[entry.parent].paint_first;
        placements[entry.parent].paint_first = entry.slot;
    }
    return NT_RESULT_OK;
}

static uint32_t next_paint(const NtSnapshot *snapshot, uint32_t slot)
{
    const NtPlacement *placements = snapshot->placements;
    if (placements[slot].paint_first != NT_NO_SLOT) return placements[slot].paint_first;
    while (placements[slot].parent.tree && placements[slot].paint_next == NT_NO_SLOT)
        slot = placements[slot].parent.slot;
    return placements[slot].paint_next;
}

static NtResult paint(NtTree *tree, NtSnapshot *snapshot)
{
    for (uint32_t slot = tree->root; slot != NT_NO_SLOT; slot = next_paint(snapshot, slot)) {
        NtPlacement *place = &snapshot->placements[slot];
        const NtNode *node = &tree->nodes[slot];
        NtPaintContext context = {&place->surface, nt_node_owner(place->id), place};
        const NtResult result = node->ops.paint(node->state, &context, place->surface.bounds.size);
        if (result != NT_RESULT_OK) return result;
        snapshot->order[snapshot->count++].slot = slot;
    }
    return NT_RESULT_OK;
}

NtResult nt_tree_publish(NtTree *tree, NtCellSize size)
{
    if (!tree) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    tree->busy = true;
    NtSnapshot candidate = {0};
    NtResult result = begin_snapshot(tree, size, &candidate);
    if (result == NT_RESULT_OK && tree->root != NT_NO_SLOT) result = arrange(tree, &candidate, size);
    if (result == NT_RESULT_OK && tree->root != NT_NO_SLOT) result = paint(tree, &candidate);
    if (result == NT_RESULT_OK) {
        const NtSnapshot previous = tree->committed;
        tree->committed = candidate;
        candidate = previous;
        tree->dirty = NT_INVALIDATE_NONE;
        nt_tree_prune_input(tree);
    }
    nt_snapshot_destroy(tree, &candidate);
    tree->busy = false;
    return result;
}

const NtFrame *nt_tree_frame(const NtTree *tree)
{
    return tree ? tree->committed.frame : NULL;
}
