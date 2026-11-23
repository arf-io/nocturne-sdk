#include <nocturne/layout.h>

#include "component_internal.h"

#include <string.h>

typedef struct BoxState {
    NtAllocator allocator;
    NtBoxLayout layout;
} BoxState;

typedef struct BoxChild {
    NtNodeId node;
    NtCellSize preferred;
    uint32_t main_size, capacity;
} BoxChild;

NT_DECLARE_ALIGNMENT(BoxState);
NT_DECLARE_ALIGNMENT(BoxChild);

static uint32_t min_extent(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static uint32_t add_extent(uint32_t a, uint32_t b)
{
    return a > UINT32_MAX - b ? UINT32_MAX : a + b;
}

static uint32_t subtract_extent(uint32_t value, uint32_t inset)
{
    return value > inset ? value - inset : 0;
}

static uint32_t bound_extent(uint32_t value, NtExtentRange range)
{
    if (value < range.min) return range.min;
    return range.bounded ? min_extent(value, range.max) : value;
}

static bool valid_item(NtBoxItem item)
{
    return (!item.columns.bounded || item.columns.min <= item.columns.max) &&
           (!item.rows.bounded || item.rows.min <= item.rows.max);
}

static NtResult copy_layout(const NtAllocator *allocator, const NtBoxLayout *layout,
                            BoxState **out)
{
    if (!layout || layout->kind < NT_BOX_LAYOUT_ROW || layout->kind > NT_BOX_LAYOUT_STACK ||
        layout->horizontal < NT_BOX_START || layout->horizontal > NT_BOX_END ||
        layout->vertical < NT_BOX_START || layout->vertical > NT_BOX_END ||
        !valid_item(layout->fallback) || (layout->item_count && !layout->items))
        return NT_RESULT_INVALID_ARGUMENT;
    if (layout->item_count > (SIZE_MAX - sizeof(BoxState)) / sizeof(NtBoxItem))
        return NT_RESULT_OUT_OF_RANGE;
    for (size_t i = 0; i < layout->item_count; ++i)
        if (!valid_item(layout->items[i])) return NT_RESULT_INVALID_ARGUMENT;

    const size_t bytes = layout->item_count * sizeof(NtBoxItem);
    BoxState *state = nt_allocate(allocator, sizeof(*state) + bytes, NT_ALIGNMENT(BoxState));
    if (!state) return NT_RESULT_ALLOCATION_FAILED;
    state->allocator = *allocator;
    state->layout = *layout;
    state->layout.items = bytes ? memcpy(state + 1, layout->items, bytes) : NULL;
    *out = state;
    return NT_RESULT_OK;
}

static void dispose_box(void *value)
{
    BoxState *state = value;
    const NtAllocator allocator = state->allocator;
    const size_t bytes = sizeof(*state) + state->layout.item_count * sizeof(NtBoxItem);
    nt_release(&allocator, state, bytes, NT_ALIGNMENT(BoxState));
}

static NtBoxItem item_at(const NtBoxLayout *layout, size_t index)
{
    return index < layout->item_count ? layout->items[index] : layout->fallback;
}

static NtExtentRange child_range(NtExtentRange item, NtExtentRange parent, uint32_t padding)
{
    if (!parent.bounded) return item;
    const uint32_t limit = bound_extent(subtract_extent(parent.max, padding), item);
    return (NtExtentRange){item.min, limit, true};
}

static NtResult measure_box(const void *value, NtMeasureContext *context,
                            NtConstraints constraints, NtCellSize *out)
{
    const NtBoxLayout *layout = &((const BoxState *)value)->layout;
    const NtBoxInsets padding = layout->padding;
    const uint32_t horizontal = add_extent(padding.left, padding.right);
    const uint32_t vertical = add_extent(padding.top, padding.bottom);
    const bool row = layout->kind == NT_BOX_LAYOUT_ROW;
    const bool column = layout->kind == NT_BOX_LAYOUT_COLUMN;
    const size_t count = nt_measure_child_count(context);
    NtNodeId child = {0};
    NtCellSize preferred = {0};
    for (size_t i = 0; i < count; ++i) {
        NtResult result = nt_measure_child_after(context, child, &child);
        if (result != NT_RESULT_OK) return result;
        const NtBoxItem item = item_at(layout, i);
        const NtConstraints bounds = {
            child_range(item.columns, constraints.columns, horizontal),
            child_range(item.rows, constraints.rows, vertical)
        };
        NtCellSize size;
        result = nt_measure_child(context, child, bounds, &size);
        if (result != NT_RESULT_OK) return result;
        preferred.columns = row ? add_extent(preferred.columns, size.columns) :
            (preferred.columns > size.columns ? preferred.columns : size.columns);
        preferred.rows = column ? add_extent(preferred.rows, size.rows) :
            (preferred.rows > size.rows ? preferred.rows : size.rows);
        if (i) {
            if (row) preferred.columns = add_extent(preferred.columns, layout->gap);
            if (column) preferred.rows = add_extent(preferred.rows, layout->gap);
        }
    }
    *out = (NtCellSize){bound_extent(add_extent(preferred.columns, horizontal), constraints.columns),
                        bound_extent(add_extent(preferred.rows, vertical), constraints.rows)};
    return NT_RESULT_OK;
}

static uint64_t share_at(const BoxChild *children, size_t count, uint32_t share)
{
    uint64_t total = 0;
    for (size_t i = 0; i < count; ++i) total += min_extent(children[i].capacity, share);
    return total;
}

static void distribute(BoxChild *children, size_t count, uint64_t amount, bool growing)
{
    if (!amount) return;
    uint32_t low = 0, high = 0;
    for (size_t i = 0; i < count; ++i)
        if (children[i].capacity > high) high = children[i].capacity;
    /* Complete shares have the same size until individual capacities bind. */
    while (low != high) {
        const uint32_t middle = low + (uint32_t)(((uint64_t)high - low + 1) / 2);
        if (share_at(children, count, middle) <= amount) low = middle;
        else high = middle - 1;
    }
    uint64_t remainder = amount - share_at(children, count, low);
    for (size_t i = 0; i < count; ++i) {
        const uint32_t capacity = children[i].capacity;
        uint32_t delta = min_extent(capacity, low);
        if (remainder && delta < capacity) {
            ++delta;
            --remainder;
        }
        if (growing) children[i].main_size += delta;
        else children[i].main_size -= delta;
    }
}

static uint32_t align_offset(NtBoxAlign align, uint32_t available, uint32_t size)
{
    const uint32_t slack = subtract_extent(available, size);
    switch (align) {
        case NT_BOX_END: return slack;
        case NT_BOX_CENTER: return slack / 2;
        default: return 0;
    }
}

static uint32_t cross_extent(NtExtentRange range, uint32_t preferred, uint32_t available,
                             bool grow)
{
    const uint32_t requested = grow ? available : min_extent(preferred, available);
    return bound_extent(requested, range);
}

static NtResult place_box_child(NtLayoutContext *context, NtNodeId child, NtCellRect clip,
                                uint64_t x, uint64_t y, NtCellSize size)
{
    const uint64_t right = (uint32_t)clip.origin.x + (uint64_t)clip.size.columns;
    const uint64_t bottom = (uint32_t)clip.origin.y + (uint64_t)clip.size.rows;
    if (x >= right || y >= bottom) return NT_RESULT_OK;
    if (x > INT32_MAX || y > INT32_MAX) return NT_RESULT_OUT_OF_RANGE;
    const NtCellRect bounds = {{(int32_t)x, (int32_t)y}, size};
    return nt_layout_place_clipped(context, child, bounds, 0, clip);
}

static NtResult arrange_stack(const NtBoxLayout *layout, NtLayoutContext *context,
                              NtCellRect content)
{
    NtNodeId child = {0};
    const size_t count = nt_layout_child_count(context);
    for (size_t i = 0; i < count; ++i) {
        NtResult result = nt_layout_child_after(context, child, &child);
        if (result != NT_RESULT_OK) return result;
        NtCellSize preferred;
        result = nt_layout_child_size(context, child, &preferred);
        if (result != NT_RESULT_OK) return result;
        const NtBoxItem item = item_at(layout, i);
        const NtCellSize size = {
            cross_extent(item.columns, preferred.columns, content.size.columns, item.grow_columns),
            cross_extent(item.rows, preferred.rows, content.size.rows, item.grow_rows)
        };
        const uint64_t x = (uint32_t)content.origin.x +
            (uint64_t)align_offset(layout->horizontal, content.size.columns, size.columns);
        const uint64_t y = (uint32_t)content.origin.y +
            (uint64_t)align_offset(layout->vertical, content.size.rows, size.rows);
        result = place_box_child(context, child, content, x, y, size);
        if (result != NT_RESULT_OK) return result;
    }
    return NT_RESULT_OK;
}

static NtResult arrange_line(const NtBoxLayout *layout, NtLayoutContext *context,
                             NtCellRect content, BoxChild *children, size_t count)
{
    const bool row = layout->kind == NT_BOX_LAYOUT_ROW;
    const uint32_t available = row ? content.size.columns : content.size.rows;
    const uint64_t gaps = (uint64_t)(count - 1) * layout->gap;
    const uint64_t budget = gaps >= available ? 0 : available - gaps;
    NtNodeId child = {0};
    uint64_t natural = 0;
    for (size_t i = 0; i < count; ++i) {
        NtResult result = nt_layout_child_after(context, child, &child);
        if (result != NT_RESULT_OK) return result;
        NtCellSize preferred;
        result = nt_layout_child_size(context, child, &preferred);
        if (result != NT_RESULT_OK) return result;
        const uint32_t size = row ? preferred.columns : preferred.rows;
        children[i] = (BoxChild){child, preferred, size, 0};
        natural += size;
    }
    const bool growing = natural < budget;
    for (size_t i = 0; i < count; ++i) {
        const NtBoxItem item = item_at(layout, i);
        const NtExtentRange range = row ? item.columns : item.rows;
        const uint32_t size = children[i].main_size;
        if (growing) {
            const bool grow = row ? item.grow_columns : item.grow_rows;
            if (grow) children[i].capacity = (range.bounded ? range.max : UINT32_MAX) - size;
        } else children[i].capacity = size - range.min;
    }
    distribute(children, count, growing ? budget - natural : natural - budget, growing);

    uint64_t used = gaps;
    for (size_t i = 0; i < count && used < available; ++i) used += children[i].main_size;
    const NtBoxAlign align = row ? layout->horizontal : layout->vertical;
    uint64_t cursor = align_offset(align, available, (uint32_t)(used < available ? used : available));
    for (size_t i = 0; i < count && cursor < available; ++i) {
        const NtBoxItem item = item_at(layout, i);
        const BoxChild *entry = &children[i];
        const NtCellSize size = row ?
            (NtCellSize){entry->main_size, cross_extent(item.rows, entry->preferred.rows,
                                                      content.size.rows, item.grow_rows)} :
            (NtCellSize){cross_extent(item.columns, entry->preferred.columns, content.size.columns,
                                     item.grow_columns), entry->main_size};
        const uint64_t x = (uint32_t)content.origin.x + (row ? cursor :
            align_offset(layout->horizontal, content.size.columns, size.columns));
        const uint64_t y = (uint32_t)content.origin.y + (row ?
            align_offset(layout->vertical, content.size.rows, size.rows) : cursor);
        const NtResult result = place_box_child(context, entry->node, content, x, y, size);
        if (result != NT_RESULT_OK) return result;
        cursor += (uint64_t)entry->main_size + layout->gap;
    }
    return NT_RESULT_OK;
}

static NtResult arrange_box(const void *value, NtLayoutContext *context, NtCellSize assigned)
{
    const BoxState *state = value;
    const NtBoxLayout *layout = &state->layout;
    const NtBoxInsets padding = layout->padding;
    const NtCellSize inner = {
        subtract_extent(assigned.columns, add_extent(padding.left, padding.right)),
        subtract_extent(assigned.rows, add_extent(padding.top, padding.bottom))
    };
    const size_t count = nt_layout_child_count(context);
    if (!count || !inner.columns || !inner.rows) return NT_RESULT_OK;
    if (padding.left > INT32_MAX || padding.top > INT32_MAX) return NT_RESULT_OUT_OF_RANGE;
    const NtCellRect content = {{(int32_t)padding.left, (int32_t)padding.top}, inner};
    if (layout->kind == NT_BOX_LAYOUT_STACK) return arrange_stack(layout, context, content);

    if (count > SIZE_MAX / sizeof(BoxChild)) return NT_RESULT_OUT_OF_RANGE;
    const size_t bytes = count * sizeof(BoxChild);
    BoxChild *children = nt_allocate(&state->allocator, bytes, NT_ALIGNMENT(BoxChild));
    if (!children) return NT_RESULT_ALLOCATION_FAILED;
    const NtResult result = arrange_line(layout, context, content, children, count);
    nt_release(&state->allocator, children, bytes, NT_ALIGNMENT(BoxChild));
    return result;
}

static NtResult paint_box(const void *state, NtPaintContext *context, NtCellSize assigned)
{
    (void)state;
    (void)context;
    (void)assigned;
    return NT_RESULT_OK;
}

static const NtComponentOps box_ops = {
    measure_box, arrange_box, NULL, paint_box, NULL, dispose_box
};

NtResult nt_box_layout_mount(NtTree *tree, NtNodeId parent, NtMountOptions options,
                             const NtBoxLayout *layout, NtNodeId *out)
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
    BoxState *state;
    NtResult result = copy_layout(&tree->allocator, layout, &state);
    tree->busy = false;
    if (result != NT_RESULT_OK) return result;
    result = nt_tree_mount(tree, parent, &box_ops, state, options, out);
    if (result != NT_RESULT_OK) dispose_box(state);
    return result;
}

NtResult nt_box_layout_set(NtTree *tree, NtNodeId node, const NtBoxLayout *layout)
{
    if (!tree) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    NtResult result = nt_node_validate(tree, node);
    if (result != NT_RESULT_OK) return result;
    NtNode *mount = &tree->nodes[node.slot];
    if (mount->ops.measure != measure_box) return NT_RESULT_INVALID_ARGUMENT;
    tree->busy = true;
    BoxState *state;
    result = copy_layout(&tree->allocator, layout, &state);
    if (result == NT_RESULT_OK) {
        BoxState *previous = mount->state;
        mount->state = state;
        nt_tree_dirty(tree, NT_INVALIDATE_LAYOUT);
        dispose_box(previous);
    }
    tree->busy = false;
    return result;
}
