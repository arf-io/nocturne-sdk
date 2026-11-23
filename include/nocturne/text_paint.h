#ifndef NOCTURNE_TEXT_PAINT_H
#define NOCTURNE_TEXT_PAINT_H

#include <nocturne/paint_sample.h>
#include <nocturne/surface.h>
#include <nocturne/text_layout.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NtTextPaintStyle {
    NtCellStyle color;
    NtOwnerToken owner;
    const NtPaint *foreground; /* Optional override sampled at the glyph lead. */
    const NtPaint *background;
} NtTextPaintStyle;

typedef struct NtTextPaintSpan {
    NtByteSpan source;
    NtTextPaintStyle style;
} NtTextPaintSpan;

typedef struct NtTextPaintOptions {
    NtTextPaintStyle base;
    const NtTextPaintSpan *spans;
    size_t count;
    NtTimeUs elapsed_us;
    NtMotionPolicy motion;
    NtGamutPolicy gamut;
} NtTextPaintOptions;

/* Paint one immutable layout in a single publication. Placement is local to
 * the view. A span applies to the first source byte of a cluster and the
 * last matching span wins. Ellipsis uses the base style. Tabs paint spaces
 * with their logical cluster's style. Any error preserves the destination. */
NtResult nt_text_paint(const NtSurfaceView *view, NtCellPoint position,
                       const NtTextLayout *layout, const NtTextPaintOptions *options);

#ifdef __cplusplus
}
#endif
#endif
