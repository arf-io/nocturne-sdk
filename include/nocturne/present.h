#ifndef NOCTURNE_PRESENT_H
#define NOCTURNE_PRESENT_H

#include <nocturne/color.h>
#include <nocturne/frame.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum NtColorMode { NT_COLOR_MONO, NT_COLOR_INDEXED, NT_COLOR_TRUECOLOR } NtColorMode;

typedef struct NtTerminalCapabilities {
    NtColorMode color;
    bool interactive, utf8, half_blocks, braille, pointer, bracketed_paste;
    NtWidthPolicy width;
} NtTerminalCapabilities;

enum { NT_XTERM256_COLORS = 256 };
/* Fill the first 256 entries with the reference xterm palette. A terminal
 * with a custom palette can supply those entries directly instead. */
NtResult nt_xterm256_palette(NtRgba *colors, size_t count);

typedef struct NtFrameSink {
    void *context;
    NtResult (*present)(void *context, const NtFrame *frame, NtTimeUs now);
} NtFrameSink;

/* The sink borrows the frame during the call. The caller owns clock and I/O. */
NtResult nt_frame_present(const NtFrameSink *sink, const NtFrame *frame, NtTimeUs now);

typedef NtResult (*NtOutputWrite)(void *context, NtByteView bytes);

typedef struct NtAnsiPresenter {
    NtTerminalCapabilities capabilities;
    NtRgba matte;             /* RGB beneath transparent frame colors. */
    NtPaletteView palette;    /* Opaque entries in the terminal's index order. */
    NtOutputWrite write;
    void *write_context;
} NtAnsiPresenter;

/* Compatible with NtFrameSink.present. Context points to an NtAnsiPresenter.
 * Interactive output clears and addresses the display. Noninteractive
 * output writes plain rows. Write callbacks must consume the entire slice
 * before returning success. Transport failures can leave partial output. */
NtResult nt_ansi_present(void *context, const NtFrame *frame, NtTimeUs now);

#ifdef __cplusplus
}
#endif
#endif
