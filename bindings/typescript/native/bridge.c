#include <nocturne/abi.h>
#include <nocturne/animation.h>
#include <nocturne/component.h>
#include <nocturne/layout.h>
#include <nocturne/text.h>
#include <nocturne/widgets.h>
#include <nocturne/window.h>
#include <stdlib.h>

#ifdef NT_WASM_graph
#include <nocturne/graph.h>
#endif
#ifdef NT_WASM_spatial
#include <nocturne/spatial.h>
#endif
#ifdef NT_WASM_simulation
#include <nocturne/simulation.h>
#endif

#define EXPORT_WASM(name) __attribute__((export_name(#name)))

typedef struct WasmView { const char *data; uint32_t count; } WasmView;
typedef struct WasmCluster { uint64_t id; uint32_t begin, end; } WasmCluster;
typedef struct WasmAnimationSample {
    double value, progress;
    uint64_t iteration;
    int32_t phase, direction;
} WasmAnimationSample;
typedef struct WasmNode { uint32_t slot, generation; } WasmNode;
typedef struct WasmCell {
    uint64_t owner;
    WasmView glyph;
    int32_t x, y;
    uint32_t columns;
    NtRgba foreground, background;
} WasmCell;

EXPORT_WASM(nocturne_wasm_abi_version)
uint32_t nocturne_wasm_abi_version(void)
{
    return nt_abi_version();
}

EXPORT_WASM(nocturne_wasm_result_name)
const char *nocturne_wasm_result_name(int32_t code)
{
    return nt_result_name(code);
}

EXPORT_WASM(nocturne_wasm_allocate)
void *nocturne_wasm_allocate(uint32_t count)
{
    return malloc(count);
}

EXPORT_WASM(nocturne_wasm_release)
void nocturne_wasm_release(void *address)
{
    free(address);
}

EXPORT_WASM(nocturne_wasm_tween_sample)
NtResult nocturne_wasm_tween_sample(double from, double to, uint64_t duration, uint64_t delay,
                                   uint32_t easing, uint32_t repeat, uint64_t elapsed,
                                   WasmAnimationSample *out)
{
    const NtTween tween = {from, to, duration, delay, (NtEasing)easing, (NtRepeat)repeat};
    NtAnimationSample sample;
    switch (nt_tween_sample(&tween, elapsed, &sample)) {
    case NT_OK:
        *out = (WasmAnimationSample){sample.value, sample.progress, sample.iteration,
                                     (int32_t)sample.phase, (int32_t)sample.direction};
        return NT_RESULT_OK;
    case NT_INVALID_ARGUMENT:
        return NT_RESULT_INVALID_ARGUMENT;
    case NT_OUT_OF_RANGE:
        return NT_RESULT_OUT_OF_RANGE;
    }
    return NT_RESULT_UNSUPPORTED_CAPABILITY;
}

EXPORT_WASM(nocturne_wasm_text_create)
NtResult nocturne_wasm_text_create(const char *data, uint32_t count, NtText **out)
{
    return nt_text_create(NULL, (NtByteView){data, count}, out);
}

EXPORT_WASM(nocturne_wasm_text_destroy)
void nocturne_wasm_text_destroy(NtText *text)
{
    nt_text_destroy(text);
}

EXPORT_WASM(nocturne_wasm_text_value)
void nocturne_wasm_text_value(const NtText *text, WasmView *out)
{
    const NtByteView view = nt_text_bytes(text);
    *out = (WasmView){view.data, (uint32_t)view.count};
}

EXPORT_WASM(nocturne_wasm_text_count)
uint32_t nocturne_wasm_text_count(const NtText *text)
{
    return (uint32_t)nt_text_cluster_count(text);
}

EXPORT_WASM(nocturne_wasm_text_cluster)
NtResult nocturne_wasm_text_cluster(const NtText *text, uint32_t index, WasmCluster *out)
{
    NtTextCluster value;
    const NtResult result = nt_text_cluster(text, index, &value);
    if (result == NT_RESULT_OK)
        *out = (WasmCluster){value.id, (uint32_t)value.source.begin, (uint32_t)value.source.end};
    return result;
}

EXPORT_WASM(nocturne_wasm_text_replace)
NtResult nocturne_wasm_text_replace(NtText *text, uint32_t begin, uint32_t end,
                                   const char *data, uint32_t count)
{
    return nt_text_replace(text, (NtByteSpan){begin, end}, (NtByteView){data, count});
}

static NtNodeId node_id(const NtTree *tree, uint32_t slot, uint32_t generation)
{
    return generation ? (NtNodeId){tree, slot, generation} : (NtNodeId){0};
}

static NtMountOptions mount_options(uint32_t flags)
{
    return (NtMountOptions){(flags & 1u) != 0, (flags & 2u) != 0, (flags & 4u) != 0};
}

static NtRgba color(uint32_t value)
{
    return (NtRgba){(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
}

static NtLabelOptions label_options(const char *data, uint32_t count, uint32_t foreground, uint32_t background)
{
    return (NtLabelOptions){.text = {data, count}, .style = {
        .width = {NT_WIDTH_ONE, NT_WIDTH_TWO}, .wrap = NT_WRAP_NONE, .align = NT_TEXT_ALIGN_START,
        .direction = NT_DIRECTION_AUTO, .tab_stop = 4, .color = {color(foreground), color(background)}
    }};
}

EXPORT_WASM(nocturne_wasm_tree_create)
NtResult nocturne_wasm_tree_create(NtTree **out)
{
    return nt_tree_create(NULL, out);
}

EXPORT_WASM(nocturne_wasm_tree_destroy)
void nocturne_wasm_tree_destroy(NtTree *tree)
{
    nt_tree_destroy(tree);
}

EXPORT_WASM(nocturne_wasm_tree_mount_box)
NtResult nocturne_wasm_tree_mount_box(NtTree *tree, uint32_t slot, uint32_t generation,
                                     uint32_t flags, uint32_t kind, uint32_t gap, WasmNode *out)
{
    const NtBoxLayout layout = {.kind = (NtBoxLayoutKind)kind, .gap = gap};
    NtNodeId node;
    const NtResult result = nt_box_layout_mount(tree, node_id(tree, slot, generation),
                                                 mount_options(flags), &layout, &node);
    if (result == NT_RESULT_OK) *out = (WasmNode){node.slot, node.generation};
    return result;
}

EXPORT_WASM(nocturne_wasm_tree_mount_label)
NtResult nocturne_wasm_tree_mount_label(NtTree *tree, uint32_t slot, uint32_t generation,
                                       uint32_t flags, const char *data, uint32_t count,
                                       uint32_t foreground, uint32_t background, WasmNode *out)
{
    const NtLabelOptions label = label_options(data, count, foreground, background);
    NtNodeId node;
    const NtResult result = nt_label_mount(tree, node_id(tree, slot, generation), mount_options(flags), &label, &node);
    if (result == NT_RESULT_OK) *out = (WasmNode){node.slot, node.generation};
    return result;
}

EXPORT_WASM(nocturne_wasm_tree_set_label)
NtResult nocturne_wasm_tree_set_label(NtTree *tree, uint32_t slot, uint32_t generation,
                                     const char *data, uint32_t count, uint32_t foreground, uint32_t background)
{
    const NtLabelOptions label = label_options(data, count, foreground, background);
    return nt_label_set(tree, node_id(tree, slot, generation), &label);
}

#ifdef NT_WASM_graph
typedef struct WasmGraph {
    NtGraphData data;
    uint32_t layout, force_iterations;
    NtCellSize preferred;
} WasmGraph;

/* The TypeScript binding writes these records to wasm32 linear memory. */
typedef char WasmGraphLayout[
    sizeof(WasmGraph) == 32 && offsetof(WasmGraph, layout) == 16 &&
    sizeof(NtGraphNode) == 32 && offsetof(NtGraphNode, pinned) == 8 &&
    offsetof(NtGraphNode, position) == 16 && sizeof(NtGraphEdge) == 32 &&
    offsetof(NtGraphEdge, directed) == 24 ? 1 : -1];

static NtGraphViewOptions graph_options(const WasmGraph *graph)
{
    NtGraphViewOptions result = nt_graph_view_options_default();
    result.data = graph->data;
    result.layout = (NtGraphLayoutOptions){(NtGraphLayoutKind)graph->layout, graph->force_iterations};
    if (graph->preferred.columns || graph->preferred.rows) result.preferred = graph->preferred;
    return result;
}

EXPORT_WASM(nocturne_wasm_tree_mount_graph)
NtResult nocturne_wasm_tree_mount_graph(NtTree *tree, uint32_t slot, uint32_t generation,
                                       uint32_t flags, const WasmGraph *graph, WasmNode *out)
{
    const NtGraphViewOptions options = graph_options(graph);
    NtNodeId node;
    const NtResult result = nt_graph_view_mount(tree, node_id(tree, slot, generation),
                                               mount_options(flags), &options, &node);
    if (result == NT_RESULT_OK) *out = (WasmNode){node.slot, node.generation};
    return result;
}

EXPORT_WASM(nocturne_wasm_tree_set_graph)
NtResult nocturne_wasm_tree_set_graph(NtTree *tree, uint32_t slot, uint32_t generation, const WasmGraph *graph)
{
    const NtGraphViewOptions options = graph_options(graph);
    return nt_graph_view_set(tree, node_id(tree, slot, generation), &options);
}
#endif

#ifdef NT_WASM_spatial
EXPORT_WASM(nocturne_wasm_vector_cross)
NtResult nocturne_wasm_vector_cross(double ax, double ay, double az, double bx, double by, double bz, NtVec3 *out)
{
    return nt_spatial_vec_cross((NtVec3){ax, ay, az}, (NtVec3){bx, by, bz}, out);
}

EXPORT_WASM(nocturne_wasm_vector_normalize)
NtResult nocturne_wasm_vector_normalize(double x, double y, double z, NtVec3 *out)
{
    return nt_spatial_vec_normalize((NtVec3){x, y, z}, out);
}
#endif

#ifdef NT_WASM_simulation
typedef char WasmWaveLayout[
    sizeof(NtWaterWave) == 40 && sizeof(NtWaterWaveSample) == 64 &&
    offsetof(NtWaterWaveSample, normal) == 24 && offsetof(NtWaterWaveSample, vertical_velocity) == 48 ? 1 : -1];

EXPORT_WASM(nocturne_wasm_water_sample)
NtResult nocturne_wasm_water_sample(const NtWaterWave *waves, uint32_t count,
                                    double x, double z, double seconds, NtWaterWaveSample *out)
{
    return nt_water_wave_sample(waves, count, (NtPoint){x, z}, seconds, out);
}
#endif

EXPORT_WASM(nocturne_wasm_tree_remove)
NtResult nocturne_wasm_tree_remove(NtTree *tree, uint32_t slot, uint32_t generation)
{
    return nt_tree_remove(tree, node_id(tree, slot, generation));
}

EXPORT_WASM(nocturne_wasm_tree_publish)
NtResult nocturne_wasm_tree_publish(NtTree *tree, uint32_t columns, uint32_t rows)
{
    return nt_tree_publish(tree, (NtCellSize){columns, rows});
}

EXPORT_WASM(nocturne_wasm_tree_has_frame)
bool nocturne_wasm_tree_has_frame(const NtTree *tree)
{
    return nt_tree_frame(tree) != NULL;
}

EXPORT_WASM(nocturne_wasm_tree_frame_size)
void nocturne_wasm_tree_frame_size(const NtTree *tree, NtCellSize *out)
{
    *out = nt_frame_size(nt_tree_frame(tree));
}

EXPORT_WASM(nocturne_wasm_tree_frame_cell)
NtResult nocturne_wasm_tree_frame_cell(const NtTree *tree, int32_t x, int32_t y, WasmCell *out)
{
    NtCellView view;
    const NtResult result = nt_frame_cell(nt_tree_frame(tree), (NtCellPoint){x, y}, &view);
    if (result == NT_RESULT_OK) {
        *out = (WasmCell){.owner = view.owner, .glyph = {view.glyph.data, (uint32_t)view.glyph.count},
            .x = view.lead.x, .y = view.lead.y, .columns = view.glyph_columns,
            .foreground = view.style.foreground, .background = view.style.background};
    }
    return result;
}

#define IMPORT_HOST(symbol, name) __attribute__((import_module("nocturne"), import_name(#name))) symbol

typedef struct WasmComponent { uint32_t handle; } WasmComponent;

extern IMPORT_HOST(NtResult host_measure, measure)(uint32_t, NtMeasureContext *,
    uint32_t, uint32_t, bool, uint32_t, uint32_t, bool, NtCellSize *);
extern IMPORT_HOST(NtResult host_layout, layout)(uint32_t, NtLayoutContext *, uint32_t, uint32_t);
extern IMPORT_HOST(NtResult host_update, update)(uint32_t, NtTimeUs, NtTimeUs, NtMotionPolicy, NtUpdateReply *);
extern IMPORT_HOST(NtResult host_paint, paint)(uint32_t, NtPaintContext *, uint32_t, uint32_t);
extern IMPORT_HOST(NtResult host_event, event)(uint32_t, NtEventContext *, const NtEvent *, NtEventReply *,
    NtEventKind, NtTimeUs);
extern IMPORT_HOST(void host_dispose, dispose)(uint32_t);

static NtResult component_measure(const void *state, NtMeasureContext *context,
                                  NtConstraints limits, NtCellSize *preferred)
{
    return host_measure(((const WasmComponent *)state)->handle, context,
        limits.columns.min, limits.columns.max, limits.columns.bounded,
        limits.rows.min, limits.rows.max, limits.rows.bounded, preferred);
}

static NtResult component_layout(const void *state, NtLayoutContext *context, NtCellSize size)
{
    return host_layout(((const WasmComponent *)state)->handle, context, size.columns, size.rows);
}

static NtResult component_update(void *state, NtUpdateInfo info, NtUpdateReply *reply)
{
    return host_update(((const WasmComponent *)state)->handle, info.now, info.elapsed, info.motion, reply);
}

static NtResult component_paint(const void *state, NtPaintContext *context, NtCellSize size)
{
    return host_paint(((const WasmComponent *)state)->handle, context, size.columns, size.rows);
}

static NtResult component_event(void *state, NtEventContext *context, const NtEvent *event, NtEventReply *reply)
{
    return host_event(((const WasmComponent *)state)->handle, context, event, reply, event->kind, event->at);
}

static void component_dispose(void *state)
{
    WasmComponent *component = state;
    host_dispose(component->handle);
    free(component);
}

EXPORT_WASM(nocturne_wasm_tree_mount_component)
NtResult nocturne_wasm_tree_mount_component(NtTree *tree, uint32_t slot, uint32_t generation,
                                          uint32_t flags, uint32_t handle, WasmNode *out)
{
    static const NtComponentOps ops = {
        component_measure, component_layout, component_update, component_paint, component_event, component_dispose
    };
    WasmComponent *component = malloc(sizeof(*component));
    if (!component) return NT_RESULT_ALLOCATION_FAILED;
    *component = (WasmComponent){handle};
    NtNodeId node;
    const NtResult result = nt_tree_mount(tree, node_id(tree, slot, generation), &ops, component, mount_options(flags), &node);
    if (result == NT_RESULT_OK) *out = (WasmNode){node.slot, node.generation};
    else free(component);
    return result;
}

EXPORT_WASM(nocturne_wasm_tree_mount_panel_content)
NtResult nocturne_wasm_tree_mount_panel_content(NtTree *tree, uint32_t slot, uint32_t generation,
                                              uint32_t flags, uint32_t handle, uint32_t columns, uint32_t rows,
                                              bool has_update, bool has_event, WasmNode *out)
{
    WasmComponent *component = malloc(sizeof(*component));
    if (!component) return NT_RESULT_ALLOCATION_FAILED;
    *component = (WasmComponent){handle};
    const NtPanelContentProvider provider = {
        .context = component, .preferred = {columns, rows}, .paint = component_paint,
        .update = has_update ? component_update : NULL, .event = has_event ? component_event : NULL,
        .dispose = component_dispose
    };
    NtNodeId node;
    const NtResult result = nt_panel_content_mount(tree, node_id(tree, slot, generation), mount_options(flags),
                                                   &provider, &node);
    if (result == NT_RESULT_OK) *out = (WasmNode){node.slot, node.generation};
    else free(component);
    return result;
}

EXPORT_WASM(nocturne_wasm_measure_count)
uint32_t nocturne_wasm_measure_count(const NtMeasureContext *context)
{
    return (uint32_t)nt_measure_child_count(context);
}

EXPORT_WASM(nocturne_wasm_measure_child)
NtResult nocturne_wasm_measure_child(NtMeasureContext *context, uint32_t index, uint32_t min_columns,
                                    uint32_t max_columns, bool columns_bounded, uint32_t min_rows,
                                    uint32_t max_rows, bool rows_bounded, NtCellSize *out)
{
    const NtConstraints limits = {{min_columns, max_columns, columns_bounded}, {min_rows, max_rows, rows_bounded}};
    NtNodeId child;
    const NtResult result = nt_measure_child_at(context, index, &child);
    return result == NT_RESULT_OK ? nt_measure_child(context, child, limits, out) : result;
}

EXPORT_WASM(nocturne_wasm_layout_count)
uint32_t nocturne_wasm_layout_count(const NtLayoutContext *context)
{
    return (uint32_t)nt_layout_child_count(context);
}

EXPORT_WASM(nocturne_wasm_layout_size)
NtResult nocturne_wasm_layout_size(const NtLayoutContext *context, uint32_t index, NtCellSize *out)
{
    NtNodeId child;
    const NtResult result = nt_layout_child_at(context, index, &child);
    return result == NT_RESULT_OK ? nt_layout_child_size(context, child, out) : result;
}

EXPORT_WASM(nocturne_wasm_layout_place)
NtResult nocturne_wasm_layout_place(NtLayoutContext *context, uint32_t index, int32_t x, int32_t y,
                                   uint32_t columns, uint32_t rows, int32_t depth)
{
    NtNodeId child;
    const NtResult result = nt_layout_child_at(context, index, &child);
    const NtCellRect bounds = {{x, y}, {columns, rows}};
    return result == NT_RESULT_OK ? nt_layout_place(context, child, bounds, depth, NULL) : result;
}

EXPORT_WASM(nocturne_wasm_update_reply)
void nocturne_wasm_update_reply(NtUpdateReply *reply, uint32_t invalidate, bool scheduled, NtTimeUs next)
{
    *reply = (NtUpdateReply){(NtInvalidation)invalidate, {scheduled, next}};
}

EXPORT_WASM(nocturne_wasm_paint_glyph)
NtResult nocturne_wasm_paint_glyph(NtPaintContext *context, int32_t x, int32_t y, const char *data,
                                  uint32_t count, uint32_t columns, uint32_t foreground, uint32_t background)
{
    if (columns > UINT8_MAX) return NT_RESULT_INVALID_ARGUMENT;
    const NtCellStyle style = {color(foreground), color(background)};
    return nt_paint_glyph(context, (NtCellPoint){x, y}, (NtByteView){data, count}, (uint8_t)columns, style);
}

EXPORT_WASM(nocturne_wasm_paint_fill)
NtResult nocturne_wasm_paint_fill(NtPaintContext *context, int32_t x, int32_t y, uint32_t columns,
                                 uint32_t rows, uint32_t fill)
{
    return nt_paint_fill(context, (NtCellRect){{x,y}, {columns, rows}}, color(fill));
}

EXPORT_WASM(nocturne_wasm_tree_tick)
NtResult nocturne_wasm_tree_tick(NtTree *tree, NtTimeUs now, NtMotionPolicy motion)
{
    return nt_tree_tick(tree, now, motion);
}

EXPORT_WASM(nocturne_wasm_tree_next_update)
NtTimeUs nocturne_wasm_tree_next_update(const NtTree *tree, uint32_t *scheduled)
{
    const NtDeadline next = nt_tree_next_update(tree);
    *scheduled = next.scheduled;
    return next.at;
}

EXPORT_WASM(nocturne_wasm_tree_set_focus)
NtResult nocturne_wasm_tree_set_focus(NtTree *tree, uint32_t slot, uint32_t generation)
{
    return nt_tree_set_focus(tree, node_id(tree, slot, generation));
}

typedef struct WasmInput {
    uint64_t at, pointer;
    uint32_t kind, key, state, modifiers, button, pressed, has_position;
    int32_t x, y, dx, dy;
    uint32_t columns, rows, reason;
} WasmInput;

typedef struct WasmDelivery {
    uint32_t admitted;
    WasmNode target;
    uint32_t reserved, consumed, text_delivered;
} WasmDelivery;

typedef struct WasmIntent {
    WasmNode source;
    uint32_t kind;
    WasmView payload;
} WasmIntent;

EXPORT_WASM(nocturne_wasm_tree_dispatch)
NtResult nocturne_wasm_tree_dispatch(NtTree *tree, const WasmInput *input, const char *bytes, uint32_t count,
                                    const NtKeyChord *reserved, uint32_t reserved_count, bool traverse_focus,
                                    WasmDelivery *delivery)
{
    NtEvent event = {.kind = (NtEventKind)input->kind, .at = input->at};
    const NtCellPoint position = {input->x, input->y};
    const NtByteView text = {bytes, count};
    NtByteView key_text = {0};
    switch (event.kind) {
    case NT_EVENT_KEY:
        event.data.key = (NtKeyInput){input->key, input->modifiers, (NtKeyState)input->state};
        key_text = text;
        break;
    case NT_EVENT_TEXT:
    case NT_EVENT_PASTE:
        event.data.text = text;
        break;
    case NT_EVENT_POINTER_DOWN:
    case NT_EVENT_POINTER_MOVE:
    case NT_EVENT_POINTER_UP:
        event.data.pointer = (NtPointerInput){input->pointer, position, input->button,
            input->pressed, input->has_position != 0, input->modifiers};
        break;
    case NT_EVENT_WHEEL:
        event.data.wheel = (NtWheelInput){position, input->dx, input->dy, input->modifiers};
        break;
    case NT_EVENT_POINTER_CANCEL:
        event.data.pointer_cancel = (NtPointerCancel){input->pointer, (NtCancelReason)input->reason};
        break;
    case NT_EVENT_HOST_CANCEL:
        event.data.reason = (NtCancelReason)input->reason;
        break;
    case NT_EVENT_RESIZE:
        event.data.resize = (NtCellSize){input->columns, input->rows};
        break;
    default:
        break;
    }
    const NtDispatchPolicy policy = {reserved, reserved_count, traverse_focus};
    NtDispatchResult out = {.target = {.slot = UINT32_MAX}};
    const NtResult result = nt_tree_dispatch(tree, &event, key_text, &policy, &out);
    *delivery = (WasmDelivery){0};
    if (out.target.slot != UINT32_MAX) {
        *delivery = (WasmDelivery){1, {out.target.slot, out.target.generation},
            out.reserved, out.consumed, out.text_delivered};
    }
    return result;
}

EXPORT_WASM(nocturne_wasm_event_key)
void nocturne_wasm_event_key(const NtEvent *event, uint32_t out[3])
{
    out[0] = event->data.key.key;
    out[1] = event->data.key.modifiers;
    out[2] = event->data.key.state;
}

EXPORT_WASM(nocturne_wasm_event_text)
void nocturne_wasm_event_text(const NtEvent *event, WasmView *out)
{
    *out = (WasmView){event->data.text.data, (uint32_t)event->data.text.count};
}

EXPORT_WASM(nocturne_wasm_event_local)
bool nocturne_wasm_event_local(const NtEventContext *context, NtCellPoint *out)
{
    return nt_event_local_position(context, out);
}

EXPORT_WASM(nocturne_wasm_event_emit)
NtResult nocturne_wasm_event_emit(NtEventContext *context, uint32_t kind, const char *data, uint32_t count)
{
    return nt_event_emit(context, kind, (NtByteView){data, count});
}

EXPORT_WASM(nocturne_wasm_event_consume)
void nocturne_wasm_event_consume(NtEventReply *reply)
{
    reply->consume = true;
}

EXPORT_WASM(nocturne_wasm_event_invalidate)
void nocturne_wasm_event_invalidate(NtEventReply *reply, uint32_t flags)
{
    reply->invalidate = (NtInvalidation)flags;
}

EXPORT_WASM(nocturne_wasm_event_capture)
void nocturne_wasm_event_capture(NtEventReply *reply, uint32_t capture)
{
    reply->capture = (NtCaptureRequest)capture;
}

EXPORT_WASM(nocturne_wasm_tree_focus)
void nocturne_wasm_tree_focus(const NtTree *tree, WasmNode *out)
{
    const NtNodeId id = nt_tree_focus(tree);
    *out = (WasmNode){id.slot, id.generation};
}

EXPORT_WASM(nocturne_wasm_tree_capture)
void nocturne_wasm_tree_capture(const NtTree *tree, NtPointerId pointer, WasmNode *out)
{
    const NtNodeId id = nt_tree_capture(tree, pointer);
    *out = (WasmNode){id.slot, id.generation};
}

EXPORT_WASM(nocturne_wasm_tree_intent)
bool nocturne_wasm_tree_intent(const NtTree *tree, WasmIntent *out)
{
    NtIntentView intent;
    if (!nt_tree_peek_intent(tree, &intent)) return false;
    *out = (WasmIntent){{intent.source.slot, intent.source.generation}, intent.kind,
        {intent.payload.data, (uint32_t)intent.payload.count}};
    return true;
}

EXPORT_WASM(nocturne_wasm_tree_pop_intent)
void nocturne_wasm_tree_pop_intent(NtTree *tree)
{
    nt_tree_pop_intent(tree);
}
