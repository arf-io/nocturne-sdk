#ifndef NOCTURNE_POSIX_HOST_H
#define NOCTURNE_POSIX_HOST_H

#include <nocturne/component.h>
#include <nocturne/input.h>
#include <nocturne/present.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NtPosixHost NtPosixHost;

typedef struct NtPosixTerminalHints {
    const char *term, *color_term, *locale;
    bool no_color;
} NtPosixTerminalHints;

typedef struct NtPosixTerminalReport {
    NtTerminalCapabilities capabilities;
    bool input_tty, output_tty, host_available;
} NtPosixTerminalReport;

/* Inspect descriptors without changing tty state or sending a query. Pass
 * -1 for absent input. NULL hints use TERM, COLORTERM, the locale environment
 * and NO_COLOR as defaults. The caller can override the suggested settings. */
NtResult nt_posix_terminal_probe(int input_fd, int output_fd, const NtPosixTerminalHints *hints,
                                 NtPosixTerminalReport *out);

typedef NtResult (*NtReservedKey)(void *context, const NtEvent *key, NtByteView key_text);
typedef NtResult (*NtPosixAfterInput)(void *context, const NtDispatchResult *dispatch);
typedef NtResult (*NtPosixAfterResize)(void *context, NtCellSize size);

typedef struct NtPosixHostOptions {
    int input_fd, output_fd;
    NtTerminalCapabilities capabilities;
    NtRgba matte;
    NtPaletteView palette;
    const NtInputOptions *input;
    NtMotionPolicy motion;
    int output_timeout_ms;
    bool alternate_screen, hide_cursor, report_focus, catch_signals;
    NtReservedKey reserved_key;
    void *reserved_context;
    /* Optional. Called after each successful dispatch and reserved-key callback,
     * before the next event or frame. May drain intents and mutate the tree. */
    NtPosixAfterInput after_input;
    void *input_context;
} NtPosixHostOptions;

typedef struct NtPosixStep {
    NtCellSize size;
    size_t input_events;
    bool resized, presented;
    int signal_number;
    NtResult cleanup_result;
} NtPosixStep;

/* Available from Nocturne::posix_host. One open adapter per process. The
 * caller serializes access and lends the tree, terminal descriptors and
 * palette until close. Options and input options are copied during open. */
NtResult nt_posix_host_open(NtTree *tree, NtTimeUs now, const NtAllocator *allocator,
                            const NtPosixHostOptions *options, NtPosixHost **out);

/* Set an optional callback after the first size or a changed size is
 * published and dispatched, before the input batch. A NULL callback clears
 * it. The host borrows context until the callback is replaced or closed. */
NtResult nt_posix_host_set_after_resize(NtPosixHost *host, NtPosixAfterResize callback, void *context);

/* Drive one update, terminal size check, bounded input batch and presentation
 * using nondecreasing caller time. The input check uses no wait. Output can
 * wait up to output_timeout_ms for each blocked write. A processing failure
 * or caught signal closes the adapter before returning. On those paths the
 * report also gives the cleanup result. */
NtResult nt_posix_host_step(NtPosixHost *host, NtTimeUs now, const NtDispatchPolicy *policy,
                            NtPosixStep *out);
bool nt_posix_host_input_deadline(const NtPosixHost *host, NtTimeUs *out);

/* Cancel the current interaction and restore terminal settings. Close is
 * idempotent. Destroy also closes and releases storage. */
NtResult nt_posix_host_close(NtPosixHost *host);
void nt_posix_host_destroy(NtPosixHost *host);

#ifdef __cplusplus
}
#endif
#endif
