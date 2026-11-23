#ifndef NOCTURNE_FRAME_H
#define NOCTURNE_FRAME_H

#include <nocturne/base.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NtFrame NtFrame;
typedef uint64_t NtOwnerToken;
typedef struct NtCellStyle { NtRgba foreground, background; } NtCellStyle;
typedef struct NtCellBackground { NtRgba color; NtOwnerToken owner; } NtCellBackground;

typedef struct NtCellView {
    NtByteView glyph;
    NtCellPoint lead;
    uint8_t glyph_columns;
    NtCellStyle style;
    NtOwnerToken owner;
} NtCellView;

/* Create an empty frame. Coordinates are nonnegative and zero-based.
 * Each extent is at most INT32_MAX+1. Either extent may be zero. The
 * caller serializes access to each frame. Failed calls leave outputs,
 * frame contents and outstanding borrows unchanged. */
NtResult nt_frame_create(const NtAllocator *allocator, NtCellSize size, NtFrame **out);
void nt_frame_destroy(NtFrame *frame);
NtCellSize nt_frame_size(const NtFrame *frame);

/* Preserve complete glyphs at their existing coordinates. Drop any glyph
 * that would lose part of its footprint. New cells are empty. */
NtResult nt_frame_resize(NtFrame *frame, NtCellSize size);

/* A continuation resolves the lead coordinates, glyph and foreground.
 * Background is sampled at the requested physical cell. A background-only
 * cell has no glyph, zero foreground, and its own coordinates and owner.
 * A visible foreground supplies the cell owner when both are present.
 * Glyph borrows are valid until a successful mutation or destruction. */
NtResult nt_frame_cell(const NtFrame *frame, NtCellPoint position, NtCellView *out);
NtResult nt_frame_background(const NtFrame *frame, NtCellPoint position, NtCellBackground *out);

/* Copy one renderable extended grapheme, with the caller's chosen one-
 * or two-cell footprint. Start on an empty cell or a glyph lead. A
 * starting continuation is invalid. Overlapping glyphs are removed in
 * full. Bounds, placement and allocation are checked before mutation.
 * The glyph may borrow from this frame. Owner zero means no owner. */
NtResult nt_frame_set_glyph(NtFrame *frame, NtCellPoint lead, NtByteView glyph,
                           uint8_t columns, NtCellStyle style, NtOwnerToken owner);

/* Clear the whole footprint at a lead or continuation, including styles
 * and owners. With no glyph, clear the background of the physical cell. */
NtResult nt_frame_clear_glyph(NtFrame *frame, NtCellPoint cell);

#ifdef __cplusplus
}
#endif
#endif
