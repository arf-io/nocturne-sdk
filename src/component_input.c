#include "event_internal.h"
#include "surface_internal.h"

#include <nocturne/text.h>

static const NtPlacement *find_placement(const NtTree *tree, NtNodeId id)
{
    const NtSnapshot *snapshot = &tree->committed;
    if (id.slot >= snapshot->slots || !nt_node_live(tree, id)) return NULL;
    const NtPlacement *place = &snapshot->placements[id.slot];
    return place->placed && nt_node_same(place->id, id) ? place : NULL;
}

const NtPlacement *nt_tree_placement(const NtTree *tree, NtNodeId id)
{
    const NtPlacement *place = find_placement(tree, id);
    for (const NtPlacement *ancestor = place; ancestor && ancestor->parent.tree;) {
        ancestor = find_placement(tree, ancestor->parent);
        if (!ancestor) return NULL;
    }
    return place;
}

NtResult nt_tree_geometry(const NtTree *tree, NtNodeId id, NtNodeGeometry *out)
{
    if (!tree || !out) return NT_RESULT_INVALID_ARGUMENT;
    const NtResult result = nt_node_validate(tree, id);
    if (result != NT_RESULT_OK) return result;
    const NtPlacement *place = nt_tree_placement(tree, id);
    *out = place ? (NtNodeGeometry){place->parent, place->surface.bounds, place->surface.clip,
                                    true, place->enabled} : (NtNodeGeometry){0};
    return NT_RESULT_OK;
}

NtResult nt_tree_hit(const NtTree *tree, NtCellPoint position, NtNodeId *out)
{
    if (!tree || !out) return NT_RESULT_INVALID_ARGUMENT;
    const NtSnapshot *snapshot = &tree->committed;
    for (size_t i = snapshot->count; i; --i) {
        const uint32_t slot = snapshot->order[i-1].slot;
        const NtPlacement *place = &snapshot->placements[slot];
        if (place->enabled && tree->nodes[slot].options.hit_testable &&
            nt_surface_contains_frame(&place->surface, position) && nt_tree_placement(tree, place->id)) {
            *out = place->id;
            return NT_RESULT_OK;
        }
    }
    *out = (NtNodeId){0};
    return NT_RESULT_OK;
}

static bool visible(const NtSurfaceView *surface)
{
    const NtCellRect clip = surface->clip;
    if (!clip.size.columns || !clip.size.rows) return false;
    const NtSurfaceView *ancestor = surface;
    while (ancestor && !ancestor->masked) ancestor = ancestor->parent;
    if (!ancestor) return true;
    for (int64_t y = clip.origin.y; y < (int64_t)clip.origin.y + clip.size.rows; ++y)
        for (int64_t x = clip.origin.x; x < (int64_t)clip.origin.x + clip.size.columns; ++x)
            if (nt_surface_contains_frame(surface, (NtCellPoint){(int32_t)x, (int32_t)y})) return true;
    return false;
}

static bool eligible(const NtTree *tree, NtNodeId id)
{
    const NtPlacement *place = nt_tree_placement(tree, id);
    return place && place->enabled && visible(&place->surface);
}

static bool focusable(const NtTree *tree, NtNodeId id)
{
    return eligible(tree, id) && tree->nodes[id.slot].options.focusable;
}

NtEventPhase nt_event_phase(const NtEventContext *context)
{
    return context ? context->phase : NT_EVENT_TARGET;
}

bool nt_event_local_position(const NtEventContext *context, NtCellPoint *out)
{
    NtLocalPosition position;
    if (!out || !nt_component_local_position(context, &position) ||
        position.x < INT32_MIN || position.x > INT32_MAX ||
        position.y < INT32_MIN || position.y > INT32_MAX) return false;
    *out = (NtCellPoint){(int32_t)position.x,(int32_t)position.y};
    return true;
}

bool nt_component_local_position(const NtEventContext *context, NtLocalPosition *out)
{
    if (!context || !context->has_position) return false;
    *out = context->local;
    return true;
}

struct NtGesture {
    NtGesture *next;
    NtPointerId id;
    NtNodeId capture, cancelled;
    NtCancelReason reason;
    uint32_t pressed;
    bool suppressed;
};

NT_DECLARE_ALIGNMENT(NtGesture);

typedef const NtPlacement *NtRouteEntry;
NT_DECLARE_ALIGNMENT(NtRouteEntry);

typedef struct RouteAction {
    NtEventReply target_reply;
    bool consumed, target_completed;
} RouteAction;

static NtResult finish_callback(NtEventContext *context, NtEventReply reply, NtResult result)
{
    NtTree *tree = context->tree;
    if (result == NT_RESULT_OK) result = context->failure;
    if (result == NT_RESULT_OK &&
        (reply.invalidate < NT_INVALIDATE_NONE || reply.invalidate > NT_INVALIDATE_LAYOUT ||
         reply.capture < NT_CAPTURE_KEEP || reply.capture > NT_CAPTURE_RELEASE))
        result = NT_RESULT_INVALID_ARGUMENT;
    if (result != NT_RESULT_OK) {
        nt_component_discard_pending_intents(context);
        return result;
    }
    nt_tree_dirty(tree, reply.invalidate);
    nt_component_commit_pending_intents(context);
    return NT_RESULT_OK;
}

static void notify(NtTree *tree, NtNodeId id, NtEvent event)
{
    if (nt_node_null(id)) return;
    const NtNode *node = &tree->nodes[id.slot];
    if (!node->ops.event) return;
    NtEventContext context = {.tree=tree,.source=id,.event=&event,.phase=NT_EVENT_TARGET};
    NtEventReply reply = {0};
    const NtResult result = node->ops.event(node->state, &context, &event, &reply);
    (void)finish_callback(&context, reply, result);
}

static NtGesture **gesture_link(NtTree *tree, NtPointerId id)
{
    NtGesture **link = &tree->gestures;
    while (*link && (*link)->id < id) link = &(*link)->next;
    return link;
}

static void release_gesture(NtTree *tree, NtGesture **link)
{
    NtGesture *gesture = *link;
    *link = gesture->next;
    nt_release(&tree->allocator, gesture, sizeof(*gesture), NT_ALIGNMENT(NtGesture));
}

void nt_tree_release_gestures(NtTree *tree)
{
    while (tree->gestures) release_gesture(tree, &tree->gestures);
}

NtNodeId nt_tree_capture(const NtTree *tree, NtPointerId pointer)
{
    const NtGesture *gesture = tree ? tree->gestures : NULL;
    while (gesture && gesture->id < pointer) gesture = gesture->next;
    return gesture && gesture->id == pointer ? gesture->capture : (NtNodeId){0};
}

static void mark_cancelled(NtGesture *gesture, NtCancelReason reason)
{
    gesture->cancelled = gesture->capture;
    gesture->capture = (NtNodeId){0};
    gesture->reason = reason;
    gesture->suppressed = gesture->pressed != 0;
}

static void notify_cancelled(NtTree *tree, NtGesture *gesture, NtTimeUs time)
{
    const NtNodeId cancelled = gesture->cancelled;
    gesture->cancelled = (NtNodeId){0};
    notify(tree, cancelled, (NtEvent){.kind=NT_EVENT_POINTER_CANCEL,.at=time,
                                     .data.pointer_cancel={gesture->id,gesture->reason}});
}

static void notify_focus(NtTree *tree, NtNodeId prior, NtCancelReason reason, NtTimeUs time)
{
    if (nt_node_same(prior, tree->focus)) return;
    notify(tree, prior, (NtEvent){.kind=NT_EVENT_FOCUS_LOSS,.at=time,.data.reason=reason});
    notify(tree, tree->focus, (NtEvent){.kind=NT_EVENT_FOCUS_GAIN,.at=time});
}

static void change_focus(NtTree *tree, NtNodeId focus, NtTimeUs time)
{
    const NtNodeId prior = tree->focus;
    if (nt_node_same(prior, focus)) return;
    tree->focus = focus;
    if (!nt_node_null(prior)) {
        for (NtGesture *g = tree->gestures; g; g = g->next)
            if (nt_node_same(g->capture, prior)) mark_cancelled(g, NT_CANCEL_FOCUS_LOSS);
        for (NtGesture *g = tree->gestures; g; g = g->next) notify_cancelled(tree, g, time);
    }
    notify_focus(tree, prior, NT_CANCEL_FOCUS_LOSS, time);
}

static NtCancelReason loss_reason(const NtTree *tree, NtNodeId id)
{
    return nt_node_live(tree, id) ? NT_CANCEL_INELIGIBLE : NT_CANCEL_REMOVED;
}

void nt_tree_prune_input(NtTree *tree)
{
    const NtNodeId prior = tree->focus;
    if (!nt_node_null(prior) && !focusable(tree, prior)) tree->focus = (NtNodeId){0};
    for (NtGesture *g = tree->gestures; g; g = g->next) {
        const NtNodeId owner = g->capture;
        if (nt_node_null(owner)) continue;
        if (!eligible(tree, owner)) mark_cancelled(g, loss_reason(tree, owner));
        else if (nt_node_same(owner, prior) && nt_node_null(tree->focus))
            mark_cancelled(g, NT_CANCEL_FOCUS_LOSS);
    }
    for (NtGesture *g = tree->gestures; g; g = g->next) notify_cancelled(tree, g, tree->watermark);
    notify_focus(tree, prior, loss_reason(tree, prior), tree->watermark);
}

NtResult nt_tree_set_focus(NtTree *tree, NtNodeId id)
{
    if (!tree) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    if (!nt_node_null(id)) {
        const NtResult result = nt_node_validate(tree, id);
        if (result != NT_RESULT_OK) return result;
        if (!focusable(tree, id)) return NT_RESULT_INVALID_ARGUMENT;
    }
    tree->busy = true;
    change_focus(tree, id, tree->watermark);
    tree->busy = false;
    return NT_RESULT_OK;
}

NtNodeId nt_tree_focus(const NtTree *tree)
{
    return tree ? tree->focus : (NtNodeId){0};
}

static NtNodeId traversal_target(const NtTree *tree, bool reverse)
{
    const NtSnapshot *snapshot = &tree->committed;
    NtNodeId first = {0}, last = {0};
    bool seen_focus = false;
    uint32_t slot = snapshot->count ? snapshot->order[0].slot : NT_NO_SLOT;
    while (slot != NT_NO_SLOT) {
        const NtPlacement *place = &snapshot->placements[slot];
        slot = place->traversal_next;
        const NtNodeId id = place->id;
        if (!focusable(tree, id)) continue;
        if (nt_node_same(id, tree->focus)) {
            if (reverse && !nt_node_null(last)) return last;
            seen_focus = true;
        } else if (!reverse && seen_focus) return id;
        if (nt_node_null(first)) first = id;
        last = id;
    }
    return reverse ? last : first;
}

NtResult nt_tree_focus_advance(NtTree *tree, bool reverse)
{
    if (!tree) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    tree->busy = true;
    change_focus(tree, traversal_target(tree, reverse), tree->watermark);
    tree->busy = false;
    return NT_RESULT_OK;
}

static bool is_pointer(NtEventKind kind)
{
    return kind == NT_EVENT_POINTER_DOWN || kind == NT_EVENT_POINTER_MOVE ||
           kind == NT_EVENT_POINTER_UP;
}

static void localize(NtEventContext *context, const NtPlacement *place, const NtEvent *event)
{
    NtCellPoint point;
    if (event->kind == NT_EVENT_WHEEL) point = event->data.wheel.position;
    else if (is_pointer(event->kind) && event->data.pointer.has_position)
        point = event->data.pointer.position;
    else return;
    context->local = (NtLocalPosition){(int64_t)point.x - place->surface.bounds.origin.x,
                                       (int64_t)point.y - place->surface.bounds.origin.y};
    context->has_position = true;
}

static bool valid_route_reply(const NtTree *tree, NtNodeId id, NtEventPhase phase,
                               const NtEvent *event, const NtGesture *gesture, NtEventReply reply)
{
    if (phase != NT_EVENT_TARGET) return !reply.focus && reply.capture == NT_CAPTURE_KEEP;
    if (reply.focus && !focusable(tree, id)) return false;
    if (reply.capture == NT_CAPTURE_KEEP) return true;
    if (!gesture || !is_pointer(event->kind)) return false;
    const bool owns = nt_node_same(gesture->capture, id);
    return reply.capture == NT_CAPTURE_RELEASE ? owns :
           gesture->pressed && (nt_node_null(gesture->capture) || owns);
}

static NtResult deliver(NtTree *tree, const NtPlacement *place, NtEventPhase phase,
                        const NtEvent *event, const NtGesture *gesture, RouteAction *action)
{
    NtEventReply reply = {0};
    const NtNode *node = &tree->nodes[place->id.slot];
    if (node->ops.event) {
        NtEventContext context = {.tree=tree,.source=place->id,.event=event,.phase=phase};
        localize(&context, place, event);
        NtResult result = node->ops.event(node->state, &context, event, &reply);
        if (result == NT_RESULT_OK && context.failure == NT_RESULT_OK &&
            !valid_route_reply(tree, place->id, phase, event, gesture, reply))
            result = NT_RESULT_INVALID_ARGUMENT;
        result = finish_callback(&context, reply, result);
        if (result != NT_RESULT_OK) return result;
    }
    if (phase == NT_EVENT_TARGET) {
        action->target_completed = node->ops.event != NULL;
        action->target_reply = reply;
    }
    action->consumed = reply.consume;
    return NT_RESULT_OK;
}

static NtResult route(NtTree *tree, NtNodeId target, const NtEvent *event,
                      const NtGesture *gesture, RouteAction *action)
{
    const NtPlacement *leaf = nt_tree_placement(tree, target);
    if (!leaf) return NT_RESULT_OK;
    const NtPlacement *storage[16];
    const NtPlacement **path = storage;
    size_t depth = 1;
    for (const NtPlacement *p = leaf; p->parent.tree;) {
        p = &tree->committed.placements[p->parent.slot];
        ++depth;
    }
    if (depth > sizeof(storage) / sizeof(storage[0])) {
        if (depth > SIZE_MAX / sizeof(*path)) return NT_RESULT_OUT_OF_RANGE;
        path = nt_allocate(&tree->allocator, depth * sizeof(*path), NT_ALIGNMENT(NtRouteEntry));
        if (!path) return NT_RESULT_ALLOCATION_FAILED;
    }
    path[0] = leaf;
    for (size_t i = 1; i < depth; ++i)
        path[i] = &tree->committed.placements[path[i-1]->parent.slot];
    NtResult result = NT_RESULT_OK;
    for (size_t i = depth - 1; i && !action->consumed && result == NT_RESULT_OK; --i)
        result = deliver(tree, path[i], NT_EVENT_PREVIEW, event, gesture, action);
    if (!action->consumed && result == NT_RESULT_OK)
        result = deliver(tree, leaf, NT_EVENT_TARGET, event, gesture, action);
    for (size_t i = 1; i < depth && !action->consumed && result == NT_RESULT_OK; ++i)
        result = deliver(tree, path[i], NT_EVENT_BUBBLE, event, gesture, action);
    if (path != storage)
        nt_release(&tree->allocator, path, depth * sizeof(*path), NT_ALIGNMENT(NtRouteEntry));
    return result;
}

static void apply_action(NtTree *tree, NtNodeId target, const RouteAction *action,
                          NtGesture *gesture, NtTimeUs time)
{
    if (gesture && action->target_reply.capture == NT_CAPTURE_TAKE) gesture->capture = target;
    if (gesture && action->target_reply.capture == NT_CAPTURE_RELEASE) {
        if (gesture->pressed) {
            mark_cancelled(gesture, NT_CANCEL_RELEASE);
            notify_cancelled(tree, gesture, time);
        } else gesture->capture = (NtNodeId){0};
    }
    if (action->target_reply.focus) change_focus(tree, target, time);
}

static NtResult admit_pointer(NtTree *tree, const NtEvent *event,
                              NtGesture ***link_out, NtGesture **gesture_out)
{
    const NtPointerInput input = event->data.pointer;
    NtGesture **link = gesture_link(tree, input.id);
    NtGesture *gesture = *link && (*link)->id == input.id ? *link : NULL;
    const uint32_t old = gesture ? gesture->pressed : 0;
    uint32_t expected = old;
    if (event->kind == NT_EVENT_POINTER_DOWN) {
        if (old & input.button) return NT_RESULT_INVALID_ARGUMENT;
        expected |= input.button;
    } else if (event->kind == NT_EVENT_POINTER_UP) {
        if (!(old & input.button)) return NT_RESULT_INVALID_ARGUMENT;
        expected &= ~input.button;
    }
    if (expected != input.pressed) return NT_RESULT_INVALID_ARGUMENT;
    if (!gesture && expected) {
        gesture = nt_allocate(&tree->allocator, sizeof(*gesture), NT_ALIGNMENT(NtGesture));
        if (!gesture) return NT_RESULT_ALLOCATION_FAILED;
        *gesture = (NtGesture){.next=*link,.id=input.id};
        *link = gesture;
    }
    if (gesture) gesture->pressed = expected;
    *link_out = link;
    *gesture_out = gesture;
    return NT_RESULT_OK;
}

static NtResult dispatch_pointer(NtTree *tree, const NtEvent *event, NtDispatchResult *out,
                                  bool *admitted)
{
    NtGesture **link;
    NtGesture *gesture;
    NtResult result = admit_pointer(tree, event, &link, &gesture);
    if (result != NT_RESULT_OK) return result;
    *admitted = true;
    const NtPointerInput input = event->data.pointer;
    const bool final = gesture && !input.pressed;
    if (gesture && gesture->suppressed) out->consumed = true;
    else {
        const NtNodeId before = gesture ? gesture->capture : (NtNodeId){0};
        out->target = before;
        if (nt_node_null(before) && input.has_position)
            (void)nt_tree_hit(tree, input.position, &out->target);
        RouteAction action = {0};
        result = route(tree, out->target, event, gesture, &action);
        out->consumed = action.consumed;
        if (gesture && event->kind == NT_EVENT_POINTER_UP && !nt_node_null(before) && !action.target_completed) {
            mark_cancelled(gesture, NT_CANCEL_ROUTE_INTERRUPTED);
            notify_cancelled(tree, gesture, event->at);
        } else if (final) {
            gesture->capture = (NtNodeId){0};
        }
        if (result == NT_RESULT_OK) apply_action(tree, out->target, &action, gesture, event->at);
    }
    if (final) release_gesture(tree, link);
    return result;
}

static void dispatch_cancel(NtTree *tree, const NtEvent *event)
{
    if (event->kind == NT_EVENT_POINTER_CANCEL) {
        const NtPointerCancel cancel = event->data.pointer_cancel;
        NtGesture **link = gesture_link(tree, cancel.id);
        NtGesture *gesture = *link;
        if (gesture && gesture->id == cancel.id) {
            const NtNodeId capture = gesture->capture;
            release_gesture(tree, link);
            notify(tree, capture, *event);
        }
        return;
    }

    const NtNodeId prior = tree->focus;
    tree->focus = (NtNodeId){0};
    for (NtGesture *g = tree->gestures; g; g = g->next) {
        mark_cancelled(g, event->data.reason);
        g->pressed = 0;
    }
    for (NtGesture *g = tree->gestures; g; g = g->next) notify_cancelled(tree, g, event->at);
    nt_tree_release_gestures(tree);
    notify_focus(tree, prior, event->data.reason, event->at);
}

static bool traversal_key(const NtEvent *event)
{
    if (event->kind != NT_EVENT_KEY) return false;
    const NtKeyInput key = event->data.key;
    const uint32_t allowed = NT_MOD_SHIFT | NT_MOD_NUM_LOCK | NT_MOD_CAPS_LOCK;
    return key.key == NT_KEY_TAB && key.state != NT_KEY_RELEASE && !(key.modifiers & ~allowed);
}

static NtResult dispatch_regular(NtTree *tree, const NtEvent *event, NtByteView key_text,
                                  const NtDispatchPolicy *policy, NtDispatchResult *out)
{
    if (event->kind == NT_EVENT_WHEEL)
        (void)nt_tree_hit(tree, event->data.wheel.position, &out->target);
    else if (event->kind == NT_EVENT_RESIZE) {
        const NtSnapshot *snapshot = &tree->committed;
        if (snapshot->count) {
            const NtPlacement *root = &snapshot->placements[snapshot->order[0].slot];
            if (root->enabled && nt_node_live(tree, root->id)) out->target = root->id;
        }
    } else out->target = tree->focus;

    RouteAction action = {0};
    NtResult result = route(tree, out->target, event, NULL, &action);
    out->consumed = action.consumed;
    if (result != NT_RESULT_OK) return result;
    apply_action(tree, out->target, &action, NULL, event->at);
    if (action.consumed) return NT_RESULT_OK;
    if (policy && policy->traverse_focus && traversal_key(event)) {
        change_focus(tree, traversal_target(tree, (event->data.key.modifiers & NT_MOD_SHIFT) != 0),
                     event->at);
        out->consumed = true;
    } else if (key_text.count) {
        const NtEvent text = {.kind=NT_EVENT_TEXT,.at=event->at,.data.text=key_text};
        RouteAction text_action = {0};
        const NtNodeId target = tree->focus;
        result = route(tree, target, &text, NULL, &text_action);
        out->text_delivered = text_action.target_completed;
        if (result == NT_RESULT_OK) apply_action(tree, target, &text_action, NULL, event->at);
    }
    return result;
}

NtResult nt_key_policy_check(const NtDispatchPolicy *policy, const NtKeyInput *input, bool *reserved)
{
    if (!policy || !policy->reserved_count) return NT_RESULT_OK;
    if (!policy->reserved) return NT_RESULT_INVALID_ARGUMENT;
    if (policy->reserved_count > SIZE_MAX / sizeof(*policy->reserved))
        return NT_RESULT_OUT_OF_RANGE;
    for (size_t i = 0; i < policy->reserved_count; ++i) {
        const NtKeyChord chord = policy->reserved[i];
        const NtEvent key = {.kind=NT_EVENT_KEY,.data.key={chord.key,chord.modifiers,NT_KEY_PRESS}};
        const NtResult result = nt_event_validate(&key);
        if (result != NT_RESULT_OK) return result;
        if (input && input->key == chord.key && input->modifiers == chord.modifiers)
            *reserved = true;
    }
    return NT_RESULT_OK;
}

static NtResult check_dispatch(const NtEvent *event, NtByteView key_text,
                               const NtDispatchPolicy *policy, bool *reserved)
{
    NtResult result = nt_event_validate(event);
    if (result != NT_RESULT_OK) return result;
    if (event->kind == NT_EVENT_FOCUS_GAIN || event->kind == NT_EVENT_FOCUS_LOSS)
        return NT_RESULT_INVALID_ARGUMENT;
    if (key_text.count) {
        if (event->kind != NT_EVENT_KEY || event->data.key.state == NT_KEY_RELEASE)
            return NT_RESULT_INVALID_ARGUMENT;
        result = nt_utf8_validate(key_text, NULL);
        if (result != NT_RESULT_OK) return result;
    }
    return event->kind == NT_EVENT_KEY ? nt_key_policy_check(policy, &event->data.key, reserved) : NT_RESULT_OK;
}

NtResult nt_tree_dispatch(NtTree *tree, const NtEvent *event, NtByteView key_text,
                           const NtDispatchPolicy *policy, NtDispatchResult *out)
{
    if (!tree || !out) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    NtDispatchResult report = {0};
    NtResult result = check_dispatch(event, key_text, policy, &report.reserved);
    if (result != NT_RESULT_OK) return result;
    if (report.reserved) {
        *out = report;
        return NT_RESULT_OK;
    }
    tree->busy = true;
    bool admitted = true;
    if (is_pointer(event->kind)) {
        admitted = false;
        result = dispatch_pointer(tree, event, &report, &admitted);
    } else if (event->kind == NT_EVENT_HOST_CANCEL || event->kind == NT_EVENT_POINTER_CANCEL) {
        dispatch_cancel(tree, event);
    } else result = dispatch_regular(tree, event, key_text, policy, &report);
    tree->busy = false;
    if (admitted) *out = report;
    return result;
}
