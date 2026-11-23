#include <nocturne/animation.h>
#include <nocturne/blend.h>
#include <nocturne/canvas.h>
#include <nocturne/input.h>
#include <nocturne/present.h>
#include <nocturne/simulation.h>

#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

enum { COLS = 112, ROWS = 36, FILE_ROW = 17 };
typedef enum { FLIGHT, TIDAL, AGENTS, SIGNAL } Scene;
typedef struct {
    NtCanvas *canvas;
    NtFrame *frame;
    NtResult result;
    Scene scene;
    int diff, approval, heading;
    bool quit, editing;
    char prompt[73], submitted[73];
    size_t length;
    NtTimeUs start, submitted_at;
} App;

static const NtRgba night = {8, 15, 24, 255};
static const NtRgba ink = {221, 232, 229, 255};
static const NtRgba mint = {129, 224, 203, 255};
static const NtRgba gold = {255, 177, 113, 255};
static const NtRgba dim = {140, 169, 185, 255};
static const NtRgba panel = {18, 34, 47, 226};
static const char *files[] = {"scene.c", "stream.c", "workspace_test.c"};
static const char *removed[] = {
    "- paint(flat_background);", "- replace_message(chunk);", "- assert(file_count == 2);"
};
static const char *added[] = {
    "+ paint(oblique_starfield);", "+ append_message(chunk);", "+ assert(diff_opens_on_click());"
};
static volatile sig_atomic_t interrupted;

static void stop(int sig) { interrupted = sig; }

static NtTimeUs clock_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (NtTimeUs)t.tv_sec * 1000000 + (NtTimeUs)t.tv_nsec / 1000;
}

static NtResult write_bytes(void *context, NtByteView bytes)
{
    return fwrite(bytes.data, 1, bytes.count, context) == bytes.count
        ? NT_RESULT_OK : NT_RESULT_TRANSPORT_FAILED;
}

static void cell(App *app, int x, int y, const char *glyph, size_t size,
                 NtRgba fg, NtRgba overlay)
{
    if (app->result != NT_RESULT_OK || x < 0 || x >= COLS || y < 0 || y >= ROWS) return;
    const NtCellPoint position = {x, y};
    NtCellBackground background;
    app->result = nt_frame_background(app->frame, position, &background);
    if (app->result != NT_RESULT_OK) return;
    NtRgba bg = background.color;
    if (overlay.alpha) app->result = nt_rgba_composite(bg, overlay, NULL, &bg);
    if (app->result == NT_RESULT_OK) {
        app->result = nt_frame_set_glyph(app->frame, position, (NtByteView){glyph, size},
                                         1, (NtCellStyle){fg, bg}, 0);
    }
}

static void label(App *app, int x, int y, const char *value, size_t limit, NtRgba color)
{
    for (size_t n = 0; n < limit && value[n]; ++n) {
        cell(app, x + (int)n, y, value + n, 1, color, (NtRgba){0});
    }
}

static void box(App *app, int x, int y, int width, int height, const char *title, NtRgba color)
{
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            const char *glyph = " ";
            if (row == 0) glyph = col == 0 ? "╭" : col == width - 1 ? "╮" : "─";
            else if (row == height - 1) glyph = col == 0 ? "╰" : col == width - 1 ? "╯" : "─";
            else if (col == 0 || col == width - 1) glyph = "│";
            NtCellView below;
            if (*glyph == ' ' && app->result == NT_RESULT_OK &&
                nt_frame_cell(app->frame, (NtCellPoint){x + col, y + row}, &below) == NT_RESULT_OK &&
                below.glyph.count == 3 && (unsigned char)below.glyph.data[0] == 0xe2 &&
                ((unsigned char)below.glyph.data[1] & 0xfc) == 0xa0) {
                NtRgba filtered;
                app->result = nt_rgba_composite(below.style.foreground,
                                               (NtRgba){18, 34, 47, 238}, NULL, &filtered);
                cell(app, x + col, y + row, below.glyph.data, below.glyph.count, filtered, panel);
            } else {
                cell(app, x + col, y + row, glyph, strlen(glyph), color, panel);
            }
        }
    }
    label(app, x + 3, y + 2, title, (size_t)width - 6, color);
}

static void dot(App *app, int x, int y, NtRgba color)
{
    if (app->result == NT_RESULT_OK) {
        app->result = nt_canvas_point(app->canvas, (NtCellPoint){x,y}, (NtCanvasInk){color,0,NULL});
    }
}

static void sky(App *app, double time)
{
    app->result = nt_canvas_clear(app->canvas, night);
    for (int i = 0; i < 360; ++i) {
        const int x = (i * 53 + i * i * 7 + (int)(time * (1 + i % 4))) % (COLS * 2);
        const int y = (i * 41 + i * i * 3 + x / 2) % (ROWS * 4);
        dot(app, x, y, i % 13 == 0 ? gold : i % 7 == 0 ? mint : (NtRgba){61,94,127,180});
    }
    static const NtWaterWave waves[] = {
        {.amplitude = .19, .cycles_per_unit = {.33,.19}, .cycles_per_second = .37},
        {.amplitude = .06, .cycles_per_unit = {.67,-.21}, .cycles_per_second = .61},
    };
    for (int x = 4; x < COLS * 2 - 4; x += 2) {
        for (int z = 0; z < 34; z += 2) {
            const double phase = x * .055 + z * .12 + time * 1.3;
            double ripple = sin(phase) * 6;
            if (app->scene == TIDAL && app->result == NT_RESULT_OK) {
                NtWaterWaveSample wave;
                app->result = nt_water_wave_sample(waves, sizeof waves / sizeof *waves,
                                                   (NtPoint){x * .037,z * .075}, time, &wave);
                if (app->result == NT_RESULT_OK) ripple = wave.height * 52;
            }
            const int y = 70 + x / 6 + z + (int)ripple;
            const int brightness = (int)(40 + 32 * (1 + cos(phase)));
            const NtRgba shade = app->scene == FLIGHT
                ? (NtRgba){(uint8_t)(brightness + 28),(uint8_t)brightness,99,230}
                : (NtRgba){45,(uint8_t)(brightness + 32),(uint8_t)(brightness + 45),225};
            dot(app, x, y, shade);
        }
    }
}

static void show_stream(App *app, NtTimeUs elapsed, int x, int y, const char *text)
{
    NtAnimationSample sample;
    const size_t length = strlen(text);
    const NtTween reveal = {0, (double)length, length * 19000, 0, NT_LINEAR, NT_ONCE};
    if (nt_tween_sample(&reveal, elapsed, &sample) == NT_OK) {
        label(app, x, y, text, (size_t)sample.value, ink);
    }
}

static void agents(App *app, NtTimeUs elapsed, NtTimeUs now)
{
    box(app, 3, 6, 65, 23, "INCOMING / AGENT STREAM", mint);
    label(app, 6, 11, "ORBIT / LAYOUT", 55, mint);
    show_stream(app, elapsed, 6, 12, "Scene graph mounted. Reviewing layer order.");
    label(app, 6, 15, "VESPER / IMPLEMENTATION", 55, gold);
    show_stream(app, elapsed > 900000 ? elapsed - 900000 : 0,
                6, 16, "Streaming updates preserve the selected file.");
    label(app, 6, 19, "KEPLER / REVIEW", 55, mint);
    show_stream(app, elapsed > 1800000 ? elapsed - 1800000 : 0,
                6, 20, "Three files changed. Open a diff to review.");
    if (app->submitted_at) {
        label(app, 6, 23, app->submitted, 56, gold);
        show_stream(app, now - app->submitted_at, 6, 24, "Prompt received by this scripted example.");
    }
    box(app, 73, 6, 36, 9, "APPROVAL / PREVIEW", gold);
    label(app, 76, 11, app->approval > 0 ? "Preview approved" :
          app->approval < 0 ? "Preview declined" : "[A] Approve    [D] Decline", 29, ink);
    box(app, 73, 14, 36, 15, "CHANGED FILES", mint);
    for (int i = 0; i < 3; ++i) {
        char item[40];
        snprintf(item, sizeof item, "[%d] %s", i + 1, files[i]);
        label(app, 76, FILE_ROW + 2 * i, item, 29, ink);
    }
    if (app->diff >= 0) {
        box(app, 22, 9, 67, 17, "REVIEW / FILE DIFF", gold);
        label(app, 25, 13, files[app->diff], 59, ink);
        label(app, 25, 16, "@@ Nocturne composition @@", 59, dim);
        label(app, 25, 18, removed[app->diff], 59, (NtRgba){255,136,143,255});
        label(app, 25, 20, added[app->diff], 59, mint);
        label(app, 25, 23, "[Esc] Close", 59, dim);
    }
}

static void instrument(App *app, double time)
{
    const bool flight = app->scene == FLIGHT;
    box(app, 4, 7, 35, 21, flight ? "NAVIGATION" : "SWELL ENERGY", mint);
    label(app, 7, 12, flight ? "HEADING / AUTOMATIC" : "SPECTRUM / OPEN WATER", 28, dim);
    for (int x = 0; x < 25; ++x) {
        const int y = 20 - (int)(3 * sin(x * .27 + time) + (flight ? 1 : 2) * cos(x * .14));
        cell(app, 8 + x, y, "•", 3, x % 4 ? mint : gold, (NtRgba){0});
    }
    char summary[35];
    if (flight) snprintf(summary, sizeof summary, "HEADING %03d / COPPER", (360 + app->heading) % 360);
    else snprintf(summary, sizeof summary, "WIND 22 DEG / FOAM %02d%%", 62 + (int)(sin(time) * 9));
    label(app, 7, 24, summary, 29, gold);
    box(app, 69, 10, 35, 16, flight ? "ROUTE / COPPER" : "SURVEY LOG", gold);
    label(app, 72, 15, flight ? "01 Cruise" : "A  North buoy online", 29, ink);
    label(app, 72, 17, flight ? "02 Bank left" : "B  South buoy online", 29, ink);
    label(app, 72, 19, flight ? "03 Climb" : "Current: cross swell", 29, ink);
    label(app, 72, 22, flight ? "Arrows adjust heading" : "Sampling: continuous", 29, dim);
}

static void signal_desk(App *app, double time)
{
    box(app, 4, 7, 64, 20, "SIGNAL / WAVEFORM", mint);
    for (int x = 0; x < 53; ++x) {
        const int y = 17 + (int)(4 * sin(x * .33 - time * 2) * cos(x * .09));
        cell(app, 9 + x, y, "•", 3, mint, (NtRgba){0});
    }
    box(app, 73, 7, 35, 12, "LEVELS / CHANNEL 01-04", gold);
    static const char bar[] = "####################";
    for (int channel = 0; channel < 4; ++channel) {
        const size_t level = (size_t)(11 + 8 * sin(time * 2 + channel * 1.4));
        label(app, 77, 11 + channel, bar, level, channel % 2 ? gold : mint);
    }
    box(app, 73, 20, 35, 7, "STATUS / MONITORING", mint);
    label(app, 76, 24, "120 Hz   /   Signal locked", 27, ink);
}

static void render(App *app, NtTimeUs now)
{
    const NtTimeUs elapsed = now - app->start;
    const double time = elapsed / 1000000.0;
    sky(app, time);
    if (app->result == NT_RESULT_OK) {
        app->result = nt_canvas_render(app->canvas, NT_SAMPLE_BRAILLE, NULL, &app->frame);
    }
    box(app, 3, 1, 106, 5,
        app->scene == FLIGHT ? "FLIGHT DECK  /  COPPER APPROACH" :
        app->scene == TIDAL ? "TIDAL CONSOLE  /  OPEN WATER" :
        app->scene == SIGNAL ? "SIGNAL DESK  /  RECEIVER 01" : "AGENT WORKSPACE  /  ORBITAL REVIEW", mint);
    if (app->scene == AGENTS) agents(app, elapsed, now);
    else if (app->scene == SIGNAL) signal_desk(app, time);
    else instrument(app, time);
    box(app, 3, 30, 106, 5, app->editing ? "PROMPT / TYPE THEN ENTER" :
        "PROMPT / [ENTER] Edit   [F1] Flight   [F2] Tidal   [F3] Agents   [F4] Signal   [Q] Exit", mint);
    const char *placeholder = app->scene == FLIGHT ? "> Course correction..." :
        app->scene == TIDAL ? "> Describe the next survey..." : "> Ask the agents to inspect a change...";
    if (app->editing) {
        label(app, 6, 33, app->prompt, 74, ink);
        if ((elapsed / 420000) % 2 == 0) label(app, 6 + (int)app->length, 33, "_", 1, mint);
    } else show_stream(app, elapsed, 6, 33, placeholder);
}

static void event(App *app, const NtInputRead *read, NtTimeUs now)
{
    if (read->event.kind == NT_EVENT_POINTER_DOWN && app->scene == AGENTS && app->diff < 0) {
        NtCellPoint p = read->event.data.pointer.position;
        if (p.x >= 75 && p.x <= 107 && p.y >= FILE_ROW && p.y <= FILE_ROW + 4) {
            app->diff = (p.y - FILE_ROW) / 2;
        } else if (p.y == 11 && p.x >= 76 && p.x <= 101) {
            app->approval = p.x < 89 ? 1 : -1;
        }
        return;
    }
    if (read->event.kind != NT_EVENT_KEY || read->event.data.key.state == NT_KEY_RELEASE) return;
    const uint32_t key = read->event.data.key.key;
    if (key == NT_KEY_ESCAPE) { app->diff = -1; app->editing = false; return; }
    if (app->diff >= 0) return;
    if (key == NT_KEY_ENTER) {
        if (app->editing && app->length) {
            memcpy(app->submitted, app->prompt, app->length + 1);
            app->submitted_at = now;
            app->length = 0;
            app->prompt[0] = 0;
        }
        app->editing = !app->editing;
        return;
    }
    if (app->editing) {
        if (key == NT_KEY_BACKSPACE && app->length) app->prompt[--app->length] = 0;
        else if (read->key_text.count == 1 && app->length + 1 < sizeof app->prompt) {
            const unsigned char ch = (unsigned char)read->key_text.data[0];
            if (ch >= 32 && ch <= 126) {
                app->prompt[app->length++] = (char)ch;
                app->prompt[app->length] = 0;
            }
        }
        return;
    }
    if (key >= NT_KEY_F1 && key <= NT_KEY_F4) {
        app->scene = (Scene)(key - NT_KEY_F1);
        app->start = now;
    } else if (key == 'q') app->quit = true;
    else if (key == NT_KEY_LEFT) app->heading = (app->heading + 355) % 360;
    else if (key == NT_KEY_RIGHT) app->heading = (app->heading + 5) % 360;
    else if (app->scene == AGENTS) {
        if (key >= '1' && key <= '3') app->diff = (int)(key - '1');
        else if (key == 'a' || key == 'd') app->approval = key == 'a' ? 1 : -1;
    }
}

int main(int argc, char **argv)
{
    App app = {.diff = -1, .scene = FLIGHT, .heading = 68};
    if (argc == 2 && strcmp(argv[1], "tidal") == 0) app.scene = TIDAL;
    else if (argc == 2 && strcmp(argv[1], "agents") == 0) app.scene = AGENTS;
    else if (argc == 2 && strcmp(argv[1], "signal") == 0) app.scene = SIGNAL;
    else if (argc > 2 || (argc == 2 && strcmp(argv[1], "flight") != 0)) {
        fputs("Usage: nocturne_scenes [flight|tidal|agents|signal]\n", stderr);
        return 2;
    }
    struct termios saved;
    if (tcgetattr(STDIN_FILENO, &saved) != 0 || !isatty(STDOUT_FILENO)) return 2;
    struct termios raw = saved;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    raw.c_iflag &= (tcflag_t)~(IXON | ICRNL);
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) return 2;
    signal(SIGINT, stop);
    signal(SIGTERM, stop);
    signal(SIGHUP, stop);
    fputs("\033[?1049h\033[?25l\033[?1000h\033[?1006h", stdout);
    NtInputDecoder *decoder = NULL;
    app.result = nt_input_create(NULL, NULL, &decoder);
    if (app.result == NT_RESULT_OK) {
        app.result = nt_canvas_create(NULL, (NtCellSize){COLS * 2, ROWS * 4}, &app.canvas);
    }
    NtAnsiPresenter presenter = {
        .capabilities = {.color = NT_COLOR_TRUECOLOR, .interactive = true, .utf8 = true, .braille = true, .width = {NT_WIDTH_ONE, NT_WIDTH_TWO}},
        .matte = night, .write = write_bytes, .write_context = stdout
    };
    app.start = clock_us();
    while (app.result == NT_RESULT_OK && !app.quit && !interrupted) {
        render(&app, clock_us());
        if (app.result == NT_RESULT_OK) app.result = nt_ansi_present(&presenter, app.frame, clock_us());
        nt_frame_destroy(app.frame);
        app.frame = NULL;
        fflush(stdout);
        struct pollfd input = {STDIN_FILENO, POLLIN, 0};
        char bytes[256];
        const int ready = poll(&input, 1, 75);
        ssize_t size = ready > 0 ? read(STDIN_FILENO, bytes, sizeof bytes) : 0;
        if (size < 0 || (ready > 0 && size == 0)) break;
        size_t offset = 0;
        do {
            NtInputRead record;
            NtTimeUs now = clock_us();
            app.result = nt_input_read(decoder, (NtByteView){bytes + offset, (size_t)size - offset}, now, &record);
            if (app.result != NT_RESULT_OK) break;
            if (record.status == NT_INPUT_READY) event(&app, &record, now);
            offset += record.consumed;
        } while (offset < (size_t)size);
    }
    nt_input_destroy(decoder);
    nt_canvas_destroy(app.canvas);
    fputs("\033[?1006l\033[?1000l\033[?25h\033[0m\033[?1049l", stdout);
    fflush(stdout);
    tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    if (app.result != NT_RESULT_OK) fprintf(stderr, "Nocturne result %d\n", app.result);
    return app.result != NT_RESULT_OK;
}
