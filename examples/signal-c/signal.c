#include <nocturne/abi.h>
#include <nocturne/component.h>
#include <nocturne/layout.h>
#include <nocturne/paint.h>
#include <nocturne/widgets.h>
#include <stdio.h>

#ifdef NOCTURNE_SIGNAL_POSIX
#include <nocturne/posix_host.h>
#include <errno.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
#endif

enum { WIDTH = 12 };
static const NtCellSize frame_size = {WIDTH, 2};
static const NtCellStyle ink = {{100, 230, 190, 255}, {24, 32, 46, 255}};

typedef struct Meter { uint32_t level; bool terminal, focused; } Meter;

static NtResult measure(const void *state, NtMeasureContext *context, NtConstraints constraints, NtCellSize *size)
{
    (void)context;
    const Meter *meter = state;
    NtCellSize measured = {WIDTH + (uint32_t)meter->terminal, 1};
    if (measured.columns < constraints.columns.min) measured.columns = constraints.columns.min;
    if (measured.rows < constraints.rows.min) measured.rows = constraints.rows.min;
    if (constraints.columns.bounded && measured.columns > constraints.columns.max)
        measured.columns = constraints.columns.max;
    if (constraints.rows.bounded && measured.rows > constraints.rows.max)
        measured.rows = constraints.rows.max;
    *size = measured;
    return NT_RESULT_OK;
}

static NtResult layout(const void *state, NtLayoutContext *context, NtCellSize size)
{
    (void)state;
    (void)context;
    (void)size;
    return NT_RESULT_OK;
}

static NtResult paint(const void *state, NtPaintContext *context, NtCellSize assigned)
{
    const Meter *meter = state;
    const uint32_t prefix = (uint32_t)meter->terminal;
    const uint32_t width = assigned.columns < WIDTH + prefix ? assigned.columns : WIDTH + prefix;
    for (uint32_t column = 0; column < width; ++column) {
        const char *mark = column < prefix ? (meter->focused ? ">" : " ") :
                          (column - prefix < meter->level ? "#" : ".");
        const NtByteView glyph = {mark, 1};
        const NtResult result = nt_paint_glyph(context, (NtCellPoint){(int32_t)column, 0}, glyph, 1, ink);
        if (result != NT_RESULT_OK) return result;
    }
    return NT_RESULT_OK;
}

static NtResult on_event(void *state, NtEventContext *context, const NtEvent *event, NtEventReply *reply)
{
    (void)context;
    Meter *meter = state;
    if (meter->terminal && (event->kind == NT_EVENT_FOCUS_GAIN || event->kind == NT_EVENT_FOCUS_LOSS)) {
        meter->focused = event->kind == NT_EVENT_FOCUS_GAIN;
        reply->invalidate = NT_INVALIDATE_PAINT;
        return NT_RESULT_OK;
    }
    if (event->kind != NT_EVENT_KEY || event->data.key.state != NT_KEY_PRESS) return NT_RESULT_OK;
    switch (event->data.key.key) {
    case '+':
        if (meter->level < WIDTH) ++meter->level;
        break;
    case '-':
        if (meter->level > 0) --meter->level;
        break;
    default:
        return NT_RESULT_OK;
    }
    reply->consume = true;
    reply->invalidate = NT_INVALIDATE_PAINT;
    return NT_RESULT_OK;
}

static NtResult render(NtTree *tree)
{
    NtResult result = nt_tree_publish(tree, frame_size);
    if (result != NT_RESULT_OK) return result;
    const NtFrame *frame = nt_tree_frame(tree);
    for (uint32_t y = 0; y < frame_size.rows; ++y) {
        for (uint32_t x = 0; x < frame_size.columns;) {
            NtCellView cell;
            result = nt_frame_cell(frame, (NtCellPoint){(int32_t)x, (int32_t)y}, &cell);
            if (result != NT_RESULT_OK) return result;
            if (cell.glyph_columns == 0) {
                if (fputc(' ', stdout) == EOF) return NT_RESULT_TRANSPORT_FAILED;
                ++x;
            } else {
                if (fwrite(cell.glyph.data, 1, cell.glyph.count, stdout) != cell.glyph.count)
                    return NT_RESULT_TRANSPORT_FAILED;
                x += cell.glyph_columns;
            }
        }
        if (fputc('\n', stdout) == EOF) return NT_RESULT_TRANSPORT_FAILED;
    }
    return fflush(stdout) == 0 ? NT_RESULT_OK : NT_RESULT_TRANSPORT_FAILED;
}

static NtResult mount_signal(NtTree *tree, Meter *meter, NtNodeId *out)
{
    const NtMountOptions passive = {.enabled = true};
    const NtBoxLayout box = {.kind = NT_BOX_LAYOUT_COLUMN};
    NtNodeId root;
    NtResult result = nt_box_layout_mount(tree, (NtNodeId){0}, passive, &box, &root);
    if (result != NT_RESULT_OK) return result;
    const NtLabelOptions label = {.text = {"Signal level", 12}, .style = {
        .width = {NT_WIDTH_ONE, NT_WIDTH_TWO}, .wrap = NT_WRAP_NONE, .align = NT_TEXT_ALIGN_START,
        .direction = NT_DIRECTION_AUTO, .tab_stop = 4, .color = ink
    }};
    NtNodeId title;
    result = nt_label_mount(tree, root, passive, &label, &title);
    if (result != NT_RESULT_OK) return result;
    static const NtComponentOps ops = {.measure = measure, .layout = layout, .paint = paint, .event = on_event};
    const NtMountOptions interactive = {.enabled = true, .focusable = true};
    return nt_tree_mount(tree, root, &ops, meter, interactive, out);
}

static int read_command(void)
{
    const int command = getchar();
    if (command == EOF || command == '\n') return command;
    int character;
    do {
        character = getchar();
    } while (character != '\n' && character != EOF);
    return command;
}

static NtResult run_signal(NtTree *tree, Meter *meter)
{
    NtNodeId signal;
    NtResult result = mount_signal(tree, meter, &signal);
    if (result != NT_RESULT_OK) return result;
    NtTimeUs now = 0;
    result = nt_tree_tick(tree, now, NT_MOTION_FULL);
    if (result == NT_RESULT_OK) result = render(tree);
    if (result == NT_RESULT_OK) result = nt_tree_set_focus(tree, signal);
    if (result != NT_RESULT_OK) return result;

    for (int command = read_command(); command != EOF && command != 'q'; command = read_command()) {
        if (command != '+' && command != '-') continue;
        if (now <= UINT64_MAX - 50000) now += 50000;
        result = nt_tree_tick(tree, now, NT_MOTION_FULL);
        if (result != NT_RESULT_OK) return result;
        const NtEvent event = {.kind = NT_EVENT_KEY, .at = now,
                               .data.key = {(uint32_t)command, 0, NT_KEY_PRESS}};
        NtDispatchResult delivery;
        result = nt_tree_dispatch(tree, &event, (NtByteView){0}, NULL, &delivery);
        if (result == NT_RESULT_OK) result = render(tree);
        if (result != NT_RESULT_OK) return result;
    }
    return ferror(stdin) ? NT_RESULT_TRANSPORT_FAILED : NT_RESULT_OK;
}

#ifdef NOCTURNE_SIGNAL_POSIX
typedef struct TerminalSignal {
    NtTree *tree;
    NtNodeId node;
    bool quit;
} TerminalSignal;

static NtResult focus_meter(void *context, NtCellSize size)
{
    (void)size;
    TerminalSignal *app = context;
    NtNodeGeometry geometry;
    const NtResult result = nt_tree_geometry(app->tree, app->node, &geometry);
    if (result != NT_RESULT_OK || !geometry.placed || !geometry.clip.size.columns || !geometry.clip.size.rows)
        return result;
    return nt_tree_set_focus(app->tree, app->node);
}

static NtResult quit_signal(void *context, const NtEvent *event, NtByteView text)
{
    (void)text;
    TerminalSignal *app = context;
    if (event->data.key.state == NT_KEY_PRESS) app->quit = true;
    return NT_RESULT_OK;
}

static NtResult current_time(NtTimeUs *out)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return NT_RESULT_TRANSPORT_FAILED;
    *out = (NtTimeUs)now.tv_sec * UINT64_C(1000000) + (NtTimeUs)now.tv_nsec / 1000;
    return NT_RESULT_OK;
}

static NtResult run_terminal(NtTree *tree, Meter *meter, NtTerminalCapabilities caps, int *signal_number)
{
    NtPaletteView palette = {0};
    NtRgba entries[NT_XTERM256_COLORS];
    if (caps.color == NT_COLOR_INDEXED) {
        const NtResult result = nt_xterm256_palette(entries, NT_XTERM256_COLORS);
        if (result != NT_RESULT_OK) return result;
        palette = (NtPaletteView){entries, NT_XTERM256_COLORS};
    }
    meter->terminal = true;
    TerminalSignal app = {.tree = tree};
    NtResult result = mount_signal(tree, meter, &app.node);
    if (result != NT_RESULT_OK) return result;
    caps.pointer = false;
    caps.bracketed_paste = false;
    const NtPosixHostOptions options = {
        .input_fd = STDIN_FILENO, .output_fd = STDOUT_FILENO, .capabilities = caps,
        .matte = {24, 32, 46, 255}, .palette = palette, .output_timeout_ms = 250,
        .motion = NT_MOTION_FULL,
        .alternate_screen = true, .hide_cursor = true, .catch_signals = true,
        .reserved_key = quit_signal, .reserved_context = &app,
    };
    const NtKeyChord quit[] = {{'q', 0}, {'c', NT_MOD_CONTROL}};
    const NtDispatchPolicy policy = {quit, sizeof(quit) / sizeof(quit[0]), false};
    NtTimeUs now;
    NtPosixHost *host = NULL;
    result = current_time(&now);
    if (result == NT_RESULT_OK) result = nt_posix_host_open(tree, now, NULL, &options, &host);
    if (result == NT_RESULT_OK) result = nt_posix_host_set_after_resize(host, focus_meter, &app);
    while (result == NT_RESULT_OK && !app.quit && !*signal_number) {
        NtPosixStep step = {0};
        result = current_time(&now);
        if (result != NT_RESULT_OK) break;
        result = nt_posix_host_step(host, now, &policy, &step);
        *signal_number = step.signal_number;
        if (result != NT_RESULT_OK || app.quit || *signal_number) break;
        struct pollfd input = {STDIN_FILENO, POLLIN, 0};
        if (poll(&input, 1, 50) < 0 && errno != EINTR) result = NT_RESULT_TRANSPORT_FAILED;
    }
    if (host) {
        const NtResult cleanup = nt_posix_host_close(host);
        nt_posix_host_destroy(host);
        if (result == NT_RESULT_OK) result = cleanup;
    }
    return result;
}
#endif

int main(void)
{
    Meter meter = {.level = 4};
    NtResult result = NT_RESULT_UNSUPPORTED_CAPABILITY;
    NtTree *tree = NULL;
    int signal_number = 0;
    if (nt_abi_version() == NT_ABI_VERSION) {
        result = nt_tree_create(NULL, &tree);
#ifdef NOCTURNE_SIGNAL_POSIX
        if (result == NT_RESULT_OK) {
            NtPosixTerminalReport terminal;
            result = nt_posix_terminal_probe(STDIN_FILENO, STDOUT_FILENO, NULL, &terminal);
            if (result == NT_RESULT_OK && terminal.host_available)
                result = run_terminal(tree, &meter, terminal.capabilities, &signal_number);
            else if (result == NT_RESULT_OK)
                result = run_signal(tree, &meter);
        }
#else
        if (result == NT_RESULT_OK) result = run_signal(tree, &meter);
#endif
    }
    nt_tree_destroy(tree);
    if (result == NT_RESULT_OK) return signal_number ? 128 + signal_number : 0;
    const char *name = nt_result_name((int32_t)result);
    fprintf(stderr, "Signal consumer failed with %s (%d)\n", name ? name : "unknown result", (int)result);
    return 1;
}
