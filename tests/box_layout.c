#include <nocturne/layout.h>

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "Box layout line %d: %s\n", __LINE__, #condition); \
    exit(EXIT_FAILURE); \
} } while (0)

static uint32_t fit(uint32_t desired, NtExtentRange range)
{
    if (desired < range.min) return range.min;
    return range.bounded && desired > range.max ? range.max : desired;
}

static NtResult measure(const void *state, NtMeasureContext *context,
                        NtConstraints constraints, NtCellSize *out)
{
    CHECK(nt_measure_child_count(context) == 0);
    const NtCellSize *size = state;
    *out = (NtCellSize){fit(size->columns, constraints.columns), fit(size->rows, constraints.rows)};
    return NT_RESULT_OK;
}

static NtResult layout(const void *state, NtLayoutContext *context, NtCellSize assigned)
{
    (void)state;
    (void)assigned;
    CHECK(nt_layout_child_count(context) == 0);
    return NT_RESULT_OK;
}

static NtResult paint(const void *state, NtPaintContext *context, NtCellSize assigned)
{
    (void)state;
    return nt_paint_fill(context, (NtCellRect){{0,0},assigned}, (NtRgba){10,20,30,255});
}

static const NtComponentOps leaf_ops = {measure,layout,NULL,paint,NULL,NULL};
static const NtMountOptions container = {true,false,false};
static const NtMountOptions leaf = {true,false,true};
static const NtNodeId zero = {0};

static void bounds(const NtTree *tree, NtNodeId id, int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    NtNodeGeometry geometry;
    CHECK(nt_tree_geometry(tree, id, &geometry) == NT_RESULT_OK);
    CHECK(geometry.placed && geometry.bounds.origin.x == x && geometry.bounds.origin.y == y);
    CHECK(geometry.bounds.size.columns == w && geometry.bounds.size.rows == h);
}

static void absent(const NtTree *tree, NtNodeId id)
{
    NtNodeGeometry geometry;
    CHECK(nt_tree_geometry(tree, id, &geometry) == NT_RESULT_OK && !geometry.placed);
}

static void hit(const NtTree *tree, int32_t x, int32_t y, NtNodeId expected)
{
    NtNodeId node;
    CHECK(nt_tree_hit(tree, (NtCellPoint){x,y}, &node) == NT_RESULT_OK);
    CHECK(node.tree == expected.tree && node.slot == expected.slot &&
          node.generation == expected.generation);
}

static void row_reflows_with_owned_options(void)
{
    NtTree *tree;
    CHECK(nt_tree_create(NULL, &tree) == NT_RESULT_OK);
    NtNodeId row, a, b, c;
    NtBoxItem items[] = {
        {.columns={2,2,true}},
        {.columns={2,5,true},.grow_columns=true},
        {.columns={1,4,true},.grow_columns=true}
    };
    const NtBoxLayout options = {.padding={1,1,1,1},.gap=1,.horizontal=NT_BOX_CENTER,
        .vertical=NT_BOX_END,.items=items,.item_count=3};
    CHECK(nt_box_layout_mount(tree, zero, container, &options, &row) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree, (NtCellSize){6,4}) == NT_RESULT_OK);
    items[0].columns = (NtExtentRange){12,12,true};
    NtCellSize first = {9,1}, second = {3,2}, third = {2,1};
    CHECK(nt_tree_mount(tree, row, &leaf_ops, &first, leaf, &a) == NT_RESULT_OK);
    CHECK(nt_tree_mount(tree, row, &leaf_ops, &second, leaf, &b) == NT_RESULT_OK);
    CHECK(nt_tree_mount(tree, row, &leaf_ops, &third, leaf, &c) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree, (NtCellSize){15,5}) == NT_RESULT_OK);
    bounds(tree, a, 1,3,2,1);
    bounds(tree, b, 4,2,5,2);
    bounds(tree, c, 10,3,4,1);
    hit(tree, 8,3,b);
    hit(tree, 9,3,zero);

    CHECK(nt_tree_publish(tree, (NtCellSize){9,4}) == NT_RESULT_OK);
    bounds(tree, a, 1,2,2,1);
    bounds(tree, b, 4,1,2,2);
    bounds(tree, c, 7,2,1,1);
    CHECK(nt_tree_publish(tree, (NtCellSize){6,4}) == NT_RESULT_OK);
    bounds(tree, b, 4,1,2,2);
    absent(tree, c);
    hit(tree, 4,1,b);
    hit(tree, 5,1,zero);
    NtCellBackground background;
    CHECK(nt_frame_background(nt_tree_frame(tree), (NtCellPoint){5,1}, &background) == NT_RESULT_OK);
    CHECK(background.color.alpha == 0);

    const NtFrame *frame = nt_tree_frame(tree);
    items[1].columns.min = 6;
    CHECK(nt_box_layout_set(tree, row, &options) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(nt_tree_frame(tree) == frame && nt_tree_pending(tree) == NT_INVALIDATE_NONE);
    CHECK(nt_tree_publish(tree, (NtCellSize){2,4}) == NT_RESULT_OK);
    absent(tree, a);
    absent(tree, b);
    CHECK(nt_tree_publish(tree, (NtCellSize){15,5}) == NT_RESULT_OK);
    bounds(tree, a, 1,3,2,1);
    bounds(tree, b, 4,2,5,2);
    nt_tree_destroy(tree);
}

static void change_kind_preserves_children_and_order(void)
{
    NtTree *tree;
    CHECK(nt_tree_create(NULL, &tree) == NT_RESULT_OK);
    NtNodeId root, a, b;
    const NtBoxLayout column = {.kind=NT_BOX_LAYOUT_COLUMN,.padding={1,1,1,1},.gap=1,
        .horizontal=NT_BOX_CENTER,.vertical=NT_BOX_CENTER};
    CHECK(nt_box_layout_mount(tree, zero, container, &column, &root) == NT_RESULT_OK);
    NtCellSize first = {3,1}, second = {1,2};
    CHECK(nt_tree_mount(tree, root, &leaf_ops, &first, leaf, &a) == NT_RESULT_OK);
    CHECK(nt_tree_mount(tree, root, &leaf_ops, &second, leaf, &b) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree, (NtCellSize){7,9}) == NT_RESULT_OK);
    bounds(tree, a, 2,2,3,1);
    bounds(tree, b, 3,4,1,2);
    CHECK(nt_box_layout_set(tree, a, &column) == NT_RESULT_INVALID_ARGUMENT);

    NtBoxLayout stack = column;
    stack.kind = NT_BOX_LAYOUT_STACK;
    stack.fallback = (NtBoxItem){.columns={2,4,true},.grow_columns=true};
    CHECK(nt_box_layout_set(tree, root, &stack) == NT_RESULT_OK);
    CHECK(nt_tree_pending(tree) == NT_INVALIDATE_LAYOUT);
    bounds(tree, b, 3,4,1,2);
    CHECK(nt_tree_publish(tree, (NtCellSize){7,9}) == NT_RESULT_OK);
    bounds(tree, a, 1,4,4,1);
    bounds(tree, b, 1,3,4,2);
    hit(tree, 3,4,b);
    CHECK(nt_tree_remove(tree, b) == NT_RESULT_OK);
    CHECK(nt_box_layout_set(tree, b, &stack) == NT_RESULT_STALE_REFERENCE);
    CHECK(nt_tree_publish(tree, (NtCellSize){7,9}) == NT_RESULT_OK);
    hit(tree, 3,4,a);
    nt_tree_destroy(tree);
}

static void trailing_edge_omits_zero_extent_children(void)
{
    NtTree *tree;
    CHECK(nt_tree_create(NULL, &tree) == NT_RESULT_OK);
    NtNodeId root, a, b, c;
    const NtBoxItem items[] = {
        {.columns={5,5,true}},
        {.columns={0,0,true}},
        {.rows={0,0,true}}
    };
    NtBoxLayout config = {.kind=NT_BOX_LAYOUT_STACK,.padding={1,1,1,1},
        .horizontal=NT_BOX_END,.vertical=NT_BOX_END,.items=items,.item_count=3};
    NtCellSize preferred = {2,1};
    CHECK(nt_box_layout_mount(tree, zero, container, &config, &root) == NT_RESULT_OK);
    CHECK(nt_tree_mount(tree, root, &leaf_ops, &preferred, leaf, &a) == NT_RESULT_OK);
    CHECK(nt_tree_mount(tree, root, &leaf_ops, &preferred, leaf, &b) == NT_RESULT_OK);
    CHECK(nt_tree_mount(tree, root, &leaf_ops, &preferred, leaf, &c) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree, (NtCellSize){5,4}) == NT_RESULT_OK);
    bounds(tree, a, 1,2,5,1);
    absent(tree, b);
    absent(tree, c);

    config.kind = NT_BOX_LAYOUT_ROW;
    config.items = &items[2];
    config.item_count = 1;
    CHECK(nt_box_layout_set(tree, root, &config) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree, (NtCellSize){5,4}) == NT_RESULT_OK);
    absent(tree, a);
    nt_tree_destroy(tree);
}

typedef struct Heap { size_t calls, fail_at, live; } Heap;

static void *allocate(void *context, size_t bytes, size_t alignment)
{
    (void)alignment;
    Heap *heap = context;
    if (++heap->calls == heap->fail_at) return NULL;
    void *pointer = malloc(bytes);
    if (pointer) ++heap->live;
    return pointer;
}

static void release(void *context, void *pointer, size_t bytes, size_t alignment)
{
    (void)bytes;
    (void)alignment;
    Heap *heap = context;
    CHECK(heap->live > 0);
    --heap->live;
    free(pointer);
}

static void nested_clip_and_allocation_failure(void)
{
    Heap heap = {0};
    const NtAllocator allocator = {&heap,allocate,release};
    NtTree *tree;
    CHECK(nt_tree_create(&allocator, &tree) == NT_RESULT_OK);
    NtNodeId row, stack, child;
    const NtBoxLayout outer = {.padding={1,0,1,0},.fallback={.columns={6,6,true}}};
    const NtBoxLayout inner = {.kind=NT_BOX_LAYOUT_STACK,.padding={0,1,0,0},
        .fallback={.columns={6,6,true}}};
    NtCellSize preferred = {6,2};
    CHECK(nt_box_layout_mount(tree, zero, container, &outer, &row) == NT_RESULT_OK);
    CHECK(nt_box_layout_mount(tree, row, container, &inner, &stack) == NT_RESULT_OK);
    CHECK(nt_tree_mount(tree, stack, &leaf_ops, &preferred, leaf, &child) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree, (NtCellSize){6,4}) == NT_RESULT_OK);
    bounds(tree, child, 1,1,6,2);
    hit(tree, 4,2,child);
    hit(tree, 5,2,zero);

    const NtFrame *frame = nt_tree_frame(tree);
    const size_t live = heap.live;
    NtBoxLayout update = outer;
    update.padding.left = 2;
    heap.fail_at = heap.calls + 1;
    CHECK(nt_box_layout_set(tree, row, &update) == NT_RESULT_ALLOCATION_FAILED);
    CHECK(nt_tree_frame(tree) == frame && heap.live == live);
    CHECK(nt_tree_pending(tree) == NT_INVALIDATE_NONE);
    CHECK(nt_box_layout_set(tree, row, &update) == NT_RESULT_OK);
    CHECK(nt_tree_pending(tree) == NT_INVALIDATE_LAYOUT);
    unsigned failures = 0;
    for (size_t skip = 1; skip <= 32; ++skip) {
        heap.fail_at = heap.calls + skip;
        NtResult result = nt_tree_publish(tree, (NtCellSize){7,4});
        if (result == NT_RESULT_OK) break;
        CHECK(result == NT_RESULT_ALLOCATION_FAILED);
        CHECK(nt_tree_frame(tree) == frame && heap.live == live);
        CHECK(nt_tree_pending(tree) == NT_INVALIDATE_LAYOUT);
        bounds(tree, child, 1,1,6,2);
        ++failures;
    }
    CHECK(failures >= 5 && failures < 32 && nt_tree_pending(tree) == NT_INVALIDATE_NONE);
    bounds(tree, child, 2,1,6,2);
    hit(tree, 5,2,child);
    hit(tree, 6,2,zero);
    nt_tree_destroy(tree);
    CHECK(heap.live == 0);
}

int main(void)
{
    row_reflows_with_owned_options();
    change_kind_preserves_children_and_order();
    trailing_edge_omits_zero_extent_children();
    nested_clip_and_allocation_failure();
    puts("Row, column, stack, sizing, alignment and padded clipping passed.");
    return EXIT_SUCCESS;
}
