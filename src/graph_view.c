#include "component_internal.h"
#include "graph_internal.h"
#include "line_raster.h"
#include "surface_internal.h"

#include <string.h>

typedef struct GraphView {
    NtAllocator allocator;
    NtNodeId mount;
    NtGraphLayout *layout;
    NtTextLayout **labels;             /* Node-ID order. */
    NtGraphViewOptions options;
    NtGraphHit selection;
} GraphView;

typedef struct GraphCell {
    uint16_t tag, cluster;
    char glyph;                       /* Zero for label leads and continuations. */
} GraphCell;

typedef struct GraphPaint {
    NtAllocator allocator;
    NtGraphLayout *layout;
    NtCellRect clip;
    NtCellSize size;
    GraphCell *cells;
    size_t bytes;
} GraphPaint;

NT_DECLARE_ALIGNMENT(GraphView);
NT_DECLARE_ALIGNMENT(GraphPaint);
NT_DECLARE_ALIGNMENT(GraphCell);
typedef NtTextLayout *GraphLabelLayout;
NT_DECLARE_ALIGNMENT(GraphLabelLayout);

enum { EDGE_TAG = NT_GRAPH_MAX_NODES + 1, CONTINUATION = UINT16_MAX };

NtGraphViewOptions nt_graph_view_options_default(void)
{
    return (NtGraphViewOptions){
        .layout={NT_GRAPH_LAYERED,0}, .width={NT_WIDTH_ONE,NT_WIDTH_TWO}, .preferred={32,9},
        .node={205,222,242,255}, .pinned={126,204,200,255}, .edge={120,143,177,255},
        .selected={255,211,127,255}
    };
}

static NtGraphHit decode_tag(const GraphPaint *map, uint16_t tag)
{
    if (!tag) return (NtGraphHit){0};
    if (tag < EDGE_TAG) return (NtGraphHit){NT_GRAPH_HIT_NODE, map->layout->nodes[tag-1].id};
    return (NtGraphHit){NT_GRAPH_HIT_EDGE, map->layout->edges[tag-EDGE_TAG].id};
}

static bool same_hit(NtGraphHit left, NtGraphHit right)
{
    return left.kind == right.kind && left.id == right.id;
}

static bool valid_selection(const NtGraphLayout *layout, NtGraphHit value)
{
    switch (value.kind) {
    case NT_GRAPH_HIT_NONE: return !value.id;
    case NT_GRAPH_HIT_NODE: return nt_graph_find_node(layout, value.id) != SIZE_MAX;
    case NT_GRAPH_HIT_EDGE: return nt_graph_find_edge(layout, value.id) != SIZE_MAX;
    }
    return false;
}

static void release_graph(GraphView *view)
{
    if (!view->layout) return;
    if (view->labels) {
        const size_t count = view->layout->node_count;
        for (size_t i = 0; i < count; ++i) nt_text_layout_destroy(view->labels[i]);
        nt_release(&view->allocator, view->labels, count * sizeof(*view->labels), NT_ALIGNMENT(GraphLabelLayout));
    }
    nt_graph_layout_destroy(view->layout);
}

static void dispose_view(void *value)
{
    GraphView *view = value;
    const NtAllocator allocator = view->allocator;
    release_graph(view);
    nt_release(&allocator, view, sizeof(*view), NT_ALIGNMENT(GraphView));
}

static NtResult build_view(GraphView *view, const NtGraphViewOptions *options)
{
    if (!options || (options->label_count && !options->labels) ||
        (options->width.ambiguous != NT_WIDTH_ONE && options->width.ambiguous != NT_WIDTH_TWO) ||
        (options->width.emoji != NT_WIDTH_ONE && options->width.emoji != NT_WIDTH_TWO))
        return NT_RESULT_INVALID_ARGUMENT;
    if (options->label_count > NT_GRAPH_MAX_NODES) return NT_RESULT_OUT_OF_RANGE;
    const NtAllocator *allocator = view->allocator.allocate ? &view->allocator : NULL;
    NtResult result = nt_graph_layout_create(allocator, options->data, options->layout, &view->layout);
    if (result != NT_RESULT_OK) return result;
    view->options = *options;
    view->options.data = (NtGraphData){0};
    view->options.labels = NULL;
    view->options.label_count = 0;
    if (!options->label_count) return NT_RESULT_OK;
    const size_t count = view->layout->node_count;
    if (options->label_count > count) return NT_RESULT_INVALID_ARGUMENT;
    view->labels = nt_allocate(&view->allocator, count * sizeof(*view->labels), NT_ALIGNMENT(GraphLabelLayout));
    if (!view->labels) return NT_RESULT_ALLOCATION_FAILED;
    memset(view->labels, 0, count * sizeof(*view->labels));
    const NtTextLayoutOptions layout = {
        .width=options->width, .bounds={.columns={0,NT_GRAPH_MAX_LABEL_COLUMNS,true}, .rows={0,1,true}},
        .align=NT_TEXT_ALIGN_START, .wrap=NT_WRAP_NONE, .base_direction=NT_DIRECTION_AUTO,
        .tab_stop=4, .ellipsis=true
    };
    for (size_t i = 0; i < options->label_count; ++i) {
        const NtGraphLabel label = options->labels[i];
        if (label.text.count > NT_GRAPH_MAX_LABEL_BYTES) return NT_RESULT_OUT_OF_RANGE;
        const size_t index = nt_graph_find_node(view->layout, label.node);
        if (index == SIZE_MAX || view->labels[index]) return NT_RESULT_INVALID_ARGUMENT;
        NtText *text = NULL;
        result = nt_text_create(allocator, label.text, &text);
        if (result == NT_RESULT_OK)
            result = nt_text_layout_create(allocator, text, &layout, &view->labels[index]);
        nt_text_destroy(text);
        if (result != NT_RESULT_OK) return result;
    }
    return NT_RESULT_OK;
}

static uint32_t fit_axis(uint32_t size, NtExtentRange bounds)
{
    if (size < bounds.min) return bounds.min;
    return bounds.bounded && size > bounds.max ? bounds.max : size;
}

static NtResult measure_view(const void *value, NtMeasureContext *context, NtConstraints bounds, NtCellSize *out)
{
    const GraphView *view = value;
    if (nt_measure_child_count(context)) return NT_RESULT_INVALID_ARGUMENT;
    *out = (NtCellSize){fit_axis(view->options.preferred.columns, bounds.columns),
                        fit_axis(view->options.preferred.rows, bounds.rows)};
    return NT_RESULT_OK;
}

static void dispose_map(void *value)
{
    GraphPaint *map = value;
    const NtAllocator allocator = map->allocator;
    nt_graph_layout_destroy(map->layout);
    nt_release(&allocator, map->cells, map->bytes, NT_ALIGNMENT(GraphCell));
    nt_release(&allocator, map, sizeof(*map), NT_ALIGNMENT(GraphPaint));
}

static NtResult begin_map(const GraphView *view, NtPaintContext *paint, NtCellSize size, GraphPaint **out)
{
    const NtCellRect clip = nt_paint_clip(paint);
    if (clip.size.columns > SIZE_MAX / sizeof(GraphCell) / clip.size.rows)
        return NT_RESULT_OUT_OF_RANGE;
    GraphPaint *map = nt_allocate(&view->allocator, sizeof(*map), NT_ALIGNMENT(GraphPaint));
    if (!map) return NT_RESULT_ALLOCATION_FAILED;
    *map = (GraphPaint){.allocator=view->allocator,.layout=view->layout,.clip=clip,.size=size};
    map->bytes = (size_t)clip.size.columns * clip.size.rows * sizeof(GraphCell);
    nt_graph_layout_retain(map->layout);
    nt_component_attach_paint_data(paint, map, dispose_map);
    map->cells = nt_allocate(&view->allocator, map->bytes, NT_ALIGNMENT(GraphCell));
    if (!map->cells) return NT_RESULT_ALLOCATION_FAILED;
    memset(map->cells, 0, map->bytes);
    *out = map;
    return NT_RESULT_OK;
}

static GraphCell *map_cell(const GraphPaint *map, int64_t x, int64_t y)
{
    x -= map->clip.origin.x;
    y -= map->clip.origin.y;
    if (x < 0 || y < 0 || x >= map->clip.size.columns || y >= map->clip.size.rows) return NULL;
    return &map->cells[(size_t)y * map->clip.size.columns + (size_t)x];
}

static int32_t project_axis(double coordinate, uint32_t size)
{
    const uint32_t last = size - 1;
    return (int32_t)(coordinate * last + .5);
}

static NtCellPoint marker(const GraphPaint *map, size_t node)
{
    const NtPoint p = map->layout->nodes[node].position;
    return (NtCellPoint){project_axis(p.x, map->size.columns),project_axis(p.y, map->size.rows)};
}

typedef struct EdgeStroke {
    GraphPaint *map;
    uint16_t tag;
    char glyph, arrow;
    bool vertical;
    int64_t arrow_at;
} EdgeStroke;

static NtResult edge_cell(const void *context, int64_t x, int64_t y)
{
    const EdgeStroke *stroke = context;
    GraphCell *cell = map_cell(stroke->map, x, y);
    const bool at_arrow = stroke->arrow && (stroke->vertical ? y : x) == stroke->arrow_at;
    if (cell) *cell = (GraphCell){.tag=stroke->tag,.glyph=at_arrow ? stroke->arrow : stroke->glyph};
    return NT_RESULT_OK;
}

static void stroke_edge(EdgeStroke *stroke, NtCellPoint a, NtCellPoint b, bool directed)
{
    const int64_t dx = (int64_t)b.x - a.x, dy = (int64_t)b.y - a.y;
    const uint64_t width = nt_line_magnitude(dx), height = nt_line_magnitude(dy);
    stroke->glyph = dx == 0 ? '|' : dy == 0 ? '-' : ((dx < 0) == (dy < 0) ? '\\' : '/');
    stroke->vertical = height > width;
    stroke->arrow = 0;
    if (directed && (width >= 2 || height >= 2)) {
        if (stroke->vertical) {
            stroke->arrow = dy < 0 ? '^' : 'v';
            stroke->arrow_at = (int64_t)b.y + (dy < 0 ? 1 : -1);
        } else {
            stroke->arrow = dx < 0 ? '<' : '>';
            stroke->arrow_at = (int64_t)b.x + (dx < 0 ? 1 : -1);
        }
    }
    (void)nt_line_raster(a, b, stroke->map->clip, edge_cell, stroke);
}

static void stroke_loop(EdgeStroke *stroke, NtCellPoint node, bool directed)
{
    const NtCellSize size = stroke->map->size;
    const int32_t x = size.columns < 2 ? node.x : node.x == 0 ? 1 : node.x - 1;
    const int32_t y = size.rows < 2 ? node.y : node.y == 0 ? 1 : node.y - 1;
    const NtCellPoint along_x = {x,node.y}, corner = {x,y}, along_y = {node.x,y};
    stroke_edge(stroke, node, along_x, false);
    stroke_edge(stroke, along_x, corner, false);
    stroke_edge(stroke, corner, along_y, false);
    stroke_edge(stroke, along_y, node, false);
    if (!directed) return;

    /* A loop's final leg has one cell before its destination. In a single
     * row, the return leg is horizontal instead. */
    if (y != node.y) {
        stroke->glyph = y < node.y ? 'v' : '^';
        (void)edge_cell(stroke, along_y.x, along_y.y);
    } else if (x != node.x) {
        stroke->glyph = x < node.x ? '>' : '<';
        (void)edge_cell(stroke, along_x.x, along_x.y);
    }
}

static void draw_edges(GraphPaint *map)
{
    for (size_t i = 0; i < map->layout->edge_count; ++i) {
        const GraphArc arc = map->layout->arcs[i];
        const NtCellPoint a = marker(map, arc.source), b = marker(map, arc.target);
        EdgeStroke stroke = {.map=map,.tag=(uint16_t)(EDGE_TAG+i)};
        const bool directed = map->layout->edges[i].directed;
        if (arc.source == arc.target) stroke_loop(&stroke,a,directed);
        else stroke_edge(&stroke,a,b,directed);
    }
}

static void draw_markers(GraphPaint *map)
{
    for (size_t i = 0; i < map->layout->node_count; ++i) {
        const NtCellPoint p = marker(map, i);
        GraphCell *cell = map_cell(map,p.x,p.y);
        if (cell) *cell = (GraphCell){.tag=(uint16_t)(i+1),
                                     .glyph=map->layout->nodes[i].pinned ? '@' : 'o'};
    }
}

static bool label_room(const GraphPaint *map, const NtSurfaceView *surface,
                        int64_t x, int64_t y, uint32_t columns)
{
    for (uint32_t i = 0; i < columns; ++i) {
        const GraphCell *cell = map_cell(map, x+i, y);
        double coverage;
        if (!cell || (cell->tag && cell->tag < EDGE_TAG) ||
            nt_surface_coverage(surface,(NtCellPoint){(int32_t)(x+i),(int32_t)y},&coverage) != NT_RESULT_OK ||
            coverage <= 0) return false;
    }
    return true;
}

static void draw_labels(const GraphView *view, GraphPaint *map, const NtSurfaceView *surface)
{
    if (!view->labels) return;
    for (size_t i = 0; i < map->layout->node_count; ++i) {
        const NtCellPoint p = marker(map, i);
        const NtTextLayout *layout = view->labels[i];
        const size_t clusters = nt_text_layout_cluster_count(layout);
        const uint16_t tag = (uint16_t)(i+1);
        for (size_t j = 0; j < clusters; ++j) {
            NtClusterView glyph;
            if (nt_text_layout_cluster(layout, j, &glyph) != NT_RESULT_OK) continue;
            const int64_t x = (int64_t)p.x + 2 + glyph.position.x, y = p.y;
            if (!label_room(map,surface,x,y,glyph.columns)) continue;
            for (uint32_t k = 0; k < glyph.columns; ++k)
                *map_cell(map,x+k,y) = (GraphCell){tag,(uint16_t)(k ? CONTINUATION : j),0};
        }
    }
}

static NtResult paint_cell(const GraphView *view, NtPaintContext *paint, const GraphPaint *map,
                            NtCellPoint point, GraphCell tile)
{
    if (!tile.tag || tile.cluster == CONTINUATION) return NT_RESULT_OK;
    const NtGraphHit item = decode_tag(map,tile.tag);
    NtRgba color = view->options.edge;
    if (item.kind == NT_GRAPH_HIT_NODE)
        color = map->layout->nodes[tile.tag-1].pinned ? view->options.pinned : view->options.node;
    if (same_hit(item,view->selection)) color = view->options.selected;
    if (tile.glyph)
        return nt_paint_glyph(paint,point,(NtByteView){&tile.glyph,1},1,(NtCellStyle){.foreground=color});
    NtClusterView glyph;
    NtResult result = nt_text_layout_cluster(view->labels[tile.tag-1],tile.cluster,&glyph);
    if (result != NT_RESULT_OK) return result;
    if (glyph.glyph.count == 1 && glyph.glyph.data[0] == ' ') {
        for (uint32_t i = 0; i < glyph.columns; ++i) {
            const NtCellPoint p = {(int32_t)((int64_t)point.x+i),point.y};
            result = nt_paint_glyph(paint,p,glyph.glyph,1,(NtCellStyle){.foreground=color});
            if (result != NT_RESULT_OK) return result;
        }
        return NT_RESULT_OK;
    }
    return nt_paint_glyph(paint,point,glyph.glyph,(uint8_t)glyph.columns,(NtCellStyle){.foreground=color});
}

static NtResult paint_view(const void *value, NtPaintContext *paint, NtCellSize size)
{
    const GraphView *view = value;
    if (size.columns > (uint32_t)INT32_MAX + 1 || size.rows > (uint32_t)INT32_MAX + 1)
        return NT_RESULT_OUT_OF_RANGE;
    const NtCellRect clip = nt_paint_clip(paint);
    if (!clip.size.columns || !clip.size.rows) return NT_RESULT_OK;
    NtResult result = nt_paint_fill(paint,(NtCellRect){{0,0},size},view->options.background);
    if (result != NT_RESULT_OK || !view->layout->node_count) return result;
    GraphPaint *map;
    result = begin_map(view,paint,size,&map);
    if (result != NT_RESULT_OK) return result;
    draw_edges(map);
    draw_markers(map);
    draw_labels(view,map,paint->surface);
    for (uint32_t y = 0; y < clip.size.rows; ++y) {
        for (uint32_t x = 0; x < clip.size.columns; ++x) {
            const NtCellPoint point = {(int32_t)((int64_t)clip.origin.x+x),(int32_t)((int64_t)clip.origin.y+y)};
            result = paint_cell(view,paint,map,point,map->cells[(size_t)y*clip.size.columns+x]);
            if (result != NT_RESULT_OK) return result;
        }
    }
    return NT_RESULT_OK;
}

static NtGraphHit local_hit(const NtPlacement *place, NtCellPoint local, NtOwnerToken owner)
{
    const GraphPaint *map = place->paint_data;
    if (!map) return (NtGraphHit){0};
    const GraphCell *tile = map_cell(map,local.x,local.y);
    if (!tile || !tile->tag) return (NtGraphHit){0};
    const NtCellPoint frame = {(int32_t)((int64_t)local.x+place->surface.bounds.origin.x),
                               (int32_t)((int64_t)local.y+place->surface.bounds.origin.y)};
    NtCellView cell;
    if (nt_frame_cell(place->surface.frame,frame,&cell) != NT_RESULT_OK || !cell.glyph_columns ||
        cell.owner != owner || !nt_surface_contains_frame(&place->surface,frame)) return (NtGraphHit){0};
    return decode_tag(map,tile->tag);
}

static NtGraphHit nearby_node(const GraphView *view, const NtPlacement *place, bool backwards)
{
    const GraphPaint *map = place->paint_data;
    if (!map) return (NtGraphHit){0};
    NtGraphHit wrap = {0};
    const NtGraphId current = view->selection.kind == NT_GRAPH_HIT_NODE ? view->selection.id : 0;
    for (size_t step = 0; step < map->layout->node_count; ++step) {
        const size_t i = backwards ? map->layout->node_count-1-step : step;
        const NtGraphId id = map->layout->nodes[i].id;
        const NtGraphHit hit = local_hit(place,marker(map,i),nt_node_owner(view->mount));
        if (hit.kind != NT_GRAPH_HIT_NODE || hit.id != id || !valid_selection(view->layout,hit)) continue;
        if (!wrap.id) wrap = hit;
        if (!current || (backwards ? id < current : id > current)) return hit;
    }
    return wrap;
}

static NtResult graph_event(void *value, NtEventContext *context, const NtEvent *event, NtEventReply *reply)
{
    GraphView *view = value;
    const NtPlacement *place = nt_tree_placement(view->mount.tree,view->mount);
    if (nt_event_phase(context) != NT_EVENT_TARGET || !place) return NT_RESULT_OK;
    NtGraphHit selected = {0};
    if (event->kind == NT_EVENT_POINTER_DOWN && event->data.pointer.button == NT_BUTTON_PRIMARY) {
        NtCellPoint local;
        if (!nt_event_local_position(context,&local)) return NT_RESULT_OK;
        selected = local_hit(place,local,nt_node_owner(view->mount));
    } else if (event->kind == NT_EVENT_KEY && event->data.key.state != NT_KEY_RELEASE &&
               !event->data.key.modifiers) {
        const uint32_t key = event->data.key.key;
        if (key == NT_KEY_LEFT || key == NT_KEY_UP) selected = nearby_node(view,place,true);
        else if (key == NT_KEY_RIGHT || key == NT_KEY_DOWN) selected = nearby_node(view,place,false);
    }
    if (!selected.id || !valid_selection(view->layout,selected)) return NT_RESULT_OK;
    if (event->kind == NT_EVENT_POINTER_DOWN)
        reply->focus = view->mount.tree->nodes[view->mount.slot].options.focusable;
    reply->consume = true;
    if (same_hit(view->selection,selected)) return NT_RESULT_OK;
    const NtIntentField fields[] = {{1,{.kind=NT_VALUE_U64,.as.u64=selected.kind}},
                                    {2,{.kind=NT_VALUE_U64,.as.u64=selected.id}}};
    const NtResult result = nt_event_emit_fields(context,NT_INTENT_GRAPH_SELECTION,fields,2);
    if (result == NT_RESULT_OK) {
        view->selection = selected;
        reply->invalidate = NT_INVALIDATE_PAINT;
    }
    return result;
}

static NtResult layout_view(const void *state, NtLayoutContext *context, NtCellSize assigned)
{
    (void)state;
    (void)context;
    (void)assigned;
    return NT_RESULT_OK;
}

static const NtComponentOps graph_ops = {measure_view,layout_view,NULL,paint_view,graph_event,dispose_view};

static NtResult validate_mount(const NtTree *tree, NtNodeId node)
{
    const NtResult result = nt_node_validate(tree,node);
    if (result != NT_RESULT_OK) return result;
    return tree->nodes[node.slot].ops.paint == paint_view ? NT_RESULT_OK : NT_RESULT_INVALID_ARGUMENT;
}

NtResult nt_graph_view_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount,
                              const NtGraphViewOptions *options, NtNodeId *out)
{
    if (!tree || !out || !options) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    if (nt_node_null(parent)) {
        if (tree->root != NT_NO_SLOT) return NT_RESULT_INVALID_ARGUMENT;
    } else {
        const NtResult result = nt_node_validate(tree,parent);
        if (result != NT_RESULT_OK) return result;
    }
    tree->busy = true;
    GraphView *view = nt_allocate(&tree->allocator,sizeof(*view),NT_ALIGNMENT(GraphView));
    NtResult result = NT_RESULT_ALLOCATION_FAILED;
    if (view) {
        *view = (GraphView){.allocator=tree->allocator};
        result = build_view(view,options);
    }
    tree->busy = false;
    if (result == NT_RESULT_OK) result = nt_tree_mount(tree,parent,&graph_ops,view,mount,out);
    if (result == NT_RESULT_OK) view->mount = *out;
    else if (view) {
        tree->busy = true;
        dispose_view(view);
        tree->busy = false;
    }
    return result;
}

NtResult nt_graph_view_set(NtTree *tree, NtNodeId node, const NtGraphViewOptions *options)
{
    if (!tree || !options) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    NtResult result = validate_mount(tree,node);
    if (result != NT_RESULT_OK) return result;
    GraphView *view = tree->nodes[node.slot].state;
    GraphView replacement = {.allocator=view->allocator,.mount=node,.selection=view->selection};
    tree->busy = true;
    result = build_view(&replacement,options);
    if (result == NT_RESULT_OK) {
        if (!valid_selection(replacement.layout,replacement.selection)) replacement.selection = (NtGraphHit){0};
        release_graph(view);
        *view = replacement;
        nt_tree_dirty(tree,NT_INVALIDATE_LAYOUT);
    } else release_graph(&replacement);
    tree->busy = false;
    return result;
}

NtResult nt_graph_view_selection(const NtTree *tree, NtNodeId node, NtGraphHit *out)
{
    if (!tree || !out) return NT_RESULT_INVALID_ARGUMENT;
    const NtResult result = validate_mount(tree,node);
    if (result != NT_RESULT_OK) return result;
    const GraphView *view = tree->nodes[node.slot].state;
    *out = view->selection;
    return NT_RESULT_OK;
}

NtResult nt_graph_view_select(NtTree *tree, NtNodeId node, NtGraphHit selection)
{
    if (!tree) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    const NtResult result = validate_mount(tree,node);
    if (result != NT_RESULT_OK) return result;
    GraphView *view = tree->nodes[node.slot].state;
    if (!valid_selection(view->layout,selection)) return NT_RESULT_INVALID_ARGUMENT;
    if (!same_hit(view->selection,selection)) {
        view->selection = selection;
        nt_tree_dirty(tree,NT_INVALIDATE_PAINT);
    }
    return NT_RESULT_OK;
}

NtResult nt_graph_view_hit(const NtTree *tree, NtNodeId node, NtCellPoint frame, NtGraphHit *out)
{
    if (!tree || !out) return NT_RESULT_INVALID_ARGUMENT;
    const NtResult result = validate_mount(tree,node);
    if (result != NT_RESULT_OK) return result;
    const NtPlacement *place = nt_tree_placement(tree,node);
    *out = (NtGraphHit){0};
    if (place && place->enabled && nt_surface_contains_frame(&place->surface,frame)) {
        const int64_t x = (int64_t)frame.x - place->surface.bounds.origin.x;
        const int64_t y = (int64_t)frame.y - place->surface.bounds.origin.y;
        if (x >= 0 && y >= 0 && x <= INT32_MAX && y <= INT32_MAX)
            *out = local_hit(place,(NtCellPoint){(int32_t)x,(int32_t)y},nt_node_owner(node));
    }
    return NT_RESULT_OK;
}

NtResult nt_graph_event_decode(NtEventIntentView intent, NtGraphHit *out)
{
    const NtIntentField *fields = intent.fields;
    if (!out || intent.intent.kind != NT_INTENT_GRAPH_SELECTION ||
        intent.payload_kind != NT_INTENT_FIELDS || intent.field_count != 2 || !fields ||
        fields[0].id != 1 || fields[0].value.kind != NT_VALUE_U64 ||
        fields[1].id != 2 || fields[1].value.kind != NT_VALUE_U64 || !fields[1].value.as.u64 ||
        fields[0].value.as.u64 < NT_GRAPH_HIT_NODE || fields[0].value.as.u64 > NT_GRAPH_HIT_EDGE)
        return NT_RESULT_INVALID_ARGUMENT;
    *out = (NtGraphHit){(NtGraphHitKind)fields[0].value.as.u64,fields[1].value.as.u64};
    return NT_RESULT_OK;
}
