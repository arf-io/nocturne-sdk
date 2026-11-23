#ifndef NOCTURNE_WINDOW_H
#define NOCTURNE_WINDOW_H

#include <nocturne/animation.h>
#include <nocturne/widgets.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { NT_WINDOW_MAX_WINDOWS = 64 };

typedef enum NtWindowMode { NT_WINDOW_NORMAL, NT_WINDOW_MINIMIZED, NT_WINDOW_MAXIMIZED } NtWindowMode;
typedef enum NtWindowDock {
    NT_WINDOW_FLOAT, NT_WINDOW_DOCK_LEFT, NT_WINDOW_DOCK_RIGHT, NT_WINDOW_DOCK_TOP, NT_WINDOW_DOCK_BOTTOM
} NtWindowDock;

typedef struct NtWindowPlacement {
    NtCellRect floating;          /* Workspace-local, retained across dock and mode changes. */
    NtWindowMode mode;
    NtWindowDock dock;
    uint32_t dock_extent;
} NtWindowPlacement;
typedef struct NtWindowOptions {
    NtWindowPlacement placement;
    NtPanelOptions panel;         /* Copied title and body mask. One direct body child. */
    bool draggable, resizable;
} NtWindowOptions;
typedef struct NtWindowAnimation {
    NtCellRect destination;
    NtTimeUs start, duration;
    NtEasing easing;
} NtWindowAnimation;
typedef struct NtWindowState {
    NtWindowPlacement requested;
    NtNodeGeometry published;
    bool animating;
} NtWindowState;

NtWindowOptions nt_window_options_default(void);
/* A workspace owns up to NT_WINDOW_MAX_WINDOWS direct window children.
 * Docks reserve space in insertion order; floating windows and maximized
 * windows are clipped to the remaining workspace. */
NtResult nt_window_workspace_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount,
                                    NtCellSize preferred, NtNodeId *out);
NtResult nt_window_mount(NtTree *tree, NtNodeId workspace, NtMountOptions mount,
                          const NtWindowOptions *options, NtNodeId *out);
NtResult nt_window_set(NtTree *tree, NtNodeId node, NtWindowPlacement placement);
NtResult nt_window_set_panel(NtTree *tree, NtNodeId node, const NtPanelOptions *panel);
NtResult nt_window_get(const NtTree *tree, NtNodeId node, NtWindowState *out);
/* Raise a published window and transfer focus to the window (zero target)
 * or a published focusable descendant. The caller chooses the next focus
 * after removing or minimizing a window. */
NtResult nt_window_focus(NtTree *tree, NtNodeId node, NtNodeId target);
/* Begin or retarget from the current requested rectangle. Floating normal
 * windows animate from explicit tree ticks. Reduced motion settles at the
 * destination on or after the start time. */
NtResult nt_window_animate(NtTree *tree, NtNodeId node, NtWindowAnimation animation);

typedef struct NtPanelContentProvider {
    void *context;
    NtCellSize preferred;
    NtResult (*paint)(const void *context, NtPaintContext *paint, NtCellSize assigned);
    NtResult (*update)(void *context, NtUpdateInfo info, NtUpdateReply *reply);
    NtResult (*event)(void *context, NtEventContext *event_context,
                      const NtEvent *event, NtEventReply *reply);
    void (*dispose)(void *context);
} NtPanelContentProvider;
/* A leaf for caller-authored content inside an ordinary panel or window.
 * On success a non-NULL dispose callback takes ownership of context.
 * A failed mount leaves ownership with the caller. */
NtResult nt_panel_content_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount,
                                 const NtPanelContentProvider *provider, NtNodeId *out);

#ifdef __cplusplus
}
#endif
#endif
