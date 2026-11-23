#include "controls_internal.h"
#include "property_internal.h"

NtControlStyle nt_control_style_default(void)
{
    return (NtControlStyle){
        .normal = {{230,234,241,255},{31,37,48,255}},
        .focused = {{250,251,255,255},{55,70,102,255}},
        .engaged = {{19,26,35,255},{147,214,255,255}},
        .width = {NT_WIDTH_ONE, NT_WIDTH_TWO},
    };
}

bool nt_control_style_valid(NtControlStyle style)
{
    return (style.width.ambiguous == NT_WIDTH_ONE || style.width.ambiguous == NT_WIDTH_TWO) &&
           (style.width.emoji == NT_WIDTH_ONE || style.width.emoji == NT_WIDTH_TWO);
}

const NtAllocator *nt_control_allocator(const NtAllocator *allocator)
{
    return allocator->allocate ? allocator : NULL;
}

NtResult nt_control_validate_mount(const NtTree *tree, NtNodeId parent, const NtNodeId *out)
{
    if (!tree || !out) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    if (nt_node_null(parent)) return tree->root == NT_NO_SLOT ? NT_RESULT_OK : NT_RESULT_INVALID_ARGUMENT;
    return nt_node_validate(tree, parent);
}

NtResult nt_control_lookup(const NtTree *tree, NtNodeId node, const NtComponentOps *ops, void **out)
{
    if (!tree) return NT_RESULT_INVALID_ARGUMENT;
    const NtResult result = nt_node_validate(tree, node);
    if (result != NT_RESULT_OK) return result;
    if (tree->nodes[node.slot].ops.measure != ops->measure) return NT_RESULT_INVALID_ARGUMENT;
    *out = tree->nodes[node.slot].state;
    return NT_RESULT_OK;
}

NtResult nt_control_leaf(const void *state, NtLayoutContext *context, NtCellSize assigned)
{
    (void)state;
    (void)assigned;
    return nt_layout_child_count(context) ? NT_RESULT_INVALID_ARGUMENT : NT_RESULT_OK;
}

static uint32_t fit(uint32_t extent, NtExtentRange bounds)
{
    if (extent < bounds.min) return bounds.min;
    return bounds.bounded && extent > bounds.max ? bounds.max : extent;
}

NtCellSize nt_control_fit(NtCellSize size, NtConstraints constraints)
{
    return (NtCellSize){fit(size.columns, constraints.columns), fit(size.rows, constraints.rows)};
}

NtCellStyle nt_control_colors(NtNodeId node, NtControlStyle style, bool engaged)
{
    if (nt_node_same(nt_tree_focus(node.tree), node)) return style.focused;
    return engaged ? style.engaged : style.normal;
}

NtResult nt_control_text_layout(const NtAllocator *allocator, const NtText *text, NtWidthPolicy width,
                                NtWrapMode wrap, NtConstraints bounds, bool owned, NtTextLayout **out)
{
    if (bounds.columns.min > INT32_MAX) bounds.columns.min = INT32_MAX;
    if (bounds.columns.max > INT32_MAX) bounds.columns.max = INT32_MAX;
    if (bounds.rows.min > INT32_MAX) bounds.rows.min = INT32_MAX;
    if (bounds.rows.max > INT32_MAX) bounds.rows.max = INT32_MAX;
    const NtTextLayoutOptions options = {.width=width,.bounds=bounds,.wrap=wrap,
                                         .base_direction=NT_DIRECTION_AUTO,.tab_stop=4};
    return owned ? nt_text_layout_create(nt_control_allocator(allocator), text, &options, out) :
        nt_text_layout_create_borrowed(nt_control_allocator(allocator), nt_text_bytes(text), &options, out);
}

NtResult nt_control_draw_label(const NtAllocator *allocator, const NtText *text, NtWidthPolicy width,
                                NtPaintContext *paint, uint32_t x, uint32_t y, uint32_t columns,
                                NtCellStyle colors)
{
    if (!columns || !nt_text_bytes(text).count) return NT_RESULT_OK;
    if (x > INT32_MAX || y > INT32_MAX) return NT_RESULT_OUT_OF_RANGE;
    NtTextLayout *layout;
    const NtConstraints bounds = {{0,columns,true},{0,1,true}};
    NtResult result = nt_control_text_layout(allocator,text,width,NT_WRAP_NONE,bounds,false,&layout);
    if (result != NT_RESULT_OK) return result;
    const NtTextPaintOptions options = {.base={.color=colors}};
    result = nt_component_paint_text(paint,(NtCellPoint){(int32_t)x,(int32_t)y},layout,&options);
    nt_text_layout_destroy(layout);
    return result;
}

NtResult nt_control_emit(NtEventContext *context, NtControlEventKind kind, uint64_t value)
{
    char bytes[9] = {(char)kind};
    for (unsigned i = 0; i < 8; ++i) bytes[i + 1] = (char)(uint8_t)(value >> (i * 8));
    return nt_event_emit(context, NT_INTENT_CONTROL, (NtByteView){bytes,sizeof(bytes)});
}

NtResult nt_control_event_decode(NtIntentView intent, NtControlEvent *out)
{
    if (!out || intent.kind != NT_INTENT_CONTROL || intent.payload.count != 9 || !intent.payload.data)
        return NT_RESULT_INVALID_ARGUMENT;
    const unsigned char *bytes = (const unsigned char *)intent.payload.data;
    if (bytes[0] > NT_CONTROL_EDITOR_SUBMITTED) return NT_RESULT_INVALID_ARGUMENT;
    const NtControlEventKind kind = (NtControlEventKind)bytes[0];
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= (uint64_t)bytes[i + 1] << (i * 8);
    if ((kind == NT_CONTROL_CHECKBOX_CHANGED || kind == NT_CONTROL_ITEM_CHANGED) && value > 1)
        return NT_RESULT_INVALID_ARGUMENT;
    if (kind == NT_CONTROL_SCROLL_CHANGED && value > UINT32_MAX) return NT_RESULT_INVALID_ARGUMENT;
    if ((kind == NT_CONTROL_ACTIVATED || kind == NT_CONTROL_EDITOR_CHANGED ||
         kind == NT_CONTROL_EDITOR_SUBMITTED) && value) return NT_RESULT_INVALID_ARGUMENT;
    *out = (NtControlEvent){kind,value};
    return NT_RESULT_OK;
}

bool nt_control_pointer_inside(NtNodeId node, const NtPointerInput *pointer)
{
    NtNodeId found;
    return pointer->has_position && nt_tree_hit(node.tree,pointer->position,&found) == NT_RESULT_OK &&
           nt_node_same(found,node);
}

bool nt_control_pressed(NtNodeId node, NtControlPress press)
{
    return press.tracking && press.inside && nt_node_same(nt_tree_capture(node.tree,press.pointer), node);
}

uint32_t nt_control_key(const NtEvent *event, bool repeat)
{
    if (event->kind != NT_EVENT_KEY) return 0;
    const NtKeyInput key = event->data.key;
    if (key.modifiers & ~(NT_MOD_NUM_LOCK | NT_MOD_CAPS_LOCK)) return 0;
    return key.state == NT_KEY_PRESS || (repeat && key.state == NT_KEY_REPEAT) ? key.key : 0;
}

bool nt_control_press(NtNodeId node, NtControlPress *press, const NtEvent *event, NtEventReply *reply)
{
    if (event->kind == NT_EVENT_FOCUS_GAIN || event->kind == NT_EVENT_FOCUS_LOSS) {
        if (event->kind == NT_EVENT_FOCUS_LOSS) press->tracking = false;
        reply->invalidate = NT_INVALIDATE_PAINT;
        return false;
    }
    if (event->kind == NT_EVENT_POINTER_CANCEL) {
        if (press->tracking && press->pointer == event->data.pointer_cancel.id) {
            press->tracking = false;
            reply->invalidate = NT_INVALIDATE_PAINT;
        }
        return false;
    }
    if (event->kind != NT_EVENT_POINTER_DOWN && event->kind != NT_EVENT_POINTER_MOVE &&
        event->kind != NT_EVENT_POINTER_UP) return false;
    const NtPointerInput *pointer = &event->data.pointer;
    const bool owns = press->tracking && nt_node_same(nt_tree_capture(node.tree,press->pointer),node);
    if (event->kind == NT_EVENT_POINTER_DOWN && pointer->button == NT_BUTTON_PRIMARY && !owns &&
        nt_control_pointer_inside(node,pointer)) {
        *press = (NtControlPress){pointer->id,true,true};
        reply->capture = NT_CAPTURE_TAKE;
        reply->focus = node.tree->nodes[node.slot].options.focusable;
        reply->consume = true;
        reply->invalidate = NT_INVALIDATE_PAINT;
    } else if (owns && press->pointer == pointer->id) {
        const bool inside = nt_control_pointer_inside(node,pointer);
        if (event->kind == NT_EVENT_POINTER_MOVE) {
            if (inside != press->inside) reply->invalidate = NT_INVALIDATE_PAINT;
            press->inside = inside;
            reply->consume = true;
        } else if (event->kind == NT_EVENT_POINTER_UP && pointer->button == NT_BUTTON_PRIMARY) {
            press->tracking = false;
            reply->capture = NT_CAPTURE_RELEASE;
            reply->consume = true;
            reply->invalidate = NT_INVALIDATE_PAINT;
            return inside;
        }
    }
    return false;
}

typedef enum PressKind { BUTTON, ITEM, CHECKBOX } PressKind;
typedef struct PressControl {
    NtAllocator allocator;
    NtNodeId node;
    NtControlStyle style;
    NtText *label;
    NtControlPress press;
    PressKind kind;
    bool value;
} PressControl;

NT_DECLARE_ALIGNMENT(PressControl);

static NtResult measure_press(const void *value, NtMeasureContext *context,
                              NtConstraints bounds, NtCellSize *out)
{
    if (nt_measure_child_count(context)) return NT_RESULT_INVALID_ARGUMENT;
    const PressControl *state = value;
    NtTextLayout *layout;
    const NtResult result = nt_control_text_layout(&state->allocator,state->label,state->style.width,
        NT_WRAP_NONE,(NtConstraints){.rows={0,1,true}},false,&layout);
    if (result != NT_RESULT_OK) return result;
    const uint32_t label = nt_text_layout_size(layout).columns;
    nt_text_layout_destroy(layout);
    const uint32_t chrome = state->kind == BUTTON ? 2u : 4u;
    *out = nt_control_fit((NtCellSize){label + chrome,1},bounds);
    return NT_RESULT_OK;
}

static NtResult press_marker(NtPaintContext *paint, uint32_t columns, uint32_t x, char glyph,
                             NtCellStyle colors)
{
    if (x >= columns) return NT_RESULT_OK;
    return nt_paint_glyph(paint,(NtCellPoint){(int32_t)x,0},(NtByteView){&glyph,1},1,colors);
}

static NtResult paint_press(const void *value, NtPaintContext *paint, NtCellSize size)
{
    if (!size.columns || !size.rows) return NT_RESULT_OK;
    const PressControl *state = value;
    const NtCellStyle colors = nt_control_pressed(state->node,state->press) ? state->style.engaged :
        nt_control_colors(state->node,state->style,state->value);
    NtResult result = nt_paint_fill(paint,(NtCellRect){{0,0},size},colors.background);
    if (result != NT_RESULT_OK) return result;
    uint32_t chrome;
    if (state->kind == BUTTON) {
        chrome = 2;
        result = press_marker(paint,size.columns,0,'[',colors);
        if (result == NT_RESULT_OK && size.columns > 1)
            result = press_marker(paint,size.columns,size.columns-1,']',colors);
    } else {
        chrome = 4;
        const char prefix[] = {'[',state->value ? (state->kind == ITEM ? '*' : 'x') : ' ',']'};
        for (uint32_t x = 0; x < 3 && result == NT_RESULT_OK; ++x)
            result = press_marker(paint,size.columns,x,prefix[x],colors);
    }
    if (result != NT_RESULT_OK || size.columns <= chrome) return result;
    return nt_control_draw_label(&state->allocator,state->label,state->style.width,paint,
                                 state->kind == BUTTON ? 1 : 4,0,size.columns-chrome,colors);
}

static NtResult event_press(void *value, NtEventContext *context, const NtEvent *event, NtEventReply *reply)
{
    if (nt_event_phase(context) != NT_EVENT_TARGET) return NT_RESULT_OK;
    PressControl *state = value;
    const uint32_t key = nt_control_key(event,false);
    if (!nt_control_press(state->node,&state->press,event,reply) && key != NT_KEY_ENTER && key != ' ')
        return NT_RESULT_OK;
    const bool next = state->kind != BUTTON && !state->value;
    const NtControlEventKind kind = state->kind == BUTTON ? NT_CONTROL_ACTIVATED :
        state->kind == ITEM ? NT_CONTROL_ITEM_CHANGED : NT_CONTROL_CHECKBOX_CHANGED;
    const NtResult result = nt_control_emit(context,kind,next);
    if (result != NT_RESULT_OK) {
        /* A failed release still retires the pressed visual. */
        if (event->kind == NT_EVENT_POINTER_UP) nt_tree_dirty((NtTree *)state->node.tree,NT_INVALIDATE_PAINT);
        return result;
    }
    state->value = next;
    reply->consume = true;
    reply->invalidate = NT_INVALIDATE_PAINT;
    return NT_RESULT_OK;
}

static void dispose_press(void *value)
{
    PressControl *state = value;
    const NtAllocator allocator = state->allocator;
    nt_text_destroy(state->label);
    nt_release(&allocator,state,sizeof(*state),NT_ALIGNMENT(PressControl));
}

static const NtComponentOps press_ops = {measure_press,nt_control_leaf,NULL,paint_press,event_press,dispose_press};

static NtResult read_press_flag(const void *value, NtValue *out)
{
    const PressControl *state = value;
    *out = (NtValue){.kind = NT_VALUE_BOOL, .as.flag = state->value};
    return NT_RESULT_OK;
}

static void finish_press_flag(void *value, NtValue flag, void *prepared, bool commit)
{
    (void)prepared;
    PressControl *state = value;
    if (commit) state->value = flag.as.flag;
}

static const NtPropertyDescriptor item_property = {
    NT_PROPERTY_SELECTED, NT_VALUE_BOOL, NT_INVALIDATE_PAINT, read_press_flag, NULL, finish_press_flag
};
static const NtPropertyDescriptor checkbox_property = {
    NT_PROPERTY_CHECKED, NT_VALUE_BOOL, NT_INVALIDATE_PAINT, read_press_flag, NULL, finish_press_flag
};

static NtResult mount_press(NtTree *tree, NtNodeId parent, NtMountOptions mount, NtByteView label,
                            const NtControlStyle *style, PressKind kind, bool value, NtNodeId *out)
{
    NtResult result = nt_control_validate_mount(tree,parent,out);
    if (result != NT_RESULT_OK) return result;
    const NtControlStyle colors = style ? *style : nt_control_style_default();
    if (!nt_control_style_valid(colors)) return NT_RESULT_INVALID_ARGUMENT;
    tree->busy = true;
    PressControl *state = nt_allocate(&tree->allocator,sizeof(*state),NT_ALIGNMENT(PressControl));
    if (!state) {
        tree->busy = false;
        return NT_RESULT_ALLOCATION_FAILED;
    }
    *state = (PressControl){.allocator=tree->allocator,.style=colors,.kind=kind,.value=value};
    result = nt_text_create(nt_control_allocator(&state->allocator),label,&state->label);
    tree->busy = false;
    if (result == NT_RESULT_OK) result = nt_tree_mount(tree,parent,&press_ops,state,mount,&state->node);
    if (result != NT_RESULT_OK) {
        tree->busy = true;
        dispose_press(state);
        tree->busy = false;
        return result;
    }
    if (kind != BUTTON)
        nt_property_stock_table(tree, state->node,
                                    kind == ITEM ? &item_property : &checkbox_property, 1);
    *out = state->node;
    return NT_RESULT_OK;
}

static NtResult press_state(const NtTree *tree, NtNodeId node, PressKind kind, PressControl **out)
{
    void *state;
    const NtResult result = nt_control_lookup(tree,node,&press_ops,&state);
    if (result != NT_RESULT_OK) return result;
    PressControl *control = state;
    if (control->kind != kind) return NT_RESULT_INVALID_ARGUMENT;
    *out = control;
    return NT_RESULT_OK;
}

static NtResult get_press(const NtTree *tree, NtNodeId node, PressKind kind, bool *out)
{
    if (!out) return NT_RESULT_INVALID_ARGUMENT;
    PressControl *state;
    const NtResult result = press_state(tree,node,kind,&state);
    if (result == NT_RESULT_OK)
        *out = kind == BUTTON ? nt_control_pressed(node,state->press) : state->value;
    return result;
}

static NtResult set_press(NtTree *tree, NtNodeId node, PressKind kind, bool value)
{
    if (!tree) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    PressControl *state;
    const NtResult result = press_state(tree,node,kind,&state);
    if (result == NT_RESULT_OK && state->value != value) {
        state->value = value;
        nt_tree_dirty(tree,NT_INVALIDATE_PAINT);
    }
    return result;
}

NtResult nt_button_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount,
                         NtByteView label, const NtControlStyle *style, NtNodeId *out)
{
    return mount_press(tree,parent,mount,label,style,BUTTON,false,out);
}

NtResult nt_item_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount,
                       NtByteView label, const NtControlStyle *style, bool selected, NtNodeId *out)
{
    return mount_press(tree,parent,mount,label,style,ITEM,selected,out);
}

NtResult nt_checkbox_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount,
                           NtByteView label, const NtControlStyle *style, bool checked, NtNodeId *out)
{
    return mount_press(tree,parent,mount,label,style,CHECKBOX,checked,out);
}

NtResult nt_button_pressed(const NtTree *tree, NtNodeId node, bool *out) { return get_press(tree,node,BUTTON,out); }
NtResult nt_item_get(const NtTree *tree, NtNodeId node, bool *out) { return get_press(tree,node,ITEM,out); }
NtResult nt_item_set(NtTree *tree, NtNodeId node, bool selected) { return set_press(tree,node,ITEM,selected); }
NtResult nt_checkbox_get(const NtTree *tree, NtNodeId node, bool *out) { return get_press(tree,node,CHECKBOX,out); }
NtResult nt_checkbox_set(NtTree *tree, NtNodeId node, bool checked) { return set_press(tree,node,CHECKBOX,checked); }
