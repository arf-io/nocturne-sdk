#include "indicator.h"

#include <nocturne/widgets.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr,"Installed composition line %d: %s\n",__LINE__,#condition); \
    exit(EXIT_FAILURE); \
} } while (0)

typedef struct Heap { size_t attempts, fail_at, live; } Heap;

static void *allocate(void *value, size_t bytes, size_t alignment)
{
    (void)alignment;
    Heap *heap = value;
    if (++heap->attempts == heap->fail_at) return NULL;
    void *block = malloc(bytes);
    if (block) ++heap->live;
    return block;
}

static void release(void *value, void *block, size_t bytes, size_t alignment)
{
    (void)bytes;
    (void)alignment;
    Heap *heap = value;
    CHECK(heap->live);
    --heap->live;
    free(block);
}

static const NtNodeId none = {0};
static const NtMountOptions visible = {true,false,true};
static const NtCellStyle ink = {{210,225,250,255},{0,0,0,0}};

static NtLabelOptions label(NtByteView text)
{
    return (NtLabelOptions){text,{{NT_WIDTH_ONE,NT_WIDTH_TWO},NT_WRAP_WORD,NT_TEXT_ALIGN_START,
        NT_DIRECTION_AUTO,4,true,ink}};
}

static void glyph(const NtTree *tree, int32_t x, int32_t y, const char *expected, NtNodeId owner)
{
    NtCellView cell;
    CHECK(nt_frame_cell(nt_tree_frame(tree),(NtCellPoint){x,y},&cell) == NT_RESULT_OK);
    const size_t count = strlen(expected);
    if (cell.glyph.count != count || cell.owner != nt_node_owner(owner))
        fprintf(stderr,"Cell (%d,%d): expected %s at slot %u, got %zu glyph bytes and owner %llu\n",
                (int)x,(int)y,expected,(unsigned)owner.slot,cell.glyph.count,
                (unsigned long long)cell.owner);
    CHECK(cell.glyph.count == count && cell.owner == nt_node_owner(owner));
    CHECK(!count || memcmp(expected,cell.glyph.data,count) == 0);
}

static void region(const NtTree *tree, NtNodeId node, NtNodeId parent, int32_t x, int32_t y)
{
    NtNodeGeometry geometry;
    CHECK(nt_tree_geometry(tree,node,&geometry) == NT_RESULT_OK && geometry.placed);
    CHECK(geometry.parent.slot == parent.slot && geometry.parent.tree == parent.tree &&
          geometry.parent.generation == parent.generation);
    CHECK(geometry.bounds.origin.x == x && geometry.bounds.origin.y == y);
}

static void two_compositions_and_lifetimes(void)
{
    Heap heap = {0};
    const NtAllocator allocator = {&heap,allocate,release};
    NtTree *tree;
    CHECK(nt_tree_create(&allocator,&tree) == NT_RESULT_OK);
    NtNodeId root, panel, well, list, stack, text, rule, meter;
    const NtBoxItem rows[] = {{.rows={5,5,true},.grow_columns=true},
                             {.rows={3,3,true},.grow_columns=true}};
    const NtBoxLayout column = {.kind=NT_BOX_LAYOUT_COLUMN,.gap=1,.items=rows,.item_count=2};
    CHECK(nt_box_layout_mount(tree,none,visible,&column,&root) == NT_RESULT_OK);

    char heading[] = "Lab";
    NtRowSpan spans[] = {{{0,0},4},{{0,1},12},{{0,2},1}};
    const NtClipMask mask = {spans,3};
    const NtPanelOptions framed = {.fill={12,25,38,255},.framed=true,.border=ink,
        .title=label((NtByteView){heading,3}),.body_mask=&mask};
    const NtPanelOptions box = {.fill={65,33,48,255}};
    CHECK(nt_panel_mount(tree,root,visible,&framed,&panel) == NT_RESULT_OK);
    CHECK(nt_panel_mount(tree,root,visible,&box,&well) == NT_RESULT_OK);
    heading[0] = 'X';
    spans[0].columns = 1;

    const NtBoxLayout body = {.kind=NT_BOX_LAYOUT_COLUMN,.fallback={.grow_columns=true}};
    const NtBoxLayout center = {.kind=NT_BOX_LAYOUT_STACK,.horizontal=NT_BOX_CENTER,.vertical=NT_BOX_CENTER};
    CHECK(nt_box_layout_mount(tree,panel,visible,&body,&list) == NT_RESULT_OK);
    CHECK(nt_box_layout_mount(tree,well,visible,&center,&stack) == NT_RESULT_OK);

    char words[] = "café";
    const NtLabelOptions initial = label((NtByteView){words,sizeof(words)-1});
    CHECK(nt_label_mount(tree,list,visible,&initial,&text) == NT_RESULT_OK);
    words[0] = 'z';
    CHECK(nt_rule_mount(tree,list,visible,(NtRuleOptions){.color=ink},&rule) == NT_RESULT_OK);
    IndicatorStats meter_stats = {0}, replacement_stats = {0};
    CHECK(indicator_mount(tree,list,&meter_stats,&meter) == NT_RESULT_OK);

    CHECK(nt_tree_tick(tree,100,NT_MOTION_FULL) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){14,10}) == NT_RESULT_OK);
    glyph(tree,0,0,"┌",panel);
    glyph(tree,1,0,"L",panel);
    glyph(tree,1,1,"c",text);
    glyph(tree,4,1,"é",text);
    glyph(tree,1,2,"─",rule);
    glyph(tree,1,3,"-",meter);
    region(tree,meter,list,1,3);
    NtCellBackground cell;
    CHECK(nt_frame_background(nt_tree_frame(tree),(NtCellPoint){1,6},&cell) == NT_RESULT_OK);
    CHECK(cell.color.red == 65 && cell.owner == nt_node_owner(well));

    const NtFrame *old_frame = nt_tree_frame(tree);
    const size_t mounted_blocks = heap.live;
    const NtLabelOptions revised = label((NtByteView){"Done",4});
    const NtLabelOptions invalid = label((NtByteView){"\xff",1});
    CHECK(nt_label_set(tree,text,&invalid) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(heap.live == mounted_blocks && nt_tree_pending(tree) == NT_INVALIDATE_NONE);
    heap.fail_at = heap.attempts + 2;
    CHECK(nt_panel_set(tree,panel,&framed) == NT_RESULT_ALLOCATION_FAILED);
    CHECK(heap.live == mounted_blocks && nt_tree_frame(tree) == old_frame);
    CHECK(nt_tree_pending(tree) == NT_INVALIDATE_NONE);
    CHECK(nt_label_set(tree,text,&revised) == NT_RESULT_OK);
    CHECK(nt_tree_frame(tree) == old_frame && nt_tree_pending(tree) == NT_INVALIDATE_LAYOUT);
    glyph(tree,1,1,"c",text);

    const NtBoxLayout row = {.kind=NT_BOX_LAYOUT_ROW,.gap=1,
        .fallback={.columns={6,6,true},.grow_rows=true}};
    CHECK(nt_box_layout_set(tree,root,&row) == NT_RESULT_OK);
    CHECK(nt_tree_move(tree,meter,stack) == NT_RESULT_OK);
    CHECK(nt_rule_set(tree,rule,(NtRuleOptions){.ascii=true,.color=ink}) == NT_RESULT_OK);
    region(tree,meter,list,1,3);
    CHECK(nt_tree_tick(tree,110,NT_MOTION_FULL) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){14,6}) == NT_RESULT_OK);
    region(tree,meter,stack,9,2);
    region(tree,text,list,1,1);
    glyph(tree,9,2,"+",meter);
    glyph(tree,1,1,"D",text);
    glyph(tree,1,2,"-",rule);

    CHECK(nt_tree_set_focus(tree,meter) == NT_RESULT_OK && meter_stats.focus_gains == 1);
    CHECK(nt_tree_remove(tree,meter) == NT_RESULT_OK && meter_stats.disposed == 1);
    CHECK(meter_stats.focus_losses == 1);
    CHECK(nt_tree_set_focus(tree,meter) == NT_RESULT_STALE_REFERENCE);
    NtNodeId replacement;
    CHECK(indicator_mount(tree,stack,&replacement_stats,&replacement) == NT_RESULT_OK);
    CHECK(replacement.slot == meter.slot && replacement.generation != meter.generation);
    CHECK(nt_tree_tick(tree,120,NT_MOTION_FULL) == NT_RESULT_OK);
    CHECK(meter_stats.updates == 2 && replacement_stats.updates == 1);
    CHECK(nt_tree_publish(tree,(NtCellSize){14,6}) == NT_RESULT_OK);
    glyph(tree,9,2,"-",replacement);
    nt_tree_destroy(tree);
    CHECK(!heap.live && meter_stats.disposed == 1 && replacement_stats.disposed == 1);
}

static void stock_resize_and_style_changes(void)
{
    NtTree *tree;
    NtNodeId node;
    CHECK(nt_tree_create(NULL,&tree) == NT_RESULT_OK);
    NtLabelOptions wrapped = label((NtByteView){"abcd",4});
    wrapped.style.wrap = NT_WRAP_CLUSTER;
    CHECK(nt_label_mount(tree,none,visible,&wrapped,&node) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){2,2}) == NT_RESULT_OK);
    glyph(tree,0,0,"a",node);
    glyph(tree,0,1,"c",node);
    CHECK(nt_tree_publish(tree,(NtCellSize){4,1}) == NT_RESULT_OK);
    glyph(tree,3,0,"d",node);
    CHECK(nt_tree_remove(tree,node) == NT_RESULT_OK);

    NtPanelOptions panel = {.framed=true,.ascii=true,.border=ink};
    CHECK(nt_panel_mount(tree,none,visible,&panel,&node) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){5,2}) == NT_RESULT_OK);
    glyph(tree,0,0,"+",node);
    glyph(tree,2,0,"-",node);
    panel.title = label((NtByteView){"B",1});
    panel.title.style.align = NT_TEXT_ALIGN_CENTER;
    CHECK(nt_panel_set(tree,node,&panel) == NT_RESULT_OK);
    glyph(tree,2,0,"-",node);
    CHECK(nt_tree_publish(tree,(NtCellSize){5,2}) == NT_RESULT_OK);
    glyph(tree,2,0,"B",node);
    CHECK(nt_tree_publish(tree,(NtCellSize){1,1}) == NT_RESULT_OK);
    glyph(tree,0,0,"+",node);
    CHECK(nt_tree_remove(tree,node) == NT_RESULT_OK);

    CHECK(nt_rule_mount(tree,none,visible,(NtRuleOptions){.vertical=true,.color=ink},&node)
          == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){3,4}) == NT_RESULT_OK);
    glyph(tree,0,3,"│",node);
    glyph(tree,1,3,"",none);
    nt_tree_destroy(tree);
}

int main(void)
{
    two_compositions_and_lifetimes();
    stock_resize_and_style_changes();
    puts("Separately compiled custom indicator, stock components, two compositions and disposal passed.");
    return EXIT_SUCCESS;
}
