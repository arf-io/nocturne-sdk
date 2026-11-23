#include <nocturne/viewport.h>

#include "component_internal.h"

typedef struct ViewportState {
    NtAllocator allocator;
    NtViewportOptions options;
} ViewportState;

NT_DECLARE_ALIGNMENT(ViewportState);

static uint32_t scroll_axis(bool enabled, uint32_t view, uint32_t content, uint32_t requested)
{
    if (!enabled || content <= view) return 0;
    const uint32_t limit = content - view;
    return requested < limit ? requested : limit;
}

NtCellSize nt_scroll_resolve(NtViewportOptions options, NtCellSize viewport, NtCellSize content)
{
    return (NtCellSize){
        scroll_axis(options.scroll_columns, viewport.columns, content.columns, options.requested.columns),
        scroll_axis(options.scroll_rows, viewport.rows, content.rows, options.requested.rows)
    };
}

static int64_t anchored_axis(uint32_t parent, uint32_t child, NtBoxAlign align)
{
    const int64_t spare = (int64_t)parent - child;
    if (align == NT_BOX_END) return spare;
    if (align == NT_BOX_CENTER) return spare / 2 - (spare % 2 < 0);
    return 0;
}

NtResult nt_anchor_rect(NtCellSize parent, NtCellSize child,
                        NtBoxAlign horizontal, NtBoxAlign vertical,
                        NtCellPoint offset, NtCellRect *out)
{
    if (!out || horizontal < NT_BOX_START || horizontal > NT_BOX_END ||
        vertical < NT_BOX_START || vertical > NT_BOX_END) return NT_RESULT_INVALID_ARGUMENT;
    const int64_t x = anchored_axis(parent.columns, child.columns, horizontal) + offset.x;
    const int64_t y = anchored_axis(parent.rows, child.rows, vertical) + offset.y;
    if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX)
        return NT_RESULT_OUT_OF_RANGE;
    *out = (NtCellRect){{(int32_t)x, (int32_t)y}, child};
    return NT_RESULT_OK;
}

static uint32_t clamp_extent(uint32_t size, NtExtentRange range)
{
    if (size < range.min) return range.min;
    return range.bounded && size > range.max ? range.max : size;
}

static NtResult measure_viewport(const void *value, NtMeasureContext *context,
                                 NtConstraints constraints, NtCellSize *out)
{
    const ViewportState *state = value;
    const size_t count = nt_measure_child_count(context);
    if (count > 1) return NT_RESULT_INVALID_ARGUMENT;
    NtCellSize content = {0};
    if (count) {
        NtNodeId child;
        NtResult result = nt_measure_child_at(context, 0, &child);
        if (result != NT_RESULT_OK) return result;
        NtConstraints bounds = constraints;
        bounds.columns.min = bounds.rows.min = 0;
        if (state->options.scroll_columns) bounds.columns.bounded = false;
        if (state->options.scroll_rows) bounds.rows.bounded = false;
        result = nt_measure_child(context, child, bounds, &content);
        if (result != NT_RESULT_OK) return result;
    }
    *out = (NtCellSize){clamp_extent(content.columns, constraints.columns),
                        clamp_extent(content.rows, constraints.rows)};
    return NT_RESULT_OK;
}

static NtResult arrange_viewport(const void *value, NtLayoutContext *context, NtCellSize assigned)
{
    if (!nt_layout_child_count(context) || !assigned.columns || !assigned.rows) return NT_RESULT_OK;
    const ViewportState *state = value;
    NtNodeId child;
    NtResult result = nt_layout_child_at(context, 0, &child);
    if (result != NT_RESULT_OK) return result;
    NtCellSize preferred;
    result = nt_layout_child_size(context, child, &preferred);
    if (result != NT_RESULT_OK) return result;
    NtCellSize content = assigned;
    if (state->options.scroll_columns && preferred.columns > assigned.columns)
        content.columns = preferred.columns;
    if (state->options.scroll_rows && preferred.rows > assigned.rows)
        content.rows = preferred.rows;
    const NtCellSize scroll = nt_scroll_resolve(state->options, assigned, content);
    const int64_t x = -(int64_t)scroll.columns, y = -(int64_t)scroll.rows;
    if (x < INT32_MIN || y < INT32_MIN) return NT_RESULT_OUT_OF_RANGE;
    return nt_layout_place(context, child, (NtCellRect){{(int32_t)x, (int32_t)y}, content}, 0, NULL);
}

static NtResult paint_viewport(const void *state, NtPaintContext *paint, NtCellSize assigned)
{
    (void)state;
    (void)paint;
    (void)assigned;
    return NT_RESULT_OK;
}

static void dispose_viewport(void *value)
{
    ViewportState *state = value;
    const NtAllocator allocator = state->allocator;
    nt_release(&allocator, state, sizeof(*state), NT_ALIGNMENT(ViewportState));
}

static const NtComponentOps viewport_ops = {
    measure_viewport, arrange_viewport, NULL, paint_viewport, NULL, dispose_viewport
};

NtResult nt_viewport_mount(NtTree *tree, NtNodeId parent, NtMountOptions options,
                           NtViewportOptions viewport, NtNodeId *out)
{
    if (!tree || !out) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    if (nt_node_null(parent)) {
        if (tree->root != NT_NO_SLOT) return NT_RESULT_INVALID_ARGUMENT;
    } else {
        const NtResult result = nt_node_validate(tree, parent);
        if (result != NT_RESULT_OK) return result;
    }
    tree->busy = true;
    ViewportState *state = nt_allocate(&tree->allocator, sizeof(*state), NT_ALIGNMENT(ViewportState));
    tree->busy = false;
    if (!state) return NT_RESULT_ALLOCATION_FAILED;
    *state = (ViewportState){tree->allocator, viewport};
    const NtResult result = nt_tree_mount(tree, parent, &viewport_ops, state, options, out);
    if (result != NT_RESULT_OK) dispose_viewport(state);
    return result;
}

static NtResult validate_viewport(const NtTree *tree, NtNodeId node)
{
    const NtResult result = nt_node_validate(tree, node);
    if (result != NT_RESULT_OK) return result;
    return tree->nodes[node.slot].ops.measure == measure_viewport ?
           NT_RESULT_OK : NT_RESULT_INVALID_ARGUMENT;
}

NtResult nt_viewport_set(NtTree *tree, NtNodeId node, NtViewportOptions viewport)
{
    if (!tree) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    const NtResult result = validate_viewport(tree, node);
    if (result != NT_RESULT_OK) return result;
    ViewportState *state = tree->nodes[node.slot].state;
    state->options = viewport;
    nt_tree_dirty(tree, NT_INVALIDATE_LAYOUT);
    return NT_RESULT_OK;
}

NtResult nt_viewport_view(const NtTree *tree, NtNodeId node, NtViewportView *out)
{
    if (!tree || !out) return NT_RESULT_INVALID_ARGUMENT;
    const NtResult result = validate_viewport(tree, node);
    if (result != NT_RESULT_OK) return result;
    NtViewportView view = {0};
    const NtPlacement *placement = nt_tree_placement(tree, node);
    if (placement) {
        view.placed = true;
        view.viewport = placement->surface.bounds.size;
        if (placement->paint_first != NT_NO_SLOT) {
            const NtPlacement *first = &tree->committed.placements[placement->paint_first];
            if (nt_node_live(tree, first->id)) {
                view.child = first->id;
                view.content = first->surface.bounds.size;
                const NtCellPoint origin = placement->surface.bounds.origin;
                const NtCellPoint child_origin = first->surface.bounds.origin;
                view.effective = (NtCellSize){(uint32_t)((int64_t)origin.x - child_origin.x),
                                             (uint32_t)((int64_t)origin.y - child_origin.y)};
            }
        }
    }
    *out = view;
    return NT_RESULT_OK;
}
