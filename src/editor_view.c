#include "controls_internal.h"
#include "editor_internal.h"

#include <string.h>

typedef struct EditorView {
    NtAllocator allocator;
    NtNodeId node;
    NtEditor *editor;
    NtEditorKeyMap keys;
    NtEditorBinding *bindings;
    NtControlStyle style;
    NtWrapMode wrap;
    NtCellSize requested;
    NtControlPress press;
    bool follow_caret, following, show_caret, submit, scroll_keys;
} EditorView;

typedef struct EditorPaint {
    NtAllocator allocator;
    NtTextLayout *layout;
    NtCellSize offset, limit, window;
} EditorPaint;

NT_DECLARE_ALIGNMENT(EditorView);
NT_DECLARE_ALIGNMENT(EditorPaint);
NT_DECLARE_ALIGNMENT(NtEditorBinding);

static uint32_t min_extent(uint32_t a, uint32_t b) { return a < b ? a : b; }
static uint32_t limit_offset(uint32_t extent, uint32_t window) { return extent > window ? extent - window : 0; }

static NtResult measure_editor(const void *value, NtMeasureContext *context, NtConstraints bounds,
                               NtCellSize *out)
{
    if (nt_measure_child_count(context)) return NT_RESULT_INVALID_ARGUMENT;
    const EditorView *view = value;
    if (bounds.columns.bounded && !bounds.columns.max) {
        *out = nt_control_fit((NtCellSize){0,1},bounds);
        return NT_RESULT_OK;
    }
    NtTextLayout *layout;
    NtConstraints text_bounds = {.columns={.min=1}};
    if (view->wrap != NT_WRAP_NONE) {
        text_bounds.columns.max = bounds.columns.max;
        text_bounds.columns.bounded = bounds.columns.bounded;
    }
    const NtResult result = nt_control_text_layout(&view->allocator,nt_editor_text(view->editor),
                                                  view->style.width,view->wrap,text_bounds,false,&layout);
    if (result != NT_RESULT_OK) return result;
    *out = nt_control_fit(nt_text_layout_size(layout),bounds);
    nt_text_layout_destroy(layout);
    return NT_RESULT_OK;
}

static void dispose_editor_paint(void *value)
{
    EditorPaint *snapshot = value;
    const NtAllocator allocator = snapshot->allocator;
    nt_text_layout_destroy(snapshot->layout);
    nt_release(&allocator,snapshot,sizeof(*snapshot),NT_ALIGNMENT(EditorPaint));
}

static uint32_t follow_offset(uint32_t offset, uint32_t window, uint32_t cell)
{
    if (cell < offset) return cell;
    return cell - offset >= window ? cell - window + 1 : offset;
}

static NtResult paint_editor(const void *value, NtPaintContext *paint, NtCellSize size)
{
    const EditorView *view = value;
    if (!size.columns || !size.rows) return NT_RESULT_OK;
    const NtCellSize window = {min_extent(size.columns,INT32_MAX),min_extent(size.rows,INT32_MAX)};
    const bool wrapped = view->wrap != NT_WRAP_NONE;
    const NtConstraints bounds = {.columns={window.columns,window.columns,wrapped}};
    EditorPaint *snapshot = nt_allocate(&view->allocator,sizeof(*snapshot),NT_ALIGNMENT(EditorPaint));
    if (!snapshot) return NT_RESULT_ALLOCATION_FAILED;
    *snapshot = (EditorPaint){.allocator=view->allocator,.window=window};
    nt_component_attach_paint_data(paint,snapshot,dispose_editor_paint);
    NtResult result = nt_control_text_layout(&view->allocator,nt_editor_text(view->editor),view->style.width,
                                            view->wrap,bounds,true,&snapshot->layout);
    if (result != NT_RESULT_OK) return result;

    NtCellPoint caret;
    const bool has_caret = (view->following || view->show_caret) &&
        nt_editor_caret_position(view->editor,snapshot->layout,&caret) == NT_RESULT_OK;
    const NtCellSize extent = nt_text_layout_size(snapshot->layout);
    /* An edge at the end of a full wrapped row highlights its last cell. */
    if (has_caret && wrapped && (uint32_t)caret.x == window.columns) --caret.x;
    const uint32_t columns = has_caret && view->show_caret && !wrapped &&
        (uint32_t)caret.x == extent.columns ? extent.columns+1 : extent.columns;
    const NtCellSize limit = {limit_offset(columns,window.columns),limit_offset(extent.rows,window.rows)};
    NtCellSize offset = {min_extent(view->requested.columns,limit.columns),
                         min_extent(view->requested.rows,limit.rows)};
    if (has_caret && view->following) {
        /* Keep the published origin until the caret crosses an edge. */
        const NtPlacement *previous = nt_tree_placement(view->node.tree,view->node);
        const EditorPaint *prior = previous ? previous->paint_data : NULL;
        if (prior) {
            offset.columns = min_extent(prior->offset.columns,limit.columns);
            offset.rows = min_extent(prior->offset.rows,limit.rows);
        }
        offset.columns = min_extent(follow_offset(offset.columns,window.columns,(uint32_t)caret.x),limit.columns);
        offset.rows = min_extent(follow_offset(offset.rows,window.rows,(uint32_t)caret.y),limit.rows);
    }
    snapshot->offset = offset;
    snapshot->limit = limit;
    const NtCellStyle colors = nt_control_colors(view->node,view->style,false);
    result = nt_paint_fill(paint,(NtCellRect){{0,0},size},colors.background);
    if (result != NT_RESULT_OK) return result;

    const NtEditorSelection selection = nt_editor_selection(view->editor);
    const size_t anchor = selection.anchor.offset, active = selection.active.offset;
    const size_t first = anchor < active ? anchor : active;
    const size_t end = anchor < active ? active : anchor;
    const NtTextPaintSpan span = {.source={first,end-first},.style={.color=view->style.engaged}};
    const NtTextPaintOptions paint_options = {.base={.color=colors},.spans=&span,.count=end > first};
    result = nt_component_paint_text(paint,(NtCellPoint){-(int32_t)offset.columns,-(int32_t)offset.rows},
                                     snapshot->layout,&paint_options);
    if (result != NT_RESULT_OK || !has_caret || !view->show_caret ||
        !nt_node_same(nt_tree_focus(view->node.tree),view->node)) return result;
    const int64_t x = (int64_t)caret.x - offset.columns, y = (int64_t)caret.y - offset.rows;
    if (x < 0 || y < 0 || x >= window.columns || y >= window.rows) return NT_RESULT_OK;
    return nt_paint_fill(paint,(NtCellRect){{(int32_t)x,(int32_t)y},{1,1}},view->style.engaged.background);
}

static const EditorPaint *editor_snapshot(NtNodeId node)
{
    const NtPlacement *placement = nt_tree_placement(node.tree,node);
    return placement ? placement->paint_data : NULL;
}

static uint32_t visible_cell(int64_t cell, uint32_t last)
{
    return cell <= 0 ? 0 : cell >= last ? last : (uint32_t)cell;
}

static NtResult editor_pointer(EditorView *view, NtEventContext *context, const NtEvent *event,
                               NtEventReply *reply)
{
    (void)nt_control_press(view->node,&view->press,event,reply);
    const bool down = event->kind == NT_EVENT_POINTER_DOWN && reply->capture == NT_CAPTURE_TAKE;
    const bool drag = event->kind == NT_EVENT_POINTER_MOVE && reply->consume;
    const bool up = event->kind == NT_EVENT_POINTER_UP && reply->capture == NT_CAPTURE_RELEASE;
    if (!down && !drag && !up) return NT_RESULT_OK;
    NtLocalPosition point;
    const EditorPaint *snapshot = editor_snapshot(view->node);
    if (!snapshot || !nt_component_local_position(context,&point)) return NT_RESULT_OK;
    const NtCellSize extent = nt_text_layout_size(snapshot->layout);
    const uint32_t x = visible_cell(point.x,snapshot->window.columns) + snapshot->offset.columns;
    const uint32_t y = visible_cell(point.y,snapshot->window.rows-1) + snapshot->offset.rows;
    const NtCellPoint cell = {(int32_t)min_extent(x,extent.columns),(int32_t)min_extent(y,extent.rows-1)};
    const bool extend = !down || (event->data.pointer.modifiers & NT_MOD_SHIFT);
    const NtResult result = nt_editor_select_at(view->editor,snapshot->layout,cell,extend);
    if (result == NT_RESULT_STALE_REFERENCE || result == NT_RESULT_OUT_OF_RANGE) {
        if (down) {
            reply->capture = NT_CAPTURE_KEEP;
            view->press.tracking = false;
        }
        if (result == NT_RESULT_STALE_REFERENCE) reply->invalidate = NT_INVALIDATE_LAYOUT;
        return NT_RESULT_OK;
    }
    if (result == NT_RESULT_OK) {
        view->following = view->follow_caret;
        reply->invalidate = NT_INVALIDATE_PAINT;
    }
    return result;
}

static uint32_t scroll_axis(uint32_t origin, int32_t delta, uint32_t limit)
{
    const int64_t next = (int64_t)origin + delta;
    return next <= 0 ? 0 : next >= limit ? limit : (uint32_t)next;
}

static void editor_scroll_input(EditorView *view, const NtEvent *event, NtEventReply *reply)
{
    const EditorPaint *snapshot = editor_snapshot(view->node);
    if (!snapshot) return;
    const NtCellSize from = view->following ? snapshot->offset : view->requested;
    const NtCellSize offset = {min_extent(from.columns,snapshot->limit.columns),
                               min_extent(from.rows,snapshot->limit.rows)};
    NtCellSize next = offset;
    if (event->kind == NT_EVENT_WHEEL) {
        const NtWheelInput wheel = event->data.wheel;
        next.columns = scroll_axis(offset.columns,wheel.dx,snapshot->limit.columns);
        next.rows = scroll_axis(offset.rows,wheel.dy,snapshot->limit.rows);
    } else {
        const int32_t page = (int32_t)snapshot->window.rows;
        switch (nt_control_key(event,true)) {
            case NT_KEY_UP: next.rows = scroll_axis(offset.rows,-1,snapshot->limit.rows); break;
            case NT_KEY_DOWN: next.rows = scroll_axis(offset.rows,1,snapshot->limit.rows); break;
            case NT_KEY_LEFT: next.columns = scroll_axis(offset.columns,-1,snapshot->limit.columns); break;
            case NT_KEY_RIGHT: next.columns = scroll_axis(offset.columns,1,snapshot->limit.columns); break;
            case NT_KEY_PAGE_UP: next.rows = scroll_axis(offset.rows,-page,snapshot->limit.rows); break;
            case NT_KEY_PAGE_DOWN: next.rows = scroll_axis(offset.rows,page,snapshot->limit.rows); break;
            case NT_KEY_HOME: next.rows = 0; break;
            case NT_KEY_END: next.rows = snapshot->limit.rows; break;
            default: return;
        }
        reply->consume = true;
    }
    if (next.columns != offset.columns || next.rows != offset.rows) {
        view->requested = next;
        view->following = false;
        reply->invalidate = NT_INVALIDATE_PAINT;
        reply->consume = true;
    }
}

static NtResult event_editor(void *value, NtEventContext *context, const NtEvent *event, NtEventReply *reply)
{
    if (nt_event_phase(context) != NT_EVENT_TARGET) return NT_RESULT_OK;
    EditorView *view = value;
    if (event->kind == NT_EVENT_WHEEL) {
        editor_scroll_input(view,event,reply);
        return NT_RESULT_OK;
    }
    if (event->kind != NT_EVENT_KEY && event->kind != NT_EVENT_TEXT && event->kind != NT_EVENT_PASTE)
        return editor_pointer(view,context,event,reply);
    if (view->submit && nt_control_key(event,true) == NT_KEY_ENTER) {
        reply->consume = true;
        return event->data.key.state == NT_KEY_PRESS ?
            nt_control_emit(context,NT_CONTROL_EDITOR_SUBMITTED,0) : NT_RESULT_OK;
    }
    const NtEditorBinding *binding = event->kind == NT_EVENT_KEY ? nt_editor_match_key(&view->keys,event->data.key) : NULL;
    if (event->kind == NT_EVENT_KEY && !binding && view->scroll_keys) {
        NtKeyInput press = event->data.key;
        press.state = NT_KEY_PRESS;
        if (!nt_editor_match_key(&view->keys,press)) editor_scroll_input(view,event,reply);
        return NT_RESULT_OK;
    }
    const bool edit = event->kind != NT_EVENT_KEY || (binding && binding->action != NT_EDITOR_ACTION_MOVE &&
                      binding->action != NT_EDITOR_ACTION_REGISTER_COPY);
    if (edit && nt_editor_is_read_only(view->editor)) return NT_RESULT_OK;
    if (edit) {
        const NtResult result = nt_control_emit(context,NT_CONTROL_EDITOR_CHANGED,0);
        if (result != NT_RESULT_OK) return result;
    }
    const uint64_t before = nt_editor_change_serial(view->editor);
    bool handled;
    const NtResult result = nt_editor_input(view->editor,event,&view->keys,&handled);
    if (result == NT_RESULT_UNSUPPORTED_CAPABILITY || result == NT_RESULT_INVALID_ARGUMENT) {
        if (edit) nt_component_discard_pending_intents(context);
        return NT_RESULT_OK;
    }
    if (result != NT_RESULT_OK) return result;
    const bool changed = before != nt_editor_change_serial(view->editor);
    if (edit && !changed) nt_component_discard_pending_intents(context);
    if (handled) {
        view->following = view->follow_caret;
        reply->consume = true;
        reply->invalidate = changed ? NT_INVALIDATE_LAYOUT : NT_INVALIDATE_PAINT;
    }
    return NT_RESULT_OK;
}

static void dispose_editor(void *value)
{
    EditorView *view = value;
    const NtAllocator allocator = view->allocator;
    nt_release(&allocator,view->bindings,view->keys.count*sizeof(*view->bindings),NT_ALIGNMENT(NtEditorBinding));
    nt_release(&allocator,view,sizeof(*view),NT_ALIGNMENT(EditorView));
}

static const NtComponentOps editor_ops = {measure_editor,nt_control_leaf,NULL,paint_editor,event_editor,dispose_editor};

static NtResult validate_bindings(const NtEditorKeyMap *keys)
{
    if (!keys) return NT_RESULT_OK;
    if (keys->count && !keys->bindings) return NT_RESULT_INVALID_ARGUMENT;
    if (keys->count > SIZE_MAX / sizeof(*keys->bindings)) return NT_RESULT_OUT_OF_RANGE;
    for (size_t i = 0; i < keys->count; ++i) {
        const NtEditorBinding b = keys->bindings[i];
        const NtEvent key = {.kind=NT_EVENT_KEY,.data.key={b.chord.key,b.chord.modifiers,NT_KEY_PRESS}};
        if (nt_event_validate(&key) != NT_RESULT_OK || b.action < NT_EDITOR_ACTION_MOVE ||
            b.action > NT_EDITOR_ACTION_REGISTER_PASTE ||
            (b.action == NT_EDITOR_ACTION_MOVE &&
             (b.move < NT_EDITOR_PREVIOUS_CLUSTER || b.move > NT_EDITOR_DOCUMENT_END)))
            return NT_RESULT_INVALID_ARGUMENT;
        if (b.action == NT_EDITOR_ACTION_REGISTER_PASTE || b.action == NT_EDITOR_ACTION_REGISTER_COPY) {
            if (!keys->registers) return NT_RESULT_INVALID_ARGUMENT;
            NtEditorRegisterValue ignored;
            const NtResult result = nt_editor_register_read(keys->registers,b.slot,&ignored);
            if (result != NT_RESULT_OK) return result;
        }
    }
    return NT_RESULT_OK;
}

NtResult nt_editor_view_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount,
                              const NtEditorViewOptions *options, NtNodeId *out)
{
    NtResult result = nt_control_validate_mount(tree,parent,out);
    if (result != NT_RESULT_OK) return result;
    if (!options || !options->editor || options->wrap < NT_WRAP_NONE || options->wrap > NT_WRAP_CLUSTER)
        return NT_RESULT_INVALID_ARGUMENT;
    const NtControlStyle style = options->style ? *options->style : nt_control_style_default();
    if (!nt_control_style_valid(style)) return NT_RESULT_INVALID_ARGUMENT;
    result = validate_bindings(options->keys);
    if (result != NT_RESULT_OK) return result;
    tree->busy = true;
    EditorView *view = nt_allocate(&tree->allocator,sizeof(*view),NT_ALIGNMENT(EditorView));
    if (!view) {
        tree->busy = false;
        return NT_RESULT_ALLOCATION_FAILED;
    }
    *view = (EditorView){.allocator=tree->allocator,.editor=options->editor,.style=style,.wrap=options->wrap,
                        .follow_caret=options->follow_caret,.following=options->follow_caret,
                        .show_caret=options->show_caret,.submit=options->submit_on_enter,
                        .scroll_keys=options->scroll_keys};
    if (options->keys && options->keys->count) {
        const size_t bytes = options->keys->count*sizeof(*view->bindings);
        view->bindings = nt_allocate(&view->allocator,bytes,NT_ALIGNMENT(NtEditorBinding));
        if (!view->bindings) {
            dispose_editor(view);
            tree->busy = false;
            return NT_RESULT_ALLOCATION_FAILED;
        }
        memcpy(view->bindings,options->keys->bindings,bytes);
        view->keys = (NtEditorKeyMap){view->bindings,options->keys->count,options->keys->registers};
    }
    tree->busy = false;
    result = nt_tree_mount(tree,parent,&editor_ops,view,mount,&view->node);
    if (result == NT_RESULT_OK) *out = view->node;
    else {
        tree->busy = true;
        dispose_editor(view);
        tree->busy = false;
    }
    return result;
}

NtResult nt_editor_view_scroll(NtTree *tree, NtNodeId node, NtCellSize requested)
{
    if (!tree) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    void *state;
    const NtResult result = nt_control_lookup(tree,node,&editor_ops,&state);
    if (result != NT_RESULT_OK) return result;
    EditorView *view = state;
    view->requested = requested;
    view->following = false;
    nt_tree_dirty(tree,NT_INVALIDATE_PAINT);
    return NT_RESULT_OK;
}

NtResult nt_editor_view_offset(const NtTree *tree, NtNodeId node, NtCellSize *out)
{
    if (!out) return NT_RESULT_INVALID_ARGUMENT;
    void *state;
    const NtResult result = nt_control_lookup(tree,node,&editor_ops,&state);
    if (result != NT_RESULT_OK) return result;
    const EditorPaint *snapshot = editor_snapshot(node);
    if (!snapshot) return NT_RESULT_WRONG_PHASE;
    *out = snapshot->offset;
    return NT_RESULT_OK;
}
