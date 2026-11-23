#define _XOPEN_SOURCE 700

#include <nocturne/posix_host.h>

#include "allocator.h"
#include "present_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#define BYTES(s) ((NtByteView){s, sizeof(s) - 1})

static const int signals[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT};
#define SIGNAL_COUNT (sizeof(signals) / sizeof(signals[0]))

typedef struct TerminalMode { NtByteView enable, disable; } TerminalMode;
#define MODE(on, off) {{on, sizeof(on) - 1}, {off, sizeof(off) - 1}}
static const TerminalMode modes[] = {
    MODE("\x1b[?1049h", "\x1b[?1049l"),
    MODE("\x1b[?25l", "\x1b[?25h"),
    MODE("\x1b[?1004h", "\x1b[?1004l"),
    MODE("\x1b[?2004h", "\x1b[?2004l"),
    MODE("\x1b[?1002h", "\x1b[?1002l"),
    MODE("\x1b[?1006h", "\x1b[?1006l"),
};
#define MODE_COUNT (sizeof(modes) / sizeof(modes[0]))

struct NtPosixHost {
    NtAllocator allocator;
    NtTree *tree;
    NtInputDecoder *decoder;
    NtPosixHostOptions options;
    NtPosixAfterResize after_resize;
    void *resize_context;
    NtAnsiPresenter presenter;
    NtTimeUs time;
    NtCellSize size;
    struct termios prior_terminal;
    struct sigaction prior_signals[SIGNAL_COUNT];
    size_t handlers;
    unsigned enabled_modes;
    int prior_output_flags;
    bool open, busy, restoring, terminal_changed, flags_changed;
    bool sized, presentation_started;
};

NT_DECLARE_ALIGNMENT(NtPosixHost);

static NtPosixHost *owner;
static volatile sig_atomic_t received_signal;

static void remember_signal(int signal_number)
{
    received_signal = signal_number;
}

static int pending_signal(const NtPosixHost *host)
{
    return host->handlers && !host->restoring ? (int)received_signal : 0;
}

static NtResult write_terminal(void *context, NtByteView bytes)
{
    const NtPosixHost *host = context;
    const int fd = host->options.output_fd;
    size_t offset = 0;
    while (offset < bytes.count && !pending_signal(host)) {
        const ssize_t count = write(fd, bytes.data + offset, bytes.count - offset);
        if (count > 0) {
            offset += (size_t)count;
        } else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct pollfd output = {.fd = fd, .events = POLLOUT};
            int ready;
            do {
                ready = poll(&output, 1, host->options.output_timeout_ms);
            } while (ready < 0 && errno == EINTR && !pending_signal(host));
            if (ready <= 0 || !(output.revents & POLLOUT) ||
                (output.revents & (POLLERR | POLLHUP | POLLNVAL)))
                return NT_RESULT_TRANSPORT_FAILED;
        } else if (count == 0 || errno != EINTR) {
            return NT_RESULT_TRANSPORT_FAILED;
        }
    }
    return offset == bytes.count ? NT_RESULT_OK : NT_RESULT_TRANSPORT_FAILED;
}

static int set_terminal(int fd, const struct termios *settings)
{
    int result;
    do {
        result = tcsetattr(fd, TCSANOW, settings);
    } while (result < 0 && errno == EINTR);
    return result;
}

static NtResult restore(NtPosixHost *host)
{
    host->restoring = true;
    NtResult result = NT_RESULT_OK;
    if (host->presentation_started && write_terminal(host, BYTES("\x1b[0m")) != NT_RESULT_OK)
        result = NT_RESULT_TRANSPORT_FAILED;
    for (size_t i = MODE_COUNT; i > 0; --i) {
        if ((host->enabled_modes & (1u << (i - 1))) &&
            write_terminal(host, modes[i - 1].disable) != NT_RESULT_OK)
            result = NT_RESULT_TRANSPORT_FAILED;
    }
    if (host->terminal_changed && set_terminal(host->options.input_fd, &host->prior_terminal) < 0)
        result = NT_RESULT_TRANSPORT_FAILED;
    if (host->flags_changed &&
        fcntl(host->options.output_fd, F_SETFL, host->prior_output_flags) < 0)
        result = NT_RESULT_TRANSPORT_FAILED;
    while (host->handlers) {
        const size_t i = --host->handlers;
        if (sigaction(signals[i], &host->prior_signals[i], NULL) < 0)
            result = NT_RESULT_TRANSPORT_FAILED;
    }
    owner = NULL;
    return result;
}

static NtResult start_terminal(NtPosixHost *host)
{
    const NtPosixHostOptions *o = &host->options;
    if (tcgetattr(o->input_fd, &host->prior_terminal) < 0) return NT_RESULT_TRANSPORT_FAILED;
    if (o->catch_signals) {
        struct sigaction action = {0};
        action.sa_handler = remember_signal;
        sigemptyset(&action.sa_mask);
        for (size_t i = 0; i < SIGNAL_COUNT; ++i) sigaddset(&action.sa_mask, signals[i]);
        while (host->handlers < SIGNAL_COUNT) {
            const size_t i = host->handlers;
            if (sigaction(signals[i], &action, &host->prior_signals[i]) < 0)
                return NT_RESULT_TRANSPORT_FAILED;
            ++host->handlers;
        }
    }

    if (!(host->prior_output_flags & O_NONBLOCK)) {
        host->flags_changed = true;
        if (fcntl(o->output_fd, F_SETFL, host->prior_output_flags | O_NONBLOCK) < 0)
            return NT_RESULT_TRANSPORT_FAILED;
    }
    struct termios raw = host->prior_terminal;
    raw.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF | INPCK);
    raw.c_oflag &= ~OPOST;
    raw.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    raw.c_cflag = (raw.c_cflag & ~(CSIZE | PARENB)) | CS8;
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    host->terminal_changed = true;
    if (set_terminal(o->input_fd, &raw) < 0) return NT_RESULT_TRANSPORT_FAILED;

    const bool enable[MODE_COUNT] = {o->alternate_screen, o->hide_cursor, o->report_focus,
                                    o->capabilities.bracketed_paste, o->capabilities.pointer,
                                    o->capabilities.pointer};
    for (size_t i = 0; i < MODE_COUNT; ++i) {
        if (!enable[i]) continue;
        host->enabled_modes |= 1u << i;
        const NtResult result = write_terminal(host, modes[i].enable);
        if (result != NT_RESULT_OK) return result;
    }
    return pending_signal(host) ? NT_RESULT_TRANSPORT_FAILED : NT_RESULT_OK;
}

typedef struct TerminalEndpoint {
    int fd, flags;
    dev_t device;
    bool tty;
} TerminalEndpoint;

static NtResult terminal_endpoint(int fd, TerminalEndpoint *out)
{
    const int flags = fcntl(fd, F_GETFL);
    struct stat status;
    if (flags < 0 || fstat(fd, &status) < 0) return NT_RESULT_TRANSPORT_FAILED;
    *out = (TerminalEndpoint){fd, flags, status.st_rdev, isatty(fd) && !ptsname(fd)};
    return NT_RESULT_OK;
}

static bool same_terminal(TerminalEndpoint input, TerminalEndpoint output)
{
    if (!input.tty || !output.tty) return false;
    if (input.device == output.device) return true;
    /* /dev/tty and its concrete device can name the same controlling tty. */
    const pid_t session = getsid(0);
    return session >= 0 && tcgetsid(input.fd) == session && tcgetsid(output.fd) == session;
}

static bool family_list(const char *term, const char *const *families, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        const size_t length = strlen(families[i]);
        if (strncmp(term, families[i], length) == 0 && (!term[length] || term[length] == '-')) return true;
    }
    return false;
}

static bool ends_with(const char *value, const char *suffix)
{
    const size_t length = strlen(value), tail = strlen(suffix);
    return length >= tail && strcmp(value + length - tail, suffix) == 0;
}

static bool ascii_equals(const char *value, size_t length, const char *lower)
{
    if (length != strlen(lower)) return false;
    for (size_t i = 0; i < length; ++i) {
        const char c = value[i];
        if ((c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c) != lower[i]) return false;
    }
    return true;
}

static bool utf8_locale(const char *locale)
{
    if (!locale) return false;
    const char *end = locale + strcspn(locale, "@");
    const char *period = memchr(locale, '.', (size_t)(end - locale));
    const char *encoding = period ? period + 1 : locale;
    const size_t length = (size_t)(end - encoding);
    return ascii_equals(encoding, length, "utf-8") || ascii_equals(encoding, length, "utf8");
}

static NtPosixTerminalHints terminal_environment(void)
{
    const char *locale = getenv("LC_ALL");
    if (!locale || !*locale) locale = getenv("LC_CTYPE");
    if (!locale || !*locale) locale = getenv("LANG");
    const char *no_color = getenv("NO_COLOR");
    return (NtPosixTerminalHints){getenv("TERM"), getenv("COLORTERM"), locale, no_color && *no_color};
}

NtResult nt_posix_terminal_probe(int input_fd, int output_fd, const NtPosixTerminalHints *hints,
                                 NtPosixTerminalReport *out)
{
    if (!out || input_fd < -1 || output_fd < 0) return NT_RESULT_INVALID_ARGUMENT;
    TerminalEndpoint input = {0}, output;
    NtResult result = terminal_endpoint(output_fd, &output);
    if (result != NT_RESULT_OK) return result;
    if (input_fd != -1) {
        result = terminal_endpoint(input_fd, &input);
        if (result != NT_RESULT_OK) return result;
    }
    const NtPosixTerminalHints selected = hints ? *hints : terminal_environment();
    const char *term = selected.term ? selected.term : "";
    static const char *const enhanced[] = {
        "xterm", "screen", "tmux", "rxvt", "alacritty", "foot", "kitty", "wezterm", "konsole", "st", "putty",
    };
    static const char *const basic[] = {"ansi", "vt100", "vt102", "vt220", "linux", "cygwin"};
    const bool extensions = family_list(term, enhanced, sizeof(enhanced) / sizeof(*enhanced));
    const bool ansi = extensions || family_list(term, basic, sizeof(basic) / sizeof(*basic));
    const bool interactive = ansi && output.tty && (output.flags & O_ACCMODE) != O_RDONLY;
    const bool can_host = interactive && (input.flags & O_ACCMODE) != O_WRONLY && same_terminal(input, output);
    const bool utf8 = utf8_locale(selected.locale);
    NtColorMode color = NT_COLOR_MONO;
    if (interactive && !selected.no_color) {
        const char *hint = selected.color_term ? selected.color_term : "";
        const size_t length = strlen(hint);
        if (ascii_equals(hint, length, "truecolor") || ascii_equals(hint, length, "24bit") ||
            ends_with(term, "-direct") || ends_with(term, "-truecolor")) color = NT_COLOR_TRUECOLOR;
        else if (ends_with(term, "-256color")) color = NT_COLOR_INDEXED;
    }
    *out = (NtPosixTerminalReport){
        .capabilities = {
            .color = color, .interactive = interactive, .utf8 = utf8,
            .half_blocks = utf8 && (!interactive || extensions),
            .braille = utf8 && (!interactive || extensions),
            .pointer = can_host && extensions, .bracketed_paste = can_host && extensions,
            .width = {NT_WIDTH_ONE, NT_WIDTH_TWO},
        },
        .input_tty = input.tty, .output_tty = output.tty, .host_available = can_host,
    };
    return NT_RESULT_OK;
}

NtResult nt_posix_host_open(NtTree *tree, NtTimeUs now, const NtAllocator *provided,
                            const NtPosixHostOptions *options, NtPosixHost **out)
{
    NtAllocator allocator;
    if (!tree || !options || !out || !nt_allocator_get(provided, &allocator) ||
        options->input_fd < 0 || options->output_fd < 0 || options->output_timeout_ms < 0 ||
        (options->motion != NT_MOTION_FULL && options->motion != NT_MOTION_REDUCED) ||
        !nt_ansi_capabilities_valid(options->capabilities, options->palette))
        return NT_RESULT_INVALID_ARGUMENT;
    if (owner) return NT_RESULT_WRONG_PHASE;
    if (!options->capabilities.interactive) return NT_RESULT_UNSUPPORTED_CAPABILITY;

    TerminalEndpoint input, output;
    NtResult result = terminal_endpoint(options->input_fd, &input);
    if (result != NT_RESULT_OK) return result;
    result = terminal_endpoint(options->output_fd, &output);
    if (result != NT_RESULT_OK) return result;
    if (!same_terminal(input, output)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    if ((input.flags & O_ACCMODE) == O_WRONLY || (output.flags & O_ACCMODE) == O_RDONLY)
        return NT_RESULT_INVALID_ARGUMENT;

    NtPosixHost *host = nt_allocate(&allocator, sizeof(*host), NT_ALIGNMENT(NtPosixHost));
    if (!host) return NT_RESULT_ALLOCATION_FAILED;
    *host = (NtPosixHost){.allocator = allocator, .tree = tree, .options = *options,
                          .time = now, .prior_output_flags = output.flags};
    host->options.input = NULL;
    host->presenter = (NtAnsiPresenter){options->capabilities, options->matte, options->palette,
                                       write_terminal, host};
    result = nt_input_create(provided, options->input, &host->decoder);
    if (result == NT_RESULT_OK) {
        received_signal = 0;
        owner = host;
        result = start_terminal(host);
        if (result != NT_RESULT_OK && restore(host) != NT_RESULT_OK)
            result = NT_RESULT_TRANSPORT_FAILED;
    }
    if (result != NT_RESULT_OK) {
        nt_input_destroy(host->decoder);
        nt_release(&allocator, host, sizeof(*host), NT_ALIGNMENT(NtPosixHost));
        return result;
    }
    host->open = true;
    *out = host;
    return NT_RESULT_OK;
}

static NtResult close_host(NtPosixHost *host)
{
    host->open = false;
    const NtResult restored = restore(host);
    NtEvent cancel;
    NtResult result = nt_input_cancel(host->decoder, host->time, NT_CANCEL_HOST, &cancel);
    if (result == NT_RESULT_OK) {
        NtDispatchResult discarded;
        result = nt_tree_dispatch(host->tree, &cancel, (NtByteView){0}, NULL, &discarded);
    }
    return restored != NT_RESULT_OK ? restored : result;
}

NtResult nt_posix_host_close(NtPosixHost *host)
{
    if (!host) return NT_RESULT_INVALID_ARGUMENT;
    if (host->busy) return NT_RESULT_WRONG_PHASE;
    if (!host->open) return NT_RESULT_OK;
    host->busy = true;
    const NtResult result = close_host(host);
    host->busy = false;
    return result;
}

void nt_posix_host_destroy(NtPosixHost *host)
{
    if (!host || host->busy) return;
    (void)nt_posix_host_close(host);
    nt_input_destroy(host->decoder);
    const NtAllocator allocator = host->allocator;
    nt_release(&allocator, host, sizeof(*host), NT_ALIGNMENT(NtPosixHost));
}

NtResult nt_posix_host_set_after_resize(NtPosixHost *host, NtPosixAfterResize callback, void *context)
{
    if (!host) return NT_RESULT_INVALID_ARGUMENT;
    if (!host->open || host->busy) return NT_RESULT_WRONG_PHASE;
    host->after_resize = callback;
    host->resize_context = callback ? context : NULL;
    return NT_RESULT_OK;
}

static NtResult resize_terminal(NtPosixHost *host, NtPosixStep *step)
{
    struct winsize terminal_size;
    int status;
    do {
        status = ioctl(host->options.input_fd, TIOCGWINSZ, &terminal_size);
    } while (status < 0 && errno == EINTR && !pending_signal(host));
    if (status < 0) return NT_RESULT_TRANSPORT_FAILED;

    const NtCellSize size = {terminal_size.ws_col, terminal_size.ws_row};
    if (host->sized && size.columns == host->size.columns && size.rows == host->size.rows)
        return NT_RESULT_OK;
    NtResult result = nt_tree_publish(host->tree, size);
    if (result != NT_RESULT_OK) return result;
    host->sized = true;
    host->size = step->size = size;
    step->resized = true;
    const NtEvent event = {.kind = NT_EVENT_RESIZE, .at = host->time, .data.resize = size};
    NtDispatchResult discarded;
    result = nt_tree_dispatch(host->tree, &event, (NtByteView){0}, NULL, &discarded);
    if (result == NT_RESULT_OK && host->after_resize && !pending_signal(host))
        result = host->after_resize(host->resize_context, size);
    return result;
}

static NtResult read_terminal(NtPosixHost *host, const NtDispatchPolicy *policy, NtPosixStep *step)
{
    char buffer[4096];
    struct pollfd input = {.fd = host->options.input_fd, .events = POLLIN};
    int ready;
    do {
        ready = poll(&input, 1, 0);
    } while (ready < 0 && errno == EINTR && !pending_signal(host));
    if (ready < 0 || (input.revents & (POLLERR | POLLHUP)))
        return NT_RESULT_TRANSPORT_FAILED;

    ssize_t count = 0;
    /* Some controlling tty aliases cannot be polled. The raw read uses VMIN=0. */
    if (input.revents & (POLLIN | POLLNVAL)) {
        do {
            count = read(input.fd, buffer, sizeof(buffer));
        } while (count < 0 && errno == EINTR && !pending_signal(host));
        if (count < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) return NT_RESULT_TRANSPORT_FAILED;
            count = 0;
        }
    }

    size_t offset = 0;
    while (!pending_signal(host)) {
        NtInputRead readout;
        NtResult result = nt_input_read(host->decoder,
            (NtByteView){buffer + offset, (size_t)count - offset}, host->time, &readout);
        if (result != NT_RESULT_OK) return result;
        offset += readout.consumed;
        if (readout.status == NT_INPUT_PENDING && offset == (size_t)count) break;
        if (readout.status != NT_INPUT_READY) continue;
        const NtEvent *event = &readout.event;
        const NtEventKind kind = event->kind;
        if (kind == NT_EVENT_RESIZE || (kind == NT_EVENT_PASTE && !host->options.capabilities.bracketed_paste) ||
            (!host->options.capabilities.pointer && kind >= NT_EVENT_POINTER_DOWN && kind <= NT_EVENT_WHEEL))
            continue;
        NtDispatchResult dispatch;
        result = nt_tree_dispatch(host->tree, event, readout.key_text, policy, &dispatch);
        if (result != NT_RESULT_OK) return result;
        ++step->input_events;
        if (dispatch.reserved && host->options.reserved_key) {
            result = host->options.reserved_key(host->options.reserved_context, event, readout.key_text);
            if (result != NT_RESULT_OK) return result;
        }
        if (host->options.after_input) {
            result = host->options.after_input(host->options.input_context, &dispatch);
            if (result != NT_RESULT_OK) return result;
        }
    }
    return NT_RESULT_OK;
}

static NtResult drive(NtPosixHost *host, const NtDispatchPolicy *policy, NtPosixStep *step)
{
    if (pending_signal(host)) return NT_RESULT_OK;
    NtResult result = nt_tree_tick(host->tree, host->time, host->options.motion);
    if (result != NT_RESULT_OK || pending_signal(host)) return result;
    result = resize_terminal(host, step);
    if (result != NT_RESULT_OK || pending_signal(host)) return result;
    result = read_terminal(host, policy, step);
    if (result != NT_RESULT_OK || pending_signal(host)) return result;

    const bool dirty = nt_tree_pending(host->tree) != NT_INVALIDATE_NONE;
    if (dirty) {
        result = nt_tree_publish(host->tree, host->size);
        if (result != NT_RESULT_OK || pending_signal(host)) return result;
    }
    if (!dirty && !step->resized) return NT_RESULT_OK;
    host->presentation_started = true;
    result = nt_ansi_present(&host->presenter, nt_tree_frame(host->tree), host->time);
    step->presented = result == NT_RESULT_OK;
    return result;
}

NtResult nt_posix_host_step(NtPosixHost *host, NtTimeUs now, const NtDispatchPolicy *policy,
                            NtPosixStep *out)
{
    if (!host || !out) return NT_RESULT_INVALID_ARGUMENT;
    if (!host->open || host->busy) return NT_RESULT_WRONG_PHASE;
    if (now < host->time) return NT_RESULT_OUT_OF_RANGE;

    host->busy = true;
    host->time = now;
    NtPosixStep step = {.size = host->size};
    NtResult result = drive(host, policy, &step);
    step.signal_number = pending_signal(host);
    if (result != NT_RESULT_OK || step.signal_number) {
        step.cleanup_result = close_host(host);
        if (step.signal_number) result = step.cleanup_result;
    }
    host->busy = false;
    *out = step;
    return result;
}

bool nt_posix_host_input_deadline(const NtPosixHost *host, NtTimeUs *out)
{
    return host && host->open && nt_input_deadline(host->decoder, out);
}
