#include <nocturne/window.h>

#include "animation_internal.h"
#include "controls_internal.h"
#include "widgets_internal.h"
#include "wide_product_internal.h"

#include <math.h>

typedef struct WindowWorkspace {
    NtAllocator allocator;
    NtCellSize preferred;
    NtNodeId order[NT_WINDOW_MAX_WINDOWS];
    size_t count;
} WindowWorkspace;

typedef enum DragKind { DRAG_NONE, DRAG_MOVE, DRAG_RESIZE } DragKind;
typedef struct WindowDrag { DragKind kind; NtPointerId id; NtCellPoint pointer; NtCellRect base; } WindowDrag;
typedef struct WindowMotion { NtWindowAnimation timing; NtCellRect from; bool active; } WindowMotion;
typedef struct WindowState {
    NtAllocator allocator;
    NtNodeId mount;
    NtWindowPlacement placement;
    void *panel;
    WindowDrag drag;
    WindowMotion motion;
    bool framed, draggable, resizable;
} WindowState;
typedef struct WindowPaint { NtAllocator allocator; bool move, resize; } WindowPaint;

NT_DECLARE_ALIGNMENT(WindowWorkspace);
NT_DECLARE_ALIGNMENT(WindowState);
NT_DECLARE_ALIGNMENT(WindowPaint);

static const NtComponentOps workspace_ops, window_ops;

static bool valid_rect(NtCellRect bounds)
{
    const uint32_t limit = (uint32_t)INT32_MAX + 1;
    return bounds.size.columns && bounds.size.rows && bounds.size.columns <= limit && bounds.size.rows <= limit;
}

static bool valid_placement(NtWindowPlacement place)
{
    return valid_rect(place.floating) && place.mode >= NT_WINDOW_NORMAL && place.mode <= NT_WINDOW_MAXIMIZED &&
           place.dock >= NT_WINDOW_FLOAT && place.dock <= NT_WINDOW_DOCK_BOTTOM &&
           (place.dock == NT_WINDOW_FLOAT || place.dock_extent != 0);
}

NtWindowOptions nt_window_options_default(void)
{
    return (NtWindowOptions){
        .placement={.floating={{0,0},{24,8}}},
        .panel={.fill={25,32,45,255},.framed=true,.border={.foreground={188,207,236,255}},
                .title={.style={.width={NT_WIDTH_ONE,NT_WIDTH_TWO},.tab_stop=4,
                                .color={.foreground={230,240,252,255}}}}},
        .draggable=true,.resizable=true
    };
}

static WindowState *window_child(const NtTree *tree, NtNodeId node)
{
    return tree->nodes[node.slot].ops.measure == window_ops.measure ? tree->nodes[node.slot].state : NULL;
}

static WindowWorkspace *workspace_parent(const NtTree *tree, NtNodeId node)
{
    const uint32_t parent = tree->nodes[node.slot].parent;
    if (parent == NT_NO_SLOT || tree->nodes[parent].ops.measure != workspace_ops.measure) return NULL;
    return tree->nodes[parent].state;
}

static void raise_window(WindowState *window)
{
    const NtNodeId mount = window->mount;
    NtTree *tree = (NtTree *)mount.tree;
    WindowWorkspace *workspace = workspace_parent(tree,mount);
    if (!workspace) return;
    const uint32_t parent = tree->nodes[mount.slot].parent;
    size_t remaining = 0;
    const bool already_top = workspace->count && nt_node_same(workspace->order[workspace->count-1],mount);
    for (size_t i = 0; i < workspace->count; ++i) {
        const NtNodeId other = workspace->order[i];
        if (!nt_node_same(other,mount) && nt_node_live(tree,other) && tree->nodes[other.slot].parent == parent)
            workspace->order[remaining++] = other;
    }
    if (remaining == NT_WINDOW_MAX_WINDOWS) return;
    workspace->order[remaining++] = mount;
    workspace->count = remaining;
    if (!already_top) nt_tree_dirty(tree,NT_INVALIDATE_LAYOUT);
}

static int32_t stack_position(const WindowWorkspace *workspace, NtNodeId window)
{
    for (size_t i = 0; i < workspace->count; ++i)
        if (nt_node_same(workspace->order[i],window)) return (int32_t)i+1;
    return 0;
}

static NtResult measure_workspace(const void *value, NtMeasureContext *context,
                                   NtConstraints bounds, NtCellSize *out)
{
    const WindowWorkspace *workspace = value;
    if (nt_measure_child_count(context) > NT_WINDOW_MAX_WINDOWS) return NT_RESULT_OUT_OF_RANGE;
    NtNodeId child = {0};
    for (;;) {
        NtResult result = nt_measure_child_after(context,child,&child);
        if (result != NT_RESULT_OK) return result;
        if (!child.tree) break;
        const WindowState *window = window_child(child.tree,child);
        if (!window) return NT_RESULT_INVALID_ARGUMENT;
        if (window->placement.mode == NT_WINDOW_MINIMIZED) continue;
        NtCellSize ignored;
        result = nt_measure_child(context,child,(NtConstraints){0},&ignored);
        if (result != NT_RESULT_OK) return result;
    }
    *out = nt_control_fit(workspace->preferred,bounds);
    return NT_RESULT_OK;
}

static NtCellRect dock_strip(NtCellRect *remaining, NtWindowDock dock, uint32_t request)
{
    NtCellRect strip = *remaining;
    const bool horizontal = dock == NT_WINDOW_DOCK_LEFT || dock == NT_WINDOW_DOCK_RIGHT;
    uint32_t *rest = horizontal ? &remaining->size.columns : &remaining->size.rows;
    uint32_t *extent = horizontal ? &strip.size.columns : &strip.size.rows;
    int32_t *rest_origin = horizontal ? &remaining->origin.x : &remaining->origin.y;
    int32_t *strip_origin = horizontal ? &strip.origin.x : &strip.origin.y;
    *extent = request < *rest ? request : *rest;
    *rest -= *extent;
    if (dock == NT_WINDOW_DOCK_RIGHT || dock == NT_WINDOW_DOCK_BOTTOM)
        *strip_origin = (int32_t)((int64_t)*strip_origin + *rest);
    else if (*rest)
        *rest_origin = (int32_t)((int64_t)*rest_origin + *extent);
    return strip;
}

static bool overlaps(NtCellRect a, NtCellRect b)
{
    return (int64_t)a.origin.x+a.size.columns > b.origin.x &&
           (int64_t)b.origin.x+b.size.columns > a.origin.x &&
           (int64_t)a.origin.y+a.size.rows > b.origin.y &&
           (int64_t)b.origin.y+b.size.rows > a.origin.y;
}

static NtResult arrange_workspace(const void *value, NtLayoutContext *context, NtCellSize size)
{
    const WindowWorkspace *workspace = value;
    if (!size.columns || !size.rows) return NT_RESULT_OK;
    NtCellRect remaining = {{0,0},size};
    if (!valid_rect(remaining)) return NT_RESULT_OUT_OF_RANGE;
    for (unsigned pass = 0; pass < 2; ++pass) {
        NtNodeId child = {0};
        for (;;) {
            NtResult result = nt_layout_child_after(context,child,&child);
            if (result != NT_RESULT_OK) return result;
            if (!child.tree) break;
            const WindowState *window = window_child(child.tree,child);
            const NtWindowPlacement place = window->placement;
            if (place.mode == NT_WINDOW_MINIMIZED) continue;
            const bool docked = place.mode == NT_WINDOW_NORMAL && place.dock != NT_WINDOW_FLOAT;
            if ((pass == 0) != docked) continue;
            if (!remaining.size.columns || !remaining.size.rows) return NT_RESULT_OK;
            const int32_t z = stack_position(workspace,child);
            if (docked)
                result = nt_layout_place(context,child,dock_strip(&remaining,place.dock,place.dock_extent),z,NULL);
            else if (place.mode == NT_WINDOW_MAXIMIZED)
                result = nt_layout_place(context,child,remaining,z,NULL);
            else if (overlaps(place.floating,remaining))
                result = nt_layout_place_clipped(context,child,place.floating,z,remaining);
            if (result != NT_RESULT_OK) return result;
        }
    }
    return NT_RESULT_OK;
}

static NtResult paint_workspace(const void *value, NtPaintContext *paint, NtCellSize assigned)
{
    (void)value;
    (void)paint;
    (void)assigned;
    return NT_RESULT_OK;
}

static void dispose_workspace(void *value)
{
    WindowWorkspace *workspace = value;
    const NtAllocator allocator = workspace->allocator;
    nt_release(&allocator,workspace,sizeof(*workspace),NT_ALIGNMENT(WindowWorkspace));
}

static const NtComponentOps workspace_ops = {
    measure_workspace,arrange_workspace,NULL,paint_workspace,NULL,dispose_workspace
};

static NtResult measure_window(const void *value, NtMeasureContext *context,
                               NtConstraints bounds, NtCellSize *out)
{
    const WindowState *window = value;
    return nt_panel_ops.measure(window->panel,context,bounds,out);
}

static NtResult arrange_window(const void *value, NtLayoutContext *context, NtCellSize assigned)
{
    const WindowState *window = value;
    return nt_panel_ops.layout(window->panel,context,assigned);
}

static bool floating(const WindowState *window)
{
    return window->placement.mode == NT_WINDOW_NORMAL && window->placement.dock == NT_WINDOW_FLOAT;
}

static void dispose_window_paint(void *value)
{
    WindowPaint *info = value;
    const NtAllocator allocator = info->allocator;
    nt_release(&allocator,info,sizeof(*info),NT_ALIGNMENT(WindowPaint));
}

static NtResult paint_window(const void *value, NtPaintContext *paint, NtCellSize assigned)
{
    const WindowState *window = value;
    if (floating(window) && window->framed && (window->draggable || window->resizable)) {
        WindowPaint *info = nt_allocate(&window->allocator,sizeof(*info),NT_ALIGNMENT(WindowPaint));
        if (!info) return NT_RESULT_ALLOCATION_FAILED;
        *info = (WindowPaint){window->allocator,window->draggable,window->resizable};
        nt_component_attach_paint_data(paint,info,dispose_window_paint);
    }
    return nt_panel_ops.paint(window->panel,paint,assigned);
}

static int32_t move_axis(int32_t origin, int64_t delta)
{
    const int64_t value = origin + delta;
    return value < INT32_MIN ? INT32_MIN : value > INT32_MAX ? INT32_MAX : (int32_t)value;
}

static uint32_t resize_axis(uint32_t extent, int64_t delta)
{
    const int64_t value = extent + delta;
    const int64_t maximum = (int64_t)INT32_MAX + 1;
    return value < 1 ? 1 : value > maximum ? (uint32_t)maximum : (uint32_t)value;
}

static void suspend_animation(WindowState *window)
{
    window->motion.active = false;
    NtTree *tree = (NtTree *)window->mount.tree;
    tree->nodes[window->mount.slot].deadline = (NtDeadline){0};
}

static DragKind drag_hit(const WindowState *window, const NtPlacement *place, NtCellPoint local)
{
    const WindowPaint *painted = place->paint_data;
    if (!painted || !window->framed || !floating(window)) return DRAG_NONE;
    const NtCellSize size = place->surface.bounds.size;
    if (painted->resize && window->resizable &&
        local.x == (int64_t)size.columns-1 && local.y == (int64_t)size.rows-1) return DRAG_RESIZE;
    if (painted->move && window->draggable && local.y == 0) return DRAG_MOVE;
    return DRAG_NONE;
}

static void start_drag(WindowState *window, const NtPlacement *place, const NtPointerInput *pointer,
                        DragKind kind, NtEventReply *reply)
{
    const NtTree *tree = window->mount.tree;
    const NtPlacement *parent = nt_tree_placement(tree,place->parent);
    if (!parent) return;
    const NtCellRect bounds = place->surface.bounds;
    const NtCellPoint origin = parent->surface.bounds.origin;
    const NtCellRect local = {{(int32_t)((int64_t)bounds.origin.x-origin.x),
                               (int32_t)((int64_t)bounds.origin.y-origin.y)},bounds.size};
    suspend_animation(window);
    window->placement.floating = local;
    window->drag = (WindowDrag){kind,pointer->id,pointer->position,local};
    reply->focus = tree->nodes[window->mount.slot].options.focusable;
    reply->capture = NT_CAPTURE_TAKE;
    reply->consume = true;
    reply->invalidate = NT_INVALIDATE_LAYOUT;
}

static NtResult event_window(void *value, NtEventContext *context, const NtEvent *event, NtEventReply *reply)
{
    WindowState *window = value;
    const NtTree *tree = window->mount.tree;
    const NtEventPhase phase = nt_event_phase(context);
    if (event->kind == NT_EVENT_FOCUS_GAIN && phase == NT_EVENT_TARGET) raise_window(window);
    if (event->kind == NT_EVENT_POINTER_CANCEL && event->data.pointer_cancel.id == window->drag.id)
        window->drag = (WindowDrag){0};
    if (event->kind == NT_EVENT_POINTER_DOWN) {
        if (phase == NT_EVENT_BUBBLE || window->placement.mode == NT_WINDOW_MINIMIZED) return NT_RESULT_OK;
        raise_window(window);
        if (event->data.pointer.button != NT_BUTTON_PRIMARY ||
            phase != NT_EVENT_TARGET || window->drag.kind != DRAG_NONE) return NT_RESULT_OK;
        const NtPlacement *place = nt_tree_placement(tree,window->mount);
        NtCellPoint local;
        if (place && nt_event_local_position(context,&local)) {
            const DragKind kind = drag_hit(window,place,local);
            if (kind != DRAG_NONE) start_drag(window,place,&event->data.pointer,kind,reply);
        }
        return NT_RESULT_OK;
    }
    if (phase != NT_EVENT_TARGET || window->drag.kind == DRAG_NONE ||
        (event->kind != NT_EVENT_POINTER_MOVE && event->kind != NT_EVENT_POINTER_UP)) return NT_RESULT_OK;
    const NtPointerInput pointer = event->data.pointer;
    if (pointer.id != window->drag.id || !nt_node_same(nt_tree_capture(tree,pointer.id),window->mount))
        return NT_RESULT_OK;
    if (event->kind == NT_EVENT_POINTER_UP && pointer.button != NT_BUTTON_PRIMARY) return NT_RESULT_OK;
    reply->consume = true;
    if (pointer.has_position && floating(window)) {
        NtCellRect rect = window->drag.base;
        const int64_t dx = (int64_t)pointer.position.x-window->drag.pointer.x;
        const int64_t dy = (int64_t)pointer.position.y-window->drag.pointer.y;
        if (window->drag.kind == DRAG_MOVE)
            rect.origin = (NtCellPoint){move_axis(rect.origin.x,dx),move_axis(rect.origin.y,dy)};
        else
            rect.size = (NtCellSize){resize_axis(rect.size.columns,dx),resize_axis(rect.size.rows,dy)};
        window->placement.floating = rect;
        reply->invalidate = NT_INVALIDATE_LAYOUT;
    }
    if (event->kind == NT_EVENT_POINTER_UP) {
        window->drag = (WindowDrag){0};
        if (pointer.pressed) reply->capture = NT_CAPTURE_RELEASE;
    }
    return NT_RESULT_OK;
}

static uint64_t linear_steps(uint64_t delta, NtTimeUs elapsed, NtTimeUs duration, uint64_t *remainder)
{
    const NtWideProduct numerator = nt_wide_product(delta,elapsed);
    if (!numerator.high) {
        *remainder = numerator.low % duration;
        return numerator.low / duration;
    }
    uint64_t low = 0, high = delta;
    while (low < high) {
        const uint64_t middle = low+(high-low+1)/2;
        const NtWideProduct trial = nt_wide_product(middle,duration);
        if (trial.high < numerator.high || (trial.high == numerator.high && trial.low <= numerator.low))
            low = middle;
        else high = middle-1;
    }
    /* A remainder smaller than the 64-bit denominator is the difference
     * of the low words, including any borrow across their boundary. */
    *remainder = numerator.low-nt_wide_product(low,duration).low;
    return low;
}

static int64_t linear_cell(int64_t from, int64_t to, NtTimeUs elapsed, NtTimeUs duration)
{
    if (from == to) return from;
    const bool ascending = from < to;
    const uint64_t delta = (uint64_t)(ascending ? to-from : from-to);
    uint64_t remainder;
    const int64_t steps = (int64_t)linear_steps(delta,elapsed,duration,&remainder);
    const int64_t base = ascending ? from+steps : from-steps;
    const uint64_t other = duration-remainder;
    const bool away = ascending ? base >= 0 : base <= 0;
    if (remainder > other || (remainder == other && away))
        return ascending ? base+1 : base-1;
    return base;
}

static NtCellRect sample_window(NtCellRect from, NtCellRect to, NtWindowAnimation timing, NtTimeUs elapsed)
{
    if (timing.easing == NT_LINEAR) {
        return (NtCellRect){
            {(int32_t)linear_cell(from.origin.x,to.origin.x,elapsed,timing.duration),
             (int32_t)linear_cell(from.origin.y,to.origin.y,elapsed,timing.duration)},
            {(uint32_t)linear_cell(from.size.columns,to.size.columns,elapsed,timing.duration),
             (uint32_t)linear_cell(from.size.rows,to.size.rows,elapsed,timing.duration)}
        };
    }
    double progress;
    (void)nt_ease(timing.easing,nt_animation_progress(elapsed,timing.duration),&progress);
    return (NtCellRect){
        {(int32_t)round(nt_animation_mix(from.origin.x,to.origin.x,progress)),
         (int32_t)round(nt_animation_mix(from.origin.y,to.origin.y,progress))},
        {(uint32_t)round(nt_animation_mix(from.size.columns,to.size.columns,progress)),
         (uint32_t)round(nt_animation_mix(from.size.rows,to.size.rows,progress))}
    };
}

static NtResult update_window(void *value, NtUpdateInfo info, NtUpdateReply *reply)
{
    WindowState *window = value;
    WindowMotion *motion = &window->motion;
    if (!motion->active) return NT_RESULT_OK;
    const NtWindowAnimation timing = motion->timing;
    if (info.now < timing.start) {
        reply->next = (NtDeadline){true,timing.start};
        return NT_RESULT_OK;
    }
    const NtTimeUs elapsed = info.now-timing.start;
    const NtCellRect to = timing.destination;
    if (elapsed >= timing.duration || info.motion == NT_MOTION_REDUCED) {
        window->placement.floating = to;
        motion->active = false;
    } else {
        window->placement.floating = sample_window(motion->from,to,timing,elapsed);
        const NtTimeUs remaining = timing.duration-elapsed;
        reply->next = (NtDeadline){true,info.now+(remaining < 16000 ? remaining : 16000)};
    }
    reply->invalidate = NT_INVALIDATE_LAYOUT;
    return NT_RESULT_OK;
}

static void dispose_window(void *value)
{
    WindowState *window = value;
    const NtAllocator allocator = window->allocator;
    if (window->panel) nt_panel_ops.dispose(window->panel);
    nt_release(&allocator,window,sizeof(*window),NT_ALIGNMENT(WindowState));
}

static const NtComponentOps window_ops = {
    measure_window,arrange_window,update_window,paint_window,event_window,dispose_window
};

NtResult nt_window_workspace_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount,
                                    NtCellSize preferred, NtNodeId *out)
{
    NtResult result = nt_control_validate_mount(tree,parent,out);
    if (result != NT_RESULT_OK) return result;
    tree->busy = true;
    WindowWorkspace *workspace = nt_allocate(&tree->allocator,sizeof(*workspace),NT_ALIGNMENT(WindowWorkspace));
    tree->busy = false;
    if (!workspace) return NT_RESULT_ALLOCATION_FAILED;
    *workspace = (WindowWorkspace){.allocator=tree->allocator,.preferred=preferred};
    result = nt_tree_mount(tree,parent,&workspace_ops,workspace,mount,out);
    if (result != NT_RESULT_OK) {
        tree->busy = true;
        dispose_workspace(workspace);
        tree->busy = false;
    }
    return result;
}

NtResult nt_window_mount(NtTree *tree, NtNodeId workspace, NtMountOptions mount,
                          const NtWindowOptions *options, NtNodeId *out)
{
    if (!tree || !out || !options || !valid_placement(options->placement)) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    NtResult result = nt_node_validate(tree,workspace);
    if (result != NT_RESULT_OK) return result;
    const NtNode *parent = &tree->nodes[workspace.slot];
    if (parent->ops.measure != workspace_ops.measure) return NT_RESULT_INVALID_ARGUMENT;
    if (parent->children >= NT_WINDOW_MAX_WINDOWS) return NT_RESULT_OUT_OF_RANGE;
    tree->busy = true;
    WindowState *window = nt_allocate(&tree->allocator,sizeof(*window),NT_ALIGNMENT(WindowState));
    if (!window) {
        tree->busy = false;
        return NT_RESULT_ALLOCATION_FAILED;
    }
    *window = (WindowState){.allocator=tree->allocator,.placement=options->placement,.framed=options->panel.framed,
                             .draggable=options->draggable,.resizable=options->resizable};
    result = nt_panel_state_create(&tree->allocator,&options->panel,&window->panel);
    tree->busy = false;
    if (result == NT_RESULT_OK) result = nt_tree_mount(tree,workspace,&window_ops,window,mount,out);
    if (result != NT_RESULT_OK) {
        tree->busy = true;
        dispose_window(window);
        tree->busy = false;
        return result;
    }
    window->mount = *out;
    raise_window(window);
    return NT_RESULT_OK;
}

static NtResult get_window(const NtTree *tree, NtNodeId node, WindowState **out)
{
    void *state;
    const NtResult result = nt_control_lookup(tree,node,&window_ops,&state);
    if (result == NT_RESULT_OK) *out = state;
    return result;
}

NtResult nt_window_set(NtTree *tree, NtNodeId node, NtWindowPlacement placement)
{
    if (!tree || !valid_placement(placement)) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    WindowState *window;
    const NtResult result = get_window(tree,node,&window);
    if (result != NT_RESULT_OK) return result;
    window->placement = placement;
    suspend_animation(window);
    window->drag = (WindowDrag){0};
    if (placement.mode == NT_WINDOW_MAXIMIZED) raise_window(window);
    nt_tree_dirty(tree,NT_INVALIDATE_LAYOUT);
    return NT_RESULT_OK;
}

NtResult nt_window_set_panel(NtTree *tree, NtNodeId node, const NtPanelOptions *options)
{
    if (!tree || !options) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    WindowState *window;
    NtResult result = get_window(tree,node,&window);
    if (result != NT_RESULT_OK) return result;
    tree->busy = true;
    void *panel;
    result = nt_panel_state_create(&tree->allocator,options,&panel);
    if (result == NT_RESULT_OK) {
        nt_panel_ops.dispose(window->panel);
        window->panel = panel;
        window->framed = options->framed;
        nt_tree_dirty(tree,NT_INVALIDATE_LAYOUT);
    }
    tree->busy = false;
    return result;
}

NtResult nt_window_get(const NtTree *tree, NtNodeId node, NtWindowState *out)
{
    if (!out) return NT_RESULT_INVALID_ARGUMENT;
    WindowState *window;
    NtResult result = get_window(tree,node,&window);
    if (result != NT_RESULT_OK) return result;
    NtNodeGeometry geometry;
    result = nt_tree_geometry(tree,node,&geometry);
    if (result == NT_RESULT_OK) *out = (NtWindowState){window->placement,geometry,window->motion.active};
    return result;
}

NtResult nt_window_focus(NtTree *tree, NtNodeId node, NtNodeId target)
{
    if (!tree) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    WindowState *window;
    NtResult result = get_window(tree,node,&window);
    if (result != NT_RESULT_OK) return result;
    if (window->placement.mode == NT_WINDOW_MINIMIZED) return NT_RESULT_INVALID_ARGUMENT;
    if (nt_node_null(target)) target = node;
    else {
        result = nt_node_validate(tree,target);
        if (result != NT_RESULT_OK) return result;
        const NtPlacement *ancestor = nt_tree_placement(tree,target);
        while (ancestor && !nt_node_same(ancestor->id,node))
            ancestor = nt_node_null(ancestor->parent) ? NULL : nt_tree_placement(tree,ancestor->parent);
        if (!ancestor) return NT_RESULT_INVALID_ARGUMENT;
    }
    result = nt_tree_set_focus(tree,target);
    if (result == NT_RESULT_OK) raise_window(window);
    return result;
}

NtResult nt_window_animate(NtTree *tree, NtNodeId node, NtWindowAnimation animation)
{
    if (!tree || !valid_rect(animation.destination) || animation.easing < NT_LINEAR ||
        animation.easing > NT_STEP_END) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    if (animation.duration > UINT64_MAX-animation.start) return NT_RESULT_OUT_OF_RANGE;
    WindowState *window;
    const NtResult result = get_window(tree,node,&window);
    if (result != NT_RESULT_OK) return result;
    if (!floating(window) || window->drag.kind != DRAG_NONE) return NT_RESULT_WRONG_PHASE;
    window->motion = (WindowMotion){animation,window->placement.floating,true};
    const NtTimeUs due = animation.start > tree->watermark ? animation.start : tree->watermark;
    tree->nodes[node.slot].deadline = (NtDeadline){true,due};
    return NT_RESULT_OK;
}
