#include <nocturne/viewport.h>

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "Viewport consumer line %d: %s\n", __LINE__, #condition); \
    exit(EXIT_FAILURE); \
} } while (0)

typedef struct Heap { bool fail_next; size_t live; } Heap;
typedef struct Content {
    NtCellSize preferred;
    bool decline_paint;
    size_t pointer_moves;
    NtCellPoint pointer_local;
} Content;

static const NtNodeId none = {0};
static const NtMountOptions container = {true,false,false}, interactive = {true,false,true};

static void *allocate(void *value, size_t bytes, size_t alignment)
{
    (void)alignment;
    Heap *heap = value;
    if (heap->fail_next) {
        heap->fail_next = false;
        return NULL;
    }
    void *block = malloc(bytes);
    if (block) ++heap->live;
    return block;
}

static void release(void *value, void *block, size_t bytes, size_t alignment)
{
    (void)bytes;
    (void)alignment;
    Heap *heap = value;
    CHECK(heap->live > 0);
    --heap->live;
    free(block);
}

static uint32_t fit(uint32_t desired, NtExtentRange range)
{
    if (desired < range.min) return range.min;
    return range.bounded && desired > range.max ? range.max : desired;
}

static NtResult measure(const void *value, NtMeasureContext *context,
                        NtConstraints constraints, NtCellSize *out)
{
    const Content *content = value;
    CHECK(nt_measure_child_count(context) == 0);
    *out = (NtCellSize){fit(content->preferred.columns, constraints.columns),
                        fit(content->preferred.rows, constraints.rows)};
    return NT_RESULT_OK;
}

static NtResult arrange(const void *value, NtLayoutContext *context, NtCellSize size)
{
    (void)value;
    (void)size;
    CHECK(nt_layout_child_count(context) == 0);
    return NT_RESULT_OK;
}

static NtResult paint(const void *value, NtPaintContext *context, NtCellSize size)
{
    (void)size;
    const Content *content = value;
    const NtCellStyle style = {{235,245,255,255},{0,0,0,0}};
    const NtByteView marker = {content->decline_paint ? "!" : ".",1};
    NtResult result = nt_paint_glyph(context, (NtCellPoint){0,3}, marker, 1, style);
    if (result != NT_RESULT_OK) return result;
    result = nt_paint_glyph(context, (NtCellPoint){4,3}, (NtByteView){"界",3}, 2, style);
    if (result != NT_RESULT_OK) return result;
    return content->decline_paint ? NT_RESULT_UNSUPPORTED_CAPABILITY : NT_RESULT_OK;
}

static NtResult event(void *value, NtEventContext *context, const NtEvent *input, NtEventReply *reply)
{
    (void)reply;
    Content *content = value;
    if (input->kind == NT_EVENT_POINTER_MOVE && nt_event_phase(context) == NT_EVENT_TARGET) {
        CHECK(nt_event_local_position(context,&content->pointer_local));
        ++content->pointer_moves;
    }
    return NT_RESULT_OK;
}

static const NtComponentOps content_ops = {measure, arrange, NULL, paint, event, NULL};

static bool same(NtNodeId a, NtNodeId b)
{
    return a.tree == b.tree && a.slot == b.slot && a.generation == b.generation;
}

static void hit(const NtTree *tree, NtCellPoint point, NtNodeId expected)
{
    NtNodeId actual;
    CHECK(nt_tree_hit(tree, point, &actual) == NT_RESULT_OK && same(actual, expected));
}

static void move_pointer(NtTree *tree, NtCellPoint point, NtNodeId expected)
{
    const NtEvent move = {.kind=NT_EVENT_POINTER_MOVE,.data.pointer={.position=point,.has_position=true}};
    NtDispatchResult delivery;
    CHECK(nt_tree_dispatch(tree,&move,(NtByteView){0},NULL,&delivery) == NT_RESULT_OK);
    CHECK(same(delivery.target,expected));
}

static NtViewportView viewport_view(const NtTree *tree, NtNodeId node)
{
    NtViewportView view;
    CHECK(nt_viewport_view(tree, node, &view) == NT_RESULT_OK);
    return view;
}

static void viewport_resize_commits_geometry_and_scroll_together(void)
{
    Heap heap = {0};
    const NtAllocator allocator = {&heap,allocate,release};
    NtTree *tree;
    CHECK(nt_tree_create(&allocator, &tree) == NT_RESULT_OK);
    const NtViewportOptions scroll = {.scroll_rows=true,.requested={0,4}};
    NtNodeId viewport, child;
    Content content = {.preferred={6,8}};
    heap.fail_next = true;
    CHECK(nt_viewport_mount(tree, none, container, scroll, &viewport) == NT_RESULT_ALLOCATION_FAILED);
    CHECK(heap.live == 1);
    CHECK(nt_viewport_mount(tree, none, container, scroll, &viewport) == NT_RESULT_OK);
    CHECK(nt_tree_mount(tree, viewport, &content_ops, &content, interactive, &child) == NT_RESULT_OK);
    CHECK(!viewport_view(tree, viewport).placed);
    CHECK(nt_tree_publish(tree, (NtCellSize){6,4}) == NT_RESULT_OK);
    NtViewportView view = viewport_view(tree, viewport);
    CHECK(view.placed && same(view.child,child) && view.effective.rows == 4 && view.content.rows == 8);
    NtNodeGeometry geometry;
    CHECK(nt_tree_geometry(tree, child, &geometry) == NT_RESULT_OK);
    CHECK(geometry.bounds.origin.y == -4 && geometry.clip.size.rows == 4);

    CHECK(nt_tree_publish(tree, (NtCellSize){6,6}) == NT_RESULT_OK);
    view = viewport_view(tree, viewport);
    CHECK(view.viewport.rows == 6 && view.effective.rows == 2 && same(view.child,child));
    const NtFrame *frame = nt_tree_frame(tree);
    const NtCellPoint lead = {4,1}, continuation = {5,1};
    NtCellView cell;
    CHECK(nt_frame_cell(frame, continuation, &cell) == NT_RESULT_OK);
    CHECK(cell.lead.x == 4 && cell.lead.y == 1 && cell.owner == nt_node_owner(child) &&
          cell.glyph_columns == 2 && cell.glyph.count == 3);
    hit(tree, continuation, child);
    move_pointer(tree, continuation, child);
    CHECK(nt_viewport_set(tree, viewport, scroll) == NT_RESULT_OK);
    const size_t live = heap.live;
    heap.fail_next = true;
    CHECK(nt_tree_publish(tree, (NtCellSize){5,6}) == NT_RESULT_ALLOCATION_FAILED);
    CHECK(heap.live == live && nt_tree_frame(tree) == frame && viewport_view(tree, viewport).effective.rows == 2);
    hit(tree, continuation, child);
    move_pointer(tree, continuation, child);

    content.decline_paint = true;
    CHECK(nt_tree_publish(tree, (NtCellSize){5,6}) == NT_RESULT_UNSUPPORTED_CAPABILITY);
    CHECK(heap.live == live && nt_tree_frame(tree) == frame && nt_tree_pending(tree) == NT_INVALIDATE_LAYOUT);
    CHECK(viewport_view(tree, viewport).viewport.columns == 6 && viewport_view(tree, viewport).effective.rows == 2);
    CHECK(nt_tree_geometry(tree, child, &geometry) == NT_RESULT_OK && geometry.clip.size.columns == 6);
    hit(tree, continuation, child);
    move_pointer(tree, continuation, child);
    CHECK(content.pointer_moves == 3 && content.pointer_local.x == 5 && content.pointer_local.y == 3);
    CHECK(cell.glyph.data[0] == "界"[0]);
    NtCellView retained;
    CHECK(nt_frame_cell(frame, (NtCellPoint){0,1}, &retained) == NT_RESULT_OK);
    CHECK(retained.glyph.count == 1 && retained.glyph.data[0] == '.');

    content.decline_paint = false;
    CHECK(nt_tree_publish(tree, (NtCellSize){5,6}) == NT_RESULT_OK);
    view = viewport_view(tree, viewport);
    CHECK(same(view.child,child) && view.effective.rows == 2 && view.viewport.columns == 5);
    hit(tree, continuation, none);
    move_pointer(tree, continuation, none);
    CHECK(content.pointer_moves == 3);
    CHECK(nt_frame_cell(nt_tree_frame(tree), lead, &cell) == NT_RESULT_OK && !cell.glyph_columns);
    CHECK(nt_tree_publish(tree, (NtCellSize){0,6}) == NT_RESULT_OK);
    view = viewport_view(tree, viewport);
    CHECK(view.placed && same(view.child,none) && view.viewport.rows == 6 && !view.effective.rows);
    CHECK(nt_frame_size(nt_tree_frame(tree)).columns == 0);
    hit(tree, lead, none);
    nt_tree_destroy(tree);
    CHECK(!heap.live);
}

static void nested_viewports_and_requested_scroll(void)
{
    NtTree *tree;
    CHECK(nt_tree_create(NULL, &tree) == NT_RESULT_OK);
    NtNodeId outer, inner, child, extra;
    const NtViewportOptions vertical = {.scroll_rows=true,.requested={0,2}};
    const NtViewportOptions horizontal = {.scroll_columns=true,.requested={3,99}};
    Content state = {.preferred={12,8}};
    CHECK(nt_viewport_mount(tree, none, container, vertical, &outer) == NT_RESULT_OK);
    CHECK(nt_viewport_mount(tree, outer, container, horizontal, &inner) == NT_RESULT_OK);
    CHECK(nt_tree_mount(tree, inner, &content_ops, &state, interactive, &child) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree, (NtCellSize){6,6}) == NT_RESULT_OK);
    NtViewportView view = viewport_view(tree, inner);
    CHECK(same(view.child,child) && view.viewport.rows == 8 && view.content.columns == 12);
    CHECK(view.effective.columns == 3 && view.effective.rows == 0);
    NtNodeGeometry geometry;
    CHECK(nt_tree_geometry(tree, child, &geometry) == NT_RESULT_OK);
    CHECK(geometry.bounds.origin.x == -3 && geometry.bounds.origin.y == -2);
    CHECK(geometry.clip.size.columns == 6 && geometry.clip.size.rows == 6);
    hit(tree, (NtCellPoint){0,0}, child);
    hit(tree, (NtCellPoint){6,0}, none);

    CHECK(nt_viewport_set(tree, inner, (NtViewportOptions){0}) == NT_RESULT_OK);
    CHECK(viewport_view(tree, inner).effective.columns == 3);
    CHECK(nt_tree_mount(tree, inner, &content_ops, &state, interactive, &extra) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree, (NtCellSize){6,6}) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(viewport_view(tree, inner).effective.columns == 3);
    CHECK(nt_tree_remove(tree, extra) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree, (NtCellSize){6,6}) == NT_RESULT_OK);
    CHECK(!viewport_view(tree, inner).effective.columns);
    CHECK(nt_tree_remove(tree, child) == NT_RESULT_OK);
    CHECK(same(viewport_view(tree, inner).child,none));
    CHECK(nt_viewport_view(tree, child, &view) == NT_RESULT_STALE_REFERENCE);
    nt_tree_destroy(tree);
}

static void scroll_and_anchor_limits(void)
{
    const NtViewportOptions scroll = {true,true,{UINT32_MAX,UINT32_MAX}};
    const NtCellSize resolved = nt_scroll_resolve(scroll, (NtCellSize){20,0}, (NtCellSize){12,UINT32_MAX});
    CHECK(resolved.columns == 0 && resolved.rows == UINT32_MAX);
    NtCellRect rect;
    CHECK(nt_anchor_rect((NtCellSize){3,4}, (NtCellSize){6,1}, NT_BOX_CENTER,
                         NT_BOX_END, (NtCellPoint){1,-2}, &rect) == NT_RESULT_OK);
    CHECK(rect.origin.x == -1 && rect.origin.y == 1 && rect.size.columns == 6);
    CHECK(nt_anchor_rect((NtCellSize){UINT32_MAX,0}, (NtCellSize){0}, NT_BOX_END,
                         NT_BOX_START, (NtCellPoint){0}, &rect) == NT_RESULT_OUT_OF_RANGE);
    CHECK(rect.origin.x == -1 && rect.origin.y == 1 && rect.size.columns == 6);
}

int main(void)
{
    viewport_resize_commits_geometry_and_scroll_together();
    nested_viewports_and_requested_scroll();
    scroll_and_anchor_limits();
    puts("Clipped viewport, scroll publication, anchor and resize passed.");
    return EXIT_SUCCESS;
}
