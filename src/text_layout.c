#include "text_layout_internal.h"

#include "allocator.h"
#include "text_analysis_internal.h"

#include <limits.h>
#include <string.h>

typedef struct LayoutCaret {
    NtCellPoint point;
    bool available;
} LayoutCaret;

typedef struct LayoutItem {
    NtByteSpan source;
    NtClusterId id;
    NtGlyphPresentation presentation;
    uint32_t intrinsic, columns;
    uint8_t base;
    bool break_after, ends_paragraph;
    size_t first_scalar, representative;
    LayoutCaret leading, trailing;
    char *transformed;
    size_t allocated;
} LayoutItem;

typedef struct LayoutGlyph {
    NtClusterView view;
    NtCaret on_left, on_right;
} LayoutGlyph;

typedef struct LayoutRow {
    size_t start, end, newline;
    uint32_t columns;
    uint8_t base;
    bool clipped, marked;
    size_t first_glyph, glyph_count;
    NtCaret empty_caret;
    bool empty_anchor;
} LayoutRow;

struct NtTextLayout {
    NtAllocator allocator;
    NtByteView source;
    char *owned;
    LayoutItem *items;
    LayoutRow *rows;
    LayoutGlyph *glyphs;
    size_t item_count, row_count, glyph_count;
    NtCellSize size;
    NtWidthPolicy width;
    LayoutCaret start, end;
};

typedef struct LayoutScalar {
    size_t item;
    uint32_t display;
    bool separator;
} LayoutScalar;

typedef struct LayoutScratch {
    NtBidi *bidi;
    LayoutScalar *scalars;
    NtBidiVisual *visual;
    size_t scalar_count;
} LayoutScratch;

NT_DECLARE_ALIGNMENT(NtTextLayout);
NT_DECLARE_ALIGNMENT(LayoutItem);
NT_DECLARE_ALIGNMENT(LayoutRow);
NT_DECLARE_ALIGNMENT(LayoutGlyph);
NT_DECLARE_ALIGNMENT(LayoutScalar);
NT_DECLARE_ALIGNMENT(NtBidiVisual);
NT_DECLARE_ALIGNMENT(NtLineBreak);

static const NtByteView ellipsis = {"\xe2\x80\xa6", 3};
static const NtByteView replacement = {"\xef\xbf\xbd", 3};
static const NtByteView space = {" ", 1};

static bool width_choice(NtWidthChoice choice)
{
    return choice == NT_WIDTH_ONE || choice == NT_WIDTH_TWO;
}

static bool extent_valid(NtExtentRange extent)
{
    return extent.min <= INT32_MAX &&
           (!extent.bounded || (extent.max <= INT32_MAX && extent.min <= extent.max));
}

bool nt_text_layout_options_valid(const NtTextLayoutOptions *options)
{
    return options && options->tab_stop && width_choice(options->width.ambiguous) &&
           width_choice(options->width.emoji) && extent_valid(options->bounds.columns) &&
           extent_valid(options->bounds.rows) &&
           (options->wrap == NT_WRAP_NONE || options->wrap == NT_WRAP_WORD ||
            options->wrap == NT_WRAP_CLUSTER) &&
           (options->align == NT_TEXT_ALIGN_START || options->align == NT_TEXT_ALIGN_CENTER ||
            options->align == NT_TEXT_ALIGN_END) &&
           (options->base_direction == NT_DIRECTION_AUTO || options->base_direction == NT_DIRECTION_LTR ||
            options->base_direction == NT_DIRECTION_RTL);
}

static uint32_t limit(NtExtentRange extent)
{
    return extent.bounded ? extent.max : INT32_MAX;
}

static size_t boundary_byte(const NtTextLayout *layout, size_t index)
{
    return index < layout->item_count ? layout->items[index].source.begin : layout->source.count;
}

static NtResult initialize_items(NtTextLayout *layout, const NtText *original,
                                 NtWidthPolicy policy)
{
    NtGraphemeIterator iterator;
    NtResult result = nt_grapheme_iterator_init(&iterator, layout->source);
    if (result != NT_RESULT_OK) return result;
    const NtGraphemeIterator initial = iterator;
    size_t count = 0;
    bool available;
    NtByteSpan span;
    while ((result = nt_grapheme_iterator_next(&iterator, &span, &available)) == NT_RESULT_OK &&
           available) ++count;
    if (result != NT_RESULT_OK) return result;
    if (count > SIZE_MAX / sizeof(LayoutItem) || count >= SIZE_MAX / sizeof(LayoutRow) ||
        count > (SIZE_MAX / sizeof(LayoutGlyph) - 1) / 2) return NT_RESULT_OUT_OF_RANGE;
    layout->item_count = count;
    if (count) {
        layout->items = nt_allocate(&layout->allocator, count * sizeof(*layout->items),
                                    NT_ALIGNMENT(LayoutItem));
        if (!layout->items) return NT_RESULT_ALLOCATION_FAILED;
        for (size_t i = 0; i < count; ++i) layout->items[i] = (LayoutItem){0};
    }

    iterator = initial;
    for (size_t i = 0; i < count; ++i) {
        (void)nt_grapheme_iterator_next(&iterator, &span, &available);
        NtGlyphMetrics metrics;
        const NtByteView glyph = {layout->source.data + span.begin, span.end - span.begin};
        result = nt_glyph_measure(glyph, policy, &metrics);
        if (result != NT_RESULT_OK) return result;
        NtClusterId id = (NtClusterId)i + 1;
        if (original) {
            NtTextCluster cluster;
            result = nt_text_cluster(original, i, &cluster);
            if (result != NT_RESULT_OK) return result;
            id = cluster.id;
        }
        layout->items[i] = (LayoutItem){.source = span, .id = id, .presentation = metrics.presentation,
                                        .intrinsic = metrics.columns, .representative = SIZE_MAX};
    }
    return NT_RESULT_OK;
}

static NtResult initialize_breaks(NtTextLayout *layout)
{
    const NtAllocator *allocator = &layout->allocator;
    const NtAllocator *api_allocator = allocator->allocate ? allocator : NULL;
    size_t count;
    NtResult result = nt_line_breaks(api_allocator, layout->source, NULL, 0, &count);
    if (result != NT_RESULT_OK) return result;
    if (count > SIZE_MAX / sizeof(NtLineBreak)) return NT_RESULT_OUT_OF_RANGE;
    const size_t bytes = count * sizeof(NtLineBreak);
    NtLineBreak *breaks = nt_allocate(allocator, bytes, NT_ALIGNMENT(NtLineBreak));
    if (!breaks) return NT_RESULT_ALLOCATION_FAILED;
    result = nt_line_breaks(api_allocator, layout->source, breaks, count, &count);
    if (result == NT_RESULT_OK) {
        size_t b = 0;
        for (size_t i = 0; i < layout->item_count; ++i) {
            LayoutItem *item = &layout->items[i];
            while (b < count && breaks[b].offset < item->source.end) ++b;
            item->break_after = b < count && breaks[b].offset == item->source.end;
        }
    }
    nt_release(allocator, breaks, bytes, NT_ALIGNMENT(NtLineBreak));
    return result;
}

static void release_scratch(const NtAllocator *allocator, LayoutScratch scratch)
{
    nt_bidi_destroy(scratch.bidi);
    nt_release(allocator, scratch.scalars, scratch.scalar_count * sizeof(*scratch.scalars),
               NT_ALIGNMENT(LayoutScalar));
    nt_release(allocator, scratch.visual, scratch.scalar_count * sizeof(*scratch.visual),
               NT_ALIGNMENT(NtBidiVisual));
}

static NtResult initialize_bidi(NtTextLayout *layout, NtDirection direction, LayoutScratch *scratch)
{
    const NtAllocator *allocator = &layout->allocator;
    NtResult result = nt_bidi_create(allocator->allocate ? allocator : NULL, layout->source, direction, &scratch->bidi);
    if (result != NT_RESULT_OK) return result;

    const size_t count = nt_bidi_count(scratch->bidi);
    if (count > SIZE_MAX / sizeof(LayoutScalar) || count > SIZE_MAX / sizeof(NtBidiVisual))
        return NT_RESULT_OUT_OF_RANGE;
    scratch->scalar_count = count;
    if (!count) return NT_RESULT_OK;
    if (!layout->items) return NT_RESULT_INVALID_ARGUMENT;

    scratch->scalars = nt_allocate(allocator, count * sizeof(*scratch->scalars),
                                   NT_ALIGNMENT(LayoutScalar));
    if (!scratch->scalars) return NT_RESULT_ALLOCATION_FAILED;
    scratch->visual = nt_allocate(allocator, count * sizeof(*scratch->visual),
                                  NT_ALIGNMENT(NtBidiVisual));
    if (!scratch->visual) return NT_RESULT_ALLOCATION_FAILED;

    size_t index = 0;
    for (size_t s = 0; s < count; ++s) {
        NtBidiScalar info;
        result = nt_bidi_scalar(scratch->bidi, s, &info);
        if (result != NT_RESULT_OK) return result;
        while (index < layout->item_count && info.source.begin >= layout->items[index].source.end)
            ++index;
        if (index == layout->item_count) return NT_RESULT_INVALID_ARGUMENT;
        LayoutItem *item = &layout->items[index];
        if (info.source.begin == item->source.begin) {
            item->base = info.paragraph_level;
            item->first_scalar = s;
        }
        if (item->representative == SIZE_MAX && info.level != NT_BIDI_REMOVED_LEVEL)
            item->representative = s;
        item->ends_paragraph = item->ends_paragraph || info.paragraph_separator;
        scratch->scalars[s] = (LayoutScalar){index, info.scalar, info.paragraph_separator};
    }
    return NT_RESULT_OK;
}

static uint32_t item_width(const LayoutItem *item, uint32_t tab, uint32_t x)
{
    return item->presentation == NT_GLYPH_TAB ? tab - x % tab : item->intrinsic;
}

/* Plan one row. Recompute tab advances when a soft wrap changes the origin. */
static NtResult plan_row(NtTextLayout *layout, const NtTextLayoutOptions *options,
                         size_t start, LayoutRow *row, size_t *next, bool *more)
{
    const size_t count = layout->item_count;
    const uint32_t max = limit(options->bounds.columns);
    const bool wrap = options->bounds.columns.bounded && max && options->wrap != NT_WRAP_NONE;
    uint8_t base = options->base_direction == NT_DIRECTION_RTL;
    if (start < count) base = layout->items[start].base;
    else if (count && !layout->items[count - 1].ends_paragraph)
        base = layout->items[count - 1].base;
    *row = (LayoutRow){.start = start, .end = start, .newline = SIZE_MAX, .base = base};
    size_t i = start, break_at = start;
    uint32_t break_width = 0;
    while (i < count && layout->items[i].presentation != NT_GLYPH_LINE_BREAK) {
        LayoutItem *item = &layout->items[i];
        uint32_t width = item_width(item, options->tab_stop, row->columns);
        if (!row->clipped && width > max - row->columns) {
            if (!options->bounds.columns.bounded) return NT_RESULT_OUT_OF_RANGE;
            if (wrap && i != start) {
                if (options->wrap == NT_WRAP_WORD && break_at != start) {
                    i = break_at;
                    row->end = break_at;
                    row->columns = break_width;
                }
                *next = i;
                *more = true;
                return NT_RESULT_OK;
            }
            row->clipped = true;
            if (wrap) {
                ++i;
                break;
            }
        }
        if (!row->clipped) {
            item->columns = width;
            row->columns += width;
            row->end = i + 1;
            if (item->break_after) {
                break_at = row->end;
                break_width = row->columns;
            }
        }
        ++i;
    }
    if (i < count && layout->items[i].presentation == NT_GLYPH_LINE_BREAK) {
        row->newline = i;
        *next = i + 1;
        *more = true;
    } else {
        *next = i;
        *more = i < count;
    }
    return NT_RESULT_OK;
}

static NtResult plan_rows(NtTextLayout *layout, const NtTextLayoutOptions *options)
{
    const uint32_t max_rows = limit(options->bounds.rows);
    const uint32_t max_cols = limit(options->bounds.columns);
    if (max_rows) {
        layout->rows = nt_allocate(&layout->allocator, (layout->item_count + 1) * sizeof(LayoutRow),
                                    NT_ALIGNMENT(LayoutRow));
        if (!layout->rows) return NT_RESULT_ALLOCATION_FAILED;
    }
    bool more = true;
    size_t next = 0;
    while (more && layout->row_count < max_rows) {
        LayoutRow *row = &layout->rows[layout->row_count++];
        NtResult result = plan_row(layout, options, next, row, &next, &more);
        if (result != NT_RESULT_OK) return result;
    }
    if (more && !options->bounds.rows.bounded) return NT_RESULT_OUT_OF_RANGE;
    if (more && layout->row_count) layout->rows[layout->row_count - 1].clipped = true;

    NtGlyphMetrics marker;
    (void)nt_glyph_measure(ellipsis, options->width, &marker);
    uint32_t width = options->bounds.columns.min;
    for (size_t r = 0; r < layout->row_count; ++r) {
        LayoutRow *row = &layout->rows[r];
        if (options->ellipsis && row->clipped && marker.columns <= max_cols) {
            if (!options->bounds.columns.bounded && row->columns > INT32_MAX - (uint32_t)marker.columns)
                return NT_RESULT_OUT_OF_RANGE;
            while (row->columns > max_cols - marker.columns) {
                row->columns -= layout->items[--row->end].columns;
            }
            row->columns += marker.columns;
            row->marked = true;
        }
        if (row->columns > width) width = row->columns;
    }
    const uint32_t height = (uint32_t)layout->row_count;
    layout->size = (NtCellSize){width, height > options->bounds.rows.min ? height : options->bounds.rows.min};
    if (layout->size.columns && layout->row_count) {
        layout->glyphs = nt_allocate(&layout->allocator, (2 * layout->item_count + 1) * sizeof(LayoutGlyph),
                                     NT_ALIGNMENT(LayoutGlyph));
        if (!layout->glyphs) return NT_RESULT_ALLOCATION_FAILED;
    }
    return NT_RESULT_OK;
}

static size_t encode_scalar(uint32_t value, char *bytes)
{
    if (value <= 0x7f) {
        bytes[0] = (char)value;
        return 1;
    }
    if (value <= 0x7ff) {
        bytes[0] = (char)(0xc0u | (value >> 6));
        bytes[1] = (char)(0x80u | (value & 0x3fu));
        return 2;
    }
    if (value <= 0xffff) {
        bytes[0] = (char)(0xe0u | (value >> 12));
        bytes[1] = (char)(0x80u | ((value >> 6) & 0x3fu));
        bytes[2] = (char)(0x80u | (value & 0x3fu));
        return 3;
    }
    bytes[0] = (char)(0xf0u | (value >> 18));
    bytes[1] = (char)(0x80u | ((value >> 12) & 0x3fu));
    bytes[2] = (char)(0x80u | ((value >> 6) & 0x3fu));
    bytes[3] = (char)(0x80u | (value & 0x3fu));
    return 4;
}

static NtResult prepare_glyph(NtTextLayout *layout, LayoutItem *item, LayoutScratch scratch,
                              NtByteView *out)
{
    if (item->presentation == NT_GLYPH_TAB) {
        *out = space;
        return NT_RESULT_OK;
    }
    if (item->presentation == NT_GLYPH_REPLACEMENT) {
        *out = replacement;
        return NT_RESULT_OK;
    }
    const NtByteView source = {layout->source.data + item->source.begin,
                              item->source.end - item->source.begin};
    if (item->presentation == NT_GLYPH_ISOLATED_MARK) {
        if (source.count > SIZE_MAX - 3) return NT_RESULT_OUT_OF_RANGE;
        const size_t bytes = 3 + source.count;
        item->transformed = nt_allocate(&layout->allocator, bytes, 1);
        if (!item->transformed) return NT_RESULT_ALLOCATION_FAILED;
        item->allocated = bytes;
        const size_t base = encode_scalar(0x25cc, item->transformed);
        memcpy(item->transformed + base, source.data, source.count);
        *out = (NtByteView){item->transformed, bytes};
        return NT_RESULT_OK;
    }

    const size_t start = item->first_scalar;
    bool changed = false;
    size_t total = 0, cursor = item->source.begin;
    for (size_t s = start; cursor < item->source.end; ++s) {
        uint32_t scalar;
        (void)nt_utf8_decode(layout->source, &cursor, &scalar);
        const uint32_t display = scratch.scalars[s].display;
        changed = changed || display != scalar;
        char bytes[4];
        const size_t width = encode_scalar(display, bytes);
        if (width > SIZE_MAX - total) return NT_RESULT_OUT_OF_RANGE;
        total += width;
    }
    if (!changed) {
        *out = source;
        return NT_RESULT_OK;
    }

    item->transformed = nt_allocate(&layout->allocator, total, 1);
    if (!item->transformed) return NT_RESULT_ALLOCATION_FAILED;
    item->allocated = total;
    size_t written = 0;
    cursor = item->source.begin;
    for (size_t s = start; cursor < item->source.end; ++s) {
        uint32_t scalar;
        (void)nt_utf8_decode(layout->source, &cursor, &scalar);
        written += encode_scalar(scratch.scalars[s].display, item->transformed + written);
    }
    const NtByteView mirrored = {item->transformed, written};
    NtGlyphMetrics metrics;
    /* A Unicode mirror substitution can have different terminal width.
     * Keep the resolved bidi level for renderers that can mirror without
     * changing cell metrics. */
    if (nt_glyph_measure(mirrored, layout->width, &metrics) != NT_RESULT_OK ||
        metrics.columns != item->intrinsic ||
        (metrics.presentation != NT_GLYPH_TEXT && metrics.presentation != NT_GLYPH_EMOJI)) {
        nt_release(&layout->allocator, item->transformed, item->allocated, 1);
        item->transformed = NULL;
        item->allocated = 0;
        *out = source;
    } else {
        *out = mirrored;
    }
    return NT_RESULT_OK;
}

static NtResult place_item(NtTextLayout *layout, LayoutItem *item, LayoutScratch scratch,
                           uint8_t level, NtCellPoint *cursor)
{
    const bool rtl = level & 1u;
    const NtCellPoint left = *cursor;
    cursor->x += (int32_t)item->columns;
    item->leading = (LayoutCaret){rtl ? *cursor : left, true};
    item->trailing = (LayoutCaret){rtl ? left : *cursor, true};
    if (!item->columns) return NT_RESULT_OK;

    NtByteView glyph;
    NtResult result = prepare_glyph(layout, item, scratch, &glyph);
    if (result != NT_RESULT_OK) return result;
    const NtCaret leading = {item->source.begin, NT_CARET_LEADING};
    const NtCaret trailing = {item->source.end, NT_CARET_TRAILING};
    layout->glyphs[layout->glyph_count++] = (LayoutGlyph){
        {item->id, item->source, glyph, left, item->columns, level},
        rtl ? trailing : leading, rtl ? leading : trailing
    };
    return NT_RESULT_OK;
}

static void place_marker(NtTextLayout *layout, LayoutRow *row, uint32_t columns, NtCellPoint *cursor)
{
    const size_t byte = boundary_byte(layout, row->end);
    const NtCaret caret = {byte, NT_CARET_LEADING};
    layout->glyphs[layout->glyph_count++] = (LayoutGlyph){
        {0, {byte, byte}, ellipsis, *cursor, columns, row->base}, caret, caret
    };
    const NtCellPoint left = *cursor;
    cursor->x += (int32_t)columns;
    const LayoutCaret anchor = {row->base & 1u ? *cursor : left, true};
    if (row->end < layout->item_count) layout->items[row->end].leading = anchor;
    else layout->end = anchor;
}

static int32_t row_origin(const NtTextLayout *layout, const LayoutRow *row, NtTextAlign align)
{
    const int32_t padding = (int32_t)(layout->size.columns - row->columns);
    if (align == NT_TEXT_ALIGN_CENTER) return padding / 2;
    const bool rtl = row->base & 1u;
    return (align == NT_TEXT_ALIGN_START ? rtl : !rtl) ? padding : 0;
}

static NtResult place_logical_range(NtTextLayout *layout, size_t first, size_t end,
                                    LayoutScratch scratch, uint8_t level, NtCellPoint *cursor)
{
    for (size_t at = first; at < end;) {
        const size_t index = level & 1u ? --end : at++;
        NtResult result = place_item(layout, &layout->items[index], scratch, level, cursor);
        if (result != NT_RESULT_OK) return result;
    }
    return NT_RESULT_OK;
}

/* Reinsert X9-only clusters beside their nearest following logical
 * representative, or beside the last preceding one at the segment end.
 * This preserves cell geometry for BN controls with visible replacements. */
static NtResult place_visual_segment(NtTextLayout *layout, LayoutScratch scratch,
                                      size_t first, size_t end, size_t count, NtCellPoint *cursor)
{
    if (!count)
        return place_logical_range(layout, first, end, scratch, layout->items[first].base, cursor);
    size_t trailing = end;
    while (trailing > first && layout->items[trailing - 1].representative == SIZE_MAX) --trailing;
    for (size_t i = 0; i < count; ++i) {
        const NtBidiVisual display = scratch.visual[i];
        const size_t index = scratch.scalars[display.logical_index].item;
        LayoutItem *item = &layout->items[index];
        if (item->representative != display.logical_index) continue;

        size_t before = index;
        while (before > first && layout->items[before - 1].representative == SIZE_MAX) --before;
        const bool rtl = display.level & 1u;
        const bool last = index + 1 == trailing;
        NtResult result = NT_RESULT_OK;
        if (rtl && last)
            result = place_logical_range(layout, trailing, end, scratch, display.level, cursor);
        else if (!rtl)
            result = place_logical_range(layout, before, index, scratch, display.level, cursor);
        if (result == NT_RESULT_OK)
            result = place_item(layout, item, scratch, display.level, cursor);
        if (result != NT_RESULT_OK) return result;
        if (rtl)
            result = place_logical_range(layout, before, index, scratch, display.level, cursor);
        else if (last)
            result = place_logical_range(layout, trailing, end, scratch, display.level, cursor);
        if (result != NT_RESULT_OK) return result;
    }
    return NT_RESULT_OK;
}

static NtResult order_row(NtTextLayout *layout, LayoutRow *row, LayoutScratch scratch,
                          size_t *scan, NtCellPoint *cursor)
{
    while (*scan < scratch.scalar_count && scratch.scalars[*scan].item < row->start) ++*scan;
    while (*scan < scratch.scalar_count && scratch.scalars[*scan].item < row->end) {
        const size_t first = *scan;
        NtBidiScalar scalar;
        (void)nt_bidi_scalar(scratch.bidi, first, &scalar);
        const size_t begin = scalar.source.begin;
        do {
            (void)nt_bidi_scalar(scratch.bidi, *scan, &scalar);
            if (scratch.scalars[(*scan)++].separator) break;
        } while (*scan < scratch.scalar_count && scratch.scalars[*scan].item < row->end);

        size_t count;
        NtResult result = nt_bidi_reorder_line(scratch.bidi, (NtByteSpan){begin, scalar.source.end},
                                               scratch.visual, scratch.scalar_count, &count);
        if (result != NT_RESULT_OK) return result;
        for (size_t i = 0; i < count; ++i) {
            const NtBidiVisual display = scratch.visual[i];
            scratch.scalars[display.logical_index].display = display.display_scalar;
        }
        result = place_visual_segment(layout, scratch, scratch.scalars[first].item,
                                      scratch.scalars[*scan - 1].item + 1, count, cursor);
        if (result != NT_RESULT_OK) return result;
    }
    return NT_RESULT_OK;
}

static NtResult place_rows(NtTextLayout *layout, LayoutScratch scratch, const NtTextLayoutOptions *options)
{
    if (!layout->size.columns || !layout->row_count) return NT_RESULT_OK;
    NtGlyphMetrics marker;
    (void)nt_glyph_measure(ellipsis, options->width, &marker);
    size_t scan = 0;
    for (size_t r = 0; r < layout->row_count; ++r) {
        LayoutRow *row = &layout->rows[r];
        const int32_t x = row_origin(layout, row, options->align);
        NtCellPoint cursor = {x, (int32_t)r};
        const NtCellPoint line_start = {x + ((row->base & 1u) ? (int32_t)row->columns : 0), (int32_t)r};
        row->first_glyph = layout->glyph_count;
        if (row->marked && (row->base & 1u)) place_marker(layout, row, marker.columns, &cursor);
        NtResult result = order_row(layout, row, scratch, &scan, &cursor);
        if (result != NT_RESULT_OK) return result;
        if (row->marked && !(row->base & 1u)) place_marker(layout, row, marker.columns, &cursor);
        row->glyph_count = layout->glyph_count - row->first_glyph;

        const LayoutCaret line_lead = row->start < row->end ? layout->items[row->start].leading :
                                                               (LayoutCaret){line_start, true};
        const LayoutCaret line_tail = row->start < row->end ? layout->items[row->end - 1].trailing :
                                                               (LayoutCaret){line_start, true};
        if (r == 0 && (!row->clipped || row->start < row->end || row->marked))
            layout->start = row->start < layout->item_count && layout->items[row->start].leading.available ?
                            layout->items[row->start].leading : line_lead;
        if (r && layout->rows[r - 1].newline != SIZE_MAX)
            layout->items[layout->rows[r - 1].newline].trailing = line_lead;
        if (row->newline != SIZE_MAX && !row->clipped)
            layout->items[row->newline].leading = line_tail;
        if (!row->clipped && row->end == layout->item_count) layout->end = line_tail;

        if (!row->glyph_count && !row->clipped) {
            row->empty_caret = (NtCaret){boundary_byte(layout, row->start), NT_CARET_LEADING};
            row->empty_anchor = true;
        }
    }
    return NT_RESULT_OK;
}

static NtResult create_layout(const NtAllocator *allocator, NtByteView input, const NtText *original,
                              const NtTextLayoutOptions *options, NtTextLayout **out)
{
    NtAllocator resolved;
    if (!out || !nt_text_layout_options_valid(options) || !nt_allocator_get(allocator, &resolved))
        return NT_RESULT_INVALID_ARGUMENT;
    if (!input.data && input.count) return NT_RESULT_INVALID_ARGUMENT;

    NtTextLayout *layout = nt_allocate(&resolved, sizeof(*layout), NT_ALIGNMENT(NtTextLayout));
    if (!layout) return NT_RESULT_ALLOCATION_FAILED;
    *layout = (NtTextLayout){.allocator = resolved, .source = input, .width = options->width};
    if (original && input.count) {
        layout->owned = nt_allocate(&resolved, input.count, 1);
        if (!layout->owned) {
            nt_text_layout_destroy(layout);
            return NT_RESULT_ALLOCATION_FAILED;
        }
        memcpy(layout->owned, input.data, input.count);
        layout->source.data = layout->owned;
    }
    LayoutScratch scratch = {0};
    NtResult result = initialize_items(layout, original, options->width);
    if (result == NT_RESULT_OK && options->wrap == NT_WRAP_WORD)
        result = initialize_breaks(layout);
    if (result == NT_RESULT_OK) result = initialize_bidi(layout, options->base_direction, &scratch);
    if (result == NT_RESULT_OK) result = plan_rows(layout, options);
    if (result == NT_RESULT_OK) result = place_rows(layout, scratch, options);
    release_scratch(&resolved, scratch);
    if (result != NT_RESULT_OK) {
        nt_text_layout_destroy(layout);
        return result;
    }
    *out = layout;
    return NT_RESULT_OK;
}

NtResult nt_text_layout_create(const NtAllocator *allocator, const NtText *text,
                               const NtTextLayoutOptions *options, NtTextLayout **out)
{
    if (!text) return NT_RESULT_INVALID_ARGUMENT;
    return create_layout(allocator, nt_text_bytes(text), text, options, out);
}

NtResult nt_text_layout_create_borrowed(const NtAllocator *allocator, NtByteView text,
                                        const NtTextLayoutOptions *options, NtTextLayout **out)
{
    return create_layout(allocator, text, NULL, options, out);
}

void nt_text_layout_destroy(NtTextLayout *layout)
{
    if (!layout) return;
    const NtAllocator allocator = layout->allocator;
    for (size_t i = 0; layout->items && i < layout->item_count; ++i) {
        LayoutItem *item = &layout->items[i];
        nt_release(&allocator, item->transformed, item->allocated, 1);
    }
    nt_release(&allocator, layout->glyphs, (2 * layout->item_count + 1) * sizeof(LayoutGlyph),
               NT_ALIGNMENT(LayoutGlyph));
    nt_release(&allocator, layout->rows, (layout->item_count + 1) * sizeof(LayoutRow),
               NT_ALIGNMENT(LayoutRow));
    nt_release(&allocator, layout->items, layout->item_count * sizeof(LayoutItem), NT_ALIGNMENT(LayoutItem));
    nt_release(&allocator, layout->owned, layout->source.count, 1);
    nt_release(&allocator, layout, sizeof(*layout), NT_ALIGNMENT(NtTextLayout));
}

NtWidthPolicy nt_text_layout_width_policy(const NtTextLayout *layout)
{
    return layout->width;
}

NtCellSize nt_text_layout_size(const NtTextLayout *layout)
{
    return layout ? layout->size : (NtCellSize){0};
}

NtByteView nt_text_layout_source(const NtTextLayout *layout)
{
    return layout ? layout->source : (NtByteView){0};
}

size_t nt_text_layout_cluster_count(const NtTextLayout *layout)
{
    return layout ? layout->glyph_count : 0;
}

NtResult nt_text_layout_cluster(const NtTextLayout *layout, size_t index, NtClusterView *out)
{
    if (!layout || !out) return NT_RESULT_INVALID_ARGUMENT;
    if (index >= layout->glyph_count) return NT_RESULT_OUT_OF_RANGE;
    *out = layout->glyphs[index].view;
    return NT_RESULT_OK;
}

static int32_t right_edge(const LayoutGlyph *glyph)
{
    return glyph->view.position.x + (int32_t)glyph->view.columns;
}

NtResult nt_text_layout_hit(const NtTextLayout *layout, NtCellPoint cell, NtCaret *out)
{
    if (!layout || !out) return NT_RESULT_INVALID_ARGUMENT;
    if (cell.x < 0 || cell.y < 0 || !layout->size.columns ||
        (uint32_t)cell.x > layout->size.columns || (size_t)cell.y >= layout->row_count)
        return NT_RESULT_OUT_OF_RANGE;

    const LayoutRow *row = &layout->rows[cell.y];
    if (!row->glyph_count) {
        if (!row->empty_anchor) return NT_RESULT_OUT_OF_RANGE;
        *out = row->empty_caret;
        return NT_RESULT_OK;
    }

    const LayoutGlyph *glyphs = layout->glyphs + row->first_glyph;
    size_t lo = 0, hi = row->glyph_count - 1;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (cell.x > right_edge(&glyphs[mid])) lo = mid + 1;
        else hi = mid;
    }
    const LayoutGlyph *glyph = &glyphs[lo];
    const int32_t left = glyph->view.position.x, right = right_edge(glyph);
    if (cell.x <= left) *out = glyph->on_left;
    else if (cell.x >= right) *out = glyph->on_right;
    else *out = cell.x - left < right - cell.x ? glyph->on_left : glyph->on_right;
    return NT_RESULT_OK;
}

NtResult nt_text_layout_caret_position(const NtTextLayout *layout, NtCaret caret,
                                       NtCellPoint *out)
{
    if (!layout || !out || (caret.affinity != NT_CARET_LEADING && caret.affinity != NT_CARET_TRAILING))
        return NT_RESULT_INVALID_ARGUMENT;
    if (caret.offset > layout->source.count) return NT_RESULT_OUT_OF_RANGE;
    const bool leading = caret.affinity == NT_CARET_LEADING;
    LayoutCaret found = {0};
    if (leading && caret.offset == layout->source.count) {
        found = layout->end;
    } else if (!leading && caret.offset == 0) {
        found = layout->start;
    } else {
        size_t lo = 0, hi = layout->item_count;
        while (lo < hi) {
            const size_t mid = lo + (hi - lo) / 2;
            const LayoutItem *item = &layout->items[mid];
            const size_t byte = leading ? item->source.begin : item->source.end;
            if (caret.offset < byte) hi = mid;
            else if (caret.offset > byte) lo = mid + 1;
            else {
                found = leading ? item->leading : item->trailing;
                break;
            }
        }
    }
    if (!found.available) return NT_RESULT_OUT_OF_RANGE;
    *out = found.point;
    return NT_RESULT_OK;
}
