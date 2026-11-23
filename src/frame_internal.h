#ifndef NOCTURNE_INTERNAL_FRAME_H
#define NOCTURNE_INTERNAL_FRAME_H

#include <nocturne/frame.h>

typedef struct FrameCell {
    char *glyph;
    size_t bytes;
    NtRgba foreground;
    NtOwnerToken glyph_owner;
    NtCellBackground background;
    uint8_t columns;
    bool continuation, attached_background;
} FrameCell;

struct NtFrame {
    NtAllocator allocator;
    NtCellSize size;
    FrameCell *cells;
};

static inline size_t nt_frame_index(const NtFrame *frame, NtCellPoint cell)
{
    return (size_t)cell.y * frame->size.columns + (uint32_t)cell.x;
}

static inline size_t nt_frame_lead(const NtFrame *frame, size_t index)
{
    return index - frame->cells[index].continuation;
}

NtResult nt_frame_validate_glyph(NtByteView glyph);
NtResult nt_frame_copy_glyph(const NtFrame *frame, NtByteView glyph, char **out);
void nt_frame_remove_glyph(NtFrame *frame, size_t index, bool preserve_background);
void nt_frame_store_glyph(NtFrame *frame, size_t index, char *glyph, size_t bytes,
                          uint8_t columns, NtRgba foreground, NtOwnerToken owner,
                          bool preserve_background);
NtResult nt_frame_stage(const NtFrame *frame, NtFrame *out);
void nt_frame_discard(NtFrame *frame);
void nt_frame_publish(NtFrame *frame, NtFrame *stage);

#endif
