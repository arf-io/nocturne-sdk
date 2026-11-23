#ifndef NOCTURNE_SURFACE_H
#define NOCTURNE_SURFACE_H

#include <nocturne/frame.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum NtSurfaceMaskKind {
    NT_MASK_RECTANGLE,
    NT_MASK_ELLIPSE,
    NT_MASK_DIAMOND,
    NT_MASK_ROUNDED_RECTANGLE,
    NT_MASK_ALPHA
} NtSurfaceMaskKind;

typedef struct NtSurfaceMask {
    NtSurfaceMaskKind kind;
    NtCellRect bounds;
    double radius;              /* Rounded rectangle, in cells. */
    const uint8_t *alpha;       /* Alpha mask, row-major. */
    size_t stride, count;       /* Alpha row stride and buffer size in bytes. */
} NtSurfaceMask;

/* Initialize through the functions below. Keep fields and parent views stable
 * while descendants use them. Views borrow the frame and any alpha bytes.
 * A frame may resize between calls. Existing views retain their original
 * bounds and are further clipped to the frame's current extent. */
typedef struct NtSurfaceView {
    NtFrame *frame;
    const struct NtSurfaceView *parent;
    NtCellRect bounds, clip;    /* Frame coordinates. */
    bool masked;
    NtSurfaceMask mask;        /* Captured bounds in frame coordinates. */
} NtSurfaceView;

NtResult nt_surface_root(NtFrame *frame, NtSurfaceView *out);

/* Placement is parent-local. Mask bounds are child-local. The descriptor is
 * copied, and alpha bytes are borrowed. A NULL mask has full coverage. */
NtResult nt_surface_child(const NtSurfaceView *parent, NtCellRect placement,
                          const NtSurfaceMask *mask, NtSurfaceView *out);
NtResult nt_surface_clip(const NtSurfaceView *view, NtCellRect *out_frame_bounds);
/* A positive mask product stays positive, including after underflow. */
NtResult nt_surface_coverage(const NtSurfaceView *view, NtCellPoint local, double *out);

/* Surface writes use linear-light source-over and byte-quantized coverage
 * for contribution. Background is independent in each physical cell.
 * The foreground takes the minimum mask coverage of a whole footprint.
 * Replacing or erasing a glyph requires every cell of each affected glyph
 * to be inside the effective clip and have positive mask coverage. */
NtResult nt_surface_fill(const NtSurfaceView *view, NtCellRect local,
                         NtRgba background, NtOwnerToken owner);
NtResult nt_surface_glyph(const NtSurfaceView *view, NtCellPoint local, NtByteView glyph,
                          uint8_t columns, NtCellStyle style, NtOwnerToken owner);
NtResult nt_surface_erase_glyph(const NtSurfaceView *view, NtCellPoint local);

typedef enum NtSurfaceOwnerPolicy {
    NT_SURFACE_OWNERS_CLEAR,
    NT_SURFACE_OWNERS_SHARED,
    NT_SURFACE_OWNERS_MAP
} NtSurfaceOwnerPolicy;

typedef NtResult (*NtSurfaceMapOwner)(void *context, NtOwnerToken source, NtOwnerToken *out);

typedef struct NtSurfaceLayer {
    const NtFrame *source;
    NtCellRect region;           /* Source frame coordinates. */
    NtCellPoint position;        /* Upper-left in the destination view. */
    double opacity;
    const NtSurfaceMask *mask;   /* Relative to the placed region. */
    NtSurfaceOwnerPolicy owners;
    NtSurfaceMapOwner map_owner;
    void *owner_context;
} NtSurfaceLayer;

/* Paint in array order. The entire batch publishes on success. Sources can
 * alias the destination and always read its pre-call state. The mapper is
 * called for contributing nonzero owners and must set its output on success.
 * It may fail the batch. It must not change inputs or reenter these frames. */
NtResult nt_surface_compose(const NtSurfaceView *view, const NtSurfaceLayer *layers, size_t count);

#ifdef __cplusplus
}
#endif
#endif
