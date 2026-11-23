#include <nocturne/notification.h>
#include <nocturne/window.h>

#include <stdio.h>

#define VIEW(s) ((NtByteView){s,sizeof(s)-1})
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr,"notification consumer line %d: %s\n",__LINE__,#condition); return 1; \
} } while (0)

static bool click(NtTree *tree, NtTimeUs time, NtPointerId id, NtCellPoint cell, bool down)
{
    const NtEvent event = {.kind=down ? NT_EVENT_POINTER_DOWN : NT_EVENT_POINTER_UP,.at=time,
        .data.pointer={.id=id,.position=cell,.button=NT_BUTTON_PRIMARY,
                       .pressed=down ? NT_BUTTON_PRIMARY : 0,.has_position=true}};
    NtDispatchResult result;
    return nt_tree_dispatch(tree,&event,(NtByteView){0},NULL,&result) == NT_RESULT_OK && result.consumed;
}

static bool key(NtTree *tree, NtTimeUs time, uint32_t key)
{
    const NtEvent event = {.kind=NT_EVENT_KEY,.at=time,.data.key={key,0,NT_KEY_PRESS}};
    NtDispatchResult result;
    return nt_tree_dispatch(tree,&event,(NtByteView){0},NULL,&result) == NT_RESULT_OK && result.consumed;
}

static bool take(NtTree *tree, NtNodeId source, NtNotificationEventKind kind, uint64_t id, uint64_t action)
{
    NtEventIntentView intent;
    NtNotificationEvent decoded;
    const bool matches = nt_tree_peek_event_intent(tree,&intent) && intent.intent.source.slot == source.slot &&
        nt_notification_event_decode(intent,&decoded) == NT_RESULT_OK && decoded.kind == kind &&
        decoded.notification == id && decoded.action == action;
    if (matches) nt_tree_pop_intent(tree);
    return matches;
}

static int replacement_and_expiry(void)
{
    NtTree *tree;
    NtNodeId notice;
    CHECK(nt_tree_create(NULL,&tree) == NT_RESULT_OK);
    NtNotificationOptions options = nt_notification_options_default();
    const NtNotificationAction first = {20,VIEW("First"),false};
    const NtNotificationAction next = {21,VIEW("Next"),false};
    options.id = 200;
    options.actions = &first;
    options.action_count = 1;
    CHECK(nt_notification_mount(tree,(NtNodeId){0},(NtMountOptions){true,true,true},&options,&notice) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){18,3}) == NT_RESULT_OK);
    CHECK(click(tree,1,8,(NtCellPoint){3,0},true));

    const NtNotificationAction duplicate[] = {first,first};
    NtNotificationOptions invalid = options;
    invalid.id = 999;
    invalid.actions = duplicate;
    invalid.action_count = 2;
    CHECK(nt_notification_set(tree,notice,&invalid) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(click(tree,2,8,(NtCellPoint){3,0},false));
    CHECK(take(tree,notice,NT_NOTIFICATION_PERFORMED,200,20));

    CHECK(click(tree,3,8,(NtCellPoint){3,0},true));
    options.id = 201;
    options.actions = &next;
    options.expires_at = 7;
    CHECK(nt_notification_set(tree,notice,&options) == NT_RESULT_OK);
    CHECK(!click(tree,4,8,(NtCellPoint){3,0},false));
    NtIntentView intent;
    CHECK(!nt_tree_peek_intent(tree,&intent));
    CHECK(nt_tree_publish(tree,(NtCellSize){18,3}) == NT_RESULT_OK);
    CHECK(click(tree,5,8,(NtCellPoint){3,0},true));
    CHECK(!click(tree,7,8,(NtCellPoint){3,0},false));
    CHECK(!nt_tree_peek_intent(tree,&intent));
    NtNotificationState status;
    CHECK(nt_notification_get(tree,notice,&status) == NT_RESULT_OK && status.status == NT_NOTIFICATION_EXPIRED);
    CHECK(nt_tree_publish(tree,(NtCellSize){18,3}) == NT_RESULT_OK);
    NtNodeId hit;
    CHECK(nt_tree_hit(tree,(NtCellPoint){3,0},&hit) == NT_RESULT_OK && !hit.tree);
    CHECK(!nt_tree_focus(tree).tree && !nt_tree_capture(tree,8).tree);
    nt_tree_destroy(tree);
    return 0;
}

int main(void)
{
    CHECK(replacement_and_expiry() == 0);
    NtTree *tree;
    NtNodeId workspace, card, notice, other;
    CHECK(nt_tree_create(NULL,&tree) == NT_RESULT_OK);
    CHECK(nt_window_workspace_mount(tree,(NtNodeId){0},(NtMountOptions){true,false,false},
                                    (NtCellSize){45,14},&workspace) == NT_RESULT_OK);
    NtWindowOptions window = nt_window_options_default();
    window.placement.floating = (NtCellRect){{2,2},{20,8}};
    CHECK(nt_window_mount(tree,workspace,(NtMountOptions){true,true,true},&window,&card) == NT_RESULT_OK);
    window.placement.dock = NT_WINDOW_DOCK_RIGHT;
    window.placement.dock_extent = 7;
    CHECK(nt_window_mount(tree,workspace,(NtMountOptions){true,true,true},&window,&other) == NT_RESULT_OK);
    char title[] = "café";
    NtNotificationAction buttons[] = {{14,VIEW("Save draft"),false},{18,VIEW("Send"),true}};
    NtNotificationOptions options = nt_notification_options_default();
    options.id = 100;
    options.title = (NtByteView){title,sizeof(title)-1};
    options.message = VIEW("Changes ready");
    options.message_rows = 1;
    options.actions = buttons;
    options.action_count = 2;
    options.expires_at = 90000;
    CHECK(nt_notification_mount(tree,card,(NtMountOptions){true,true,true},&options,&notice) == NT_RESULT_OK);
    title[0] = 'Z';
    buttons[0].id = 89;
    CHECK(nt_tree_tick(tree,1,NT_MOTION_FULL) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){45,14}) == NT_RESULT_OK);
    CHECK(nt_tree_next_update(tree).scheduled && nt_tree_next_update(tree).at == 90000);
    NtCellView caption;
    CHECK(nt_frame_cell(nt_tree_frame(tree),(NtCellPoint){4,3},&caption) == NT_RESULT_OK);
    CHECK(caption.glyph.count == 1 && caption.glyph.data[0] == 'c');

    CHECK(nt_window_focus(tree,other,(NtNodeId){0}) == NT_RESULT_OK);
    CHECK(click(tree,2,6,(NtCellPoint){6,5},true));
    CHECK(nt_tree_focus(tree).slot == notice.slot && nt_tree_capture(tree,6).slot == notice.slot);
    CHECK(nt_window_animate(tree,card,(NtWindowAnimation){{{10,2},{20,8}},2,40000,NT_LINEAR}) == NT_RESULT_OK);
    CHECK(nt_tree_tick(tree,20002,NT_MOTION_FULL) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){45,14}) == NT_RESULT_OK);
    CHECK(click(tree,20003,6,(NtCellPoint){10,5},false));
    CHECK(take(tree,notice,NT_NOTIFICATION_PERFORMED,100,14));
    CHECK(!nt_tree_capture(tree,6).tree);
    NtNotificationState state;
    CHECK(nt_notification_get(tree,notice,&state) == NT_RESULT_OK && state.status == NT_NOTIFICATION_ACTIVE);

    CHECK(key(tree,20004,NT_KEY_DOWN));
    CHECK(key(tree,20005,NT_KEY_ENTER));
    CHECK(take(tree,notice,NT_NOTIFICATION_PERFORMED,100,14));
    CHECK(key(tree,20006,NT_KEY_ESCAPE));
    CHECK(take(tree,notice,NT_NOTIFICATION_DISMISSED_EVENT,100,0));
    CHECK(nt_notification_get(tree,notice,&state) == NT_RESULT_OK && state.status == NT_NOTIFICATION_DISMISSED);
    CHECK(nt_tree_publish(tree,(NtCellSize){45,14}) == NT_RESULT_OK && !nt_tree_focus(tree).tree);

    options.id = 101;
    options.action_count = 0;
    options.title = VIEW("Synchronizing");
    CHECK(nt_notification_set(tree,notice,&options) == NT_RESULT_OK);
    CHECK(nt_tree_tick(tree,90000,NT_MOTION_REDUCED) == NT_RESULT_OK);
    CHECK(nt_notification_get(tree,notice,&state) == NT_RESULT_OK && state.status == NT_NOTIFICATION_EXPIRED);
    CHECK(nt_tree_publish(tree,(NtCellSize){40,12}) == NT_RESULT_OK);
    NtNodeId hit;
    CHECK(nt_tree_hit(tree,(NtCellPoint){12,3},&hit) == NT_RESULT_OK && hit.slot == card.slot);
    NtIntentView unused;
    CHECK(!nt_tree_peek_intent(tree,&unused));
    CHECK(nt_tree_remove(tree,card) == NT_RESULT_OK);
    CHECK(nt_notification_get(tree,notice,&state) == NT_RESULT_STALE_REFERENCE);
    nt_tree_destroy(tree);
    return 0;
}
