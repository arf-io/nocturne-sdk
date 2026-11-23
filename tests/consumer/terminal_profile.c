#include <nocturne/posix_host.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "Terminal consumer line %d: %s\n", __LINE__, #condition); \
    exit(EXIT_FAILURE); \
} } while (0)
#define BYTES(value) {value, sizeof(value) - 1}

typedef struct ProfileDemo { NtMotionPolicy motion; } ProfileDemo;

static NtResult measure(const void *state, NtMeasureContext *context, NtConstraints limits,
                        NtCellSize *size)
{
    (void)state;
    (void)context;
    *size = (NtCellSize){limits.columns.min, limits.rows.min};
    return NT_RESULT_OK;
}

static NtResult layout(const void *state, NtLayoutContext *context, NtCellSize size)
{
    (void)state;
    (void)context;
    (void)size;
    return NT_RESULT_OK;
}

static NtResult update(void *state, NtUpdateInfo info, NtUpdateReply *reply)
{
    ProfileDemo *demo = state;
    demo->motion = info.motion;
    if (info.motion == NT_MOTION_FULL) reply->next = (NtDeadline){true, info.now + 1000};
    reply->invalidate = NT_INVALIDATE_PAINT;
    return NT_RESULT_OK;
}

static NtResult paint(const void *state, NtPaintContext *context, NtCellSize size)
{
    if (size.columns < 12 || size.rows < 2) return NT_RESULT_OK;
    const ProfileDemo *demo = state;
    const NtCellStyle style = {{95,175,215,255},{8,8,8,255}};
    const char *status = demo->motion == NT_MOTION_REDUCED ? "Queue ready" : "Queue active";
    for (size_t i = 0; status[i]; ++i) {
        NtResult result = nt_paint_glyph(context, (NtCellPoint){(int32_t)i,0},
                                        (NtByteView){status + i,1}, 1, style);
        if (result != NT_RESULT_OK) return result;
    }
    static const struct { int32_t x; uint8_t width; NtByteView text; } glyphs[] = {
        {0,1,BYTES("c")}, {1,1,BYTES("a")}, {2,1,BYTES("f")}, {3,1,BYTES("é")},
        {5,2,BYTES("界")}, {8,1,BYTES("⠿")}, {10,1,BYTES("▀")}, {11,1,BYTES("|")},
    };
    for (size_t i = 0; i < sizeof(glyphs) / sizeof(*glyphs); ++i) {
        NtResult result = nt_paint_glyph(context, (NtCellPoint){glyphs[i].x,1}, glyphs[i].text,
                                        glyphs[i].width, style);
        if (result != NT_RESULT_OK) return result;
    }
    /* An application-supplied control scalar cannot enter the frame or sink. */
    const NtByteView escape = BYTES("\x1b");
    CHECK(nt_paint_glyph(context, (NtCellPoint){0,1}, escape, 1, style) == NT_RESULT_INVALID_ARGUMENT);
    return NT_RESULT_OK;
}

static NtResult file_write(void *stream, NtByteView bytes)
{
    return fwrite(bytes.data, 1, bytes.count, stream) == bytes.count ?
        NT_RESULT_OK : NT_RESULT_TRANSPORT_FAILED;
}

int main(int argc, char **argv)
{
    const bool full = argc == 2 && strcmp(argv[1], "full") == 0;
    const bool explicit_hints = argc == 2 && strcmp(argv[1], "hints") == 0;
    CHECK(argc == 1 || full || explicit_hints);
    const NtPosixTerminalHints hints = {"xterm-256color", "24BIT", "unused.uTf8@modifier", false};
    NtPosixTerminalReport report;
    CHECK(nt_posix_terminal_probe(0, 1, explicit_hints ? &hints : NULL, &report) == NT_RESULT_OK);
    NtPosixTerminalReport invalid = {.host_available = true};
    CHECK(nt_posix_terminal_probe(-2, 1, NULL, &invalid) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(invalid.host_available);
    CHECK(nt_posix_terminal_probe(-1, 1, explicit_hints ? &hints : NULL, &invalid) == NT_RESULT_OK);
    CHECK(!invalid.input_tty && !invalid.host_available && !invalid.capabilities.pointer &&
          !invalid.capabilities.bracketed_paste && invalid.capabilities.color == report.capabilities.color);

    const NtTerminalCapabilities caps = report.capabilities;
    fprintf(stderr, "%d %d %d %d %d %d %d %d %d %d\n", report.input_tty, report.output_tty,
            report.host_available, caps.interactive, caps.color, caps.utf8, caps.braille,
            caps.half_blocks, caps.pointer, caps.bracketed_paste);
    const NtMotionPolicy motion = full ? NT_MOTION_FULL : NT_MOTION_REDUCED;
    NtRgba colors[NT_XTERM256_COLORS];
    NtPaletteView palette = {0};
    if (caps.color == NT_COLOR_INDEXED) {
        CHECK(nt_xterm256_palette(colors, NT_XTERM256_COLORS) == NT_RESULT_OK);
        palette = (NtPaletteView){colors, NT_XTERM256_COLORS};
    }
    NtTree *tree;
    NtNodeId root;
    ProfileDemo demo = {0};
    const NtComponentOps ops = {measure, layout, update, paint, NULL, NULL};
    CHECK(nt_tree_create(NULL, &tree) == NT_RESULT_OK);
    CHECK(nt_tree_mount(tree, (NtNodeId){0}, &ops, &demo, (NtMountOptions){.enabled=true}, &root)
          == NT_RESULT_OK);
    if (report.host_available) {
        const NtPosixHostOptions options = {.input_fd=0,.output_fd=1,.capabilities=caps,.palette=palette,
                                            .motion=motion,.output_timeout_ms=100};
        NtPosixHost *host;
        NtPosixStep step;
        CHECK(nt_posix_host_open(tree, 1, NULL, &options, &host) == NT_RESULT_OK);
        CHECK(nt_posix_host_step(host, 1, NULL, &step) == NT_RESULT_OK && step.presented && step.resized);
        CHECK(nt_posix_host_close(host) == NT_RESULT_OK);
        nt_posix_host_destroy(host);
    } else {
        CHECK(nt_tree_tick(tree, 1, motion) == NT_RESULT_OK);
        CHECK(nt_tree_publish(tree, (NtCellSize){24,2}) == NT_RESULT_OK);
        NtAnsiPresenter presenter = {.capabilities=caps,.palette=palette,.write=file_write,
                                     .write_context=stdout};
        const NtFrameSink sink = {&presenter, nt_ansi_present};
        CHECK(nt_frame_present(&sink, nt_tree_frame(tree), 1) == NT_RESULT_OK && fflush(stdout) == 0);
    }
    CHECK(nt_tree_next_update(tree).scheduled == full);
    nt_tree_destroy(tree);
    return EXIT_SUCCESS;
}
