#include <nocturne/controls.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr,"controls consumer line %d: %s\n",__LINE__,#condition); \
    exit(EXIT_FAILURE); \
} } while (0)

static const NtNodeId root = {0};
static const NtMountOptions interactive = {true,true,true};
static const NtMountOptions display = {true,false,false};

static NtByteView bytes(const char *s) { return (NtByteView){s,strlen(s)}; }

static bool same(NtNodeId a, NtNodeId b)
{
    return a.tree == b.tree && a.slot == b.slot && a.generation == b.generation;
}

static NtDispatchResult input(NtTree *tree, NtEvent event, NtByteView text)
{
    NtDispatchResult result;
    CHECK(nt_tree_dispatch(tree,&event,text,NULL,&result) == NT_RESULT_OK);
    return result;
}

static NtDispatchResult key(NtTree *tree, uint32_t identity, NtKeyState state)
{
    return input(tree,(NtEvent){.kind=NT_EVENT_KEY,.data.key={identity,0,state}},(NtByteView){0});
}

static void pointer(NtTree *tree, NtEventKind kind, int32_t x, int32_t y)
{
    const NtPointerInput p = {.id=4,.position={x,y},.has_position=true,
        .button=kind == NT_EVENT_POINTER_MOVE ? 0 : NT_BUTTON_PRIMARY,
        .pressed=kind == NT_EVENT_POINTER_UP ? 0 : NT_BUTTON_PRIMARY};
    (void)input(tree,(NtEvent){.kind=kind,.data.pointer=p},(NtByteView){0});
}

static void click(NtTree *tree, int32_t x, int32_t y)
{
    pointer(tree,NT_EVENT_POINTER_DOWN,x,y);
    pointer(tree,NT_EVENT_POINTER_UP,x,y);
}

static void intent(NtTree *tree, NtNodeId source, NtControlEventKind kind, uint64_t value)
{
    NtIntentView view;
    NtControlEvent event;
    CHECK(nt_tree_peek_intent(tree,&view) && same(view.source,source));
    CHECK(nt_control_event_decode(view,&event) == NT_RESULT_OK);
    CHECK(event.kind == kind && event.value == value);
    nt_tree_pop_intent(tree);
}

static void no_intent(NtTree *tree)
{
    NtIntentView view;
    CHECK(!nt_tree_peek_intent(tree,&view));
}

static NtCellView cell(NtTree *tree, int32_t x, int32_t y)
{
    NtCellView view;
    CHECK(nt_frame_cell(nt_tree_frame(tree),(NtCellPoint){x,y},&view) == NT_RESULT_OK);
    return view;
}

static void glyph(NtTree *tree, int32_t x, int32_t y, const char *expected)
{
    const NtByteView actual = cell(tree,x,y).glyph;
    const size_t count = strlen(expected);
    if (actual.count != count || (count && memcmp(actual.data,expected,count))) {
        fprintf(stderr,"cell (%d,%d): expected '%s', got '%.*s'\n",
                x,y,expected,(int)actual.count,actual.data ? actual.data : "");
        exit(EXIT_FAILURE);
    }
}

static void press_controls(void)
{
    NtTree *tree;
    NtNodeId box, button, item, checkbox, rule;
    CHECK(nt_tree_create(NULL,&tree) == NT_RESULT_OK);
    const NtBoxLayout column = {.kind=NT_BOX_LAYOUT_COLUMN,.fallback={.grow_columns=true}};
    CHECK(nt_box_layout_mount(tree,root,display,&column,&box) == NT_RESULT_OK);
    char label[] = "Build";
    CHECK(nt_button_mount(tree,box,interactive,bytes(label),NULL,&button) == NT_RESULT_OK);
    label[0] = 'x';
    CHECK(nt_item_mount(tree,box,interactive,bytes("Notes"),NULL,false,&item) == NT_RESULT_OK);
    CHECK(nt_checkbox_mount(tree,box,interactive,bytes("Archive"),NULL,false,&checkbox) == NT_RESULT_OK);
    const NtRuleOptions rule_options = {.ascii=true,.color=nt_control_style_default().normal};
    CHECK(nt_rule_mount(tree,box,display,rule_options,&rule) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){14,4}) == NT_RESULT_OK);
    glyph(tree,1,0,"B");
    glyph(tree,3,3,"-");
    CHECK(nt_tree_set_focus(tree,button) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){14,4}) == NT_RESULT_OK);
    CHECK(cell(tree,0,0).style.background.red == nt_control_style_default().focused.background.red);
    CHECK(key(tree,NT_KEY_ENTER,NT_KEY_PRESS).consumed);
    intent(tree,button,NT_CONTROL_ACTIVATED,0);
    CHECK(!key(tree,NT_KEY_ENTER,NT_KEY_REPEAT).consumed);
    const NtEvent space = {.kind=NT_EVENT_KEY,.data.key={' ',0,NT_KEY_PRESS}};
    const NtDispatchResult space_result = input(tree,space,bytes(" "));
    CHECK(space_result.consumed && !space_result.text_delivered);
    intent(tree,button,NT_CONTROL_ACTIVATED,0);

    bool value = false;
    pointer(tree,NT_EVENT_POINTER_DOWN,2,0);
    CHECK(nt_button_pressed(tree,button,&value) == NT_RESULT_OK && value);
    pointer(tree,NT_EVENT_POINTER_MOVE,2,1);
    CHECK(nt_button_pressed(tree,button,&value) == NT_RESULT_OK && !value);
    pointer(tree,NT_EVENT_POINTER_UP,2,1);
    no_intent(tree);
    click(tree,2,0);
    intent(tree,button,NT_CONTROL_ACTIVATED,0);
    pointer(tree,NT_EVENT_POINTER_DOWN,2,0);
    CHECK(nt_tree_set_focus(tree,item) == NT_RESULT_OK);
    CHECK(nt_button_pressed(tree,button,&value) == NT_RESULT_OK && !value);
    pointer(tree,NT_EVENT_POINTER_UP,2,0);
    no_intent(tree);

    CHECK(key(tree,' ',NT_KEY_PRESS).consumed);
    intent(tree,item,NT_CONTROL_ITEM_CHANGED,1);
    CHECK(nt_item_get(tree,item,&value) == NT_RESULT_OK && value);
    CHECK(nt_item_set(tree,item,false) == NT_RESULT_OK);
    no_intent(tree);
    click(tree,1,2);
    CHECK(same(nt_tree_focus(tree),checkbox));
    intent(tree,checkbox,NT_CONTROL_CHECKBOX_CHANGED,1);
    CHECK(nt_checkbox_get(tree,checkbox,&value) == NT_RESULT_OK && value);
    CHECK(key(tree,NT_KEY_ENTER,NT_KEY_PRESS).consumed);
    intent(tree,checkbox,NT_CONTROL_CHECKBOX_CHANGED,0);
    CHECK(nt_checkbox_set(tree,checkbox,true) == NT_RESULT_OK);
    no_intent(tree);
    CHECK(nt_checkbox_get(tree,item,&value) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(nt_tree_remove(tree,checkbox) == NT_RESULT_OK);
    CHECK(nt_checkbox_get(tree,checkbox,&value) == NT_RESULT_STALE_REFERENCE);
    nt_tree_destroy(tree);
    puts("button, item, checkbox: focus, capture, cancellation, change, activation, silent setters");
}

static void choice_controls(void)
{
    char first[] = "Alpha";
    const NtChoiceEntry entries[] = {{{first,5},false},{{"Hold",4},true},
        {{"Add",3},false},{{"Write",5},false}};
    for (NtChoiceKind kind = NT_CHOICE_LIST; kind <= NT_CHOICE_SELECT; ++kind) {
        NtTree *tree;
        NtNodeId choice;
        NtChoiceState state;
        const NtChoiceOptions options = {kind,entries,4,0,2,NULL};
        CHECK(nt_tree_create(NULL,&tree) == NT_RESULT_OK);
        first[0] = 'A';
        CHECK(nt_choice_mount(tree,root,interactive,&options,&choice) == NT_RESULT_OK);
        first[0] = 'x';
        CHECK(nt_tree_publish(tree,(NtCellSize){12,3}) == NT_RESULT_OK);
        glyph(tree,kind == NT_CHOICE_SELECT ? 2 : 4,0,"A");
        CHECK(nt_tree_set_focus(tree,choice) == NT_RESULT_OK);
        CHECK(key(tree,NT_KEY_DOWN,NT_KEY_PRESS).consumed);
        intent(tree,choice,NT_CONTROL_CHOICE_CHANGED,2);
        CHECK(nt_choice_get(tree,choice,&state) == NT_RESULT_OK && state.selected == 2 && !state.expanded);
        CHECK(nt_choice_set(tree,choice,1) == NT_RESULT_INVALID_ARGUMENT);
        CHECK(nt_choice_set(tree,choice,7) == NT_RESULT_OUT_OF_RANGE);

        // The old frame still shows indices zero and one. Row one is disabled.
        if (kind != NT_CHOICE_SELECT) {
            click(tree,4,1);
            no_intent(tree);
        } else {
            CHECK(key(tree,NT_KEY_ENTER,NT_KEY_PRESS).consumed);
            CHECK(nt_choice_get(tree,choice,&state) == NT_RESULT_OK && state.expanded);
        }
        CHECK(nt_tree_publish(tree,(NtCellSize){12,3}) == NT_RESULT_OK);
        const int32_t row = kind == NT_CHOICE_SELECT ? 2 : 1;
        glyph(tree,4,row,"A");
        click(tree,4,row);
        no_intent(tree);
        CHECK(nt_choice_get(tree,choice,&state) == NT_RESULT_OK && !state.expanded);
        CHECK(key(tree,NT_KEY_END,NT_KEY_PRESS).consumed);
        intent(tree,choice,NT_CONTROL_CHOICE_CHANGED,3);
        CHECK(nt_choice_set(tree,choice,NT_CHOICE_NONE) == NT_RESULT_OK);
        no_intent(tree);
        CHECK(key(tree,NT_KEY_HOME,NT_KEY_PRESS).consumed);
        intent(tree,choice,NT_CONTROL_CHOICE_CHANGED,0);
        CHECK(nt_tree_publish(tree,(NtCellSize){12,3}) == NT_RESULT_OK);

        // Replacing the model while a pointer is down cannot activate new rows.
        pointer(tree,NT_EVENT_POINTER_DOWN,4,0);
        const NtChoiceEntry replacement[] = {{{"New",3},false}};
        const NtChoiceOptions next = {kind,replacement,1,NT_CHOICE_NONE,1,NULL};
        CHECK(nt_choice_configure(tree,choice,&next) == NT_RESULT_OK);
        CHECK(nt_tree_publish(tree,(NtCellSize){12,3}) == NT_RESULT_OK);
        pointer(tree,NT_EVENT_POINTER_UP,4,0);
        no_intent(tree);
        CHECK(nt_choice_get(tree,choice,&state) == NT_RESULT_OK && state.selected == NT_CHOICE_NONE && !state.expanded);
        if (kind == NT_CHOICE_SELECT) {
            click(tree,1,0);
            CHECK(nt_choice_get(tree,choice,&state) == NT_RESULT_OK && state.expanded);
            CHECK(nt_tree_set_focus(tree,root) == NT_RESULT_OK);
            CHECK(nt_choice_get(tree,choice,&state) == NT_RESULT_OK && !state.expanded);
        }
        nt_tree_destroy(tree);
    }
    puts("list, radio, select: disabled entries, navigation, committed rows, model replacement");
}

static void indicators(void)
{
    NtTree *tree;
    NtNodeId box, cursor, wait, progress, scrollbar;
    const NtControlStyle colors = nt_control_style_default();
    const NtBoxLayout column = {.kind=NT_BOX_LAYOUT_COLUMN,.fallback={.grow_columns=true}};
    const NtProgressOptions initial = {3,8,colors.engaged,colors.normal,false};
    const NtScrollBarOptions bar = {40,10,100,colors.engaged,colors.normal,false};
    CHECK(nt_tree_create(NULL,&tree) == NT_RESULT_OK);
    CHECK(nt_box_layout_mount(tree,root,display,&column,&box) == NT_RESULT_OK);
    CHECK(nt_cursor_mount(tree,box,display,(NtCursorOptions){colors.normal,10,true},&cursor) == NT_RESULT_OK);
    CHECK(nt_wait_mount(tree,box,display,(NtWaitOptions){colors.normal,10,true},&wait) == NT_RESULT_OK);
    CHECK(nt_progress_mount(tree,box,display,initial,&progress) == NT_RESULT_OK);
    CHECK(nt_scrollbar_mount(tree,box,interactive,bar,&scrollbar) == NT_RESULT_OK);
    CHECK(nt_tree_tick(tree,0,NT_MOTION_FULL) == NT_RESULT_OK);
    CHECK(nt_tree_next_update(tree).scheduled && nt_tree_next_update(tree).at == 10);
    CHECK(nt_tree_publish(tree,(NtCellSize){8,4}) == NT_RESULT_OK);
    glyph(tree,0,0,"|");
    glyph(tree,0,1,"|");
    glyph(tree,2,2,"#");
    glyph(tree,3,2,"-");
    glyph(tree,5,3,".");
    glyph(tree,6,3,"#");
    CHECK(nt_tree_tick(tree,10,NT_MOTION_FULL) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){8,4}) == NT_RESULT_OK);
    glyph(tree,0,0,"");
    glyph(tree,0,1,"/");
    CHECK(nt_tree_tick(tree,20,NT_MOTION_REDUCED) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){8,4}) == NT_RESULT_OK);
    glyph(tree,0,0,"|");
    glyph(tree,0,1,"|");
    CHECK(nt_wait_set(tree,wait,(NtWaitOptions){colors.normal,0,false}) == NT_RESULT_OK);
    CHECK(nt_cursor_set(tree,cursor,(NtCursorOptions){colors.normal,0,false}) == NT_RESULT_OK);
    CHECK(nt_tree_tick(tree,UINT64_MAX,NT_MOTION_FULL) == NT_RESULT_OK);
    CHECK(!nt_tree_next_update(tree).scheduled);

    NtProgressOptions next = initial, queried_progress;
    next.maximum = 0;
    CHECK(nt_progress_set(tree,progress,next) == NT_RESULT_OUT_OF_RANGE);
    CHECK(nt_progress_get(tree,progress,&queried_progress) == NT_RESULT_OK && queried_progress.value == 3);
    next.value = next.maximum = UINT32_MAX;
    CHECK(nt_progress_set(tree,progress,next) == NT_RESULT_OK);
    CHECK(nt_tree_set_focus(tree,scrollbar) == NT_RESULT_OK);
    CHECK(key(tree,NT_KEY_PAGE_UP,NT_KEY_PRESS).consumed);
    intent(tree,scrollbar,NT_CONTROL_SCROLL_CHANGED,20);
    CHECK(key(tree,NT_KEY_END,NT_KEY_REPEAT).consumed);
    intent(tree,scrollbar,NT_CONTROL_SCROLL_CHANGED,30);
    CHECK(key(tree,NT_KEY_END,NT_KEY_PRESS).consumed);
    no_intent(tree);
    CHECK(nt_tree_publish(tree,(NtCellSize){8,4}) == NT_RESULT_OK);
    glyph(tree,0,1,"⠋");
    glyph(tree,7,2,"#");
    glyph(tree,7,3,"@");
    NtScrollBarOptions queried;
    CHECK(nt_scrollbar_get(tree,scrollbar,&queried) == NT_RESULT_OK && queried.offset == 30);
    CHECK(nt_scrollbar_set(tree,scrollbar,(NtScrollBarOptions){2,3,5,bar.thumb,bar.track,false}) == NT_RESULT_OK);
    CHECK(nt_scrollbar_get(tree,scrollbar,&queried) == NT_RESULT_OK && queried.offset == 0);
    no_intent(tree);
    nt_tree_destroy(tree);

    CHECK(nt_tree_create(NULL,&tree) == NT_RESULT_OK);
    const NtBoxLayout inset = {.kind=NT_BOX_LAYOUT_ROW,.padding={.left=1}};
    CHECK(nt_box_layout_mount(tree,root,display,&inset,&box) == NT_RESULT_OK);
    CHECK(nt_scrollbar_mount(tree,box,interactive,bar,&scrollbar) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){9,1}) == NT_RESULT_OK);
    pointer(tree,NT_EVENT_POINTER_DOWN,1,0);
    intent(tree,scrollbar,NT_CONTROL_SCROLL_CHANGED,0);
    pointer(tree,NT_EVENT_POINTER_MOVE,10,0);
    intent(tree,scrollbar,NT_CONTROL_SCROLL_CHANGED,30);
    pointer(tree,NT_EVENT_POINTER_MOVE,INT32_MIN,0);
    intent(tree,scrollbar,NT_CONTROL_SCROLL_CHANGED,0);
    NtScrollBarOptions longer = bar;
    longer.content = 50;
    longer.offset = 0;
    CHECK(nt_scrollbar_set(tree,scrollbar,longer) == NT_RESULT_OK);
    pointer(tree,NT_EVENT_POINTER_MOVE,10,0);
    no_intent(tree);
    CHECK(nt_tree_publish(tree,(NtCellSize){9,1}) == NT_RESULT_OK);
    pointer(tree,NT_EVENT_POINTER_MOVE,10,0);
    intent(tree,scrollbar,NT_CONTROL_SCROLL_CHANGED,40);
    pointer(tree,NT_EVENT_POINTER_UP,10,0);
    CHECK(!nt_tree_capture(tree,4).tree);
    nt_tree_destroy(tree);
    puts("cursor, wait, progress, scrollbar: explicit deadlines, reduced motion, ratios, keyboard and drag");
}

static void editor_view(void)
{
    NtTree *tree;
    NtEditor *editor;
    NtNodeId view, panel;
    CHECK(nt_editor_create(NULL,bytes("ab\nאב\nlast"),NULL,&editor) == NT_RESULT_OK);
    CHECK(nt_editor_set_undo_limit(editor,4) == NT_RESULT_OK);
    NtEditorBinding bindings[] = {
        {{NT_KEY_END,0},NT_EDITOR_ACTION_MOVE,NT_EDITOR_DOCUMENT_END,0,false,false},
        {{'z',NT_MOD_CONTROL},NT_EDITOR_ACTION_UNDO,0,0,false,false}
    };
    const NtEditorKeyMap keys = {bindings,2,NULL};
    const NtEditorViewOptions options = {.editor=editor,.keys=&keys,.wrap=NT_WRAP_NONE,
        .follow_caret=true,.show_caret=true,.submit_on_enter=true};
    CHECK(nt_tree_create(NULL,&tree) == NT_RESULT_OK);
    const NtPanelOptions frame = {.framed=true,.ascii=true,.border=nt_control_style_default().normal};
    CHECK(nt_panel_mount(tree,root,display,&frame,&panel) == NT_RESULT_OK);
    CHECK(nt_editor_view_mount(tree,panel,interactive,&options,&view) == NT_RESULT_OK);
    bindings[0].chord.key = NT_KEY_HOME;
    NtCellSize offset;
    CHECK(nt_editor_view_offset(tree,view,&offset) == NT_RESULT_WRONG_PHASE);
    CHECK(nt_tree_publish(tree,(NtCellSize){6,4}) == NT_RESULT_OK);
    glyph(tree,0,0,"+");
    glyph(tree,1,1,"a");
    glyph(tree,3,2,"ב");
    CHECK(nt_tree_set_focus(tree,view) == NT_RESULT_OK);
    CHECK(key(tree,NT_KEY_END,NT_KEY_PRESS).consumed);
    CHECK(nt_tree_publish(tree,(NtCellSize){6,4}) == NT_RESULT_OK);
    CHECK(nt_editor_view_offset(tree,view,&offset) == NT_RESULT_OK && offset.rows == 1 && offset.columns == 1);

    const NtEvent printable = {.kind=NT_EVENT_KEY,.data.key={0xe9,0,NT_KEY_PRESS}};
    CHECK(input(tree,printable,bytes("é")).text_delivered);
    intent(tree,view,NT_CONTROL_EDITOR_CHANGED,0);
    CHECK(nt_editor_text(editor) && nt_text_bytes(nt_editor_text(editor)).count == strlen("ab\nאב\nlasté"));
    const NtEditorSelection prior = nt_editor_selection(editor);
    click(tree,1,1);
    CHECK(nt_editor_selection(editor).active.offset == prior.active.offset);
    CHECK(!nt_tree_capture(tree,4).tree);
    CHECK(nt_tree_publish(tree,(NtCellSize){8,5}) == NT_RESULT_OK);
    CHECK(nt_editor_view_offset(tree,view,&offset) == NT_RESULT_OK && !offset.rows && !offset.columns);
    click(tree,6,2);
    CHECK(nt_editor_selection(editor).active.offset == strlen("ab\nא"));

    CHECK(key(tree,NT_KEY_ENTER,NT_KEY_PRESS).consumed);
    intent(tree,view,NT_CONTROL_EDITOR_SUBMITTED,0);
    CHECK(key(tree,NT_KEY_ENTER,NT_KEY_REPEAT).consumed);
    no_intent(tree);
    const NtEvent undo = {.kind=NT_EVENT_KEY,.data.key={'z',NT_MOD_CONTROL,NT_KEY_PRESS}};
    CHECK(input(tree,undo,(NtByteView){0}).consumed);
    intent(tree,view,NT_CONTROL_EDITOR_CHANGED,0);
    CHECK(input(tree,undo,(NtByteView){0}).consumed);
    no_intent(tree);
    nt_tree_destroy(tree);
    nt_editor_destroy(editor);

    const NtEditorOptions read_only = {NT_EDITOR_MULTILINE,true};
    CHECK(nt_editor_create(NULL,bytes("zero\none\ntwo\nthree"),&read_only,&editor) == NT_RESULT_OK);
    CHECK(nt_tree_create(NULL,&tree) == NT_RESULT_OK);
    const NtEditorViewOptions reader = {.editor=editor,.wrap=NT_WRAP_CLUSTER,.scroll_keys=true};
    CHECK(nt_editor_view_mount(tree,root,interactive,&reader,&view) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){5,2}) == NT_RESULT_OK);
    CHECK(nt_tree_set_focus(tree,view) == NT_RESULT_OK);
    CHECK(key(tree,NT_KEY_END,NT_KEY_PRESS).consumed);
    CHECK(nt_editor_view_offset(tree,view,&offset) == NT_RESULT_OK && !offset.rows);
    CHECK(nt_tree_publish(tree,(NtCellSize){5,2}) == NT_RESULT_OK);
    glyph(tree,0,0,"t");
    glyph(tree,1,0,"w");
    CHECK(nt_editor_view_offset(tree,view,&offset) == NT_RESULT_OK && offset.rows == 2);
    const NtEvent paste = {.kind=NT_EVENT_PASTE,.data.text={"change",6}};
    CHECK(!input(tree,paste,(NtByteView){0}).consumed);
    no_intent(tree);
    CHECK(nt_editor_view_scroll(tree,view,(NtCellSize){UINT32_MAX,UINT32_MAX}) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){5,4}) == NT_RESULT_OK);
    CHECK(nt_editor_view_offset(tree,view,&offset) == NT_RESULT_OK && !offset.rows && !offset.columns);
    nt_tree_destroy(tree);
    nt_editor_destroy(editor);
    puts("editor and reader: framed layout, bidi pointer, associated text, caret scroll, undo and submit");
}

static NtResult sentinel_measure(const void *state, NtMeasureContext *context, NtConstraints bounds, NtCellSize *out)
{
    (void)state;
    (void)context;
    *out = (NtCellSize){bounds.columns.min,bounds.rows.min};
    return NT_RESULT_OK;
}

static NtResult sentinel_layout(const void *state, NtLayoutContext *context, NtCellSize assigned)
{
    (void)state;
    (void)context;
    (void)assigned;
    return NT_RESULT_OK;
}

static NtResult sentinel_paint(const void *state, NtPaintContext *paint, NtCellSize assigned)
{
    (void)paint;
    (void)assigned;
    return *(const bool *)state ? NT_RESULT_UNSUPPORTED_CAPABILITY : NT_RESULT_OK;
}

static void *allocate(void *refuse, size_t count, size_t alignment)
{
    (void)alignment;
    return *(bool *)refuse ? NULL : malloc(count);
}

static void release(void *refuse, void *address, size_t count, size_t alignment)
{
    (void)refuse;
    (void)count;
    (void)alignment;
    free(address);
}

static void refused_button_release(void)
{
    bool refuse = false;
    const NtAllocator allocator = {&refuse,allocate,release};
    NtTree *tree;
    NtNodeId button;
    CHECK(nt_tree_create(&allocator,&tree) == NT_RESULT_OK);
    CHECK(nt_button_mount(tree,root,interactive,bytes("Apply"),NULL,&button) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){8,1}) == NT_RESULT_OK);
    pointer(tree,NT_EVENT_POINTER_DOWN,1,0);
    NtEvent press = {.kind=NT_EVENT_POINTER_DOWN,.data.pointer={
        .id=4,.position={1,0},.button=NT_BUTTON_SECONDARY,
        .pressed=NT_BUTTON_PRIMARY | NT_BUTTON_SECONDARY,.has_position=true}};
    (void)input(tree,press,(NtByteView){0});
    CHECK(same(nt_tree_capture(tree,4),button));

    NtEvent up = press;
    up.kind = NT_EVENT_POINTER_UP;
    up.data.pointer.button = NT_BUTTON_PRIMARY;
    up.data.pointer.pressed = NT_BUTTON_SECONDARY;
    NtDispatchResult result;
    refuse = true;
    CHECK(nt_tree_dispatch(tree,&up,(NtByteView){0},NULL,&result) == NT_RESULT_ALLOCATION_FAILED);
    refuse = false;
    bool pressed;
    CHECK(nt_button_pressed(tree,button,&pressed) == NT_RESULT_OK && !pressed);
    CHECK(!nt_tree_capture(tree,4).tree);
    no_intent(tree);
    up.data.pointer.button = NT_BUTTON_SECONDARY;
    up.data.pointer.pressed = 0;
    CHECK(input(tree,up,(NtByteView){0}).consumed);
    click(tree,1,0);
    intent(tree,button,NT_CONTROL_ACTIVATED,0);
    nt_tree_destroy(tree);
    puts("button refusal: primary release clears capture with another button held");
}

static void editor_transactions(void)
{
    static const NtComponentOps sentinel = {sentinel_measure,sentinel_layout,NULL,sentinel_paint,NULL,NULL};
    bool fail_paint = false, fail_alloc = false;
    const NtAllocator allocator = {&fail_alloc,allocate,release};
    NtTree *tree;
    NtEditor *editor;
    NtNodeId stack, view, sibling;
    CHECK(nt_tree_create(&allocator,&tree) == NT_RESULT_OK);
    CHECK(nt_editor_create(NULL,bytes(""),NULL,&editor) == NT_RESULT_OK);
    const NtBoxLayout box = {.kind=NT_BOX_LAYOUT_STACK,.fallback={.grow_columns=true,.grow_rows=true}};
    CHECK(nt_box_layout_mount(tree,root,display,&box,&stack) == NT_RESULT_OK);
    const NtEditorViewOptions options = {.editor=editor,.wrap=NT_WRAP_CLUSTER};
    CHECK(nt_editor_view_mount(tree,stack,interactive,&options,&view) == NT_RESULT_OK);
    CHECK(nt_tree_mount(tree,stack,&sentinel,&fail_paint,display,&sibling) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){4,2}) == NT_RESULT_OK);
    CHECK(nt_tree_set_focus(tree,view) == NT_RESULT_OK);
    click(tree,3,1);
    CHECK(nt_editor_selection(editor).active.offset == 0);
    CHECK(!key(tree,NT_KEY_BACKSPACE,NT_KEY_PRESS).consumed);
    no_intent(tree);

    const NtEvent paste = {.kind=NT_EVENT_PASTE,.data.text={"abcdefghi",9}};
    fail_alloc = true;
    NtDispatchResult result;
    CHECK(nt_tree_dispatch(tree,&paste,(NtByteView){0},NULL,&result) == NT_RESULT_ALLOCATION_FAILED);
    CHECK(nt_text_bytes(nt_editor_text(editor)).count == 0);
    no_intent(tree);
    fail_alloc = false;
    CHECK(input(tree,paste,(NtByteView){0}).consumed);
    intent(tree,view,NT_CONTROL_EDITOR_CHANGED,0);
    CHECK(nt_tree_publish(tree,(NtCellSize){4,2}) == NT_RESULT_OK);
    glyph(tree,0,1,"e");
    const NtFrame *committed = nt_tree_frame(tree);
    CHECK(nt_editor_view_scroll(tree,view,(NtCellSize){0,99}) == NT_RESULT_OK);
    fail_paint = true;
    CHECK(nt_tree_publish(tree,(NtCellSize){3,2}) == NT_RESULT_UNSUPPORTED_CAPABILITY);
    CHECK(committed == nt_tree_frame(tree));
    NtCellSize offset;
    CHECK(nt_editor_view_offset(tree,view,&offset) == NT_RESULT_OK && offset.rows == 0);
    click(tree,0,1);
    CHECK(nt_editor_selection(editor).active.offset == 4);

    pointer(tree,NT_EVENT_POINTER_DOWN,0,0);
    pointer(tree,NT_EVENT_POINTER_MOVE,4,0);
    pointer(tree,NT_EVENT_POINTER_UP,4,0);
    CHECK(nt_editor_selected_text(editor).count == 4);
    fail_paint = false;
    CHECK(nt_tree_publish(tree,(NtCellSize){3,2}) == NT_RESULT_OK);
    CHECK(nt_editor_view_offset(tree,view,&offset) == NT_RESULT_OK && offset.rows == 1);
    glyph(tree,0,0,"d");
    nt_tree_destroy(tree);
    nt_editor_destroy(editor);
    puts("editor transactions: empty input, refused change intent, failed resize, published selection");
}

int main(void)
{
    NtControlEvent decoded = {NT_CONTROL_SCROLL_CHANGED,20};
    CHECK(nt_control_event_decode((NtIntentView){0},&decoded) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(decoded.kind == NT_CONTROL_SCROLL_CHANGED && decoded.value == 20);
    press_controls();
    choice_controls();
    indicators();
    editor_view();
    refused_button_release();
    editor_transactions();
    return EXIT_SUCCESS;
}
