#ifndef NOCTURNE_WINDOWS_HOST_H
#define NOCTURNE_WINDOWS_HOST_H

#include <nocturne/component.h>
#include <nocturne/present.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NtWindowsHost NtWindowsHost;
typedef struct NtWindowsTerminalReport {
    NtTerminalCapabilities capabilities;
    bool input_console, output_console, host_available;
} NtWindowsTerminalReport;

/* Handles are Win32 HANDLE values. Pass NULL for absent input. A probe
 * inspects endpoints without enabling VT mode or querying the terminal. */
NtResult nt_windows_terminal_probe(void *input_handle, void *output_handle,
                                   NtWindowsTerminalReport *out);

typedef NtResult (*NtWindowsReservedKey)(void *context, const NtEvent *key, NtByteView key_text);
typedef NtResult (*NtWindowsAfterInput)(void *context, const NtDispatchResult *dispatch);
typedef NtResult (*NtWindowsAfterResize)(void *context, NtCellSize size);

typedef struct NtWindowsHostOptions {
    void *input_handle, *output_handle;
    NtTerminalCapabilities capabilities;
    NtRgba matte;
    NtPaletteView palette;
    NtMotionPolicy motion;
    bool alternate_screen, hide_cursor;
    NtWindowsReservedKey reserved_key;
    void *reserved_context;
    NtWindowsAfterInput after_input;
    void *input_context;
    NtWindowsAfterResize after_resize;
    void *resize_context;
} NtWindowsHostOptions;

typedef struct NtWindowsStep {
    NtCellSize size;
    size_t input_events;
    bool resized, presented;
    NtResult cleanup_result;
} NtWindowsStep;

/* Available from Nocturne::windows_host. The caller supplies an attached
 * console input buffer and its active screen buffer, and lends the handles,
 * tree, palette and callback contexts until close. One open host per process.
 * The caller serializes use, provides exclusive console I/O, and handles
 * process termination. A console control handler can request close on the
 * thread driving the host. */
NtResult nt_windows_host_open(NtTree *tree, NtTimeUs now, const NtAllocator *allocator,
                              const NtWindowsHostOptions *options, NtWindowsHost **out);

/* Advance on nondecreasing caller time, publish a changed viewport,
 * dispatch a bounded batch of available records, and present if dirty.
 * Console writes are synchronous. A processing failure closes the host. */
NtResult nt_windows_host_step(NtWindowsHost *host, NtTimeUs now, const NtDispatchPolicy *policy,
                              NtWindowsStep *out);

/* Cancel interaction and restore console settings. Close is idempotent. */
NtResult nt_windows_host_close(NtWindowsHost *host);
void nt_windows_host_destroy(NtWindowsHost *host);

#ifdef __cplusplus
}
#endif
#endif
