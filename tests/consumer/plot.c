#include <nocturne/plot.h>
#include <nocturne/property.h>

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr,"plot line %d: %s\n",__LINE__,#e); exit(EXIT_FAILURE); } } while (0)
#define OK(e) CHECK((e) == NT_RESULT_OK)

static const NtRgba red = {255,0,0,255}, green = {0,255,0,255};
static const NtMountOptions enabled = {.enabled=true};

static void same(NtPoint actual, double x, double y)
{
    CHECK(fabs(actual.x-x) < 1e-12 && fabs(actual.y-y) < 1e-12);
}

static void geometry(void)
{
    NtPlotViewport view = {.domain={{-2,6},{1,5}},.cells={{-3,7},{20,8}}};
    NtPoint out;
    OK(nt_plot_project(&view,(NtPoint){-2,1},&out)); same(out,-3,15);
    OK(nt_plot_project(&view,(NtPoint){6,5},&out)); same(out,17,7);
    OK(nt_plot_project(&view,(NtPoint){0,4},&out)); same(out,2,9);
    view.equal_units = true;
    OK(nt_plot_project(&view,(NtPoint){-2,1},&out)); same(out,-3,13.5);
    OK(nt_plot_project(&view,(NtPoint){6,5},&out)); same(out,17,8.5);
    view.cell_aspect = 1;
    OK(nt_plot_project(&view,(NtPoint){-2,1},&out)); same(out,-1,15);
    OK(nt_plot_project(&view,(NtPoint){6,5},&out)); same(out,15,7);
    view.domain.x = (NtPlotDomain){4,4};
    OK(nt_plot_project(&view,(NtPoint){4,4},&out)); same(out,7,9);
    CHECK(nt_plot_project(&view,(NtPoint){5,4},&out) == NT_RESULT_OUT_OF_RANGE); same(out,7,9);
    view.domain = (NtPlotBounds){{-DBL_MAX,DBL_MAX},{-DBL_MAX,DBL_MAX}};
    OK(nt_plot_project(&view,(NtPoint){0,0},&out)); same(out,7,11);
    view.domain = (NtPlotBounds){{0,0x1p-1073},{0,0x1p-1073}};
    view.equal_units = false;
    OK(nt_plot_project(&view,(NtPoint){0x1p-1074,0x1p-1074},&out)); same(out,7,11);
    view.cell_aspect = NAN;
    CHECK(nt_plot_project(&view,(NtPoint){0,0},&out) == NT_RESULT_INVALID_ARGUMENT); same(out,7,11);

    bool visible = false;
    NtPoint a, b;
    const NtPlotBounds domain = {{-1,1},{-2,2}};
    OK(nt_plot_clip_segment(domain,(NtPoint){-4,-3},(NtPoint){4,1},&visible,&a,&b));
    CHECK(visible); same(a,-1,-1.5); same(b,1,-.5);
    OK(nt_plot_clip_segment(domain,(NtPoint){4,1},(NtPoint){-4,-3},&visible,&a,&b));
    CHECK(visible); same(a,1,-.5); same(b,-1,-1.5);
    OK(nt_plot_clip_segment(domain,(NtPoint){-DBL_MAX,-DBL_MAX},(NtPoint){DBL_MAX,DBL_MAX},&visible,&a,&b));
    CHECK(visible); same(a,-1,-1); same(b,1,1);
    OK(nt_plot_clip_segment(domain,(NtPoint){-7,3},(NtPoint){9,3},&visible,&a,&b));
    CHECK(!visible); same(a,-1,-1); same(b,1,1);
    OK(nt_plot_clip_segment(domain,(NtPoint){1,2},(NtPoint){4,9},&visible,&a,&b));
    CHECK(visible); same(a,1,2); same(b,1,2);
    const NtPlotBounds constant = {{1,1},{-2,2}};
    OK(nt_plot_clip_segment(constant,(NtPoint){-7,0},(NtPoint){9,0},&visible,&a,&b));
    CHECK(visible); same(a,1,0); same(b,1,0);
    const NtPlotBounds corner = {{0,4},{0,4}};
    OK(nt_plot_clip_segment(corner,(NtPoint){2,-10},(NtPoint){-1,5},&visible,&a,&b));
    CHECK(visible && a.x == 0 && a.y == 0 && b.x == 0 && b.y == 0);
    OK(nt_plot_clip_segment(corner,(NtPoint){-1,5},(NtPoint){2,-10},&visible,&a,&b));
    CHECK(visible && a.x == 0 && a.y == 0 && b.x == 0 && b.y == 0);
    OK(nt_plot_clip_segment(corner,(NtPoint){2,nextafter(-10,-INFINITY)},(NtPoint){-1,5},&visible,&a,&b));
    CHECK(!visible);
}

static NtCellView cell(const NtTree *tree, int32_t x, int32_t y)
{
    NtCellView result;
    OK(nt_frame_cell(nt_tree_frame(tree),(NtCellPoint){x,y},&result));
    return result;
}

static bool marked(const NtTree *tree, int32_t x, int32_t y)
{
    const NtCellView view = cell(tree,x,y);
    return view.glyph.count == 1 && view.glyph.data[0] == '#';
}

static NtPlotOptions options(NtPlotKind kind, const NtPlotDatum *data, size_t count)
{
    return (NtPlotOptions){.kind=kind,.data=data,.count=count,.auto_x=true,.auto_y=true,
                           .ascii=true,.foreground=red,.sampling=NT_SAMPLE_BRAILLE};
}

static void bars_and_reveal(void)
{
    NtTree *tree;
    NtNodeId node;
    OK(nt_tree_create(NULL,&tree));
    NtPlotDatum data[] = {{{0,-2},{0}},{{0,2},{0}}};
    NtPlotOptions spec = options(NT_PLOT_BARS,data,2);
    spec.reveal = (NtPlotReveal){10,600,NT_LINEAR};
    OK(nt_plot_mount(tree,(NtNodeId){0},enabled,&spec,&node));
    data[0].point.y = 7;
    const NtCellSize size = {8,4};
    OK(nt_tree_publish(tree,size));
    CHECK(!marked(tree,1,3) && !marked(tree,6,0));
    OK(nt_tree_tick(tree,0,NT_MOTION_FULL));
    CHECK(nt_tree_next_update(tree).scheduled && nt_tree_next_update(tree).at == 10);
    OK(nt_tree_tick(tree,310,NT_MOTION_FULL));
    OK(nt_tree_publish(tree,size));
    CHECK(marked(tree,1,3) && !marked(tree,6,0));
    CHECK(cell(tree,1,3).owner == nt_node_owner(node));
    OK(nt_tree_tick(tree,320,NT_MOTION_REDUCED));
    OK(nt_tree_publish(tree,size));
    CHECK(marked(tree,6,0) && !marked(tree,6,3) && !marked(tree,1,0));
    CHECK(!nt_tree_next_update(tree).scheduled);
    const NtFrame *frame = nt_tree_frame(tree);
    spec.data_limit = 1;
    CHECK(nt_plot_set(tree,node,&spec) == NT_RESULT_OUT_OF_RANGE && nt_tree_frame(tree) == frame);
    CHECK(nt_tree_pending(tree) == NT_INVALIDATE_NONE);
    spec.data_limit = 0;
    spec.reveal.duration_us = 0;
    spec.count = 0;
    OK(nt_plot_set(tree,node,&spec));
    OK(nt_tree_publish(tree,size));
    CHECK(!marked(tree,1,3) && !marked(tree,6,0));
    nt_tree_destroy(tree);
}

static void sectors(void)
{
    NtTree *tree;
    NtNodeId node;
    OK(nt_tree_create(NULL,&tree));
    const NtPlotDatum data[] = {{{0,DBL_MAX},{255,0,0,255}},{{0,DBL_MAX},{0,255,0,255}}};
    NtPlotOptions spec = options(NT_PLOT_PIE,data,2);
    spec.sample_colors = true;
    const NtCellSize size = {12,6};
    OK(nt_plot_mount(tree,(NtNodeId){0},enabled,&spec,&node));
    OK(nt_tree_publish(tree,size));
    CHECK(cell(tree,3,2).style.foreground.green == 255);
    CHECK(cell(tree,8,2).style.foreground.red == 255);
    CHECK(!marked(tree,0,0) && marked(tree,5,2));
    spec.kind = NT_PLOT_DONUT;
    OK(nt_plot_set(tree,node,&spec));
    OK(nt_tree_publish(tree,size));
    CHECK(!marked(tree,5,2) && !marked(tree,6,3));
    CHECK(marked(tree,8,2) && marked(tree,2,2));
    spec.kind = NT_PLOT_PIE;
    spec.cell_aspect = 1;
    OK(nt_plot_set(tree,node,&spec));
    OK(nt_tree_publish(tree,size));
    CHECK(!marked(tree,2,2) && marked(tree,8,2));
    const NtPlotDatum zero[] = {{{0,0},{0}},{{0,0},{0}}};
    spec.data = zero;
    OK(nt_plot_set(tree,node,&spec));
    OK(nt_tree_publish(tree,size));
    CHECK(!marked(tree,8,2));
    nt_tree_destroy(tree);
}

typedef struct Container { bool clipped; } Container;

static NtResult measure_parent(const void *value, NtMeasureContext *ctx, NtConstraints bounds, NtCellSize *out)
{
    (void)value;
    NtNodeId child;
    NtResult result = nt_measure_child_at(ctx,0,&child);
    if (result == NT_RESULT_OK) result = nt_measure_child(ctx,child,bounds,out);
    return result;
}

static NtResult layout_parent(const void *value, NtLayoutContext *ctx, NtCellSize size)
{
    const Container *state = value;
    NtNodeId child;
    const NtResult result = nt_layout_child_at(ctx,0,&child);
    if (result != NT_RESULT_OK) return result;
    const NtCellRect bounds = {{0,0},size};
    return state->clipped ? nt_layout_place_clipped(ctx,child,bounds,0,(NtCellRect){{3,1},{5,3}}) :
                            nt_layout_place(ctx,child,bounds,0,NULL);
}

static NtResult paint_parent(const void *value, NtPaintContext *paint, NtCellSize size)
{
    (void)value;
    (void)paint;
    (void)size;
    return NT_RESULT_OK;
}

static void clipped_plot(void)
{
    NtTree *tree;
    NtNodeId root, chart;
    Container state = {0};
    const NtComponentOps ops = {.measure=measure_parent,.layout=layout_parent,.paint=paint_parent};
    OK(nt_tree_create(NULL,&tree));
    OK(nt_tree_mount(tree,(NtNodeId){0},&ops,&state,enabled,&root));
    const NtPlotDatum data[] = {{{-4,-1},{0}},{{4,1},{0}}};
    NtPlotOptions spec = options(NT_PLOT_LINE,data,2);
    spec.auto_x = false;
    spec.auto_y = false;
    spec.domain = (NtPlotBounds){{-2,2},{-.5,.5}};
    spec.ascii = false;
    OK(nt_plot_mount(tree,root,enabled,&spec,&chart));
    OK(nt_tree_publish(tree,(NtCellSize){10,5}));
    char baseline[5][10][3] = {0};
    size_t visible = 0;
    for (int32_t y = 0; y < 5; ++y) for (int32_t x = 0; x < 10; ++x) {
        const NtCellView view = cell(tree,x,y);
        if (view.glyph.count) {
            CHECK(view.owner == nt_node_owner(chart) && view.glyph.count == 3);
            memcpy(baseline[y][x],view.glyph.data,3);
            ++visible;
        }
    }
    CHECK(visible >= 10);
    state.clipped = true;
    OK(nt_tree_invalidate(tree,root,NT_INVALIDATE_LAYOUT));
    OK(nt_tree_publish(tree,(NtCellSize){10,5}));
    for (int32_t y = 0; y < 5; ++y) for (int32_t x = 0; x < 10; ++x) {
        const NtCellView view = cell(tree,x,y);
        if (x >= 3 && x < 8 && y >= 1 && y < 4 && baseline[y][x][0])
            CHECK(view.glyph.count == 3 && !memcmp(view.glyph.data,baseline[y][x],3));
        else CHECK(view.glyph.count == 0);
    }
    state.clipped = false;
    spec.kind = NT_PLOT_EDGES;
    const NtPlotDatum pair[] = {{{-2,-.5},{0}},{{-2,.5},{0}},{{2,-.5},{0}},{{2,.5},{0}}};
    spec.ascii = true;
    spec.data = pair;
    spec.count = 4;
    OK(nt_plot_set(tree,chart,&spec));
    OK(nt_tree_invalidate(tree,root,NT_INVALIDATE_LAYOUT));
    OK(nt_tree_publish(tree,(NtCellSize){10,5}));
    CHECK(marked(tree,0,2) && marked(tree,9,2) && !marked(tree,5,2));
    const NtPlotDatum dots[] = {{{0,0},{0}},{{1,0},{0}},{{2,0},{0}},{{3,0},{0}}};
    spec = options(NT_PLOT_SCATTER,dots,4);
    spec.sample_stride = SIZE_MAX;
    OK(nt_plot_set(tree,chart,&spec));
    OK(nt_tree_publish(tree,(NtCellSize){10,5}));
    CHECK(marked(tree,0,2) && marked(tree,9,2) && !marked(tree,3,2) && !marked(tree,6,2));
    NtValue ink = {.kind=NT_VALUE_RGBA,.as.rgba=green};
    OK(nt_tree_property_set(tree,(NtPropertyRef){chart,NT_PROPERTY_FOREGROUND},ink));
    OK(nt_tree_publish(tree,(NtCellSize){10,5}));
    CHECK(cell(tree,0,2).style.foreground.green == 255);
    nt_tree_destroy(tree);
}

static void digits(void)
{
    NtTree *tree;
    NtNodeId node;
    OK(nt_tree_create(NULL,&tree));
    NtDigitsOptions spec = {.value=-12,.fractional_digits=1,.ascii=true,.foreground=green};
    OK(nt_digits_mount(tree,(NtNodeId){0},enabled,&spec,&node));
    OK(nt_tree_publish(tree,(NtCellSize){14,5}));
    CHECK(marked(tree,1,2) && !marked(tree,1,1));
    CHECK(marked(tree,6,1) && !marked(tree,4,1));
    CHECK(marked(tree,8,4) && !marked(tree,8,3));
    CHECK(marked(tree,10,0) && marked(tree,10,3));
    OK(nt_tree_property_set(tree,(NtPropertyRef){node,NT_PROPERTY_VALUE},(NtValue){.kind=NT_VALUE_I64,.as.i64=INT64_MIN}));
    OK(nt_tree_publish(tree,(NtCellSize){88,5}));
    CHECK(marked(tree,1,2) && marked(tree,4,0) && marked(tree,6,1));
    NtValue actual;
    OK(nt_tree_property_get(tree,(NtPropertyRef){node,NT_PROPERTY_VALUE},&actual));
    CHECK(actual.kind == NT_VALUE_I64 && actual.as.i64 == INT64_MIN);
    spec.fractional_digits = 19;
    CHECK(nt_digits_set(tree,node,&spec) == NT_RESULT_OUT_OF_RANGE);
    nt_tree_destroy(tree);
}

static void line_ink(void)
{
    NtTree *tree;
    NtNodeId chart;
    OK(nt_tree_create(NULL,&tree));
    NtPlotDatum data[] = {{{0,0},{0,0,255,255}},{{1,0},{0,255,0,0}}};
    NtPlotOptions spec = options(NT_PLOT_LINE,data,2);
    spec.sample_colors = true;
    const NtCellSize size = {4,2};
    OK(nt_plot_mount(tree,(NtNodeId){0},enabled,&spec,&chart));
    OK(nt_tree_publish(tree,size));
    CHECK(!marked(tree,0,1) && !marked(tree,3,1));
    data[1].color.alpha = 128;
    OK(nt_plot_set(tree,chart,&spec));
    OK(nt_tree_publish(tree,size));
    const NtRgba first = cell(tree,0,1).style.foreground;
    const NtRgba last = cell(tree,3,1).style.foreground;
    CHECK(first.green == 255 && first.blue == 0 && first.alpha == 128);
    CHECK(last.green == 255 && last.blue == 0 && last.alpha == 128);
    spec.count = 1;
    OK(nt_plot_set(tree,chart,&spec));
    OK(nt_tree_publish(tree,size));
    CHECK(marked(tree,2,1) && cell(tree,2,1).style.foreground.blue == 255);
    nt_tree_destroy(tree);
}

int main(void)
{
    geometry();
    bars_and_reveal();
    sectors();
    clipped_plot();
    line_ink();
    digits();
    return EXIT_SUCCESS;
}
