#ifndef NOCTURNE_PAINT_H
#define NOCTURNE_PAINT_H

#include <nocturne/present.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NtPaintContext NtPaintContext;
/* The returned clip is in component-local coordinates. */
NtCellRect nt_paint_clip(const NtPaintContext *paint);
NtResult nt_paint_glyph(NtPaintContext *paint, NtCellPoint lead, NtByteView glyph,
                       uint8_t columns, NtCellStyle style);
NtResult nt_paint_fill(NtPaintContext *paint, NtCellRect bounds, NtRgba color);
NtResult nt_paint_erase_glyph(NtPaintContext *paint, NtCellPoint lead);

#ifdef __cplusplus
}
#endif
#endif
