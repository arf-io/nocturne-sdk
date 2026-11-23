#define _POSIX_C_SOURCE 200809L

#include <nocturne/controls.h>
#include <nocturne/field.h>
#include <nocturne/graph.h>
#include <nocturne/layout.h>
#include <nocturne/particle_deposits.h>
#include <nocturne/plot.h>
#include <nocturne/posix_host.h>
#include <nocturne/text_effect.h>
#include <nocturne/window.h>

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define BYTES(s) ((NtByteView){(s),sizeof(s)-1})
#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr,"survey terminal line %d: %s\n",__LINE__,#test); exit(EXIT_FAILURE); \
} } while (0)
#define OK(call) CHECK((call) == NT_RESULT_OK)

enum { CONTACT_AT = 100000, TEXT_DURATION = 160000 };
static const NtWidthPolicy width = {NT_WIDTH_ONE,NT_WIDTH_TWO};
static const NtMountOptions passive = {true,false,false}, interactive = {true,true,true};

typedef struct App App;
typedef struct Weather {
    App *app;
    NtParticleEmitter *falling;
    NtParticleDeposits *deposits;
    NtTextLayout *title;
    NtTimeUs now;
    NtMotionPolicy motion;
    bool landed;
} Weather;

struct App {
    NtTree *tree;
    NtEditor *editor;
    NtNodeId workspace, notes, entry, charts, graph, plot, weather_window, weather_leaf;
    NtNodeId last_focus;
    Weather *weather;
    unsigned updates, removals, selections;
    NtGraphId last_selected;
    bool quit;
};

static void consider(NtDeadline *next, NtDeadline candidate)
{
    if (candidate.scheduled && (!next->scheduled || candidate.at < next->at)) *next = candidate;
}

static NtResult settle(Weather *weather)
{
    /* The application knows this floor is crossed at CONTACT_AT, even if
     * presentation skipped that tick. Deposit both real emitter samples. */
    for (uint64_t i = 0; i < 2; ++i) {
        bool alive;
        NtParticleSample sample;
        OK(nt_particle_emitter_particle(weather->falling,i,CONTACT_AT,&alive,&sample));
        CHECK(alive && sample.position.x == 18.5 && sample.position.y == 3);
        const NtParticleContact contact = {{(int32_t)floor(sample.position.x),(int32_t)floor(sample.position.y)},
                                           CONTACT_AT,2,sample.color};
        const NtResult result = nt_particle_deposits_add(weather->deposits,contact,NULL);
        if (result != NT_RESULT_OK) return result;
    }
    weather->landed = true;
    return NT_RESULT_OK;
}

static NtResult weather_update(void *context, NtUpdateInfo info, NtUpdateReply *reply)
{
    Weather *weather = context;
    if (!weather->landed && info.now >= CONTACT_AT) {
        const NtResult result = settle(weather);
        if (result != NT_RESULT_OK) return result;
    }
    NtResult result = nt_particle_deposits_advance(weather->deposits,info.now);
    if (result != NT_RESULT_OK) return result;
    weather->now = info.now;
    weather->motion = info.motion;
    ++weather->app->updates;
    reply->invalidate = NT_INVALIDATE_PAINT;
    result = nt_particle_deposits_deadline(weather->deposits,info.now,info.motion,10000,&reply->next);
    if (result != NT_RESULT_OK) return result;
    if (!weather->landed) consider(&reply->next,(NtDeadline){true,CONTACT_AT});
    /* The Unicode sweep, finite optical pulse and falling particles share
     * the same clock and become idle when their last animation settles. */
    if (info.motion == NT_MOTION_FULL && info.now < TEXT_DURATION)
        consider(&reply->next,(NtDeadline){true,info.now+10000});
    return NT_RESULT_OK;
}

static NtResult float_title(void *context, const NtTextEffectSample *sample, NtTextPose *pose)
{
    (void)context;
    pose->offset.y = 2*sample->travel;
    return NT_RESULT_OK;
}

static NtResult weather_paint(const void *context, NtPaintContext *paint, NtCellSize size)
{
    const Weather *weather = context;
    const NtField ripple = {.kind=NT_FIELD_RIPPLE,.as.ripple={.origin={18.5,3.5},.aspect={1,2},
        .last_radius=10,.width=1,.peak=.6,.lifetime_us=TEXT_DURATION,.rings=1,
        .falloff=NT_OPTICAL_GAUSSIAN,.fade=NT_EASE_IN}};
    const NtFieldInk ink = {.color={50,155,162,255},.opacity=1};
    NtResult result = nt_field_draw(&ripple,&ink,paint,(NtCellRect){{0,0},size},weather->now,weather->motion);
    if (result != NT_RESULT_OK) return result;
    const NtTextPaintOptions text = {.base={.color={.foreground={192,237,224,255}}},
        .elapsed_us=weather->now,.motion=weather->motion};
    const NtTextEffect effect = {.duration_us=TEXT_DURATION,.visibility=NT_TEXT_VISIBLE,.transform=float_title};
    result = nt_paint_text_effect(paint,(NtCellPoint){1,0},weather->title,&text,&effect);
    if (result != NT_RESULT_OK) return result;
    NtParticleDepositPaint particles = nt_particle_deposit_paint_default();
    if (!weather->landed) {
        particles.release.glyph = BYTES("*");
        return nt_particle_emitter_draw(weather->falling,paint,weather->now,weather->motion,&particles.release);
    }
    particles.pile_glyph = BYTES("#");
    particles.release.glyph = BYTES("+");
    return nt_particle_deposits_draw(weather->deposits,paint,weather->now,weather->motion,&particles);
}

static void weather_dispose(void *context)
{
    Weather *weather = context;
    nt_particle_emitter_destroy(weather->falling);
    nt_particle_deposits_destroy(weather->deposits);
    nt_text_layout_destroy(weather->title);
    ++weather->app->removals;
    weather->app->weather = NULL;
    free(weather);
}

static void mount_weather(App *app)
{
    Weather *weather = calloc(1,sizeof(*weather));
    CHECK(weather);
    weather->app = app;
    NtParticleEmitterOptions falling = nt_particle_emitter_options_default();
    falling.period_us = 0;
    falling.births_per_tick = 2;
    falling.lifetime_min_us = falling.lifetime_max_us = 200000;
    falling.origin = (NtParticleRange){{18.5,.5},{18.5,.5}};
    falling.velocity = (NtParticleRange){{0,25},{0,25}};
    falling.birth_color = falling.end_color = (NtRgba){236,211,151,255};
    OK(nt_particle_emitter_create(NULL,&falling,&weather->falling));
    NtParticleDepositsOptions deposits = nt_particle_deposits_options_default();
    deposits.capacity = 2;
    deposits.hold_us = 120000;
    deposits.release.count = 1;
    deposits.release.lifetime_min_us = deposits.release.lifetime_max_us = 120000;
    deposits.release.velocity = (NtParticleRange){{0,-20},{0,-20}};
    deposits.release.end_color = falling.birth_color;
    OK(nt_particle_deposits_create(NULL,&deposits,&weather->deposits));

    NtText *title;
    OK(nt_text_create(NULL,BYTES("Aube 界"),&title));
    const NtTextLayoutOptions layout = {.width=width,.tab_stop=4};
    OK(nt_text_layout_create(NULL,title,&layout,&weather->title));
    nt_text_destroy(title);
    const NtPanelContentProvider provider = {.context=weather,.preferred={23,4},.paint=weather_paint,
                                             .update=weather_update,.dispose=weather_dispose};
    OK(nt_panel_content_mount(app->tree,app->weather_window,passive,&provider,&app->weather_leaf));
    app->weather = weather;
}

static void mount_charts(App *app)
{
    NtNodeId column;
    const NtBoxItem items[] = {{.rows={8,8,true},.grow_columns=true},
                               {.rows={5,5,true},.grow_columns=true}};
    const NtBoxLayout layout = {.kind=NT_BOX_LAYOUT_COLUMN,.gap=1,
                                 .items=items,.item_count=sizeof(items)/sizeof(*items)};
    OK(nt_box_layout_mount(app->tree,app->charts,passive,&layout,&column));
    const NtGraphNode nodes[] = {{.id=1},{.id=2},{.id=3}};
    const NtGraphEdge edges[] = {{1,1,2,true},{2,2,3,true}};
    const NtGraphLabel labels[] = {{1,BYTES("Ridge")},{2,BYTES("Lake")},{3,BYTES("Cape")}};
    NtGraphViewOptions graph = nt_graph_view_options_default();
    graph.data = (NtGraphData){nodes,3,edges,2};
    graph.labels = labels;
    graph.label_count = 3;
    OK(nt_graph_view_mount(app->tree,column,interactive,&graph,&app->graph));
    const NtPlotDatum data[] = {{.point={0,1}},{.point={1,4}},{.point={2,2}},{.point={3,5}}};
    const NtPlotOptions plot = {.kind=NT_PLOT_LINE,.data=data,.count=4,.auto_x=true,.auto_y=true,
                              .sampling=NT_SAMPLE_BRAILLE,
                              .preferred={20,5},.foreground={132,225,211,255},.background={15,26,38,255}};
    OK(nt_plot_mount(app->tree,column,passive,&plot,&app->plot));
}

static void mount(App *app)
{
    OK(nt_tree_create(NULL,&app->tree));
    OK(nt_editor_create(NULL,(NtByteView){0},NULL,&app->editor));
    OK(nt_window_workspace_mount(app->tree,(NtNodeId){0},passive,(NtCellSize){72,18},&app->workspace));
    NtWindowOptions window = nt_window_options_default();
    window.placement.dock = NT_WINDOW_DOCK_RIGHT;
    window.placement.dock_extent = 27;
    window.panel.title.text = BYTES("Route and altitude");
    OK(nt_window_mount(app->tree,app->workspace,interactive,&window,&app->charts));
    mount_charts(app);
    window.placement.dock = NT_WINDOW_DOCK_BOTTOM;
    window.placement.dock_extent = 6;
    window.panel.title.text = BYTES("Airflow");
    OK(nt_window_mount(app->tree,app->workspace,passive,&window,&app->weather_window));
    mount_weather(app);
    window.placement.dock = NT_WINDOW_FLOAT;
    window.placement.floating = (NtCellRect){{2,2},{22,7}};
    window.panel.title.text = BYTES("Survey notes");
    OK(nt_window_mount(app->tree,app->workspace,interactive,&window,&app->notes));
    const NtEditorViewOptions view = {.editor=app->editor,.follow_caret=true};
    OK(nt_editor_view_mount(app->tree,app->notes,interactive,&view,&app->entry));
}

static NtResult focus_visible(App *app, NtNodeId window, NtNodeId target)
{
    NtNodeGeometry region;
    const NtResult result = nt_tree_geometry(app->tree,target,&region);
    if (result != NT_RESULT_OK) return result;
    if (!region.placed || !region.enabled || !region.clip.size.columns || !region.clip.size.rows)
        return NT_RESULT_OK;
    return window.tree ? nt_window_focus(app->tree,window,target) : nt_tree_set_focus(app->tree,target);
}

static NtResult restore_focus(void *context, NtCellSize size)
{
    (void)size;
    App *app = context;
    if (nt_tree_focus(app->tree).tree) return NT_RESULT_OK;
    return focus_visible(app,(NtNodeId){0},app->last_focus);
}

static NtResult reserved(void *context, const NtEvent *event, NtByteView text)
{
    (void)text;
    App *app = context;
    switch (event->data.key.key) {
    case 'r': return nt_window_animate(app->tree,app->notes,
                                      (NtWindowAnimation){{{11,2},{22,7}},event->at,80000,NT_LINEAR});
    case 'g': return focus_visible(app,app->charts,app->graph);
    case 'e': return focus_visible(app,app->notes,app->entry);
    case 'd': return app->weather ? nt_tree_remove(app->tree,app->weather_window) : NT_RESULT_OK;
    case 'q': app->quit = true; return NT_RESULT_OK;
    default: return NT_RESULT_OK;
    }
}

static NtResult selections(void *context, const NtDispatchResult *dispatch)
{
    (void)dispatch;
    App *app = context;
    NtEventIntentView intent;
    while (nt_tree_peek_event_intent(app->tree,&intent)) {
        if (nt_node_owner(intent.intent.source) == nt_node_owner(app->graph)) {
            NtGraphHit hit;
            OK(nt_graph_event_decode(intent,&hit));
            ++app->selections;
            app->last_selected = hit.id;
        }
        nt_tree_pop_intent(app->tree);
    }
    return NT_RESULT_OK;
}

typedef struct Published {
    unsigned graph, plot, falling, piles, releases;
    int32_t title_row;
} Published;

static Published published(const App *app)
{
    Published counts = {.title_row=-1};
    const NtFrame *frame = nt_tree_frame(app->tree);
    const NtCellSize size = nt_frame_size(frame);
    for (uint32_t y = 0; y < size.rows; ++y) {
        for (uint32_t x = 0; x < size.columns; ++x) {
            NtCellView cell;
            OK(nt_frame_cell(frame,(NtCellPoint){(int32_t)x,(int32_t)y},&cell));
            if (!cell.glyph.count) continue;
            if (cell.owner == nt_node_owner(app->graph)) ++counts.graph;
            if (cell.owner == nt_node_owner(app->plot)) ++counts.plot;
            if (!app->weather || cell.owner != nt_node_owner(app->weather_leaf)) continue;
            if (cell.glyph.count == sizeof("界")-1 && !memcmp(cell.glyph.data,"界",sizeof("界")-1))
                counts.title_row = (int32_t)y;
            if (cell.glyph.count != 1) continue;
            if (*cell.glyph.data == '*') ++counts.falling;
            if (*cell.glyph.data == '#') ++counts.piles;
            if (*cell.glyph.data == '+') ++counts.releases;
        }
    }
    return counts;
}

static void report(const App *app, NtPosixStep step)
{
    NtWindowState notes;
    OK(nt_window_get(app->tree,app->notes,&notes));
    const Published shown = published(app);
    int32_t title_row = shown.title_row;
    if (app->weather && title_row >= 0) {
        NtNodeGeometry region;
        OK(nt_tree_geometry(app->tree,app->weather_leaf,&region));
        title_row -= region.bounds.origin.y;
    }
    const NtOwnerToken focus = nt_node_owner(nt_tree_focus(app->tree));
    const NtByteView document = nt_text_bytes(nt_editor_text(app->editor));
    size_t stored = 0;
    NtParticlePile pile;
    if (app->weather) OK(nt_particle_deposits_piles(app->weather->deposits,app->weather->now,&pile,1,&stored));
    fprintf(stderr,"{\"result\":0,\"resized\":%d,\"left\":%d,\"columns\":%u,\"rows\":%u,\"focus\":%d,"
                   "\"graph\":%u,\"plot\":%u,\"falling\":%u,\"piles\":%u,\"releases\":%u,\"title_row\":%d,"
                   "\"mass\":%" PRIu64 ",\"selected\":%" PRIu64 ",\"selections\":%u,\"updates\":%u,\"removals\":%u,"
                   "\"next\":%d,\"document\":\"",
            step.resized,notes.published.bounds.origin.x,step.size.columns,step.size.rows,
            focus == nt_node_owner(app->entry) ? 1 : focus == nt_node_owner(app->graph) ? 2 : 0,
            shown.graph,shown.plot,shown.falling,shown.piles,shown.releases,title_row,
            stored ? pile.mass : (uint64_t)0,app->last_selected,app->selections,app->updates,app->removals,
            nt_tree_next_update(app->tree).scheduled);
    for (size_t i = 0; i < document.count; ++i) fprintf(stderr,"%02x",(unsigned char)document.data[i]);
    fputs("\"}\n",stderr);
}

static NtTimeUs clock_us(void)
{
    struct timespec now;
    CHECK(clock_gettime(CLOCK_MONOTONIC,&now) == 0);
    return (NtTimeUs)now.tv_sec*1000000+(NtTimeUs)now.tv_nsec/1000;
}

int main(int argc, char **argv)
{
    CHECK(argc <= 3);
    FILE *commands = argc > 1 ? fdopen(atoi(argv[1]),"r") : NULL;
    CHECK(argc == 1 || commands);
    App app = {0};
    mount(&app);
    app.last_focus = app.entry;
    const NtPosixHostOptions options = {
        .input_fd=STDIN_FILENO,.output_fd=STDOUT_FILENO,.matte={12,20,31,255},
        .capabilities={.color=NT_COLOR_TRUECOLOR,.interactive=true,.utf8=true,.pointer=true,
                       .bracketed_paste=true,.width={NT_WIDTH_ONE,NT_WIDTH_TWO}},
        .motion=argc == 3 ? NT_MOTION_REDUCED : NT_MOTION_FULL,.output_timeout_ms=250,
        .alternate_screen=true,.hide_cursor=true,.report_focus=true,.catch_signals=true,
        .reserved_key=reserved,.reserved_context=&app,.after_input=selections,.input_context=&app
    };
    const NtKeyChord chords[] = {{'r',NT_MOD_CONTROL},{'g',NT_MOD_CONTROL},{'e',NT_MOD_CONTROL},
                                  {'d',NT_MOD_CONTROL},{'q',NT_MOD_CONTROL}};
    const NtDispatchPolicy policy = {chords,sizeof(chords)/sizeof(*chords),true};
    NtPosixHost *host;
    OK(nt_posix_host_open(app.tree,0,NULL,&options,&host));
    OK(nt_posix_host_set_after_resize(host,restore_focus,&app));
    if (commands) fputs("READY\n",stderr);
    const NtTimeUs begun = clock_us();
    while (!app.quit) {
        NtTimeUs now;
        if (commands) {
            char command[80];
            if (!fgets(command,sizeof(command),commands) || command[0] == 'q') break;
            CHECK(sscanf(command,"s %" SCNu64,&now) == 1);
        } else now = clock_us()-begun;
        NtPosixStep step;
        OK(nt_posix_host_step(host,now,&policy,&step));
        if (step.signal_number) break;
        const NtNodeId focus = nt_tree_focus(app.tree);
        if (focus.tree) app.last_focus = focus;
        if (commands) report(&app,step);
        else if (nt_tree_pending(app.tree) == NT_INVALIDATE_NONE) {
            struct pollfd input = {STDIN_FILENO,POLLIN,0};
            CHECK(poll(&input,1,10) >= 0 || errno == EINTR);
        }
    }
    OK(nt_posix_host_close(host));
    nt_posix_host_destroy(host);
    nt_tree_destroy(app.tree);
    nt_editor_destroy(app.editor);
    CHECK(!app.weather && app.removals == 1);
    if (commands) {
        fclose(commands);
        fputs("DONE\n",stderr);
    }
    return EXIT_SUCCESS;
}
