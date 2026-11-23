/* Consumer example. All application objects, drawing, terminal output, and the
 * clock live here. The SDK supplies independently composable motion components.
 * This tiny ASCII canvas is example code, not the future SDK text renderer. */
#define _POSIX_C_SOURCE 200809L
#include <nocturne/animation.h>
#include <nocturne/motion.h>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <time.h>
#include <unistd.h>
#endif

enum { WIDTH = 56, HEIGHT = 7 };
static volatile sig_atomic_t stopped;
static int terminal_active;

typedef struct {
    const char *text;
    NtPath path;
    NtTween distance;
    NtTween brightness;
} AnimatedLabel;

static void restore_terminal(void) {
    if (terminal_active) fputs("\033[0m\033[?25h\033[?1049l", stdout);
}

static void stop_animation(int signal_number) {
    (void)signal_number;
    stopped = 1;
}

static int draw_label(const AnimatedLabel *label, uint64_t elapsed,
                      char cells[HEIGHT][WIDTH + 1], unsigned color[HEIGHT][WIDTH]) {
    NtAnimationSample travel, brightness;
    NtPathSample position;
    if (nt_tween_sample(&label->distance, elapsed, &travel) != NT_OK ||
        nt_tween_sample(&label->brightness, elapsed, &brightness) != NT_OK ||
        nt_path_sample(&label->path, travel.value, &position) != NT_OK) return 0;
    int x = (int)floor(position.position.x + .5), y = (int)floor(position.position.y + .5);
    if (y < 0 || y >= HEIGHT) return 1;
    unsigned channel = (unsigned)(80 + brightness.value * 175);
    for (size_t i = 0; label->text[i]; ++i) {
        int column = x + (int)i;
        if (column >= 0 && column < WIDTH) {
            cells[y][column] = label->text[i];
            color[y][column] = channel;
        }
    }
    return 1;
}

static int frame(uint64_t elapsed, int ansi) {
    const NtPoint route_a[] = {{1, 1}, {44, 1}, {44, 4}};
    const NtPoint route_b[] = {{1, 4}, {1, 2}, {38, 2}};
    const AnimatedLabel labels[] = {
        {"[build]", {route_a, 3, 0, NT_ONCE},
         {0, 46, 2400000, 0, NT_SMOOTH, NT_PING_PONG},
         {.2, 1, 700000, 0, NT_SINE, NT_PING_PONG}},
        {"[deploy]", {route_b, 3, 0, NT_ONCE},
         {0, 39, 3200000, 200000, NT_EASE_IN_OUT, NT_PING_PONG},
         {.3, 1, 1100000, 0, NT_SMOOTH, NT_PING_PONG}},
    };
    const NtKeyframe keys[] = {{0, 0, NT_EASE_IN}, {1000000, .3, NT_LINEAR},
                               {2200000, .3, NT_SMOOTH}, {4000000, 1, NT_LINEAR}};
    const NtKeyframeTrack progress = {keys, 4, 0, NT_LOOP};
    NtAnimationSample value;
    if (nt_keyframes_sample(&progress, elapsed, &value) != NT_OK) return 0;
    char cells[HEIGHT][WIDTH + 1];
    unsigned color[HEIGHT][WIDTH] = {{0}};
    for (int y = 0; y < HEIGHT; ++y) {
        memset(cells[y], ' ', WIDTH); cells[y][WIDTH] = '\0';
    }
    for (size_t i = 0; i < sizeof labels / sizeof labels[0]; ++i)
        if (!draw_label(&labels[i], elapsed, cells, color)) return 0;
    memcpy(cells[6], "Progress  [", 11); cells[6][51] = ']';
    for (int i = 0; i < 40; ++i) cells[6][11 + i] = i < (int)(value.value * 40) ? '=' : '.';
    if (ansi) fputs("\033[H", stdout);
    printf("Nocturne components / motion     %6.2f s\n", elapsed / 1000000.0);
    for (int y = 0; y < HEIGHT; ++y) {
        for (int x = 0; x < WIDTH; ++x) {
            if (ansi) printf("\033[38;2;%u;%u;%um", color[y][x] ? 70 : 125,
                             color[y][x] ? color[y][x] : 138, color[y][x] ? 230 : 156);
            putchar(cells[y][x]);
        }
        if (ansi) fputs("\033[0m", stdout);
        putchar('\n');
    }
    fflush(stdout);
    return !ferror(stdout);
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--animate") == 0) {
#if !defined(_WIN32)
        if (!isatty(STDOUT_FILENO)) {
            fputs("--animate requires a terminal; use a millisecond value for plain output.\n", stderr);
            return 2;
        }
        struct timespec start, now, delay = {0, 33333333};
        if (clock_gettime(CLOCK_MONOTONIC, &start)) return 1;
        NtPlayback playback;
        uint64_t start_us = (uint64_t)start.tv_sec * 1000000 + (uint64_t)start.tv_nsec / 1000;
        if (nt_playback_init(&playback, start_us) != NT_OK) return 1;
        signal(SIGINT, stop_animation); signal(SIGTERM, stop_animation);
        atexit(restore_terminal); terminal_active = 1;
        fputs("\033[?1049h\033[2J\033[H\033[?25l", stdout);
        while (!stopped) {
            if (clock_gettime(CLOCK_MONOTONIC, &now)) return 1;
            uint64_t elapsed, now_us = (uint64_t)now.tv_sec * 1000000 + (uint64_t)now.tv_nsec / 1000;
            if (nt_playback_elapsed(&playback, now_us, &elapsed) != NT_OK) return 1;
            if (elapsed >= 6000000) break;
            if (!frame(elapsed, 1)) return 1;
            if (nanosleep(&delay, NULL) && errno != EINTR) return 1;
        }
        return 0;
#else
        fputs("The timed example currently supports POSIX hosts.\n", stderr);
        return 2;
#endif
    }
    uint64_t elapsed = 750000;
    if (argc > 1) {
        char *end;
        errno = 0;
        if (argc != 2 || argv[1][0] < '0' || argv[1][0] > '9') goto usage;
        unsigned long long milliseconds = strtoull(argv[1], &end, 10);
        if (errno || *end || milliseconds > UINT64_MAX / 1000) goto usage;
        elapsed = (uint64_t)milliseconds * 1000;
    }
    return frame(elapsed, 0) ? 0 : 1;
usage:
    fputs("Usage: nocturne_motion [elapsed_ms | --animate]\n", stderr);
    return 2;
}
