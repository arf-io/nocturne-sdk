#include "controls_internal.h"
#include "property_internal.h"

typedef struct ChoiceEntry {
    NtText *text;
    bool disabled;
} ChoiceEntry;

typedef struct ChoiceModel {
    ChoiceEntry *entries;
    size_t count, selected;
    uint32_t visible_rows, width;
    NtChoiceKind kind;
    NtControlStyle style;
} ChoiceModel;

typedef struct Choice {
    NtAllocator allocator;
    NtNodeId node;
    ChoiceModel model;
    NtControlPress press;
    uint64_t revision, pressed_revision;
    bool expanded;
} Choice;

typedef struct ChoicePaint {
    NtAllocator allocator;
    size_t first, count;
    uint64_t revision;
    bool header;
} ChoicePaint;

NT_DECLARE_ALIGNMENT(ChoiceEntry);
NT_DECLARE_ALIGNMENT(Choice);
NT_DECLARE_ALIGNMENT(ChoicePaint);

static void dispose_model(const NtAllocator *allocator, ChoiceModel model)
{
    if (!model.entries) return;
    for (size_t i = 0; i < model.count; ++i) nt_text_destroy(model.entries[i].text);
    nt_release(allocator,model.entries,model.count * sizeof(*model.entries),NT_ALIGNMENT(ChoiceEntry));
}

static NtResult copy_model(const NtAllocator *allocator, const NtChoiceOptions *options, ChoiceModel *out)
{
    if (!options || options->kind < NT_CHOICE_LIST || options->kind > NT_CHOICE_SELECT ||
        (options->count && !options->entries)) return NT_RESULT_INVALID_ARGUMENT;
    const NtControlStyle style = options->style ? *options->style : nt_control_style_default();
    if (!nt_control_style_valid(style)) return NT_RESULT_INVALID_ARGUMENT;
    if (options->count > SIZE_MAX / sizeof(ChoiceEntry) || options->count > INT32_MAX)
        return NT_RESULT_OUT_OF_RANGE;
    if (options->selected != NT_CHOICE_NONE) {
        if (options->selected >= options->count) return NT_RESULT_OUT_OF_RANGE;
        if (options->entries[options->selected].disabled) return NT_RESULT_INVALID_ARGUMENT;
    }
    ChoiceModel model = {.count=options->count,.selected=options->selected,.visible_rows=options->visible_rows,
                         .kind=options->kind,.style=style,.width=4};
    if (model.count) {
        model.entries = nt_allocate(allocator,model.count * sizeof(*model.entries),NT_ALIGNMENT(ChoiceEntry));
        if (!model.entries) return NT_RESULT_ALLOCATION_FAILED;
        for (size_t i = 0; i < model.count; ++i) model.entries[i] = (ChoiceEntry){0};
    }
    for (size_t i = 0; i < model.count; ++i) {
        ChoiceEntry *entry = &model.entries[i];
        entry->disabled = options->entries[i].disabled;
        NtResult result = nt_text_create(nt_control_allocator(allocator),options->entries[i].label,&entry->text);
        if (result == NT_RESULT_OK) {
            NtTextLayout *layout;
            result = nt_control_text_layout(allocator,entry->text,style.width,NT_WRAP_NONE,
                                            (NtConstraints){.rows={0,1,true}},false,&layout);
            if (result == NT_RESULT_OK) {
                const uint32_t width = nt_text_layout_size(layout).columns + 4;
                if (width > model.width) model.width = width;
                nt_text_layout_destroy(layout);
            }
        }
        if (result != NT_RESULT_OK) {
            dispose_model(allocator,model);
            return result;
        }
    }
    *out = model;
    return NT_RESULT_OK;
}

static uint32_t choice_rows(const Choice *choice)
{
    return choice->model.visible_rows && choice->model.visible_rows < choice->model.count ?
        choice->model.visible_rows : (uint32_t)choice->model.count;
}

static NtResult measure_choice(const void *value, NtMeasureContext *context, NtConstraints bounds,
                               NtCellSize *out)
{
    if (nt_measure_child_count(context)) return NT_RESULT_INVALID_ARGUMENT;
    const Choice *choice = value;
    uint32_t rows = choice_rows(choice);
    if (choice->model.kind == NT_CHOICE_SELECT) rows = choice->expanded ? rows + 1 : 1;
    *out = nt_control_fit((NtCellSize){choice->model.width,rows},bounds);
    return NT_RESULT_OK;
}

static void dispose_choice_paint(void *value)
{
    ChoicePaint *snapshot = value;
    const NtAllocator allocator = snapshot->allocator;
    nt_release(&allocator,snapshot,sizeof(*snapshot),NT_ALIGNMENT(ChoicePaint));
}

static NtResult paint_row(const Choice *choice, NtPaintContext *paint, NtCellSize size, uint32_t row,
                          size_t index, bool header)
{
    const ChoiceModel *model = &choice->model;
    const ChoiceEntry *entry = index == NT_CHOICE_NONE ? NULL : &model->entries[index];
    const bool selected = index != NT_CHOICE_NONE && model->selected == index;
    const NtCellStyle colors = entry && entry->disabled ? model->style.normal :
        nt_control_colors(choice->node,model->style,selected && !header);
    NtResult result = nt_paint_fill(paint,(NtCellRect){{0,(int32_t)row},{size.columns,1}},colors.background);
    const char *marker = header ? (choice->expanded ? "^ " : "v ") :
        entry->disabled ? " -  " :
        model->kind == NT_CHOICE_RADIO ? (selected ? "(*) " : "( ) ") : (selected ? " >  " : "    ");
    const uint32_t prefix = header ? 2 : 4;
    for (uint32_t x = 0; x < size.columns && x < prefix && result == NT_RESULT_OK; ++x)
        result = nt_paint_glyph(paint,(NtCellPoint){(int32_t)x,(int32_t)row},
                               (NtByteView){marker+x,1},1,colors);
    if (result != NT_RESULT_OK || !entry || size.columns <= prefix) return result;
    return nt_control_draw_label(&choice->allocator,entry->text,model->style.width,paint,
                                 prefix,row,size.columns-prefix,colors);
}

static NtResult paint_choice(const void *value, NtPaintContext *paint, NtCellSize size)
{
    if (!size.columns || !size.rows) return NT_RESULT_OK;
    const Choice *choice = value;
    const ChoiceModel *model = &choice->model;
    const bool header = model->kind == NT_CHOICE_SELECT;
    size_t count = header && !choice->expanded ? 0 : choice_rows(choice);
    if (count > size.rows - header) count = size.rows - header;
    size_t first = 0;
    if (count && model->selected != NT_CHOICE_NONE && model->selected >= count)
        first = model->selected - count + 1;
    ChoicePaint *snapshot = nt_allocate(&choice->allocator,sizeof(*snapshot),NT_ALIGNMENT(ChoicePaint));
    if (!snapshot) return NT_RESULT_ALLOCATION_FAILED;
    *snapshot = (ChoicePaint){choice->allocator,first,count,choice->revision,header};
    nt_component_attach_paint_data(paint,snapshot,dispose_choice_paint);
    const NtCellRect clip = nt_paint_clip(paint);
    NtResult result = nt_paint_fill(paint,(NtCellRect){{0,0},size},model->style.normal.background);
    if (!clip.size.columns || !clip.size.rows) return result;
    const uint32_t top = (uint32_t)clip.origin.y;
    const uint32_t bottom = top + clip.size.rows;
    if (result == NT_RESULT_OK && header && !top)
        result = paint_row(choice,paint,size,0,model->selected,true);
    for (size_t row = top < header ? header : top;
         result == NT_RESULT_OK && row < bottom && row - header < count; ++row)
        result = paint_row(choice,paint,size,(uint32_t)row,first+row-header,false);
    return result;
}

static size_t walk_choice(const ChoiceModel *model, size_t from, bool backwards)
{
    size_t i = from == NT_CHOICE_NONE ? (backwards ? model->count : 0) : (backwards ? from : from + 1);
    if (backwards) {
        while (i) if (!model->entries[--i].disabled) return i;
    } else {
        for (; i < model->count; ++i) if (!model->entries[i].disabled) return i;
    }
    return NT_CHOICE_NONE;
}

static void expand_choice(Choice *choice, bool expanded, NtEventReply *reply)
{
    if (choice->expanded != expanded) {
        choice->expanded = expanded;
        reply->invalidate = NT_INVALIDATE_LAYOUT;
    }
}

static NtResult choose(Choice *choice, size_t index, NtEventContext *context, NtEventReply *reply)
{
    if (index == NT_CHOICE_NONE || choice->model.entries[index].disabled || index == choice->model.selected)
        return NT_RESULT_OK;
    const NtResult result = nt_control_emit(context,NT_CONTROL_CHOICE_CHANGED,index);
    if (result == NT_RESULT_OK) {
        choice->model.selected = index;
        reply->invalidate = NT_INVALIDATE_PAINT;
    }
    return result;
}

static NtResult event_choice(void *value, NtEventContext *context, const NtEvent *event, NtEventReply *reply)
{
    if (nt_event_phase(context) != NT_EVENT_TARGET) return NT_RESULT_OK;
    Choice *choice = value;
    const ChoiceModel *model = &choice->model;
    const bool select = model->kind == NT_CHOICE_SELECT;
    const bool click = nt_control_press(choice->node,&choice->press,event,reply);
    if (reply->capture == NT_CAPTURE_TAKE) choice->pressed_revision = choice->revision;
    if (event->kind == NT_EVENT_FOCUS_LOSS) expand_choice(choice,false,reply);
    if (click) {
        const NtPlacement *placement = nt_tree_placement(choice->node.tree,choice->node);
        const ChoicePaint *snapshot = placement ? placement->paint_data : NULL;
        NtCellPoint cell;
        if (!snapshot || snapshot->revision != choice->revision || choice->pressed_revision != choice->revision ||
            !nt_event_local_position(context,&cell) || cell.y < 0) return NT_RESULT_OK;
        if (!cell.y && snapshot->header) {
            expand_choice(choice,!choice->expanded,reply);
            return NT_RESULT_OK;
        }
        const size_t row = (size_t)cell.y - snapshot->header;
        if (row >= snapshot->count || model->entries[snapshot->first + row].disabled) return NT_RESULT_OK;
        const NtResult result = choose(choice,snapshot->first+row,context,reply);
        if (result == NT_RESULT_OK && select) expand_choice(choice,false,reply);
        return result;
    }

    const uint32_t key = nt_control_key(event,true);
    if (select && (nt_control_key(event,false) == NT_KEY_ENTER || nt_control_key(event,false) == ' ')) {
        reply->consume = true;
        expand_choice(choice,!choice->expanded,reply);
        return NT_RESULT_OK;
    }
    if (select && key == NT_KEY_ESCAPE && choice->expanded) {
        reply->consume = true;
        expand_choice(choice,false,reply);
        return NT_RESULT_OK;
    }
    const int32_t wheel = event->kind == NT_EVENT_WHEEL ? event->data.wheel.dy : 0;
    const bool backwards = key == NT_KEY_UP || key == NT_KEY_LEFT || key == NT_KEY_END || wheel < 0;
    if (!backwards && key != NT_KEY_DOWN && key != NT_KEY_RIGHT && key != NT_KEY_HOME && wheel <= 0)
        return NT_RESULT_OK;
    reply->consume = true;
    const size_t from = key == NT_KEY_HOME || key == NT_KEY_END ? NT_CHOICE_NONE : model->selected;
    return choose(choice,walk_choice(model,from,backwards),context,reply);
}

static void dispose_choice(void *value)
{
    Choice *choice = value;
    const NtAllocator allocator = choice->allocator;
    dispose_model(&allocator,choice->model);
    nt_release(&allocator,choice,sizeof(*choice),NT_ALIGNMENT(Choice));
}

static const NtComponentOps choice_ops = {measure_choice,nt_control_leaf,NULL,paint_choice,event_choice,dispose_choice};

static NtResult validate_selection(const Choice *choice, size_t selected)
{
    if (selected == NT_CHOICE_NONE) return NT_RESULT_OK;
    if (selected >= choice->model.count) return NT_RESULT_OUT_OF_RANGE;
    return choice->model.entries[selected].disabled ? NT_RESULT_INVALID_ARGUMENT : NT_RESULT_OK;
}

static NtResult read_choice_selected(const void *value, NtValue *out)
{
    const Choice *choice = value;
    const size_t selected = choice->model.selected;
    *out = (NtValue){.kind = NT_VALUE_U64,
        .as.u64 = selected == NT_CHOICE_NONE ? UINT64_MAX : (uint64_t)selected};
    return NT_RESULT_OK;
}

static NtResult prepare_choice_selected(const void *value, NtValue index, void **prepared)
{
    (void)prepared;
    if (index.as.u64 == UINT64_MAX) return NT_RESULT_OK;
    if (index.as.u64 >= SIZE_MAX) return NT_RESULT_OUT_OF_RANGE;
    return validate_selection(value, (size_t)index.as.u64);
}

static void finish_choice_selected(void *value, NtValue index, void *prepared, bool commit)
{
    (void)prepared;
    Choice *choice = value;
    if (commit) choice->model.selected = index.as.u64 == UINT64_MAX ?
        NT_CHOICE_NONE : (size_t)index.as.u64;
}

static const NtPropertyDescriptor choice_property = {
    NT_PROPERTY_SELECTED, NT_VALUE_U64, NT_INVALIDATE_PAINT,
    read_choice_selected, prepare_choice_selected, finish_choice_selected
};

NtResult nt_choice_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount,
                         const NtChoiceOptions *options, NtNodeId *out)
{
    NtResult result = nt_control_validate_mount(tree,parent,out);
    if (result != NT_RESULT_OK) return result;
    tree->busy = true;
    ChoiceModel model;
    result = copy_model(&tree->allocator,options,&model);
    Choice *choice = NULL;
    if (result == NT_RESULT_OK) {
        choice = nt_allocate(&tree->allocator,sizeof(*choice),NT_ALIGNMENT(Choice));
        if (!choice) {
            dispose_model(&tree->allocator,model);
            result = NT_RESULT_ALLOCATION_FAILED;
        }
    }
    tree->busy = false;
    if (result != NT_RESULT_OK) return result;
    *choice = (Choice){.allocator=tree->allocator,.model=model};
    result = nt_tree_mount(tree,parent,&choice_ops,choice,mount,&choice->node);
    if (result == NT_RESULT_OK) {
        nt_property_stock_table(tree, choice->node, &choice_property, 1);
        *out = choice->node;
    } else {
        tree->busy = true;
        dispose_choice(choice);
        tree->busy = false;
    }
    return result;
}

NtResult nt_choice_configure(NtTree *tree, NtNodeId node, const NtChoiceOptions *options)
{
    if (!tree) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    void *state;
    NtResult result = nt_control_lookup(tree,node,&choice_ops,&state);
    if (result != NT_RESULT_OK) return result;
    Choice *choice = state;
    if (choice->revision == UINT64_MAX) return NT_RESULT_OUT_OF_RANGE;
    ChoiceModel next;
    tree->busy = true;
    result = copy_model(&choice->allocator,options,&next);
    if (result == NT_RESULT_OK) {
        dispose_model(&choice->allocator,choice->model);
        choice->model = next;
        choice->expanded = false;
        ++choice->revision;
        nt_tree_dirty(tree,NT_INVALIDATE_LAYOUT);
    }
    tree->busy = false;
    return result;
}

NtResult nt_choice_get(const NtTree *tree, NtNodeId node, NtChoiceState *out)
{
    if (!out) return NT_RESULT_INVALID_ARGUMENT;
    void *state;
    const NtResult result = nt_control_lookup(tree,node,&choice_ops,&state);
    if (result == NT_RESULT_OK) {
        const Choice *choice = state;
        *out = (NtChoiceState){choice->model.selected,choice->expanded};
    }
    return result;
}

NtResult nt_choice_set(NtTree *tree, NtNodeId node, size_t selected)
{
    if (!tree) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    void *state;
    const NtResult result = nt_control_lookup(tree,node,&choice_ops,&state);
    if (result != NT_RESULT_OK) return result;
    Choice *choice = state;
    const NtResult valid = validate_selection(choice, selected);
    if (valid != NT_RESULT_OK) return valid;
    if (choice->model.selected != selected) {
        choice->model.selected = selected;
        nt_tree_dirty(tree,NT_INVALIDATE_PAINT);
    }
    return NT_RESULT_OK;
}
