#define _POSIX_C_SOURCE 200809L

#include <nocturne/notification.h>
#include <nocturne/posix_host.h>
#include <nocturne/window.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define BYTES(s) (NtByteView){s,sizeof(s)-1}
#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr,"window terminal line %d: %s\n",__LINE__,#test); exit(EXIT_FAILURE); \
} } while (0)
#define OK(call) CHECK((call) == NT_RESULT_OK)

typedef struct App { NtTree *tree; NtNodeId floating, docked, notice; unsigned actions; } App;

static void mount(App *app)
{
    NtNodeId workspace;
    OK(nt_tree_create(NULL,&app->tree));
    OK(nt_window_workspace_mount(app->tree,(NtNodeId){0},(NtMountOptions){true,false,false},
                                 (NtCellSize){42,12},&workspace));
    NtWindowOptions window = nt_window_options_default();
    window.placement.floating = (NtCellRect){{2,2},{18,7}};
    window.panel.title.text = BYTES("Dispatch");
    const NtMountOptions interactive = {true,true,true};
    OK(nt_window_mount(app->tree,workspace,interactive,&window,&app->floating));
    window.placement.dock = NT_WINDOW_DOCK_RIGHT;
    window.placement.dock_extent = 8;
    window.panel.title.text = BYTES("Shelf");
    OK(nt_window_mount(app->tree,workspace,interactive,&window,&app->docked));

    const NtNotificationAction action = {12,BYTES("Reserve"),false};
    NtNotificationOptions options = nt_notification_options_default();
    options.id = 800;
    options.title = BYTES("Café queue");
    options.message = BYTES("Ready");
    options.message_rows = 1;
    options.actions = &action;
    options.action_count = 1;
    options.expires_at = 120000;
    OK(nt_notification_mount(app->tree,app->floating,interactive,&options,&app->notice));
}

static NtResult reserved(void *value, const NtEvent *event, NtByteView text)
{
    (void)text;
    App *app = value;
    if (event->data.key.key == 'r') {
        return nt_window_animate(app->tree,app->floating,
                (NtWindowAnimation){{{10,2},{18,7}},event->at,40000,NT_LINEAR});
    }
    const NtResult result = nt_tree_remove(app->tree,app->floating);
    return result == NT_RESULT_OK ? nt_window_focus(app->tree,app->docked,(NtNodeId){0}) : result;
}

static NtResult collect(void *value, const NtDispatchResult *result)
{
    (void)result;
    App *app = value;
    NtEventIntentView intent;
    while (nt_tree_peek_event_intent(app->tree,&intent)) {
        NtNotificationEvent action;
        OK(nt_notification_event_decode(intent,&action));
        CHECK(action.kind == NT_NOTIFICATION_PERFORMED && action.notification == 800 && action.action == 12);
        CHECK(intent.intent.source.tree == app->tree && intent.intent.source.slot == app->notice.slot);
        ++app->actions;
        nt_tree_pop_intent(app->tree);
    }
    return NT_RESULT_OK;
}

static void report(const App *app, NtPosixStep step)
{
    NtWindowState window;
    const bool visible = nt_window_get(app->tree,app->floating,&window) == NT_RESULT_OK && window.published.placed;
    NtWindowState dock;
    OK(nt_window_get(app->tree,app->docked,&dock));
    const NtOwnerToken focus = nt_node_owner(nt_tree_focus(app->tree));
    const int focused = focus == nt_node_owner(app->notice) ? 1 : focus == nt_node_owner(app->docked) ? 2 : 0;
    fprintf(stderr,"{\"result\":0,\"events\":%zu,\"left\":%d,\"dock\":%d,\"resized\":%d,"
                   "\"actions\":%u,\"focus\":%d,\"capture\":%d}\n",
            step.input_events,visible ? window.published.bounds.origin.x : -1,
            dock.published.bounds.origin.x,step.resized,app->actions,focused,nt_tree_capture(app->tree,0).tree != NULL);
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    FILE *commands = fdopen(atoi(argv[1]),"r");
    CHECK(commands);
    App app = {0};
    mount(&app);
    const NtPosixHostOptions options = {
        .input_fd=STDIN_FILENO,.output_fd=STDOUT_FILENO,
        .capabilities={.color=NT_COLOR_TRUECOLOR,.interactive=true,.utf8=true,.pointer=true,
                       .bracketed_paste=true,.width={NT_WIDTH_ONE,NT_WIDTH_TWO}},
        .motion=NT_MOTION_FULL,.output_timeout_ms=100,.alternate_screen=true,.hide_cursor=true,
        .report_focus=true,.catch_signals=true,.reserved_key=reserved,.reserved_context=&app,
        .after_input=collect,.input_context=&app
    };
    const NtKeyChord chords[] = {{'r',NT_MOD_CONTROL},{'x',NT_MOD_CONTROL}};
    const NtDispatchPolicy policy = {chords,sizeof(chords)/sizeof(*chords),false};
    NtPosixHost *host;
    OK(nt_posix_host_open(app.tree,0,NULL,&options,&host));
    fputs("READY\n",stderr);
    char command[80];
    while (fgets(command,sizeof(command),commands) && command[0] != 'q') {
        NtTimeUs now;
        CHECK(sscanf(command,"s %" SCNu64,&now) == 1);
        NtPosixStep step;
        OK(nt_posix_host_step(host,now,&policy,&step));
        report(&app,step);
    }
    OK(nt_posix_host_close(host));
    nt_posix_host_destroy(host);
    nt_tree_destroy(app.tree);
    fclose(commands);
    fputs("DONE\n",stderr);
    return EXIT_SUCCESS;
}
