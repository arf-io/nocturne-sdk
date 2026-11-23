#ifndef NOCTURNE_PANEL_TRANSFORM_H
#define NOCTURNE_PANEL_TRANSFORM_H

#include <nocturne/motion.h>
#include <nocturne/paint.h>
#include <nocturne/surface.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NtPanelPose {
    NtPoint anchor, offset, scale; /* Cell coordinates. Identity scale is {1,1}. */
    double turns, opacity;        /* Positive turns rotate clockwise in the display. */
} NtPanelPose;

typedef struct NtPanelTransition {
    NtPanelPose from, to;
    NtTimeUs delay_us, duration_us;
    NtEasing easing;
} NtPanelTransition;

/* Scale around anchor, rotate around anchor, then translate by offset. */
NtResult nt_panel_project_point(const NtPanelPose *pose, NtPoint source, NtPoint *out);
/* Sample a caller-owned clock. Reduced motion selects the authored target. */
NtResult nt_panel_pose_at(const NtPanelTransition *transition, NtTimeUs elapsed_us,
                         NtMotionPolicy motion, NtPanelPose *out);

typedef struct NtPanelSample {
    NtPoint destination, source; /* Viewport-local center and inverse-mapped region-local point. */
    NtCellSize viewport_size, region_size;
    NtTimeUs elapsed_us;
} NtPanelSample;

typedef struct NtPanelPixel {
    NtPoint source;              /* Region-local sample position, before rounding down. */
    double opacity;              /* In [0,1]. Zero skips the cell. */
} NtPanelPixel;

typedef NtResult (*NtPanelRemap)(void *context, const NtPanelSample *sample, NtPanelPixel *pixel);

typedef struct NtPanelLayer {
    const NtFrame *source;
    NtCellRect region;           /* Source-frame crop. */
    NtCellRect viewport;         /* Destination view-local placement and fixed clip. */
    NtPanelPose pose;
    NtTimeUs elapsed_us;
    const NtSurfaceMask *mask;   /* Viewport-local fixed mask. */
    NtPanelRemap remap;
    void *remap_context;
    NtSurfaceOwnerPolicy owners;
    NtSurfaceMapOwner map_owner;
    void *owner_context;
} NtPanelLayer;

/* Sample layers in array order and publish atomically. Borrow all sources and
 * callbacks for the call. A source may alias the destination, reading its
 * pre-call state. Keep all inputs stable while callbacks run. */
NtResult nt_surface_project_panels(const NtSurfaceView *view, const NtPanelLayer *layers, size_t count);

/* The mounted node owns every contribution. Layout and hit geometry are
 * controlled by the component's layout callback. */
NtResult nt_paint_project_panels(NtPaintContext *paint, const NtPanelLayer *layers, size_t count);

#ifdef __cplusplus
}
#endif
#endif
