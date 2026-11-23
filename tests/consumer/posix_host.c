#define _XOPEN_SOURCE 700

#include <nocturne/posix_host.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "PTY consumer line %d: %s\n", __LINE__, #condition); \
    exit(EXIT_FAILURE); \
} } while (0)

typedef struct App {
    NtTree *tree;
    NtPosixHost *host;
    NtNodeId node;
    unsigned updates, resize_events, keys, texts, pastes, reserved, input_callbacks, reserved_callbacks;
    unsigned downs, moves, ups, wheels, cancels, focus_losses;
    uint32_t last_key;
    char marker;
    bool fail_after_input;
} App;

static NtResult after_resize(void *value, NtCellSize size)
{
    App *app = value;
    const NtCellSize published = nt_frame_size(nt_tree_frame(app->tree));
    CHECK(size.columns == published.columns && size.rows == published.rows);
    CHECK(nt_posix_host_set_after_resize(app->host, NULL, NULL) == NT_RESULT_WRONG_PHASE);
    CHECK(!app->keys && !app->input_callbacks);
    if (app->resize_events == 1) return NT_RESULT_OK;
    CHECK(app->resize_events == 2 && size.columns == 12 && size.rows == 6);
    return NT_RESULT_OUT_OF_RANGE;
}

static NtResult measure(const void *value, NtMeasureContext *context, NtConstraints limits,
                        NtCellSize *out)
{
    (void)value;
    (void)context;
    *out = (NtCellSize){limits.columns.min, limits.rows.min};
    return NT_RESULT_OK;
}

static NtResult layout(const void *value, NtLayoutContext *context, NtCellSize assigned)
{
    (void)value;
    (void)context;
    (void)assigned;
    return NT_RESULT_OK;
}

static NtResult update(void *value, NtUpdateInfo info, NtUpdateReply *reply)
{
    App *app = value;
    CHECK(info.motion == NT_MOTION_REDUCED);
    ++app->updates;
    app->marker = app->updates == 1 ? 'A' : 'B';
    reply->invalidate = NT_INVALIDATE_PAINT;
    if (app->updates == 1) reply->next = (NtDeadline){true, info.now + 100000};
    return NT_RESULT_OK;
}

static NtResult paint(const void *value, NtPaintContext *context, NtCellSize assigned)
{
    const App *app = value;
    if (!assigned.columns || !assigned.rows) return NT_RESULT_OK;
    const NtCellStyle style = {{125, 220, 80, 255}, {15, 10, 8, 255}};
    return nt_paint_glyph(context, (NtCellPoint){0, 0}, (NtByteView){&app->marker, 1}, 1, style);
}

static NtResult event(void *value, NtEventContext *context, const NtEvent *input, NtEventReply *reply)
{
    App *app = value;
    CHECK(nt_event_phase(context) == NT_EVENT_TARGET);
    switch (input->kind) {
        case NT_EVENT_RESIZE: {
            ++app->resize_events;
            const NtCellSize size = nt_frame_size(nt_tree_frame(app->tree));
            CHECK(size.columns == input->data.resize.columns && size.rows == input->data.resize.rows);
            reply->focus = true;
            break;
        }
        case NT_EVENT_KEY:
            ++app->keys;
            app->last_key = input->data.key.key;
            if (app->last_key == '!') return NT_RESULT_OUT_OF_RANGE;
            CHECK(app->last_key == 0xe9 || app->last_key == NT_KEY_ESCAPE);
            CHECK(input->data.key.state == NT_KEY_PRESS && !input->data.key.modifiers);
            break;
        case NT_EVENT_TEXT:
            CHECK(input->data.text.count == 2 && memcmp(input->data.text.data, "é", 2) == 0);
            ++app->texts;
            break;
        case NT_EVENT_PASTE:
            CHECK(input->data.text.count == 5 && memcmp(input->data.text.data, "a\0é\n", 5) == 0);
            ++app->pastes;
            break;
        case NT_EVENT_POINTER_DOWN: {
            NtCellPoint local;
            CHECK(nt_event_local_position(context, &local) && local.x == 1 && local.y == 1);
            CHECK(input->data.pointer.pressed == NT_BUTTON_PRIMARY);
            ++app->downs;
            reply->capture = NT_CAPTURE_TAKE;
            break;
        }
        case NT_EVENT_POINTER_MOVE:
            CHECK(input->data.pointer.position.x == 99 && input->data.pointer.position.y == 49);
            CHECK(nt_tree_capture(app->tree, input->data.pointer.id).tree == app->tree);
            ++app->moves;
            break;
        case NT_EVENT_POINTER_UP:
            CHECK(!input->data.pointer.pressed);
            ++app->ups;
            break;
        case NT_EVENT_WHEEL:
            CHECK(input->data.wheel.dx == 0 && input->data.wheel.dy == 1);
            ++app->wheels;
            break;
        case NT_EVENT_POINTER_CANCEL:
            CHECK(input->data.pointer_cancel.reason == NT_CANCEL_HOST);
            CHECK(nt_tree_capture(app->tree, input->data.pointer_cancel.id).tree == NULL);
            ++app->cancels;
            break;
        case NT_EVENT_FOCUS_LOSS:
            CHECK(nt_tree_focus(app->tree).tree == NULL);
            ++app->focus_losses;
            break;
        default:
            break;
    }
    return NT_RESULT_OK;
}

static NtResult reserved_key(void *value, const NtEvent *input, NtByteView text)
{
    App *app = value;
    CHECK(input->kind == NT_EVENT_KEY);
    if (input->data.key.key == 'x') {
        CHECK(!input->data.key.modifiers && text.count == 1 && *text.data == 'x');
    } else {
        CHECK(input->data.key.key == 'c' && input->data.key.modifiers == NT_MOD_CONTROL && !text.count);
    }
    ++app->reserved;
    return NT_RESULT_OK;
}

static NtResult after_input(void *value, const NtDispatchResult *dispatch)
{
    App *app = value;
    ++app->input_callbacks;
    if (dispatch->reserved) CHECK(++app->reserved_callbacks == app->reserved);
    if (dispatch->text_delivered) CHECK(app->texts == app->keys);
    CHECK(nt_tree_invalidate(app->tree, app->node, NT_INVALIDATE_NONE) == NT_RESULT_OK);
    if (app->fail_after_input && app->input_callbacks == 4) return NT_RESULT_OUT_OF_RANGE;
    return NT_RESULT_OK;
}

static volatile sig_atomic_t original_calls;

static void original_handler(int signal_number)
{
    if (signal_number == SIGTERM) ++original_calls;
}

static void report(App *app, NtPosixHost *host, NtResult result, NtPosixStep step)
{
    NtTimeUs deadline = 0;
    (void)nt_posix_host_input_deadline(host, &deadline);
    fprintf(stderr, "{\"result\":%d,\"events\":%zu,\"resized\":%d,\"presented\":%d,"
            "\"signal\":%d,\"cleanup\":%d,\"columns\":%u,\"rows\":%u,\"deadline\":%" PRIu64 ","
            "\"keys\":%u,\"last_key\":%" PRIu32 ",\"texts\":%u,\"pastes\":%u,\"reserved\":%u,"
            "\"downs\":%u,\"moves\":%u,\"ups\":%u,\"wheels\":%u,\"cancels\":%u,\"focus_losses\":%u,"
            "\"updates\":%u,\"resize_events\":%u,\"capture\":%d,"
            "\"input_callbacks\":%u,\"reserved_callbacks\":%u}\n",
            result, step.input_events, step.resized, step.presented, step.signal_number, step.cleanup_result,
            step.size.columns, step.size.rows, deadline, app->keys, app->last_key,
            app->texts, app->pastes, app->reserved, app->downs, app->moves, app->ups,
            app->wheels, app->cancels, app->focus_losses, app->updates, app->resize_events,
            nt_tree_capture(app->tree, 0).tree == app->tree, app->input_callbacks, app->reserved_callbacks);
}

int main(int argc, char **argv)
{
    const bool alias_input = argc == 3 && strcmp(argv[2], "alias-input") == 0;
    const bool alias_output = argc == 3 && strcmp(argv[2], "alias-output") == 0;
    const bool fail_after_input = argc == 3 && strcmp(argv[2], "after-input-failure") == 0;
    const bool fail_after_resize = argc == 3 && strcmp(argv[2], "after-resize-failure") == 0;
    CHECK(argc == 2 || alias_input || alias_output || fail_after_input || fail_after_resize);
    FILE *commands = fdopen(atoi(argv[1]), "r");
    CHECK(commands);
    struct sigaction prior = {0};
    prior.sa_handler = original_handler;
    sigemptyset(&prior.sa_mask);
    CHECK(sigaction(SIGTERM, &prior, NULL) == 0);

    App app = {.fail_after_input=fail_after_input};
    CHECK(nt_tree_create(NULL, &app.tree) == NT_RESULT_OK);
    const NtComponentOps ops = {measure, layout, update, paint, event, NULL};
    CHECK(nt_tree_mount(app.tree, (NtNodeId){0}, &ops, &app, (NtMountOptions){true, true, true},
                        &app.node) == NT_RESULT_OK);
    const NtInputOptions input = {.escape_delay = 25, .paste_limit = 64};
    NtPosixHostOptions options = {
        .input_fd = STDIN_FILENO, .output_fd = STDOUT_FILENO,
        .capabilities = {.color = NT_COLOR_TRUECOLOR, .interactive = true, .utf8 = true,
                         .pointer = true, .bracketed_paste = true, .width = {NT_WIDTH_ONE, NT_WIDTH_TWO}},
        .input = &input, .motion = NT_MOTION_REDUCED, .output_timeout_ms = 100,
        .alternate_screen = true, .hide_cursor = true, .report_focus = true, .catch_signals = true,
        .reserved_key = reserved_key, .reserved_context = &app,
        .after_input = after_input, .input_context = &app,
    };
    NtPosixHost *host = NULL;
    const int master = posix_openpt(O_RDWR | O_NOCTTY);
    CHECK(master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0);
    const char *name = ptsname(master);
    CHECK(name);
    const int other_tty = open(name, O_RDWR | O_NOCTTY);
    CHECK(other_tty >= 0);
    NtPosixHostOptions unsupported = options;
    unsupported.input_fd = unsupported.output_fd = master;
    CHECK(nt_posix_host_open(app.tree, 1, NULL, &unsupported, &host) == NT_RESULT_UNSUPPORTED_CAPABILITY);
    CHECK(!host);
    unsupported.input_fd = STDIN_FILENO;
    unsupported.output_fd = other_tty;
    CHECK(nt_posix_host_open(app.tree, 1, NULL, &unsupported, &host) == NT_RESULT_UNSUPPORTED_CAPABILITY);
    CHECK(!host);
    CHECK(close(other_tty) == 0 && close(master) == 0);
    int alias_fd = -1;
    if (alias_input || alias_output) {
        CHECK(ioctl(STDIN_FILENO, TIOCSCTTY, 0) == 0);
        alias_fd = open("/dev/tty", (alias_input ? O_RDONLY : O_WRONLY) | O_NOCTTY);
        CHECK(alias_fd >= 0);
        if (alias_input) options.input_fd = alias_fd;
        if (alias_output) options.output_fd = alias_fd;
    }
    const int output_flags = fcntl(options.output_fd, F_GETFL);
    CHECK(nt_posix_host_open(app.tree, 1, NULL, &options, &host) == NT_RESULT_OK);
    app.host = host;
    if (fail_after_resize) {
        CHECK(nt_posix_host_set_after_resize(NULL, after_resize, &app) == NT_RESULT_INVALID_ARGUMENT);
        CHECK(nt_posix_host_set_after_resize(host, after_resize, &app) == NT_RESULT_OK);
    }
    NtPosixHost *second = NULL;
    CHECK(nt_posix_host_open(app.tree, 1, NULL, &options, &second) == NT_RESULT_WRONG_PHASE && !second);
    NtPosixStep sentinel = {.input_events = 17};
    CHECK(nt_posix_host_step(host, 0, NULL, &sentinel) == NT_RESULT_OUT_OF_RANGE);
    CHECK(sentinel.input_events == 17);
    fprintf(stderr, "READY\n");

    const NtKeyChord chords[] = {{'x', 0}, {'c', NT_MOD_CONTROL}};
    const NtDispatchPolicy policy = {chords, sizeof(chords) / sizeof(chords[0]), true};
    for (;;) {
        char line[80];
        if (!fgets(line, sizeof(line), commands)) {
            if (errno == EINTR) {
                clearerr(commands);
                continue;
            }
            CHECK(false);
        }
        if (line[0] == 'q') {
            const NtResult result = nt_posix_host_close(host);
            report(&app, host, result, (NtPosixStep){0});
            break;
        }
        NtTimeUs now;
        CHECK(sscanf(line, "s %" SCNu64, &now) == 1);
        NtPosixStep step;
        const NtResult result = nt_posix_host_step(host, now, &policy, &step);
        report(&app, host, result, step);
        if (result != NT_RESULT_OK || step.signal_number) break;
    }
    CHECK(nt_posix_host_step(host, 200000, &policy, &sentinel) == NT_RESULT_WRONG_PHASE);
    CHECK(sentinel.input_events == 17);
    CHECK(nt_posix_host_close(host) == NT_RESULT_OK);
    CHECK(nt_posix_host_set_after_resize(host, NULL, NULL) == NT_RESULT_WRONG_PHASE);
    CHECK((fcntl(options.output_fd, F_GETFL) & O_NONBLOCK) == (output_flags & O_NONBLOCK));
    CHECK(raise(SIGTERM) == 0 && original_calls == 1);
    /* Keep the controlling tty active while the driver checks restoration. */
    if (alias_fd >= 0) CHECK(fgetc(commands) == 'd');
    nt_posix_host_destroy(host);
    if (alias_fd >= 0) CHECK(close(alias_fd) == 0);
    nt_tree_destroy(app.tree);
    fclose(commands);
    return EXIT_SUCCESS;
}
