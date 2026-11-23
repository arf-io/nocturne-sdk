#ifndef NOCTURNE_VIEWPORT_H
#define NOCTURNE_VIEWPORT_H

#include <nocturne/layout.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NtViewportOptions {
    bool scroll_columns, scroll_rows;
    NtCellSize requested;       /* Nonnegative distances from the content origin. */
} NtViewportOptions;

typedef struct NtViewportView {
    bool placed;
    NtNodeId child;             /* Zero when no live content placement is published. */
    NtCellSize viewport, content, effective;
} NtViewportView;

/* Clamp each requested offset to the available scroll distance on an
 * enabled axis. Disabled axes always resolve to zero. */
NtCellSize nt_scroll_resolve(NtViewportOptions options, NtCellSize viewport,
                             NtCellSize content);

/* Mount a clipped viewport with zero or one direct child. The tree owns a
 * copy of the options. Enabled axes measure the child without a maximum. */
NtResult nt_viewport_mount(NtTree *tree, NtNodeId parent, NtMountOptions options,
                           NtViewportOptions viewport, NtNodeId *out);
/* Request a new configuration. Effective scroll changes on publication. */
NtResult nt_viewport_set(NtTree *tree, NtNodeId node, NtViewportOptions viewport);
/* Read the committed placement and scroll, including during callbacks. */
NtResult nt_viewport_view(const NtTree *tree, NtNodeId node, NtViewportView *out);

/* Anchor an assigned child size in a parent, then translate in cells.
 * Center rounds toward the leading edge. Oversized children may have a
 * negative origin. Errors leave out unchanged. */
NtResult nt_anchor_rect(NtCellSize parent, NtCellSize child,
                        NtBoxAlign horizontal, NtBoxAlign vertical,
                        NtCellPoint offset, NtCellRect *out);

#ifdef __cplusplus
}
#endif
#endif
