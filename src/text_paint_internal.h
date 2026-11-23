#ifndef NOCTURNE_INTERNAL_TEXT_PAINT_H
#define NOCTURNE_INTERNAL_TEXT_PAINT_H

#include <nocturne/text_effect.h>

typedef NtResult (*NtTextStyleResolve)(void *context, NtClusterView cluster, NtTextPaintStyle *style);
NtResult nt_text_paint_resolved(const NtSurfaceView *view, NtCellPoint position,
                                const NtTextLayout *layout, const NtTextPaintOptions *options,
                                NtTextStyleResolve resolve, void *context, bool mounted, NtOwnerToken owner);

/* Paint directly into a frame candidate whose caller handles publication. */
NtResult nt_text_paint_candidate(const NtSurfaceView *view, NtCellPoint position,
                                 const NtTextLayout *layout, const NtTextPaintOptions *options,
                                 NtOwnerToken owner);

NtResult nt_text_effect_paint_owner(const NtSurfaceView *view, NtCellPoint position,
                                    const NtTextLayout *layout, const NtTextPaintOptions *options,
                                    const NtTextEffect *effect, NtOwnerToken owner);

#endif
