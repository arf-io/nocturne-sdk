#include <nocturne/controls.h>
#include <nocturne/event_rules.h>
#include <nocturne/posix_host.h>
#include <nocturne/spatial_mesh.h>
#include <nocturne/spatial_surface.h>

#include <errno.h>
#include <inttypes.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum { ORBIT_LEFT = 1, ORBIT_RIGHT, MOVE_MODEL, SELECT_FACE, ORBIT_STEPS = 16 };
enum { TITLE, NOTES_TITLE, MODEL_TITLE, NOTES, MODEL, STATUS, HELP, SLOT_COUNT };

typedef struct ViewFrame {
    NtSpatialRaster *raster;
    NtCellPoint origin;
} ViewFrame;

typedef struct Bench {
    NtTree *tree;
    NtEditor *editor;
    NtSpatialMesh *mesh;
    NtNodeId slots[SLOT_COUNT], desired_focus, last_focus;
    NtControlStyle style;
    NtSamplingMode sampling;
    ViewFrame current, next;
    unsigned orbit;
    uint64_t object, face;
    bool raised, selected, quit, ascii_mesh;
} Bench;

static NtByteView bytes(const char *text)
{
    return (NtByteView){text,strlen(text)};
}

static bool same_node(NtNodeId a, NtNodeId b)
{
    return a.tree == b.tree && a.slot == b.slot && a.generation == b.generation;
}

static NtLabelOptions label(const Bench *app, const char *text, NtCellStyle color)
{
    return (NtLabelOptions){.text=bytes(text),.style={.width=app->style.width,.tab_stop=4,
        .direction=NT_DIRECTION_LTR,.ellipsis=true,.color=color}};
}

static NtResult layout_measure(const void *state, NtMeasureContext *context,
                                NtConstraints constraints, NtCellSize *out)
{
    const Bench *app = state;
    for (unsigned i = 0; i < SLOT_COUNT; ++i) {
        NtCellSize size;
        const NtResult status = nt_measure_child(context,app->slots[i],(NtConstraints){0},&size);
        if (status != NT_RESULT_OK) return status;
    }
    *out = (NtCellSize){constraints.columns.min,constraints.rows.min};
    return NT_RESULT_OK;
}

static NtResult layout_place(const void *state, NtLayoutContext *context, NtCellSize size)
{
    const Bench *app = state;
    if (size.columns < 24 || size.rows < 6) return NT_RESULT_OK;
    const uint32_t left = size.columns/2, right = size.columns-left;
    const NtCellRect areas[SLOT_COUNT] = {
        {{1,0},{size.columns-2,1}},
        {{1,1},{left-2,1}},
        {{(int32_t)left+1,1},{right-2,1}},
        {{1,2},{left-2,size.rows-4}},
        {{(int32_t)left+1,2},{right-2,size.rows-4}},
        {{1,(int32_t)size.rows-2},{size.columns-2,1}},
        {{1,(int32_t)size.rows-1},{size.columns-2,1}}
    };
    for (unsigned i = 0; i < SLOT_COUNT; ++i) {
        const NtResult status = nt_layout_place(context,app->slots[i],areas[i],0,NULL);
        if (status != NT_RESULT_OK) return status;
    }
    return NT_RESULT_OK;
}

static NtResult paint_ascii_mesh(ViewFrame frame, NtPaintContext *paint, NtCellStyle style)
{
    const NtCellSize size = nt_spatial_raster_cell_size(frame.raster);
    for (uint32_t y = 0; y < size.rows; ++y) {
        for (uint32_t x = 0; x < size.columns; ++x) {
            const NtCellPoint cell = {(int32_t)x, (int32_t)y};
            bool covered;
            NtSpatialCellHit hit;
            NtResult result = nt_spatial_raster_cell_hit(frame.raster, cell, &covered, &hit);
            if (result != NT_RESULT_OK) return result;
            if (!covered) continue;
            const NtCellPoint at = {frame.origin.x + cell.x, frame.origin.y + cell.y};
            result = nt_paint_glyph(paint, at, (NtByteView){"#", 1}, 1, style);
            if (result != NT_RESULT_OK) return result;
        }
    }
    return NT_RESULT_OK;
}

static NtResult view_paint(const void *state, NtPaintContext *paint, NtCellSize assigned)
{
    (void)assigned;
    const Bench *app = state;
    const ViewFrame frame = app->next.raster ? app->next : app->current;
    if (!frame.raster) return NT_RESULT_OK;
    if (app->ascii_mesh) return paint_ascii_mesh(frame, paint, app->style.normal);
    return nt_spatial_raster_paint(frame.raster, paint, frame.origin, app->sampling);
}

static NtResult view_measure(const void *state, NtMeasureContext *context,
                              NtConstraints constraints, NtCellSize *out)
{
    (void)state;
    if (nt_measure_child_count(context)) return NT_RESULT_INVALID_ARGUMENT;
    *out = (NtCellSize){constraints.columns.min,constraints.rows.min};
    return NT_RESULT_OK;
}

static NtResult leaf_layout(const void *state, NtLayoutContext *context, NtCellSize assigned)
{
    (void)state;
    (void)context;
    (void)assigned;
    return NT_RESULT_OK;
}

static NtResult layout_paint(const void *state, NtPaintContext *paint, NtCellSize assigned)
{
    (void)state;
    (void)paint;
    (void)assigned;
    return NT_RESULT_OK;
}

static NtResult select_cell(const Bench *app, NtEventContext *context)
{
    NtCellPoint point;
    if (!app->current.raster || !nt_event_local_position(context,&point)) return NT_RESULT_OK;
    const NtCellSize size = nt_spatial_raster_cell_size(app->current.raster);
    const int64_t x = (int64_t)point.x-app->current.origin.x, y = (int64_t)point.y-app->current.origin.y;
    bool found = false;
    NtSpatialCellHit hit;
    if (x >= 0 && y >= 0 && x < size.columns && y < size.rows) {
        const NtResult status = nt_spatial_raster_cell_hit(app->current.raster,(NtCellPoint){(int32_t)x,(int32_t)y},
                                                           &found,&hit);
        if (status != NT_RESULT_OK) return status;
    }
    const NtIntentField fields[] = {
        {1,{.kind=NT_VALUE_BOOL,.as.flag=found}},
        {2,{.kind=NT_VALUE_U64,.as.u64=found ? hit.sample.object_id : 0}},
        {3,{.kind=NT_VALUE_U64,.as.u64=found ? hit.sample.primitive_id : 0}}
    };
    return nt_event_emit_fields(context,SELECT_FACE,fields,sizeof(fields)/sizeof(fields[0]));
}

static NtResult view_event(void *state, NtEventContext *context, const NtEvent *event, NtEventReply *reply)
{
    const Bench *app = state;
    if (nt_event_phase(context) != NT_EVENT_TARGET) return NT_RESULT_OK;
    if (event->kind == NT_EVENT_POINTER_DOWN && event->data.pointer.button == NT_BUTTON_PRIMARY) {
        reply->consume = reply->focus = true;
        return select_cell(app,context);
    }
    if (event->kind != NT_EVENT_KEY || event->data.key.state == NT_KEY_RELEASE ||
        event->data.key.modifiers) return NT_RESULT_OK;
    const uint32_t key = event->data.key.key;
    const uint32_t action = key == NT_KEY_LEFT ? ORBIT_LEFT : key == NT_KEY_RIGHT ? ORBIT_RIGHT :
                            key == NT_KEY_ENTER ? MOVE_MODEL : 0;
    if (!action) return NT_RESULT_OK;
    reply->consume = true;
    return nt_event_emit(context,action,(NtByteView){0});
}

static NtResult make_mesh(NtSpatialMesh **out)
{
    const NtSpatialVertex vertices[] = {
        {{-1.2,-.65,0},{0,1}}, {{1.2,-.65,0},{1,1}},
        {{1.05,.65,-.2},{1,0}}, {{-.85,.7,-.15},{0,0}},
        {{.15,.10,.7},{.5,.5}}
    };
    const NtSpatialFace faces[] = {{{0,1,4},10},{{1,2,4},11},{{2,3,4},12},{{3,0,4},13}};
    const NtSpatialMeshData mesh = {vertices,5,faces,4};
    return nt_spatial_mesh_create(NULL,&mesh,out);
}

static NtResult set_status(Bench *app, NtNodeId focus)
{
    char picked[48] = "Click a face";
    if (app->selected)
        (void)snprintf(picked,sizeof(picked),"Object %" PRIu64 " / face %" PRIu64,app->object,app->face);
    char line[120];
    const char *name = same_node(focus,app->slots[MODEL]) ? "View" : "Editor";
    (void)snprintf(line,sizeof(line),"%s  |  Orbit %u  |  %s  |  %s",name,app->orbit,
                   app->raised ? "Raised" : "Level",picked);
    const NtLabelOptions options = label(app,line,app->style.focused);
    return nt_label_set(app->tree,app->slots[STATUS],&options);
}

static NtResult build(Bench *app)
{
    static const NtComponentOps layout_ops = {.measure=layout_measure,.layout=layout_place,.paint=layout_paint};
    static const NtComponentOps view_ops = {
        .measure=view_measure,.layout=leaf_layout,.paint=view_paint,.event=view_event
    };
    const NtMountOptions passive = {true,false,false}, interactive = {true,true,true};
    const NtEditorOptions options = {NT_EDITOR_MULTILINE,false};
    const NtEditorBinding bindings[] = {
        {{NT_KEY_LEFT,0},NT_EDITOR_ACTION_MOVE,NT_EDITOR_PREVIOUS_CLUSTER,0,false,true},
        {{NT_KEY_RIGHT,0},NT_EDITOR_ACTION_MOVE,NT_EDITOR_NEXT_CLUSTER,0,false,true},
        {{NT_KEY_BACKSPACE,0},NT_EDITOR_ACTION_DELETE_PREVIOUS,0,0,false,true}
    };
    const NtEditorKeyMap keys = {bindings,sizeof(bindings)/sizeof(bindings[0]),NULL};
    NtResult status = nt_tree_create(NULL,&app->tree);
    if (status == NT_RESULT_OK) status = make_mesh(&app->mesh);
    if (status == NT_RESULT_OK) status = nt_editor_create(NULL,bytes("Material: clay\n"),&options,&app->editor);
    if (status == NT_RESULT_OK) status = nt_editor_move(app->editor,NT_EDITOR_DOCUMENT_END,false);
    NtNodeId layout;
    if (status == NT_RESULT_OK)
        status = nt_tree_mount(app->tree,(NtNodeId){0},&layout_ops,app,passive,&layout);
    const char *titles[SLOT_COUNT] = {
        [TITLE]="Model bench",[NOTES_TITLE]="Notes",[MODEL_TITLE]="Mesh view",
        [STATUS]="Editor  |  Orbit 0  |  Level  |  Click a face",
        [HELP]="Tab switches panes  |  Arrows orbit  |  Enter lifts  |  Ctrl+Q quits"
    };
    for (unsigned i = 0; i < SLOT_COUNT && status == NT_RESULT_OK; ++i) {
        if (i == NOTES) {
            const NtEditorViewOptions view = {
                .editor=app->editor,.keys=&keys,.style=&app->style,.wrap=NT_WRAP_WORD,
                .follow_caret=true,.show_caret=true
            };
            status = nt_editor_view_mount(app->tree,layout,interactive,&view,&app->slots[i]);
        } else if (i == MODEL) {
            status = nt_tree_mount(app->tree,layout,&view_ops,app,interactive,&app->slots[i]);
        } else {
            const NtCellStyle color = i == HELP ? app->style.normal : app->style.focused;
            const NtLabelOptions text = label(app,titles[i],color);
            status = nt_label_mount(app->tree,layout,passive,&text,&app->slots[i]);
        }
    }
    app->desired_focus = app->slots[NOTES];
    return status;
}

static NtResult stage_scene(Bench *app)
{
    NtNodeGeometry view;
    NtResult status = nt_tree_geometry(app->tree,app->slots[MODEL],&view);
    if (status != NT_RESULT_OK) return status;
    const NtCellSize area = view.placed ? view.bounds.size : (NtCellSize){0};
    const uint32_t columns = area.columns > 64 ? 64 : area.columns;
    const uint32_t rows = area.rows > 24 ? 24 : area.rows;
    const NtCellPoint origin = {(int32_t)((area.columns-columns)/2),(int32_t)((area.rows-rows)/2)};
    NtSpatialCamera camera;
    const double azimuth = app->orbit*(0x1.921fb54442d18p2/ORBIT_STEPS);
    status = nt_spatial_camera_orbit((NtVec3){0,0,0},4,azimuth,.18,&camera);
    const NtSpatialSurfaceMotion lift = {.linear_velocity={0,.53,0}};
    NtSpatialTransform transform;
    if (status == NT_RESULT_OK) status = nt_spatial_surface_pose(&lift,app->raised ? 1 : 0,&transform);
    static const NtRgba colors[] = {{244,153,108,255},{133,204,237,255},
                                    {157,182,228,255},{224,131,116,255}};
    const NtImageView texture = {colors,{2,2},2,4};
    const NtSpatialMeshInstance model = {
        .mesh=app->mesh,.transform=&transform,.object_id=401,.tint={255,255,255,255},
        .texture=&texture,.sampling={NT_IMAGE_BILINEAR,NT_PAINT_PAD,NT_PAINT_PAD}
    };
    const double aspect = columns && rows ? (double)columns/(2*rows) : 1;
    const NtSpatialRasterOptions opts = {
        .projection={NT_SPATIAL_PERSPECTIVE,.35*aspect,.35,.1,16},.camera=&camera,
        .samples={2*columns,4*rows},.background={21,27,41,255},.sample_limit=64*24*8,.triangle_limit=4
    };
    NtSpatialRaster *raster = NULL;
    if (status == NT_RESULT_OK) status = nt_spatial_rasterize(NULL,&opts,&model,1,&raster);
    if (status == NT_RESULT_OK) status = nt_tree_invalidate(app->tree,app->slots[MODEL],NT_INVALIDATE_PAINT);
    if (status == NT_RESULT_OK) {
        nt_spatial_raster_destroy(app->next.raster);
        app->next = (ViewFrame){raster,origin};
    } else nt_spatial_raster_destroy(raster);
    return status;
}

static void commit_view(Bench *app)
{
    if (app->next.raster) {
        nt_spatial_raster_destroy(app->current.raster);
        app->current = app->next;
        app->next = (ViewFrame){0};
    }
}

static NtResult update_status(Bench *app, bool changed)
{
    const NtNodeId focus = nt_tree_focus(app->tree);
    if (focus.tree) app->desired_focus = focus;
    if (!changed && same_node(app->last_focus,focus)) return NT_RESULT_OK;
    const NtResult status = set_status(app,focus);
    if (status == NT_RESULT_OK) app->last_focus = focus;
    return status;
}

static NtResult after_resize(void *context, NtCellSize size)
{
    Bench *app = context;
    NtResult status = stage_scene(app);
    if (status == NT_RESULT_OK && !nt_tree_focus(app->tree).tree) {
        NtNodeGeometry geometry;
        status = nt_tree_geometry(app->tree,app->desired_focus,&geometry);
        if (status == NT_RESULT_OK && geometry.enabled && geometry.clip.size.columns && geometry.clip.size.rows)
            status = nt_tree_set_focus(app->tree,app->desired_focus);
    }
    if (status == NT_RESULT_OK) status = update_status(app,true);
    /* Commit raster and focus before this host step reads its input batch. */
    if (status == NT_RESULT_OK) status = nt_tree_publish(app->tree,size);
    if (status == NT_RESULT_OK) commit_view(app);
    return status;
}

static NtResult after_input(void *context, const NtDispatchResult *dispatch)
{
    (void)dispatch;
    Bench *app = context;
    bool redraw = false, changed = false;
    NtEventIntentView intent;
    while (nt_tree_peek_event_intent(app->tree,&intent)) {
        if (same_node(intent.intent.source,app->slots[MODEL])) {
            switch (intent.intent.kind) {
            case ORBIT_RIGHT: app->orbit = (app->orbit+1)%ORBIT_STEPS; redraw = true; break;
            case ORBIT_LEFT: app->orbit = (app->orbit+ORBIT_STEPS-1)%ORBIT_STEPS; redraw = true; break;
            case MOVE_MODEL: app->raised = !app->raised; redraw = true; break;
            case SELECT_FACE:
                if (intent.payload_kind == NT_INTENT_FIELDS && intent.field_count == 3) {
                    app->selected = intent.fields[0].value.as.flag;
                    app->object = intent.fields[1].value.as.u64;
                    app->face = intent.fields[2].value.as.u64;
                    changed = true;
                }
                break;
            default: break;
            }
        }
        nt_tree_pop_intent(app->tree);
    }
    const NtResult status = redraw ? stage_scene(app) : NT_RESULT_OK;
    return status == NT_RESULT_OK ? update_status(app,changed || redraw) : status;
}

static NtResult quit_key(void *context, const NtEvent *key, NtByteView text)
{
    (void)text;
    Bench *app = context;
    if (key->data.key.state == NT_KEY_PRESS) app->quit = true;
    return NT_RESULT_OK;
}

static NtResult current_time(NtTimeUs *out)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC,&now)) return NT_RESULT_TRANSPORT_FAILED;
    *out = (NtTimeUs)now.tv_sec*1000000+(NtTimeUs)now.tv_nsec/1000;
    return NT_RESULT_OK;
}

static NtResult wait_input(NtPosixHost *host)
{
    NtTimeUs now, deadline;
    NtResult status = current_time(&now);
    if (status != NT_RESULT_OK) return status;
    NtTimeUs wait = 40000;
    if (nt_posix_host_input_deadline(host,&deadline)) {
        if (now >= deadline) return NT_RESULT_OK;
        if (deadline-now < wait) wait = deadline-now;
    }
    struct pollfd input = {STDIN_FILENO,POLLIN,0};
    return poll(&input,1,(int)((wait+999)/1000)) < 0 && errno != EINTR ?
        NT_RESULT_TRANSPORT_FAILED : NT_RESULT_OK;
}

static NtResult run(Bench *app, NtTerminalCapabilities capabilities, int *signal)
{
    NtRgba indexed[NT_XTERM256_COLORS];
    NtPaletteView palette = {0};
    if (capabilities.color == NT_COLOR_INDEXED) {
        const NtResult status = nt_xterm256_palette(indexed,NT_XTERM256_COLORS);
        if (status != NT_RESULT_OK) return status;
        palette = (NtPaletteView){indexed,NT_XTERM256_COLORS};
    }
    const NtPosixHostOptions options = {
        .input_fd=STDIN_FILENO,.output_fd=STDOUT_FILENO,.capabilities=capabilities,.palette=palette,
        .matte={13,17,28,255},.output_timeout_ms=250,.alternate_screen=true,.hide_cursor=true,.catch_signals=true,
        .reserved_key=quit_key,.reserved_context=app,.after_input=after_input,.input_context=app
    };
    const NtKeyChord quit = {'q',NT_MOD_CONTROL};
    const NtDispatchPolicy policy = {&quit,1,true};
    NtTimeUs now;
    NtPosixHost *host = NULL;
    NtResult status = current_time(&now);
    if (status == NT_RESULT_OK) status = nt_posix_host_open(app->tree,now,NULL,&options,&host);
    if (status == NT_RESULT_OK) status = nt_posix_host_set_after_resize(host,after_resize,app);
    while (status == NT_RESULT_OK && !app->quit && !*signal) {
        status = current_time(&now);
        if (status != NT_RESULT_OK) break;
        NtPosixStep step = {0};
        status = nt_posix_host_step(host,now,&policy,&step);
        *signal = step.signal_number;
        if (status != NT_RESULT_OK || app->quit || *signal) break;
        if (step.presented) commit_view(app);
        status = wait_input(host);
    }
    if (host) {
        const NtResult cleanup = nt_posix_host_close(host);
        nt_posix_host_destroy(host);
        if (status == NT_RESULT_OK) status = cleanup;
    }
    return status;
}

int main(void)
{
    NtPosixTerminalReport terminal;
    NtResult status = nt_posix_terminal_probe(STDIN_FILENO,STDOUT_FILENO,NULL,&terminal);
    if (status != NT_RESULT_OK || !terminal.host_available) {
        fprintf(stderr,"model_bench requires a terminal on stdin and stdout\n");
        return EXIT_FAILURE;
    }
    const NtSamplingMode sampling = terminal.capabilities.braille ? NT_SAMPLE_BRAILLE :
                                     terminal.capabilities.half_blocks ? NT_SAMPLE_HALF_BLOCK : NT_SAMPLE_CELL;
    Bench app = {.style=nt_control_style_default(),.sampling=sampling,
                 .ascii_mesh=sampling == NT_SAMPLE_CELL && terminal.capabilities.color == NT_COLOR_MONO};
    app.style.width = terminal.capabilities.width;
    status = build(&app);
    int signal = 0;
    if (status == NT_RESULT_OK) status = run(&app,terminal.capabilities,&signal);
    nt_tree_destroy(app.tree);
    nt_editor_destroy(app.editor);
    nt_spatial_mesh_destroy(app.mesh);
    nt_spatial_raster_destroy(app.current.raster);
    nt_spatial_raster_destroy(app.next.raster);
    if (status != NT_RESULT_OK) {
        fprintf(stderr,"model_bench stopped with Nocturne status %d\n",(int)status);
        return EXIT_FAILURE;
    }
    return signal ? 128+signal : EXIT_SUCCESS;
}
