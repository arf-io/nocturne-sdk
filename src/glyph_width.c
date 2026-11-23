#include <nocturne/text.h>

#include "text_internal.h"

typedef struct WidthRange {
    uint32_t first, last;
    uint16_t flags;
} WidthRange;

#include "unicode_width.inc"

static uint16_t width_flags(uint32_t scalar)
{
    size_t begin = 0;
    size_t end = sizeof(width_ranges) / sizeof(width_ranges[0]);
    while (begin < end) {
        const size_t middle = begin + (end - begin) / 2;
        const WidthRange entry = width_ranges[middle];
        if (scalar < entry.first)
            end = middle;
        else if (scalar > entry.last)
            begin = middle + 1;
        else
            return entry.flags;
    }
    return 0;
}

static bool valid_choice(NtWidthChoice choice)
{
    return choice == NT_WIDTH_ONE || choice == NT_WIDTH_TWO;
}

/* The callers below have validated the bytes and cursor. The sentinel is
 * outside Unicode and distinguishes the end from the NUL scalar. */
static uint32_t next_scalar(NtByteView glyph, size_t *offset)
{
    uint32_t scalar;
    return nt_utf8_decode(glyph, offset, &scalar) == NT_RESULT_OK ? scalar : UINT32_MAX;
}

static bool keycap(NtByteView glyph, uint32_t base, size_t offset, uint32_t next)
{
    if (base != '#' && base != '*' && (base < '0' || base > '9')) return false;
    if (next == 0xfe0f) next = next_scalar(glyph, &offset);
    return next == 0x20e3;
}

static NtGlyphMetrics printable_metrics(NtByteView glyph, NtWidthPolicy policy,
                                        uint32_t scalar, uint32_t next, size_t offset)
{
    bool emoji = false;
    bool mark = false;
    bool modifier_base = false;
    bool pictograph = false;
    bool pictograph_zwj = false;
    bool joined = false;
    bool explicit_text = false;
    uint8_t width = 0;
    for (; scalar != UINT32_MAX; scalar = next, next = next_scalar(glyph, &offset)) {
        const uint16_t flags = width_flags(scalar);
        const bool text_style = (flags & W_TEXT_VARIATION) && next == 0xfe0e;
        const bool emoji_style = (flags & W_EMOJI_VARIATION) && next == 0xfe0f;

        if ((flags & W_CONTROL) && !(flags & W_IGNORABLE))
            return (NtGlyphMetrics){ NT_GLYPH_REPLACEMENT, 1 };

        /* UAX #29's pictographic join shape. A valid text selector opts
         * out of joined presentation, even if another base is emoji. */
        explicit_text = explicit_text || text_style;
        if ((flags & W_PICTOGRAPH) && pictograph_zwj) joined = true;
        pictograph_zwj = scalar == 0x200d && pictograph;
        pictograph = (flags & W_PICTOGRAPH) || ((flags & W_EXTEND) && pictograph);

        if (flags & W_MODIFIER) {
            if (modifier_base || width == 0) emoji = true;
        } else if (!(flags & W_IGNORABLE)) {
            if (flags & (W_MARK | W_EXTEND)) {
                mark = true;
            } else {
                const uint8_t base_width = (flags & W_WIDE) ? 2 :
                    (flags & W_AMBIGUOUS) ? (uint8_t)policy.ambiguous : 1;
                if (base_width > width) width = base_width;
                if (emoji_style || ((flags & W_EMOJI) && !text_style) ||
                    keycap(glyph, scalar, offset, next))
                    emoji = true;
            }
        }

        if (scalar != 0xfe0f)
            modifier_base = (flags & W_MODIFIER_BASE) != 0 && !text_style;
    }
    if (emoji || (joined && !explicit_text))
        return (NtGlyphMetrics){ NT_GLYPH_EMOJI, (uint8_t)policy.emoji };
    if (width) return (NtGlyphMetrics){ NT_GLYPH_TEXT, width };
    if (mark) return (NtGlyphMetrics){ NT_GLYPH_ISOLATED_MARK, 1 };
    return (NtGlyphMetrics){ NT_GLYPH_INVISIBLE, 0 };
}

NtResult nt_glyph_measure(NtByteView glyph, NtWidthPolicy policy, NtGlyphMetrics *out)
{
    if (!out || !glyph.count || !valid_choice(policy.ambiguous) || !valid_choice(policy.emoji))
        return NT_RESULT_INVALID_ARGUMENT;
    NtGraphemeIterator iterator;
    NtResult result = nt_grapheme_iterator_init(&iterator, glyph);
    if (result != NT_RESULT_OK) return result;
    NtByteSpan span;
    bool available;
    result = nt_grapheme_iterator_next(&iterator, &span, &available);
    if (result != NT_RESULT_OK) return result;
    if (!available || span.end != glyph.count) return NT_RESULT_INVALID_ARGUMENT;

    size_t offset = 0;
    const uint32_t first = next_scalar(glyph, &offset);
    if (nt_text_hard_break(first)) {
        *out = (NtGlyphMetrics){ NT_GLYPH_LINE_BREAK, 0 };
    } else if (first == 0x09) {
        *out = (NtGlyphMetrics){ NT_GLYPH_TAB, 0 };
    } else {
        const uint32_t second = next_scalar(glyph, &offset);
        *out = printable_metrics(glyph, policy, first, second, offset);
    }
    return NT_RESULT_OK;
}
