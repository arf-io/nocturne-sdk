#ifndef NOCTURNE_INTERNAL_SURFACE_H
#define NOCTURNE_INTERNAL_SURFACE_H

#include <nocturne/surface.h>

NtResult nt_surface_limit(NtSurfaceView *view, NtCellPoint parent_origin, NtCellRect clip);

/* The view is initialized. Mask bounds are in frame coordinates. */
NtResult nt_surface_attach_mask(NtSurfaceView *view, const NtSurfaceMask *mask);

bool nt_surface_contains_frame(const NtSurfaceView *view, NtCellPoint position);

/* Isolate mask and blend arithmetic when requested, restoring before allocator calls. */
NtResult nt_surface_write_glyph(const NtSurfaceView *view, NtCellPoint local, NtByteView glyph,
                                uint8_t columns, NtCellStyle style, NtOwnerToken owner, bool isolate_math);

#endif
