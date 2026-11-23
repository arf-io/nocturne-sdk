#include "frame_internal.h"

#include <nocturne/text.h>

#include "allocator.h"
#include <string.h>

NT_DECLARE_ALIGNMENT(NtFrame);
NT_DECLARE_ALIGNMENT(FrameCell);

static NtResult allocate_cells(const NtAllocator *allocator, NtCellSize size, FrameCell **out)
{
    const uint32_t limit = (uint32_t)INT32_MAX + 1;
    if (size.columns > limit || size.rows > limit)
        return NT_RESULT_OUT_OF_RANGE;
    if (!size.columns || !size.rows) {
        *out = NULL;
        return NT_RESULT_OK;
    }
    if (size.rows > (SIZE_MAX / sizeof(FrameCell)) / size.columns)
        return NT_RESULT_OUT_OF_RANGE;
    const size_t count = (size_t)size.columns * size.rows;
    FrameCell *cells = nt_allocate(allocator, count * sizeof(*cells), NT_ALIGNMENT(FrameCell));
    if (!cells) return NT_RESULT_ALLOCATION_FAILED;
    for (size_t i = 0; i < count; ++i)
        cells[i] = (FrameCell){0};
    *out = cells;
    return NT_RESULT_OK;
}

static void release_glyph(const NtAllocator *allocator, const FrameCell *cell)
{
    nt_release(allocator, cell->glyph, cell->bytes, 1);
}

static void release_cells(const NtFrame *frame)
{
    if (!frame->cells) return;
    const size_t count = (size_t)frame->size.columns * frame->size.rows;
    for (size_t i = 0; i < count; ++i)
        release_glyph(&frame->allocator, &frame->cells[i]);
    nt_release(&frame->allocator, frame->cells, count * sizeof(*frame->cells),
               NT_ALIGNMENT(FrameCell));
}

NtResult nt_frame_create(const NtAllocator *allocator, NtCellSize size, NtFrame **out)
{
    NtAllocator resolved;
    if (!out || !nt_allocator_get(allocator, &resolved)) return NT_RESULT_INVALID_ARGUMENT;
    FrameCell *cells;
    NtResult result = allocate_cells(&resolved, size, &cells);
    if (result != NT_RESULT_OK) return result;

    NtFrame *frame = nt_allocate(&resolved, sizeof(*frame), NT_ALIGNMENT(NtFrame));
    if (!frame) {
        const NtFrame unpublished = { resolved, size, cells };
        release_cells(&unpublished);
        return NT_RESULT_ALLOCATION_FAILED;
    }
    *frame = (NtFrame){ resolved, size, cells };
    *out = frame;
    return NT_RESULT_OK;
}

void nt_frame_destroy(NtFrame *frame)
{
    if (!frame) return;
    const NtAllocator allocator = frame->allocator;
    release_cells(frame);
    nt_release(&allocator, frame, sizeof(*frame), NT_ALIGNMENT(NtFrame));
}

NtCellSize nt_frame_size(const NtFrame *frame)
{
    return frame ? frame->size : (NtCellSize){0};
}

NtResult nt_frame_resize(NtFrame *frame, NtCellSize size)
{
    if (!frame) return NT_RESULT_INVALID_ARGUMENT;
    if (size.columns == frame->size.columns && size.rows == frame->size.rows)
        return NT_RESULT_OK;
    FrameCell *cells;
    NtResult result = allocate_cells(&frame->allocator, size, &cells);
    if (result != NT_RESULT_OK) return result;

    if (cells && frame->cells) {
        const uint32_t rows = size.rows < frame->size.rows ? size.rows : frame->size.rows;
        const uint32_t cols = size.columns < frame->size.columns ? size.columns : frame->size.columns;
        for (uint32_t y = 0; y < rows; ++y) {
            FrameCell *old_row = frame->cells + (size_t)y * frame->size.columns;
            FrameCell *new_row = cells + (size_t)y * size.columns;
            for (uint32_t x = 0; x < cols; ++x) {
                FrameCell *old = &old_row[x];
                if (old->columns > cols - x) {
                    if (!old->attached_background) new_row[x].background = old->background;
                    continue;
                }
                new_row[x] = *old;
                old->glyph = NULL;
            }
        }
    }
    release_cells(frame);
    frame->size = size;
    frame->cells = cells;
    return NT_RESULT_OK;
}

static bool contains(const NtFrame *frame, NtCellPoint cell)
{
    return cell.x >= 0 && cell.y >= 0 && (uint32_t)cell.x < frame->size.columns &&
           (uint32_t)cell.y < frame->size.rows;
}

NtResult nt_frame_cell(const NtFrame *frame, NtCellPoint position, NtCellView *out)
{
    if (!frame || !out) return NT_RESULT_INVALID_ARGUMENT;
    if (!contains(frame, position)) return NT_RESULT_OUT_OF_RANGE;

    const FrameCell *cell = &frame->cells[nt_frame_index(frame, position)];
    const FrameCell *lead = cell - cell->continuation;
    position.x -= cell->continuation;
    const NtOwnerToken owner = lead->columns &&
        (lead->foreground.alpha || !cell->background.color.alpha) ?
        lead->glyph_owner : cell->background.owner;
    *out = (NtCellView){ {lead->glyph, lead->bytes}, position, lead->columns,
                         {lead->foreground, cell->background.color}, owner };
    return NT_RESULT_OK;
}

NtResult nt_frame_background(const NtFrame *frame, NtCellPoint position, NtCellBackground *out)
{
    if (!frame || !out) return NT_RESULT_INVALID_ARGUMENT;
    if (!contains(frame, position)) return NT_RESULT_OUT_OF_RANGE;
    *out = frame->cells[nt_frame_index(frame, position)].background;
    return NT_RESULT_OK;
}

void nt_frame_remove_glyph(NtFrame *frame, size_t index, bool preserve_background)
{
    FrameCell *lead = &frame->cells[nt_frame_lead(frame, index)];
    const unsigned count = lead->columns == 2 ? 2 : 1;
    release_glyph(&frame->allocator, lead);
    for (unsigned i = 0; i < count; ++i) {
        const NtCellBackground background = preserve_background ?
            lead[i].background : (NtCellBackground){0};
        lead[i] = (FrameCell){.background = background};
    }
}

NtResult nt_frame_validate_glyph(NtByteView glyph)
{
    NtGlyphMetrics metrics;
    const NtWidthPolicy policy = { NT_WIDTH_ONE, NT_WIDTH_ONE };
    const NtResult result = nt_glyph_measure(glyph, policy, &metrics);
    if (result != NT_RESULT_OK) return result;
    return metrics.presentation == NT_GLYPH_TEXT || metrics.presentation == NT_GLYPH_EMOJI ?
           NT_RESULT_OK : NT_RESULT_INVALID_ARGUMENT;
}

NtResult nt_frame_copy_glyph(const NtFrame *frame, NtByteView glyph, char **out)
{
    char *copy = nt_allocate(&frame->allocator, glyph.count, 1);
    if (!copy) return NT_RESULT_ALLOCATION_FAILED;
    memcpy(copy, glyph.data, glyph.count);
    *out = copy;
    return NT_RESULT_OK;
}

void nt_frame_store_glyph(NtFrame *frame, size_t index, char *glyph, size_t bytes,
                          uint8_t columns, NtRgba foreground, NtOwnerToken owner,
                          bool preserve_background)
{
    nt_frame_remove_glyph(frame, index, preserve_background);
    if (columns == 2) {
        nt_frame_remove_glyph(frame, index + 1, preserve_background);
        frame->cells[index + 1].continuation = true;
    }
    FrameCell *lead = &frame->cells[index];
    lead->glyph = glyph;
    lead->bytes = bytes;
    lead->foreground = foreground;
    lead->glyph_owner = owner;
    lead->columns = columns;
}

NtResult nt_frame_set_glyph(NtFrame *frame, NtCellPoint lead, NtByteView glyph,
                           uint8_t columns, NtCellStyle style, NtOwnerToken owner)
{
    if (!frame || (columns != 1 && columns != 2)) return NT_RESULT_INVALID_ARGUMENT;
    if (!contains(frame, lead) || columns > frame->size.columns - (uint32_t)lead.x)
        return NT_RESULT_OUT_OF_RANGE;
    const size_t index = nt_frame_index(frame, lead);
    if (frame->cells[index].continuation) return NT_RESULT_INVALID_ARGUMENT;
    NtResult result = nt_frame_validate_glyph(glyph);
    if (result != NT_RESULT_OK) return result;

    char *copy;
    result = nt_frame_copy_glyph(frame, glyph, &copy);
    if (result != NT_RESULT_OK) return result;
    nt_frame_store_glyph(frame, index, copy, glyph.count, columns, style.foreground, owner, false);
    const NtCellBackground background = {style.background, style.background.alpha ? owner : 0};
    for (unsigned i = 0; i < columns; ++i) {
        frame->cells[index + i].background = background;
        frame->cells[index + i].attached_background = true;
    }
    return NT_RESULT_OK;
}

NtResult nt_frame_clear_glyph(NtFrame *frame, NtCellPoint cell)
{
    if (!frame) return NT_RESULT_INVALID_ARGUMENT;
    if (!contains(frame, cell)) return NT_RESULT_OUT_OF_RANGE;
    nt_frame_remove_glyph(frame, nt_frame_index(frame, cell), false);
    return NT_RESULT_OK;
}

NtResult nt_frame_stage(const NtFrame *frame, NtFrame *out)
{
    NtFrame stage = {frame->allocator, frame->size, NULL};
    NtResult result = allocate_cells(&stage.allocator, stage.size, &stage.cells);
    if (result != NT_RESULT_OK) return result;
    const size_t count = (size_t)stage.size.columns * stage.size.rows;
    for (size_t i = 0; i < count; ++i) {
        FrameCell cell = frame->cells[i];
        if (cell.glyph) {
            result = nt_frame_copy_glyph(&stage, (NtByteView){cell.glyph, cell.bytes}, &cell.glyph);
            if (result != NT_RESULT_OK) {
                release_cells(&stage);
                return result;
            }
        }
        stage.cells[i] = cell;
    }
    *out = stage;
    return NT_RESULT_OK;
}

void nt_frame_discard(NtFrame *frame)
{
    release_cells(frame);
}

void nt_frame_publish(NtFrame *frame, NtFrame *stage)
{
    FrameCell *previous = frame->cells;
    frame->cells = stage->cells;
    stage->cells = previous;
    release_cells(stage);
}
