#include <nocturne/component.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr,"Routing consumer line %d: %s\n",__LINE__,#condition); \
    exit(EXIT_FAILURE); \
} } while (0)

typedef struct Seen {
    char node;
    NtEventKind kind;
    NtEventPhase phase;
    NtPointerId pointer;
    NtCancelReason reason;
    NtCellPoint local;
    bool positioned;
} Seen;

typedef struct App {
    NtTree *tree;
    Seen seen[64];
    size_t count, text_count;
    char text[16];
    bool verify_cleared;
} App;

typedef struct Part {
    App *app;
    char name;
    NtNodeId id;
    size_t disposals;
    bool capture, release, mask, hide, fail_paint, consume_preview, fail_bubble, fail_target;
    bool consume_key, focus_on_press, fail_down_bubble;
} Part;

typedef struct Rig {
    App app;
    Part root, left, right, child, peer;
} Rig;

static const NtNodeId none = {0};

static bool same(NtNodeId a, NtNodeId b)
{
    return a.tree == b.tree && a.slot == b.slot && a.generation == b.generation;
}

static NtResult measure(const void *value, NtMeasureContext *context, NtConstraints limits,
                        NtCellSize *out)
{
    (void)value;
    NtNodeId child = {0};
    for (size_t i = 0; i < nt_measure_child_count(context); ++i) {
        NtResult result = nt_measure_child_after(context,child,&child);
        if (result != NT_RESULT_OK) return result;
        NtCellSize preferred;
        result = nt_measure_child(context,child,limits,&preferred);
        if (result != NT_RESULT_OK) return result;
    }
    *out = (NtCellSize){limits.columns.min,limits.rows.min};
    return NT_RESULT_OK;
}

static NtResult layout(const void *value, NtLayoutContext *context, NtCellSize assigned)
{
    (void)assigned;
    const Part *part = value;
    if (part->hide) return NT_RESULT_OK;
    const NtRowSpan spans[] = {{{0,0},2},{{1,1},1}};
    const NtClipMask mask = {spans,2};
    NtNodeId child = {0};
    for (size_t i = 0; i < nt_layout_child_count(context); ++i) {
        NtResult result = nt_layout_child_after(context,child,&child);
        if (result != NT_RESULT_OK) return result;
        const NtCellRect bounds = part->name == 'R' ?
            (NtCellRect){{(int32_t)(6*i),0},{5,4}} : (NtCellRect){{1,1},{3,2}};
        result = nt_layout_place(context,child,bounds,(int32_t)(3 - i),part->mask ? &mask : NULL);
        if (result != NT_RESULT_OK) return result;
    }
    return NT_RESULT_OK;
}

static NtResult paint(const void *value, NtPaintContext *context, NtCellSize assigned)
{
    (void)assigned;
    const Part *part = value;
    if (part->fail_paint) {
        const NtResult result = nt_paint_fill(context,(NtCellRect){{0,0},{1,1}},(NtRgba){255,0,0,255});
        return result == NT_RESULT_OK ? NT_RESULT_UNSUPPORTED_CAPABILITY : result;
    }
    return NT_RESULT_OK;
}

static NtResult event(void *value, NtEventContext *context, const NtEvent *input, NtEventReply *reply)
{
    Part *part = value;
    App *app = part->app;
    CHECK(app->count < sizeof(app->seen)/sizeof(app->seen[0]));
    Seen *seen = &app->seen[app->count++];
    *seen = (Seen){.node=part->name,.kind=input->kind,.phase=nt_event_phase(context)};
    seen->positioned = nt_event_local_position(context,&seen->local);
    if (input->kind == NT_EVENT_POINTER_CANCEL) {
        seen->pointer = input->data.pointer_cancel.id;
        seen->reason = input->data.pointer_cancel.reason;
        CHECK(same(nt_tree_capture(app->tree,seen->pointer),none));
        if (app->verify_cleared) {
            CHECK(same(nt_tree_capture(app->tree,5),none) && same(nt_tree_capture(app->tree,7),none));
            CHECK(same(nt_tree_focus(app->tree),none));
        }
        CHECK(nt_tree_set_focus(app->tree,part->id) == NT_RESULT_WRONG_PHASE);
        reply->capture = NT_CAPTURE_TAKE;
    }
    if (input->kind == NT_EVENT_POINTER_UP) {
        if (seen->phase == NT_EVENT_PREVIEW && part->consume_preview) reply->consume = true;
        if (seen->phase == NT_EVENT_BUBBLE && part->fail_bubble) return NT_RESULT_UNSUPPORTED_CAPABILITY;
        if (seen->phase == NT_EVENT_TARGET && part->fail_target) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    }
    if (input->kind == NT_EVENT_POINTER_DOWN && seen->phase == NT_EVENT_TARGET) {
        if (part->capture) reply->capture = NT_CAPTURE_TAKE;
        if (part->focus_on_press) {
            reply->focus = true;
            reply->invalidate = NT_INVALIDATE_PAINT;
            return nt_event_emit(context,42,(NtByteView){"down",4});
        }
    }
    if (input->kind == NT_EVENT_POINTER_DOWN && seen->phase == NT_EVENT_BUBBLE &&
        part->fail_down_bubble) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    if (input->kind == NT_EVENT_POINTER_MOVE && seen->phase == NT_EVENT_TARGET && part->release)
        reply->capture = NT_CAPTURE_RELEASE;
    if (input->kind == NT_EVENT_KEY && seen->phase == NT_EVENT_TARGET && part->consume_key)
        reply->consume = true;
    if ((input->kind == NT_EVENT_TEXT || input->kind == NT_EVENT_PASTE) &&
        seen->phase == NT_EVENT_TARGET) {
        CHECK(input->data.text.count < sizeof(app->text));
        memcpy(app->text,input->data.text.data,input->data.text.count);
        app->text_count = input->data.text.count;
    }
    return NT_RESULT_OK;
}

static void dispose(void *value)
{
    Part *part = value;
    ++part->disposals;
}

static void mount(App *app, Part *part, NtNodeId parent, char name, bool interactive)
{
    *part = (Part){.app=app,.name=name};
    const NtComponentOps ops = {measure,layout,NULL,paint,event,dispose};
    const NtMountOptions options = {true,interactive,interactive};
    CHECK(nt_tree_mount(app->tree,parent,&ops,part,options,&part->id) == NT_RESULT_OK);
}

static void prepare(Rig *rig, bool peer)
{
    *rig = (Rig){0};
    CHECK(nt_tree_create(NULL,&rig->app.tree) == NT_RESULT_OK);
    mount(&rig->app,&rig->root,none,'R',false);
    mount(&rig->app,&rig->left,rig->root.id,'A',false);
    mount(&rig->app,&rig->right,rig->root.id,'B',false);
    mount(&rig->app,&rig->child,rig->left.id,'C',true);
    if (peer) mount(&rig->app,&rig->peer,rig->right.id,'D',true);
    rig->child.capture = true;
    CHECK(nt_tree_publish(rig->app.tree,(NtCellSize){12,4}) == NT_RESULT_OK);
}

static NtDispatchResult pointer(App *app, NtEventKind kind, NtPointerId id, int32_t x, int32_t y,
                                uint32_t button, uint32_t pressed, NtResult expected)
{
    const NtEvent input = {.kind=kind,.at=25,.data.pointer={id,{x,y},button,pressed,true,0}};
    NtDispatchResult result = {0};
    CHECK(nt_tree_dispatch(app->tree,&input,(NtByteView){0},NULL,&result) == expected);
    return result;
}

static void press(Rig *rig, NtPointerId id)
{
    CHECK(same(pointer(&rig->app,NT_EVENT_POINTER_DOWN,id,1,1,NT_BUTTON_PRIMARY,
                       NT_BUTTON_PRIMARY,NT_RESULT_OK).target,rig->child.id));
    CHECK(same(nt_tree_capture(rig->app.tree,id),rig->child.id));
}

static void path(const App *app, const char *nodes)
{
    CHECK(app->count == strlen(nodes));
    for (size_t i = 0; i < app->count; ++i) CHECK(app->seen[i].node == nodes[i]);
    if (app->count == 5) {
        CHECK(app->seen[0].phase == NT_EVENT_PREVIEW && app->seen[1].phase == NT_EVENT_PREVIEW);
        CHECK(app->seen[2].phase == NT_EVENT_TARGET);
        CHECK(app->seen[3].phase == NT_EVENT_BUBBLE && app->seen[4].phase == NT_EVENT_BUBBLE);
    }
}

static void final_pointer_release_retires_capture_after_route_error(void)
{
    Rig rig;
    prepare(&rig,false);
    App *app = &rig.app;
    press(&rig,7);
    app->count = 0;
    pointer(app,NT_EVENT_POINTER_MOVE,7,-4,9,0,NT_BUTTON_PRIMARY,NT_RESULT_OK);
    path(app,"RACAR");
    CHECK(app->seen[2].positioned && app->seen[2].local.x == -5 && app->seen[2].local.y == 8);
    app->count = 0;
    pointer(app,NT_EVENT_POINTER_MOVE,7,INT32_MIN,9,0,NT_BUTTON_PRIMARY,NT_RESULT_OK);
    path(app,"RACAR");
    CHECK(!app->seen[2].positioned);

    app->count = 0;
    rig.root.consume_preview = true;
    CHECK(pointer(app,NT_EVENT_POINTER_UP,7,-4,9,NT_BUTTON_PRIMARY,0,NT_RESULT_OK).consumed);
    path(app,"RC");
    CHECK(app->seen[1].kind == NT_EVENT_POINTER_CANCEL && app->seen[1].pointer == 7);
    CHECK(app->seen[1].reason == NT_CANCEL_ROUTE_INTERRUPTED);

    rig.root.consume_preview = false;
    press(&rig,7);
    rig.left.fail_bubble = true;
    app->count = 0;
    pointer(app,NT_EVENT_POINTER_UP,7,1,1,NT_BUTTON_PRIMARY,0,NT_RESULT_UNSUPPORTED_CAPABILITY);
    path(app,"RACA");
    CHECK(same(nt_tree_capture(app->tree,7),none));

    rig.left.fail_bubble = false;
    press(&rig,7);
    rig.child.fail_target = true;
    app->count = 0;
    pointer(app,NT_EVENT_POINTER_UP,7,1,1,NT_BUTTON_PRIMARY,0,NT_RESULT_UNSUPPORTED_CAPABILITY);
    path(app,"RACC");
    CHECK(app->seen[3].kind == NT_EVENT_POINTER_CANCEL);

    rig.child.fail_target = false;
    press(&rig,7);
    pointer(app,NT_EVENT_POINTER_DOWN,7,1,1,NT_BUTTON_SECONDARY,
            NT_BUTTON_PRIMARY | NT_BUTTON_SECONDARY,NT_RESULT_OK);
    app->count = 0;
    rig.root.consume_preview = true;
    CHECK(pointer(app,NT_EVENT_POINTER_UP,7,1,1,NT_BUTTON_PRIMARY,
                  NT_BUTTON_SECONDARY,NT_RESULT_OK).consumed);
    path(app,"RC");
    CHECK(app->seen[1].kind == NT_EVENT_POINTER_CANCEL);
    CHECK(app->seen[1].reason == NT_CANCEL_ROUTE_INTERRUPTED);
    CHECK(same(nt_tree_capture(app->tree,7),none));
    app->count = 0;
    CHECK(pointer(app,NT_EVENT_POINTER_UP,7,1,1,NT_BUTTON_SECONDARY,0,NT_RESULT_OK).consumed);
    CHECK(app->count == 0);
    nt_tree_destroy(app->tree);
}

static void capture_outside_clip_then_cancel_before_focus_loss(void)
{
    for (unsigned remove = 0; remove < 2; ++remove) {
        Rig rig;
        prepare(&rig,false);
        App *app = &rig.app;
        CHECK(nt_tree_set_focus(app->tree,rig.child.id) == NT_RESULT_OK);
        press(&rig,7);
        press(&rig,5);
        app->count = 0;
        app->verify_cleared = true;
        if (remove) CHECK(nt_tree_remove(app->tree,rig.child.id) == NT_RESULT_OK);
        else {
            rig.left.hide = true;
            CHECK(nt_tree_publish(app->tree,(NtCellSize){12,4}) == NT_RESULT_OK);
        }
        path(app,"CCC");
        CHECK(app->seen[0].kind == NT_EVENT_POINTER_CANCEL && app->seen[0].pointer == 5);
        CHECK(app->seen[1].kind == NT_EVENT_POINTER_CANCEL && app->seen[1].pointer == 7);
        CHECK(app->seen[0].reason == (remove ? NT_CANCEL_REMOVED : NT_CANCEL_INELIGIBLE));
        CHECK(app->seen[2].kind == NT_EVENT_FOCUS_LOSS);
        CHECK(rig.child.disposals == remove);
        if (!remove) {
            app->verify_cleared = false;
            rig.left.hide = false;
            CHECK(nt_tree_publish(app->tree,(NtCellSize){12,4}) == NT_RESULT_OK);
            app->count = 0;
            CHECK(pointer(app,NT_EVENT_POINTER_MOVE,7,1,1,0,NT_BUTTON_PRIMARY,NT_RESULT_OK).consumed);
            CHECK(pointer(app,NT_EVENT_POINTER_UP,7,1,1,NT_BUTTON_PRIMARY,0,NT_RESULT_OK).consumed);
            CHECK(!app->count);
            press(&rig,7);
        }
        nt_tree_destroy(app->tree);
    }
}

static void reparent_switches_committed_path_and_geometry_together(void)
{
    Rig rig;
    prepare(&rig,false);
    App *app = &rig.app;
    CHECK(nt_tree_set_focus(app->tree,rig.child.id) == NT_RESULT_OK);
    press(&rig,5);
    CHECK(nt_tree_move(app->tree,rig.child.id,rig.right.id) == NT_RESULT_OK);
    rig.child.fail_paint = true;
    CHECK(nt_tree_publish(app->tree,(NtCellSize){12,4}) == NT_RESULT_UNSUPPORTED_CAPABILITY);
    app->count = 0;
    pointer(app,NT_EVENT_POINTER_MOVE,5,7,1,0,NT_BUTTON_PRIMARY,NT_RESULT_OK);
    path(app,"RACAR");
    CHECK(app->seen[2].local.x == 6);
    rig.child.fail_paint = false;
    CHECK(nt_tree_publish(app->tree,(NtCellSize){12,4}) == NT_RESULT_OK);
    app->count = 0;
    pointer(app,NT_EVENT_POINTER_MOVE,5,7,1,0,NT_BUTTON_PRIMARY,NT_RESULT_OK);
    path(app,"RBCBR");
    CHECK(app->seen[2].local.x == 0 && same(nt_tree_capture(app->tree,5),rig.child.id));
    CHECK(same(nt_tree_focus(app->tree),rig.child.id));

    CHECK(nt_tree_move(app->tree,rig.child.id,rig.left.id) == NT_RESULT_OK);
    rig.child.fail_paint = true;
    CHECK(nt_tree_publish(app->tree,(NtCellSize){12,4}) == NT_RESULT_UNSUPPORTED_CAPABILITY);
    app->count = 0;
    CHECK(nt_tree_remove(app->tree,rig.right.id) == NT_RESULT_OK);
    path(app,"CC");
    CHECK(app->seen[0].kind == NT_EVENT_POINTER_CANCEL && app->seen[1].kind == NT_EVENT_FOCUS_LOSS);
    CHECK(same(nt_tree_focus(app->tree),none) && same(nt_tree_capture(app->tree,5),none));
    CHECK(!rig.child.disposals);
    rig.child.fail_paint = false;
    CHECK(nt_tree_publish(app->tree,(NtCellSize){12,4}) == NT_RESULT_OK);
    nt_tree_destroy(app->tree);
}

static NtDispatchResult dispatch(App *app, NtEvent input, NtByteView associated, const NtDispatchPolicy *policy)
{
    NtDispatchResult out;
    CHECK(nt_tree_dispatch(app->tree,&input,associated,policy,&out) == NT_RESULT_OK);
    return out;
}

static void focus_and_shortcuts(void)
{
    Rig rig;
    prepare(&rig,true);
    App *app = &rig.app;
    const NtKeyChord reserved = {NT_KEY_TAB,NT_MOD_SHIFT};
    const NtDispatchPolicy policy = {&reserved,1,true};
    const NtByteView no_text = {0};
    CHECK(nt_tree_set_focus(app->tree,rig.child.id) == NT_RESULT_OK);
    press(&rig,5);
    press(&rig,7);
    app->count = 0;
    dispatch(app,(NtEvent){.kind=NT_EVENT_POINTER_CANCEL,.data.pointer_cancel={5,NT_CANCEL_HOST}},
             no_text,NULL);
    path(app,"C");
    CHECK(same(nt_tree_capture(app->tree,7),rig.child.id) && same(nt_tree_focus(app->tree),rig.child.id));
    app->count = 0;
    dispatch(app,(NtEvent){.kind=NT_EVENT_HOST_CANCEL,.data.reason=NT_CANCEL_HOST},no_text,NULL);
    path(app,"CC");
    CHECK(app->seen[0].pointer == 7 && app->seen[1].kind == NT_EVENT_FOCUS_LOSS);

    app->count = 0;
    NtEvent tab = {.kind=NT_EVENT_KEY,.data.key={NT_KEY_TAB,NT_MOD_SHIFT,NT_KEY_PRESS}};
    CHECK(dispatch(app,tab,(NtByteView){"x",1},&policy).reserved);
    CHECK(!app->count && same(nt_tree_focus(app->tree),none));
    const NtEvent text = {.kind=NT_EVENT_TEXT,.data.text={"é",2}};
    const NtEvent paste = {.kind=NT_EVENT_PASTE,.data.text={"a\0b",3}};
    CHECK(same(dispatch(app,text,no_text,NULL).target,none));
    CHECK(same(dispatch(app,paste,no_text,NULL).target,none));
    CHECK(!app->count);
    tab.data.key.modifiers = 0;
    CHECK(dispatch(app,tab,no_text,&policy).consumed);
    CHECK(same(nt_tree_focus(app->tree),rig.child.id));
    CHECK(same(dispatch(app,text,no_text,NULL).target,rig.child.id));
    CHECK(app->text_count == 2 && !memcmp(app->text,"é",2));
    CHECK(same(dispatch(app,paste,no_text,NULL).target,rig.child.id));
    CHECK(app->text_count == 3 && !memcmp(app->text,"a\0b",3));
    const NtEvent letter = {.kind=NT_EVENT_KEY,.data.key={'a',0,NT_KEY_PRESS}};
    CHECK(dispatch(app,letter,(NtByteView){"a",1},NULL).text_delivered);
    rig.child.consume_key = true;
    NtDispatchResult consumed = dispatch(app,letter,(NtByteView){"b",1},NULL);
    CHECK(consumed.consumed && !consumed.text_delivered && app->text[0] == 'a');
    rig.child.consume_key = false;
    dispatch(app,tab,no_text,&policy);
    CHECK(same(nt_tree_focus(app->tree),rig.peer.id));
    CHECK(nt_tree_focus_advance(app->tree,true) == NT_RESULT_OK);
    CHECK(same(nt_tree_focus(app->tree),rig.child.id));
    CHECK(nt_tree_focus_advance(app->tree,true) == NT_RESULT_OK);
    CHECK(same(nt_tree_focus(app->tree),rig.peer.id));

    const size_t before = app->count;
    const NtEvent malformed = {.kind=NT_EVENT_TEXT,.data.text={"\xff",1}};
    NtDispatchResult out = {.reserved=true};
    CHECK(nt_tree_dispatch(app->tree,&malformed,no_text,NULL,&out) == NT_RESULT_INVALID_ARGUMENT);
    pointer(app,NT_EVENT_POINTER_MOVE,2,1,1,0,NT_BUTTON_PRIMARY,NT_RESULT_INVALID_ARGUMENT);
    CHECK(out.reserved && app->count == before && same(nt_tree_focus(app->tree),rig.peer.id));
    nt_tree_destroy(app->tree);
}

static void masked_hit_and_early_release(void)
{
    Rig rig;
    prepare(&rig,false);
    App *app = &rig.app;
    rig.left.mask = true;
    CHECK(nt_tree_publish(app->tree,(NtCellSize){12,4}) == NT_RESULT_OK);
    CHECK(same(pointer(app,NT_EVENT_POINTER_MOVE,8,3,1,0,0,NT_RESULT_OK).target,none));
    CHECK(same(pointer(app,NT_EVENT_POINTER_MOVE,8,1,1,0,0,NT_RESULT_OK).target,rig.child.id));
    press(&rig,8);
    rig.child.release = true;
    app->count = 0;
    pointer(app,NT_EVENT_POINTER_MOVE,8,2,1,0,NT_BUTTON_PRIMARY,NT_RESULT_OK);
    path(app,"RACARC");
    CHECK(app->seen[5].kind == NT_EVENT_POINTER_CANCEL && app->seen[5].reason == NT_CANCEL_RELEASE);
    app->count = 0;
    CHECK(pointer(app,NT_EVENT_POINTER_UP,8,1,1,NT_BUTTON_PRIMARY,0,NT_RESULT_OK).consumed);
    CHECK(!app->count);
    nt_tree_destroy(app->tree);
}

static void failed_bubble_discards_requests_only(void)
{
    Rig rig;
    prepare(&rig,false);
    App *app = &rig.app;
    rig.child.focus_on_press = true;
    rig.left.fail_down_bubble = true;
    pointer(app,NT_EVENT_POINTER_DOWN,9,1,1,NT_BUTTON_PRIMARY,NT_BUTTON_PRIMARY,
            NT_RESULT_UNSUPPORTED_CAPABILITY);
    path(app,"RACA");
    CHECK(same(nt_tree_focus(app->tree),none) && same(nt_tree_capture(app->tree,9),none));
    CHECK(nt_tree_pending(app->tree) == NT_INVALIDATE_PAINT);
    NtIntentView intent;
    CHECK(nt_tree_peek_intent(app->tree,&intent) && same(intent.source,rig.child.id));
    CHECK(intent.kind == 42 && intent.payload.count == 4 && !memcmp(intent.payload.data,"down",4));
    nt_tree_pop_intent(app->tree);
    CHECK(!nt_tree_peek_intent(app->tree,&intent));
    pointer(app,NT_EVENT_POINTER_UP,9,1,1,NT_BUTTON_PRIMARY,0,NT_RESULT_OK);
    nt_tree_destroy(app->tree);
}

static void absent_event_callback_has_no_text_delivery(void)
{
    App app = {0};
    CHECK(nt_tree_create(NULL,&app.tree) == NT_RESULT_OK);
    Part root = {.name='R',.app=&app};
    const NtComponentOps ops = {measure,layout,NULL,paint,NULL,dispose};
    CHECK(nt_tree_mount(app.tree,none,&ops,&root,(NtMountOptions){true,true,false},&root.id)
          == NT_RESULT_OK);
    CHECK(nt_tree_publish(app.tree,(NtCellSize){4,2}) == NT_RESULT_OK);
    CHECK(nt_tree_set_focus(app.tree,root.id) == NT_RESULT_OK);
    const NtEvent key = {.kind=NT_EVENT_KEY,.data.key={'a',0,NT_KEY_PRESS}};
    const NtDispatchResult result = dispatch(&app,key,(NtByteView){"a",1},NULL);
    CHECK(same(result.target,root.id) && !result.text_delivered);
    nt_tree_destroy(app.tree);
}

int main(void)
{
    final_pointer_release_retires_capture_after_route_error();
    capture_outside_clip_then_cancel_before_focus_loss();
    reparent_switches_committed_path_and_geometry_together();
    focus_and_shortcuts();
    masked_hit_and_early_release();
    failed_bubble_discards_requests_only();
    absent_event_callback_has_no_text_delivery();
    puts("Committed routes, capture cleanup, focus and shortcuts passed.");
    return EXIT_SUCCESS;
}
