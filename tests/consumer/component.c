#include "indicator.h"
#include <nocturne/layout.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(condition) do { if (!(condition)) { \
    fprintf(stderr, "Component consumer line %d: %s\n", __LINE__, #condition); \
    goto done; \
} } while (0)

static NtResult center_measure(const void *state, NtMeasureContext *context,
                               NtConstraints constraints, NtCellSize *out)
{
    (void)state;
    NtNodeId child;
    NtResult result = nt_measure_child_at(context, 0, &child);
    if (result != NT_RESULT_OK) return result;
    return nt_measure_child(context, child, constraints, out);
}

static NtResult center_layout(const void *state, NtLayoutContext *context, NtCellSize assigned)
{
    (void)state;
    NtNodeId child;
    NtResult result = nt_layout_child_at(context, 0, &child);
    if (result != NT_RESULT_OK) return result;
    NtCellSize size;
    result = nt_layout_child_size(context, child, &size);
    if (result != NT_RESULT_OK) return result;
    if (size.columns > assigned.columns) size.columns = assigned.columns;
    if (size.rows > assigned.rows) size.rows = assigned.rows;
    const NtCellRect bounds = {
        {(int32_t)((assigned.columns - size.columns) / 2),
         (int32_t)((assigned.rows - size.rows) / 2)}, size
    };
    return nt_layout_place(context, child, bounds, 0, NULL);
}

static NtResult center_paint(const void *state, NtPaintContext *context, NtCellSize assigned)
{
    (void)state;
    (void)context;
    (void)assigned;
    return NT_RESULT_OK;
}

int main(int argc, char **argv)
{
    NtTree *tree;
    if (nt_tree_create(NULL, &tree) != NT_RESULT_OK) return EXIT_FAILURE;
    int result = EXIT_FAILURE;
    IndicatorStats indicator = {0};
    NtNodeId root, node;
    const NtComponentOps center = {center_measure,center_layout,NULL,center_paint,NULL,NULL};
    const NtMountOptions options = {true,false,true};
    const NtCellSize window = {3,1};
    NtCellView cell;
    const NtCellPoint first = {1,0}, second = {2,0};
    NtNodeGeometry geometry;
    if (argc > 1 && !strcmp(argv[1], "stack")) {
        const NtBoxLayout stack = {.kind=NT_BOX_LAYOUT_STACK,.horizontal=NT_BOX_CENTER,
            .vertical=NT_BOX_CENTER};
        REQUIRE(nt_box_layout_mount(tree, (NtNodeId){0}, options, &stack, &root) == NT_RESULT_OK);
    } else {
        REQUIRE(nt_tree_mount(tree, (NtNodeId){0}, &center, NULL, options, &root) == NT_RESULT_OK);
    }
    REQUIRE(indicator_mount(tree, root, &indicator, &node) == NT_RESULT_OK);
    REQUIRE(nt_tree_tick(tree, 100, NT_MOTION_FULL) == NT_RESULT_OK);
    const NtDeadline deadline = nt_tree_next_update(tree);
    REQUIRE(deadline.scheduled && deadline.at == 110);
    REQUIRE(nt_tree_publish(tree, window) == NT_RESULT_OK);
    const NtFrame *frame = nt_tree_frame(tree);
    REQUIRE(nt_frame_cell(frame, first, &cell) == NT_RESULT_OK);
    REQUIRE(cell.owner == nt_node_owner(node) && cell.glyph.count == 1 && cell.glyph.data[0] == '-');

    REQUIRE(nt_tree_tick(tree, 110, NT_MOTION_FULL) == NT_RESULT_OK);
    REQUIRE(nt_tree_pending(tree) == NT_INVALIDATE_PAINT);
    const NtCellSize resized = {5,1};
    indicator.defer_paint = true;
    REQUIRE(nt_tree_publish(tree, resized) == NT_RESULT_UNSUPPORTED_CAPABILITY);
    REQUIRE(nt_tree_frame(tree) == frame && nt_tree_pending(tree) == NT_INVALIDATE_PAINT);
    REQUIRE(nt_tree_geometry(tree, node, &geometry) == NT_RESULT_OK);
    REQUIRE(geometry.placed && geometry.bounds.origin.x == first.x && cell.glyph.data[0] == '-');

    indicator.defer_paint = false;
    REQUIRE(nt_tree_publish(tree, resized) == NT_RESULT_OK);
    REQUIRE(nt_tree_geometry(tree, node, &geometry) == NT_RESULT_OK);
    REQUIRE(geometry.placed && geometry.bounds.origin.x == second.x);
    REQUIRE(nt_frame_cell(nt_tree_frame(tree), second, &cell) == NT_RESULT_OK);
    REQUIRE(cell.owner == nt_node_owner(node) && cell.glyph.count == 1 && cell.glyph.data[0] == '+');
    result = EXIT_SUCCESS;

done:
    nt_tree_destroy(tree);
    return indicator.disposed == 1 ? result : EXIT_FAILURE;
}
