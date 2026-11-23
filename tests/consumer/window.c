#include <nocturne/window.h>

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr,"window consumer line %d: %s\n",__LINE__,#condition); return 1; \
} } while (0)

typedef struct Provider { size_t paints, updates, disposals, focus; bool fail; } Provider;
static NtResult content_paint(const void *value, NtPaintContext *paint, NtCellSize size)
{
    Provider *provider = (Provider *)value;
    ++provider->paints;
    if (provider->fail) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    if (!size.columns || !size.rows) return NT_RESULT_OK;
    return nt_paint_glyph(paint,(NtCellPoint){0,0},(NtByteView){"A",1},1,
                          (NtCellStyle){.foreground={255,255,255,255}});
}
static NtResult content_update(void *value, NtUpdateInfo info, NtUpdateReply *reply)
{
    Provider *provider = value;
    ++provider->updates;
    *reply = (NtUpdateReply){.invalidate=NT_INVALIDATE_PAINT};
    return info.now >= 1 ? NT_RESULT_OK : NT_RESULT_INVALID_ARGUMENT;
}
static NtResult content_event(void *value, NtEventContext *context, const NtEvent *event, NtEventReply *reply)
{
    Provider *provider = value;
    (void)reply;
    if (event->kind == NT_EVENT_FOCUS_GAIN && nt_event_phase(context) == NT_EVENT_TARGET)
        ++provider->focus;
    return NT_RESULT_OK;
}
static void content_dispose(void *value) { ++((Provider *)value)->disposals; }

static bool bounds_are(const NtTree *tree, NtNodeId node, int x, int y, unsigned cols, unsigned rows)
{
    NtNodeGeometry g;
    return nt_tree_geometry(tree,node,&g) == NT_RESULT_OK && g.placed &&
           g.bounds.origin.x == x && g.bounds.origin.y == y &&
           g.bounds.size.columns == cols && g.bounds.size.rows == rows;
}

static bool pointer(NtTree *tree, NtEventKind kind, NtTimeUs at, int x, int y)
{
    const NtEvent event = {.kind=kind,.at=at,.data.pointer={
        .id=7,.position={x,y},.button=kind==NT_EVENT_POINTER_MOVE ? 0 : NT_BUTTON_PRIMARY,
        .pressed=kind==NT_EVENT_POINTER_UP ? 0 : NT_BUTTON_PRIMARY,.has_position=true}};
    NtDispatchResult result;
    return nt_tree_dispatch(tree,&event,(NtByteView){0},NULL,&result) == NT_RESULT_OK && result.consumed;
}

static int step_endpoint(void)
{
    NtTree *tree;
    NtNodeId workspace, window;
    CHECK(nt_tree_create(NULL,&tree) == NT_RESULT_OK);
    CHECK(nt_window_workspace_mount(tree,(NtNodeId){0},(NtMountOptions){true,false,false},
                                    (NtCellSize){30,10},&workspace) == NT_RESULT_OK);
    NtWindowOptions options = nt_window_options_default();
    options.placement.floating = (NtCellRect){{2,1},{8,4}};
    CHECK(nt_window_mount(tree,workspace,(NtMountOptions){true,true,true},&options,&window) == NT_RESULT_OK);
    CHECK(nt_tree_tick(tree,1,NT_MOTION_FULL) == NT_RESULT_OK);
    CHECK(nt_window_animate(tree,window,(NtWindowAnimation){{{10,1},{8,4}},2,UINT64_MAX-2,NT_STEP_END}) == NT_RESULT_OK);
    CHECK(nt_tree_tick(tree,UINT64_MAX-1,NT_MOTION_FULL) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){30,10}) == NT_RESULT_OK);
    CHECK(bounds_are(tree,window,2,1,8,4));
    CHECK(nt_tree_next_update(tree).at == UINT64_MAX);
    CHECK(nt_tree_tick(tree,UINT64_MAX,NT_MOTION_FULL) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){30,10}) == NT_RESULT_OK);
    CHECK(bounds_are(tree,window,10,1,8,4));
    nt_tree_destroy(tree);
    return 0;
}

static int rounding_and_chrome(void)
{
    NtTree *tree;
    NtNodeId workspace, first, upper, hit;
    CHECK(nt_tree_create(NULL,&tree) == NT_RESULT_OK);
    CHECK(nt_window_workspace_mount(tree,(NtNodeId){0},(NtMountOptions){true,false,false},
                                    (NtCellSize){30,10},&workspace) == NT_RESULT_OK);
    NtWindowOptions options = nt_window_options_default();
    options.placement.floating = (NtCellRect){{0,0},{7,1}};
    options.draggable = false;
    CHECK(nt_window_mount(tree,workspace,(NtMountOptions){true,true,true},&options,&first) == NT_RESULT_OK);
    options.placement.floating.size.columns = 5;
    CHECK(nt_window_mount(tree,workspace,(NtMountOptions){true,true,true},&options,&upper) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){30,10}) == NT_RESULT_OK);
    CHECK(nt_tree_hit(tree,(NtCellPoint){1,0},&hit) == NT_RESULT_OK && hit.slot == upper.slot);
    NtEvent secondary = {.kind=NT_EVENT_POINTER_DOWN,.at=1,.data.pointer=
        {.id=2,.position={6,0},.button=NT_BUTTON_SECONDARY,.pressed=NT_BUTTON_SECONDARY,.has_position=true}};
    NtDispatchResult result;
    CHECK(nt_tree_dispatch(tree,&secondary,(NtByteView){0},NULL,&result) == NT_RESULT_OK && !result.consumed);
    CHECK(nt_tree_publish(tree,(NtCellSize){30,10}) == NT_RESULT_OK);
    CHECK(nt_tree_hit(tree,(NtCellPoint){1,0},&hit) == NT_RESULT_OK && hit.slot == first.slot);

    CHECK(pointer(tree,NT_EVENT_POINTER_DOWN,2,6,0));
    CHECK(pointer(tree,NT_EVENT_POINTER_MOVE,3,8,1));
    CHECK(pointer(tree,NT_EVENT_POINTER_UP,4,8,1));
    CHECK(nt_tree_publish(tree,(NtCellSize){30,10}) == NT_RESULT_OK);
    CHECK(bounds_are(tree,first,0,0,9,2));

    CHECK(nt_window_animate(tree,first,(NtWindowAnimation){{{-11,11},{20,13}},5,22,NT_LINEAR}) == NT_RESULT_OK);
    CHECK(nt_tree_tick(tree,20,NT_MOTION_FULL) == NT_RESULT_OK);
    NtWindowState window;
    CHECK(nt_window_get(tree,first,&window) == NT_RESULT_OK);
    NtCellRect pending = window.requested.floating;
    CHECK(pending.origin.x == -8 && pending.origin.y == 8 && pending.size.columns == 17 && pending.size.rows == 10);
    NtWindowPlacement placement = window.requested;
    placement.floating = (NtCellRect){{0,0},{9,2}};
    CHECK(nt_window_set(tree,first,placement) == NT_RESULT_OK);
    const NtTimeUs duration = UINT64_MAX-21;
    CHECK(nt_window_animate(tree,first,(NtWindowAnimation){{{INT32_MIN,INT32_MAX},{9,2}},21,duration,NT_LINEAR}) == NT_RESULT_OK);
    CHECK(nt_tree_tick(tree,21+duration/2,NT_MOTION_FULL) == NT_RESULT_OK);
    CHECK(nt_window_get(tree,first,&window) == NT_RESULT_OK);
    CHECK(window.requested.floating.origin.x == -1073741824 && window.requested.floating.origin.y == 1073741824);
    nt_tree_destroy(tree);
    return 0;
}

int main(void)
{
    CHECK(step_endpoint() == 0);
    CHECK(rounding_and_chrome() == 0);
    const NtMountOptions visible = {true,true,true};
    NtTree *tree;
    CHECK(nt_tree_create(NULL,&tree) == NT_RESULT_OK);
    NtNodeId workspace, window, dock, provider;
    CHECK(nt_window_workspace_mount(tree,(NtNodeId){0},(NtMountOptions){true,false,false},
                                    (NtCellSize){40,16},&workspace) == NT_RESULT_OK);
    NtWindowOptions main = nt_window_options_default();
    char title[] = "Gauge";
    main.panel.title.text = (NtByteView){title,strlen(title)};
    main.placement.floating = (NtCellRect){{10,2},{13,7}};
    CHECK(nt_window_mount(tree,workspace,visible,&main,&window) == NT_RESULT_OK);
    title[0] = 'X';
    NtWindowOptions sidebar = nt_window_options_default();
    sidebar.placement.dock = NT_WINDOW_DOCK_LEFT;
    sidebar.placement.dock_extent = 6;
    CHECK(nt_window_mount(tree,workspace,visible,&sidebar,&dock) == NT_RESULT_OK);
    Provider state = {0};
    const NtPanelContentProvider feed = {
        .context=&state,.preferred={8,2},.paint=content_paint,.update=content_update,.event=content_event,.dispose=content_dispose
    };
    CHECK(nt_panel_content_mount(tree,window,visible,&feed,&provider) == NT_RESULT_OK);
    CHECK(nt_tree_tick(tree,1,NT_MOTION_FULL) == NT_RESULT_OK && state.updates == 1);
    CHECK(nt_tree_publish(tree,(NtCellSize){40,16}) == NT_RESULT_OK);
    CHECK(bounds_are(tree,dock,0,0,6,16));
    CHECK(bounds_are(tree,window,10,2,13,7));
    CHECK(bounds_are(tree,provider,11,3,11,5));
    NtCellView cell;
    CHECK(nt_frame_cell(nt_tree_frame(tree),(NtCellPoint){11,2},&cell) == NT_RESULT_OK);
    CHECK(cell.glyph.count == 1 && cell.glyph.data[0] == 'G');
    CHECK(nt_window_focus(tree,dock,(NtNodeId){0}) == NT_RESULT_OK && nt_tree_focus(tree).slot == dock.slot);

    CHECK(pointer(tree,NT_EVENT_POINTER_DOWN,2,12,2));
    CHECK(nt_tree_capture(tree,7).slot == window.slot && nt_tree_focus(tree).slot == window.slot);
    CHECK(pointer(tree,NT_EVENT_POINTER_MOVE,3,15,4));
    CHECK(bounds_are(tree,window,10,2,13,7));
    CHECK(nt_tree_publish(tree,(NtCellSize){40,16}) == NT_RESULT_OK);
    CHECK(bounds_are(tree,window,13,4,13,7));
    CHECK(pointer(tree,NT_EVENT_POINTER_UP,4,15,4));
    CHECK(!nt_tree_capture(tree,7).tree);
    CHECK(pointer(tree,NT_EVENT_POINTER_DOWN,5,25,10));
    CHECK(pointer(tree,NT_EVENT_POINTER_MOVE,6,28,11));
    CHECK(pointer(tree,NT_EVENT_POINTER_UP,7,28,11));
    CHECK(nt_tree_publish(tree,(NtCellSize){40,16}) == NT_RESULT_OK);
    CHECK(bounds_are(tree,window,13,4,16,8));

    CHECK(nt_window_animate(tree,window,(NtWindowAnimation){{{17,6},{18,10}},8,40000,NT_LINEAR}) == NT_RESULT_OK);
    CHECK(nt_tree_tick(tree,20008,NT_MOTION_FULL) == NT_RESULT_OK);
    const NtFrame *pressed = nt_tree_frame(tree);
    state.fail = true;
    CHECK(nt_tree_publish(tree,(NtCellSize){40,16}) == NT_RESULT_UNSUPPORTED_CAPABILITY);
    CHECK(nt_tree_frame(tree) == pressed && bounds_are(tree,window,13,4,16,8));
    state.fail = false;
    CHECK(nt_tree_publish(tree,(NtCellSize){40,16}) == NT_RESULT_OK);
    CHECK(bounds_are(tree,window,15,5,17,9));
    CHECK(nt_window_focus(tree,window,provider) == NT_RESULT_OK && nt_tree_focus(tree).slot == provider.slot);

    CHECK(state.focus == 1);

    NtWindowState mode;
    CHECK(nt_window_get(tree,window,&mode) == NT_RESULT_OK && mode.animating);
    mode.requested.mode = NT_WINDOW_MAXIMIZED;
    CHECK(nt_window_set(tree,window,mode.requested) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){34,12}) == NT_RESULT_OK);
    CHECK(bounds_are(tree,window,6,0,28,12));
    mode.requested.mode = NT_WINDOW_MINIMIZED;
    CHECK(nt_window_set(tree,window,mode.requested) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){34,12}) == NT_RESULT_OK);
    CHECK(nt_window_get(tree,window,&mode) == NT_RESULT_OK && !mode.published.placed);
    CHECK(!nt_tree_focus(tree).tree && state.disposals == 0);
    CHECK(nt_window_focus(tree,dock,(NtNodeId){0}) == NT_RESULT_OK);
    mode.requested.mode = NT_WINDOW_NORMAL;
    CHECK(nt_window_set(tree,window,mode.requested) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){34,12}) == NT_RESULT_OK);
    CHECK(bounds_are(tree,window,15,5,17,9));
    NtWindowPlacement offscreen = mode.requested;
    offscreen.floating.origin.x = INT32_MAX;
    CHECK(nt_window_set(tree,window,offscreen) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){34,12}) == NT_RESULT_OK);
    CHECK(nt_window_get(tree,window,&mode) == NT_RESULT_OK && !mode.published.placed);
    CHECK(nt_tree_remove(tree,window) == NT_RESULT_OK && state.disposals == 1);
    CHECK(nt_window_get(tree,window,&mode) == NT_RESULT_STALE_REFERENCE);
    nt_tree_destroy(tree);
    CHECK(state.disposals == 1);
    return 0;
}
