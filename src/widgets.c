#include "widgets_internal.h"
#include <nocturne/widgets.h>

#include "property_internal.h"
#include "text_layout_internal.h"

#include <string.h>

typedef union WidgetStyle {
    NtLabelStyle label;
    NtPanelOptions panel;
    NtRuleOptions rule;
} WidgetStyle;

typedef struct WidgetState {
    NtAllocator allocator;
    WidgetStyle style;
    NtText *text;
    NtClipMask mask;
} WidgetState;

NT_DECLARE_ALIGNMENT(WidgetState);
NT_DECLARE_ALIGNMENT(NtRowSpan);

static const NtAllocator *text_allocator(const WidgetState *state)
{
    return state->allocator.allocate ? &state->allocator : NULL;
}

static uint32_t limit_extent(uint32_t value, NtExtentRange range)
{
    if (value < range.min) return range.min;
    return range.bounded && value > range.max ? range.max : value;
}

static NtCellSize fit_size(NtCellSize size, NtConstraints bounds)
{
    return (NtCellSize){limit_extent(size.columns, bounds.columns),
                        limit_extent(size.rows, bounds.rows)};
}

static uint32_t add_extent(uint32_t a, uint32_t b)
{
    return a > UINT32_MAX - b ? UINT32_MAX : a + b;
}

static uint32_t subtract_extent(uint32_t extent, uint32_t inset)
{
    return extent > inset ? extent - inset : 0;
}

static NtExtentRange text_extent(NtExtentRange range)
{
    if (range.min > INT32_MAX) range.min = INT32_MAX;
    if (range.bounded && range.max > INT32_MAX) range.max = INT32_MAX;
    return range;
}

static NtTextLayoutOptions layout_options(NtLabelStyle style, NtConstraints bounds)
{
    bounds.columns = text_extent(bounds.columns);
    bounds.rows = text_extent(bounds.rows);
    return (NtTextLayoutOptions){style.width,bounds,style.wrap,style.align,style.direction,
                                style.tab_stop,style.ellipsis};
}

static NtResult text_layout(const WidgetState *state, NtLabelStyle style,
                            NtConstraints bounds, NtTextLayout **out)
{
    const NtTextLayoutOptions options = layout_options(style, bounds);
    return nt_text_layout_create_borrowed(text_allocator(state), nt_text_bytes(state->text),
                                          &options, out);
}

static NtResult measure_text(const WidgetState *state, NtLabelStyle style,
                             NtConstraints bounds, NtCellSize *out)
{
    NtTextLayout *layout;
    NtResult result = text_layout(state, style, bounds, &layout);
    if (result != NT_RESULT_OK) return result;
    *out = fit_size(nt_text_layout_size(layout), bounds);
    nt_text_layout_destroy(layout);
    return NT_RESULT_OK;
}

static NtResult paint_text(const WidgetState *state, NtPaintContext *paint, NtLabelStyle style,
                           NtCellPoint origin, NtCellSize area)
{
    if (!state->text || !area.columns || !area.rows) return NT_RESULT_OK;
    const NtCellRect clip = nt_paint_clip(paint);
    if (!clip.size.columns || !clip.size.rows) return NT_RESULT_OK;
    const NtConstraints bounds = {{area.columns,area.columns,true},{0,area.rows,true}};
    NtTextLayout *layout;
    NtResult result = text_layout(state, style, bounds, &layout);
    if (result != NT_RESULT_OK) return result;
    const NtTextPaintOptions options = {.base={.color=style.color}};
    result = nt_component_paint_text(paint, origin, layout, &options);
    nt_text_layout_destroy(layout);
    return result;
}

static NtResult measure_label(const void *value, NtMeasureContext *context,
                              NtConstraints bounds, NtCellSize *out)
{
    if (nt_measure_child_count(context)) return NT_RESULT_INVALID_ARGUMENT;
    const WidgetState *state = value;
    return measure_text(state, state->style.label, bounds, out);
}

static NtResult arrange_leaf(const void *value, NtLayoutContext *context, NtCellSize assigned)
{
    (void)value;
    (void)assigned;
    return nt_layout_child_count(context) ? NT_RESULT_INVALID_ARGUMENT : NT_RESULT_OK;
}

static NtResult paint_label(const void *value, NtPaintContext *paint, NtCellSize assigned)
{
    const WidgetState *state = value;
    return paint_text(state, paint, state->style.label, (NtCellPoint){0}, assigned);
}

static NtBoxInsets panel_insets(const NtPanelOptions *panel)
{
    NtBoxInsets insets = panel->padding;
    if (panel->framed)
        insets = (NtBoxInsets){add_extent(insets.left,1),add_extent(insets.top,1),
                               add_extent(insets.right,1),add_extent(insets.bottom,1)};
    return insets;
}

static NtExtentRange inner_range(NtExtentRange outer, uint32_t inset)
{
    return (NtExtentRange){0, subtract_extent(outer.max,inset), outer.bounded};
}

static NtLabelStyle title_style(const NtPanelOptions *panel)
{
    NtLabelStyle style = panel->title.style;
    style.wrap = NT_WRAP_NONE;
    return style;
}

static NtResult measure_panel(const void *value, NtMeasureContext *context,
                              NtConstraints bounds, NtCellSize *out)
{
    const WidgetState *state = value;
    const NtPanelOptions *panel = &state->style.panel;
    const size_t count = nt_measure_child_count(context);
    if (count > 1) return NT_RESULT_INVALID_ARGUMENT;
    const NtBoxInsets insets = panel_insets(panel);
    const uint32_t horizontal = add_extent(insets.left,insets.right);
    const uint32_t vertical = add_extent(insets.top,insets.bottom);
    NtCellSize content = {0};
    if (count) {
        NtNodeId child;
        NtResult result = nt_measure_child_at(context, 0, &child);
        if (result != NT_RESULT_OK) return result;
        const NtConstraints inner = {inner_range(bounds.columns,horizontal),
                                     inner_range(bounds.rows,vertical)};
        result = nt_measure_child(context, child, inner, &content);
        if (result != NT_RESULT_OK) return result;
    }
    NtCellSize desired = {add_extent(content.columns,horizontal),add_extent(content.rows,vertical)};
    if (state->text) {
        NtCellSize title;
        const NtConstraints strip = {inner_range(bounds.columns,2),{0,1,true}};
        const NtResult result = measure_text(state, title_style(panel), strip, &title);
        if (result != NT_RESULT_OK) return result;
        const uint32_t title_width = add_extent(title.columns,2);
        if (title_width > desired.columns) desired.columns = title_width;
    }
    *out = fit_size(desired, bounds);
    return NT_RESULT_OK;
}

static NtResult arrange_panel(const void *value, NtLayoutContext *context, NtCellSize assigned)
{
    const WidgetState *state = value;
    const NtPanelOptions *panel = &state->style.panel;
    if (!nt_layout_child_count(context)) return NT_RESULT_OK;
    const NtBoxInsets insets = panel_insets(panel);
    const NtCellSize inner = {subtract_extent(assigned.columns,add_extent(insets.left,insets.right)),
                              subtract_extent(assigned.rows,add_extent(insets.top,insets.bottom))};
    if (!inner.columns || !inner.rows) return NT_RESULT_OK;
    if (insets.left > INT32_MAX || insets.top > INT32_MAX) return NT_RESULT_OUT_OF_RANGE;
    NtNodeId child;
    const NtResult result = nt_layout_child_at(context, 0, &child);
    if (result != NT_RESULT_OK) return result;
    const NtCellRect body = {{(int32_t)insets.left,(int32_t)insets.top},inner};
    return nt_layout_place(context, child, body, 0, panel->body_mask);
}

typedef enum Stroke { HORIZONTAL, VERTICAL, TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, BOTTOM_RIGHT } Stroke;

static NtResult paint_stroke(NtPaintContext *paint, int32_t x, int32_t y, Stroke stroke,
                             bool ascii, NtCellStyle style)
{
    static const char *const unicode[] = {"─","│","┌","┐","└","┘"};
    static const char *const plain[] = {"-","|","+","+","+","+"};
    const NtByteView glyph = {ascii ? plain[stroke] : unicode[stroke],ascii ? 1 : 3};
    return nt_paint_glyph(paint, (NtCellPoint){x,y}, glyph, 1, style);
}

static int64_t clip_end(int32_t start, uint32_t extent)
{
    const int64_t end = (int64_t)start + extent;
    const int64_t limit = (int64_t)INT32_MAX + 1;
    return end < limit ? end : limit;
}

static NtResult paint_border(const NtPanelOptions *panel, NtPaintContext *paint,
                             NtCellSize assigned)
{
    const NtCellRect clip = nt_paint_clip(paint);
    if (!clip.size.columns || !clip.size.rows) return NT_RESULT_OK;
    const int64_t left = clip.origin.x, right = clip_end(clip.origin.x,clip.size.columns);
    const int64_t bottom = clip_end(clip.origin.y,clip.size.rows);
    const int64_t far_x = (int64_t)assigned.columns - 1, far_y = (int64_t)assigned.rows - 1;
    for (int64_t y = clip.origin.y; y < bottom; ++y) {
        if (y == 0 || y == far_y) {
            for (int64_t x = left; x < right; ++x) {
                Stroke stroke = HORIZONTAL;
                if (x == 0) stroke = y == 0 ? TOP_LEFT : BOTTOM_LEFT;
                else if (x == far_x) stroke = y == 0 ? TOP_RIGHT : BOTTOM_RIGHT;
                const NtResult result = paint_stroke(paint,(int32_t)x,(int32_t)y,stroke,
                                                     panel->ascii,panel->border);
                if (result != NT_RESULT_OK) return result;
            }
            continue;
        }
        if (left == 0) {
            const NtResult result = paint_stroke(paint,0,(int32_t)y,VERTICAL,panel->ascii,panel->border);
            if (result != NT_RESULT_OK) return result;
        }
        if (far_x > 0 && far_x >= left && far_x < right) {
            const NtResult result = paint_stroke(paint,(int32_t)far_x,(int32_t)y,VERTICAL,
                                                 panel->ascii,panel->border);
            if (result != NT_RESULT_OK) return result;
        }
    }
    return NT_RESULT_OK;
}

static NtResult paint_panel(const void *value, NtPaintContext *paint, NtCellSize assigned)
{
    const WidgetState *state = value;
    const NtPanelOptions *panel = &state->style.panel;
    NtResult result = nt_paint_fill(paint, (NtCellRect){{0,0},assigned}, panel->fill);
    if (result != NT_RESULT_OK || !panel->framed) return result;
    result = paint_border(panel, paint, assigned);
    if (result != NT_RESULT_OK || assigned.columns <= 2) return result;
    const NtCellSize strip = {assigned.columns - 2, assigned.rows ? 1 : 0};
    return paint_text(state, paint, title_style(panel), (NtCellPoint){1,0}, strip);
}

static NtResult measure_rule(const void *value, NtMeasureContext *context,
                             NtConstraints bounds, NtCellSize *out)
{
    (void)value;
    if (nt_measure_child_count(context)) return NT_RESULT_INVALID_ARGUMENT;
    *out = fit_size((NtCellSize){1,1}, bounds);
    return NT_RESULT_OK;
}

static NtResult paint_rule(const void *value, NtPaintContext *paint, NtCellSize assigned)
{
    (void)assigned;
    const NtRuleOptions rule = ((const WidgetState *)value)->style.rule;
    const NtCellRect clip = nt_paint_clip(paint);
    if (!clip.size.columns || !clip.size.rows) return NT_RESULT_OK;
    if (rule.vertical && clip.origin.x != 0) return NT_RESULT_OK;
    if (!rule.vertical && clip.origin.y != 0) return NT_RESULT_OK;
    const int32_t start = rule.vertical ? clip.origin.y : clip.origin.x;
    const uint32_t length = rule.vertical ? clip.size.rows : clip.size.columns;
    const int64_t end = clip_end(start,length);
    for (int64_t i = start; i < end; ++i) {
        const NtResult result = paint_stroke(paint,rule.vertical ? 0 : (int32_t)i,
            rule.vertical ? (int32_t)i : 0,rule.vertical ? VERTICAL : HORIZONTAL,rule.ascii,rule.color);
        if (result != NT_RESULT_OK) return result;
    }
    return NT_RESULT_OK;
}

static void dispose_widget(void *value)
{
    WidgetState *state = value;
    const NtAllocator allocator = state->allocator;
    nt_text_destroy(state->text);
    nt_release(&allocator,(void *)state->mask.spans,state->mask.count * sizeof(NtRowSpan),
               NT_ALIGNMENT(NtRowSpan));
    nt_release(&allocator,state,sizeof(*state),NT_ALIGNMENT(WidgetState));
}

static const NtComponentOps label_ops = {
    measure_label,arrange_leaf,NULL,paint_label,NULL,dispose_widget
};
const NtComponentOps nt_panel_ops = {
    measure_panel,arrange_panel,NULL,paint_panel,NULL,dispose_widget
};
static const NtComponentOps rule_ops = {
    measure_rule,arrange_leaf,NULL,paint_rule,NULL,dispose_widget
};

static NtResult copy_widget(const NtAllocator *allocator, const NtComponentOps *ops,
                            WidgetStyle style, NtByteView text, WidgetState **out)
{
    const NtClipMask *mask = NULL;
    if (ops == &nt_panel_ops) {
        text = style.panel.title.text;
        mask = style.panel.body_mask;
        style.panel.title.text = (NtByteView){0};
        style.panel.body_mask = NULL;
    }
    if (mask && mask->count && !mask->spans) return NT_RESULT_INVALID_ARGUMENT;
    if (mask && mask->count > SIZE_MAX / sizeof(NtRowSpan)) return NT_RESULT_OUT_OF_RANGE;
    WidgetState *state = nt_allocate(allocator,sizeof(*state),NT_ALIGNMENT(WidgetState));
    if (!state) return NT_RESULT_ALLOCATION_FAILED;
    *state = (WidgetState){.allocator=*allocator,.style=style};
    if (mask) {
        state->style.panel.body_mask = &state->mask;
        if (mask->count) {
            const size_t bytes = mask->count * sizeof(NtRowSpan);
            NtRowSpan *copy = nt_allocate(allocator,bytes,NT_ALIGNMENT(NtRowSpan));
            if (!copy) {
                dispose_widget(state);
                return NT_RESULT_ALLOCATION_FAILED;
            }
            memcpy(copy,mask->spans,bytes);
            state->mask = (NtClipMask){copy,mask->count};
        }
    }
    if (text.count) {
        const NtResult result = nt_text_create(text_allocator(state),text,&state->text);
        if (result != NT_RESULT_OK) {
            dispose_widget(state);
            return result;
        }
    }
    *out = state;
    return NT_RESULT_OK;
}

static NtResult mount_widget(NtTree *tree, NtNodeId parent, NtMountOptions options,
                             const NtComponentOps *ops, WidgetStyle style,
                             NtByteView label_text, NtNodeId *out)
{
    if (!tree || !out) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    if (nt_node_null(parent)) {
        if (tree->root != NT_NO_SLOT) return NT_RESULT_INVALID_ARGUMENT;
    } else {
        const NtResult result = nt_node_validate(tree,parent);
        if (result != NT_RESULT_OK) return result;
    }
    tree->busy = true;
    WidgetState *state;
    NtResult result = copy_widget(&tree->allocator,ops,style,label_text,&state);
    tree->busy = false;
    if (result != NT_RESULT_OK) return result;
    result = nt_tree_mount(tree,parent,ops,state,options,out);
    if (result != NT_RESULT_OK) dispose_widget(state);
    return result;
}

static NtResult set_widget(NtTree *tree, NtNodeId node, const NtComponentOps *ops,
                           WidgetStyle style, NtByteView label_text)
{
    if (!tree) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    NtResult result = nt_node_validate(tree,node);
    if (result != NT_RESULT_OK) return result;
    NtNode *mount = &tree->nodes[node.slot];
    if (mount->ops.measure != ops->measure) return NT_RESULT_INVALID_ARGUMENT;
    tree->busy = true;
    WidgetState *state;
    result = copy_widget(&tree->allocator,ops,style,label_text,&state);
    if (result == NT_RESULT_OK) {
        WidgetState *old = mount->state;
        mount->state = state;
        nt_tree_dirty(tree,NT_INVALIDATE_LAYOUT);
        dispose_widget(old);
    }
    tree->busy = false;
    return result;
}

static bool valid_label(const NtLabelOptions *label)
{
    if (!label || (label->text.count && !label->text.data)) return false;
    const NtTextLayoutOptions options = layout_options(label->style,(NtConstraints){0});
    return nt_text_layout_options_valid(&options);
}

static bool valid_panel(const NtPanelOptions *panel)
{
    return panel && (!panel->title.text.count || (panel->framed && valid_label(&panel->title)));
}

NtResult nt_panel_state_create(const NtAllocator *allocator, const NtPanelOptions *options, void **out)
{
    if (!valid_panel(options)) return NT_RESULT_INVALID_ARGUMENT;
    WidgetState *panel;
    const NtResult result = copy_widget(allocator,&nt_panel_ops,(WidgetStyle){.panel=*options},
                                        (NtByteView){0},&panel);
    if (result == NT_RESULT_OK) *out = panel;
    return result;
}

static NtResult read_label_text(const void *value, NtValue *out)
{
    const WidgetState *state = value;
    *out = (NtValue){.kind = NT_VALUE_TEXT, .as.text = nt_text_bytes(state->text)};
    return NT_RESULT_OK;
}

static NtResult prepare_label_text(const void *value, NtValue text, void **prepared)
{
    const WidgetState *state = value;
    NtText *copy;
    const NtResult result = nt_text_create(text_allocator(state), text.as.text, &copy);
    if (result == NT_RESULT_OK) *prepared = copy;
    return result;
}

static void finish_label_text(void *value, NtValue text, void *prepared, bool commit)
{
    (void)text;
    WidgetState *state = value;
    if (commit) {
        nt_text_destroy(state->text);
        state->text = prepared;
    } else nt_text_destroy(prepared);
}

static NtResult read_label_foreground(const void *value, NtValue *out)
{
    const WidgetState *state = value;
    *out = (NtValue){.kind = NT_VALUE_RGBA, .as.rgba = state->style.label.color.foreground};
    return NT_RESULT_OK;
}

static NtResult read_label_background(const void *value, NtValue *out)
{
    const WidgetState *state = value;
    *out = (NtValue){.kind = NT_VALUE_RGBA, .as.rgba = state->style.label.color.background};
    return NT_RESULT_OK;
}

static void finish_label_foreground(void *value, NtValue color, void *prepared, bool commit)
{
    (void)prepared;
    WidgetState *state = value;
    if (commit) state->style.label.color.foreground = color.as.rgba;
}

static void finish_label_background(void *value, NtValue color, void *prepared, bool commit)
{
    (void)prepared;
    WidgetState *state = value;
    if (commit) state->style.label.color.background = color.as.rgba;
}

static const NtPropertyDescriptor label_properties[] = {
    {NT_PROPERTY_TEXT, NT_VALUE_TEXT, NT_INVALIDATE_LAYOUT, read_label_text,
     prepare_label_text, finish_label_text},
    {NT_PROPERTY_FOREGROUND, NT_VALUE_RGBA, NT_INVALIDATE_PAINT, read_label_foreground,
     NULL, finish_label_foreground},
    {NT_PROPERTY_BACKGROUND, NT_VALUE_RGBA, NT_INVALIDATE_PAINT, read_label_background,
     NULL, finish_label_background}
};

NtResult nt_label_mount(NtTree *tree, NtNodeId parent, NtMountOptions options,
                        const NtLabelOptions *label, NtNodeId *out)
{
    if (!valid_label(label)) return NT_RESULT_INVALID_ARGUMENT;
    const NtResult result = mount_widget(tree,parent,options,&label_ops,
                                         (WidgetStyle){.label=label->style},label->text,out);
    if (result == NT_RESULT_OK)
        nt_property_stock_table(tree, *out, label_properties,
                                sizeof(label_properties) / sizeof(*label_properties));
    return result;
}

NtResult nt_label_set(NtTree *tree, NtNodeId node, const NtLabelOptions *label)
{
    if (!valid_label(label)) return NT_RESULT_INVALID_ARGUMENT;
    return set_widget(tree,node,&label_ops,(WidgetStyle){.label=label->style},label->text);
}

NtResult nt_panel_mount(NtTree *tree, NtNodeId parent, NtMountOptions options,
                        const NtPanelOptions *panel, NtNodeId *out)
{
    if (!valid_panel(panel)) return NT_RESULT_INVALID_ARGUMENT;
    return mount_widget(tree,parent,options,&nt_panel_ops,(WidgetStyle){.panel=*panel},(NtByteView){0},out);
}

NtResult nt_panel_set(NtTree *tree, NtNodeId node, const NtPanelOptions *panel)
{
    if (!valid_panel(panel)) return NT_RESULT_INVALID_ARGUMENT;
    return set_widget(tree,node,&nt_panel_ops,(WidgetStyle){.panel=*panel},(NtByteView){0});
}

NtResult nt_rule_mount(NtTree *tree, NtNodeId parent, NtMountOptions options,
                       NtRuleOptions rule, NtNodeId *out)
{
    return mount_widget(tree,parent,options,&rule_ops,(WidgetStyle){.rule=rule},(NtByteView){0},out);
}

NtResult nt_rule_set(NtTree *tree, NtNodeId node, NtRuleOptions rule)
{
    return set_widget(tree,node,&rule_ops,(WidgetStyle){.rule=rule},(NtByteView){0});
}
