#define _POSIX_C_SOURCE 200809L

#include <nocturne/controls.h>
#include <nocturne/panel_transform.h>
#include <nocturne/posix_host.h>
#include <nocturne/text_effect.h>

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr, "panel/editor line %d: %s\n", __LINE__, #test); exit(EXIT_FAILURE); \
} } while (0)
#define OK(expr) CHECK((expr) == NT_RESULT_OK)
#define RECT(x, y, w, h) ((NtCellRect){{(x), (y)}, {(w), (h)}})
#define BYTES(s) ((NtByteView){(s), sizeof(s) - 1})

static const char accent[] = "e\xcc\x81", astronaut[] = "👩‍🚀", wide[] = "界";
static const NtCellSize source_size = {6, 2};
static const NtSpringTuning tuning = {50000, 1e-4, 1e-3};

typedef struct {
    NtTree *tree;
    NtEditor *editor;
    NtNodeId panel, entry;
    NtTextLayout *title;
    NtFrame *source;
    NtSpring offset, width;
    NtTimeUs now, text_start;
    int32_t left;
    uint32_t columns;
    unsigned updates, interruptions, cancels, disposals;
    bool opening, reduced, running;
} App;

static NtResult measure(const void *state, NtMeasureContext *context, NtConstraints limits,
                        NtCellSize *out)
{
    (void)state;
    (void)context;
    *out = (NtCellSize){limits.columns.min, limits.rows.min};
    return NT_RESULT_OK;
}

static NtResult measure_root(const void *state, NtMeasureContext *context, NtConstraints limits,
                             NtCellSize *out)
{
    const App *app = state;
    const uint32_t title_rows = limits.rows.max < 2 ? limits.rows.max : 2;
    NtCellSize child;
    if (!app->disposals)
        OK(nt_measure_child(context, app->panel,
                            (NtConstraints){limits.columns, {title_rows, title_rows, true}}, &child));
    const NtConstraints entry = {limits.columns, {0, limits.rows.max - title_rows, true}};
    OK(nt_measure_child(context, app->entry, entry, &child));
    *out = (NtCellSize){limits.columns.max, limits.rows.max};
    return NT_RESULT_OK;
}

static NtResult layout(const void *state, NtLayoutContext *context, NtCellSize size)
{
    const App *app = state;
    uint32_t title_rows = size.rows < 2 ? size.rows : 2;
    if (!app->disposals)
        OK(nt_layout_place(context, app->panel, RECT(0, 0, size.columns, title_rows), 0, NULL));
    return nt_layout_place(context, app->entry,
                           RECT(0, (int32_t)title_rows, size.columns, size.rows - title_rows), 0, NULL);
}

static NtResult empty_paint(const void *state, NtPaintContext *context, NtCellSize size)
{
    (void)state;
    (void)context;
    (void)size;
    return NT_RESULT_OK;
}

static NtResult leaf_layout(const void *state, NtLayoutContext *context, NtCellSize size)
{
    (void)state;
    (void)context;
    (void)size;
    return NT_RESULT_OK;
}

static NtResult animate_title(void *context, const NtTextEffectSample *sample, NtTextPose *pose)
{
    (void)context;
    if (pose->glyph.count == 1 && pose->glyph.data[0] == 'Z')
        pose->offset.y = sample->travel > .5 ? 1 : 0;
    return NT_RESULT_OK;
}

static NtResult update(void *state, NtUpdateInfo info, NtUpdateReply *reply)
{
    App *app = state;
    CHECK(info.motion == (app->reduced ? NT_MOTION_REDUCED : NT_MOTION_FULL));
    CHECK(info.now >= app->text_start);
    const NtTimeUs elapsed = info.now - app->text_start;
    NtSpringSample offset, width;
    CHECK(nt_spring_sample(&app->offset, info.now, app->reduced, &offset) == NT_OK);
    CHECK(nt_spring_sample(&app->width, info.now, app->reduced, &width) == NT_OK);
    const int32_t left = (int32_t)lround(offset.value);
    const uint32_t columns = (uint32_t)lround(fmin(source_size.columns, fmax(0, width.value)));
    for (int32_t y = 0; y < (int32_t)source_size.rows; ++y)
        for (int32_t x = 0; x < (int32_t)source_size.columns; ++x)
            OK(nt_frame_clear_glyph(app->source, (NtCellPoint){x, y}));
    NtSurfaceView view;
    OK(nt_surface_root(app->source, &view));
    const NtTextPaintOptions options = {
        .base = {.color = {{240, 230, 210, 255}, {25, 90, 90, 255}}},
        .elapsed_us = elapsed, .motion = info.motion
    };
    const NtTextEffect effect = {
        .duration_us = 100000, .visibility = NT_TEXT_VISIBLE, .transform = animate_title
    };
    OK(nt_text_effect_paint(&view, (NtCellPoint){0, 0}, app->title, &options, &effect));
    app->left = left;
    app->columns = columns;
    app->running = offset.phase == NT_RUNNING || width.phase == NT_RUNNING;
    ++app->updates;
    reply->invalidate = NT_INVALIDATE_PAINT;
    if (!app->reduced && (app->running || elapsed < effect.duration_us))
        reply->next = (NtDeadline){true, info.now + 1000};
    return NT_RESULT_OK;
}

static NtResult paint(const void *state, NtPaintContext *context, NtCellSize size)
{
    const App *app = state;
    const NtPanelLayer layer = {
        .source = app->source, .region = RECT(0, 0, source_size.columns, source_size.rows),
        .viewport = RECT(app->left, 0, app->columns, size.rows),
        .pose = {.scale = {1, 1}, .opacity = 1}
    };
    return nt_paint_project_panels(context, &layer, 1);
}

static NtResult panel_event(void *state, NtEventContext *context, const NtEvent *event,
                            NtEventReply *reply)
{
    App *app = state;
    if (event->kind == NT_EVENT_POINTER_DOWN && nt_event_phase(context) == NT_EVENT_TARGET)
        reply->capture = NT_CAPTURE_TAKE;
    if (event->kind == NT_EVENT_POINTER_CANCEL) {
        CHECK(event->data.pointer_cancel.reason == NT_CANCEL_REMOVED);
        CHECK(!nt_tree_capture(app->tree, event->data.pointer_cancel.id).tree);
        ++app->cancels;
    }
    return NT_RESULT_OK;
}

static void dispose(void *state)
{
    App *app = state;
    CHECK(!app->disposals && app->source && app->title);
    nt_frame_destroy(app->source);
    nt_text_layout_destroy(app->title);
    app->source = NULL;
    app->title = NULL;
    ++app->disposals;
}

static void build(App *app)
{
    const NtComponentOps root_ops = {.measure = measure_root, .layout = layout, .paint = empty_paint};
    const NtComponentOps panel_ops = {
        .measure = measure, .layout = leaf_layout, .update = update, .paint = paint,
        .event = panel_event, .dispose = dispose
    };
    const NtMountOptions root = {.enabled = true};
    const NtMountOptions title = {.enabled = true, .hit_testable = true};
    const NtMountOptions entry = {.enabled = true, .focusable = true, .hit_testable = true};
    const NtTextLayoutOptions layout_options = {
        .width = {NT_WIDTH_ONE, NT_WIDTH_TWO}, .bounds = {{0, 6, true}, {0, 1, true}}, .tab_stop = 4
    };
    NtNodeId root_node;
    CHECK(nt_spring_init(&app->offset, tuning, 0, 0, 4, 0) == NT_OK);
    CHECK(nt_spring_init(&app->width, tuning, 6, 0, 2, 0) == NT_OK);
    OK(nt_frame_create(NULL, source_size, &app->source));
    OK(nt_text_layout_create_borrowed(NULL, BYTES("e\xcc\x81👩‍🚀界Z"), &layout_options, &app->title));
    OK(nt_tree_create(NULL, &app->tree));
    OK(nt_tree_mount(app->tree, (NtNodeId){0}, &root_ops, app, root, &root_node));
    OK(nt_tree_mount(app->tree, root_node, &panel_ops, app, title, &app->panel));
    OK(nt_editor_create(NULL, (NtByteView){0}, NULL, &app->editor));
    OK(nt_editor_set_undo_limit(app->editor, 8));
    const NtEditorBinding keys[] = {
        {{NT_KEY_BACKSPACE, 0}, NT_EDITOR_ACTION_DELETE_PREVIOUS, 0, 0, false, true},
        {{'z', NT_MOD_CONTROL}, NT_EDITOR_ACTION_UNDO, 0, 0, false, false}
    };
    const NtEditorKeyMap keymap = {keys, sizeof(keys) / sizeof(*keys), NULL};
    const NtEditorViewOptions options = {.editor = app->editor, .keys = &keymap, .follow_caret = true};
    OK(nt_editor_view_mount(app->tree, root_node, entry, &options, &app->entry));
}

static void interrupt(NtSpring *spring, NtTimeUs now, double target)
{
    NtSpringSample from, to;
    CHECK(nt_spring_sample(spring, now, false, &from) == NT_OK);
    CHECK(nt_spring_retarget(spring, now, target, NT_RETARGET_KEEP_VELOCITY) == NT_OK);
    CHECK(nt_spring_sample(spring, now, false, &to) == NT_OK);
    CHECK(from.value == to.value && from.velocity == to.velocity);
}

static NtResult reserved(void *state, const NtEvent *event, NtByteView text)
{
    (void)text;
    App *app = state;
    if (event->data.key.key == 'r') {
        CHECK(!app->disposals && !app->reduced);
        app->opening = !app->opening;
        interrupt(&app->offset, app->now, app->opening ? 0 : 4);
        interrupt(&app->width, app->now, app->opening ? 6 : 2);
        app->text_start = app->now;
        ++app->interruptions;
    } else {
        CHECK(event->data.key.key == 'd' && app->running && !app->disposals);
        OK(nt_tree_remove(app->tree, app->panel));
        CHECK(nt_tree_invalidate(app->tree, app->panel, NT_INVALIDATE_PAINT) == NT_RESULT_STALE_REFERENCE);
    }
    return NT_RESULT_OK;
}

static bool same_glyph(NtCellView cell, const char *glyph)
{
    size_t count = strlen(glyph);
    return cell.glyph.count == count && memcmp(cell.glyph.data, glyph, count) == 0;
}

static void report(const App *app, NtPosixStep step)
{
    const NtFrame *frame = nt_tree_frame(app->tree);
    const NtCellSize size = nt_frame_size(frame);
    const NtOwnerToken owner = nt_node_owner(app->panel);
    int32_t a = -1, astronaut_column = -1, wide_column = -1, z_row = -1;
    for (int32_t y = 0; y < (int32_t)size.rows; ++y) {
        for (int32_t x = 0; x < (int32_t)size.columns; ++x) {
            NtCellView cell;
            NtCellBackground background;
            OK(nt_frame_cell(frame, (NtCellPoint){x, y}, &cell));
            OK(nt_frame_background(frame, (NtCellPoint){x, y}, &background));
            if (background.owner == owner)
                CHECK(!app->disposals && y < 2 && x >= app->left &&
                      x < app->left + (int32_t)app->columns);
            if (cell.owner != owner || !cell.glyph.count) continue;
            CHECK(y < 2 && !app->disposals);
            CHECK(cell.lead.x >= app->left && cell.lead.y == y);
            CHECK(cell.lead.x + cell.glyph_columns <= (int32_t)size.columns);
            CHECK(cell.lead.x + cell.glyph_columns <= app->left + (int32_t)app->columns);
            if (cell.lead.x != x) continue;
            if (cell.glyph_columns == 2) {
                NtCellView trailing;
                OK(nt_frame_cell(frame, (NtCellPoint){x + 1, y}, &trailing));
                CHECK(trailing.lead.x == x && trailing.owner == owner);
            }
            if (same_glyph(cell, accent)) a = x;
            else if (same_glyph(cell, astronaut)) astronaut_column = x;
            else if (same_glyph(cell, wide)) wide_column = x;
            else if (same_glyph(cell, "Z")) z_row = y;
            else CHECK(false);
        }
    }
    CHECK(nt_node_owner(nt_tree_focus(app->tree)) == nt_node_owner(app->entry));
    NtByteView text = nt_text_bytes(nt_editor_text(app->editor));
    fprintf(stderr, "{\"result\":0,\"events\":%zu,\"resized\":%d,\"presented\":%d,"
            "\"columns\":%u,\"rows\":%u,\"left\":%d,\"width\":%u,\"accent\":%d,"
            "\"astronaut\":%d,\"wide\":%d,\"z_row\":%d,\"updates\":%u,\"interruptions\":%u,"
            "\"cancels\":%u,\"disposals\":%u,\"capture\":%d,\"running\":%d,\"document\":\"",
            step.input_events, step.resized, step.presented, size.columns, size.rows, app->left,
            app->columns, a, astronaut_column, wide_column, z_row, app->updates, app->interruptions,
            app->cancels, app->disposals, nt_tree_capture(app->tree, 0).tree != NULL, app->running);
    for (size_t i = 0; i < text.count; ++i) fprintf(stderr, "%02x", (unsigned char)text.data[i]);
    fputs("\"}\n", stderr);
}

int main(int argc, char **argv)
{
    CHECK(argc == 2 || (argc == 3 && strcmp(argv[2], "reduced") == 0));
    FILE *commands = fdopen(atoi(argv[1]), "r");
    CHECK(commands);
    App app = {.reduced = argc == 3};
    build(&app);
    const NtPosixHostOptions options = {
        .input_fd = STDIN_FILENO, .output_fd = STDOUT_FILENO,
        .capabilities = {.color = NT_COLOR_TRUECOLOR, .interactive = true, .utf8 = true,
                         .pointer = true, .bracketed_paste = true, .width = {NT_WIDTH_ONE, NT_WIDTH_TWO}},
        .motion = app.reduced ? NT_MOTION_REDUCED : NT_MOTION_FULL, .output_timeout_ms = 100,
        .alternate_screen = true, .hide_cursor = true, .report_focus = true, .catch_signals = true,
        .reserved_key = reserved, .reserved_context = &app
    };
    const NtKeyChord chords[] = {{'r', NT_MOD_CONTROL}, {'d', NT_MOD_CONTROL}};
    const NtDispatchPolicy policy = {chords, sizeof(chords) / sizeof(*chords), false};
    NtPosixHost *host;
    OK(nt_posix_host_open(app.tree, 0, NULL, &options, &host));
    fputs("READY\n", stderr);
    char command[80];
    bool focused = false;
    while (fgets(command, sizeof(command), commands) && command[0] != 'q') {
        CHECK(sscanf(command, "s %" SCNu64, &app.now) == 1);
        NtPosixStep step;
        OK(nt_posix_host_step(host, app.now, &policy, &step));
        if (!focused) {
            OK(nt_tree_set_focus(app.tree, app.entry));
            focused = true;
        }
        report(&app, step);
    }
    OK(nt_posix_host_close(host));
    nt_posix_host_destroy(host);
    nt_tree_destroy(app.tree);
    CHECK(app.disposals == 1 && !app.source && !app.title);
    nt_editor_destroy(app.editor);
    fclose(commands);
    fputs("DONE\n", stderr);
    return EXIT_SUCCESS;
}
