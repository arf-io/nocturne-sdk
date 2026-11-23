#include <nocturne/text.h>

#include "grapheme_internal.h"

typedef enum GraphemeClass {
    G_OTHER, G_CR, G_LF, G_CONTROL, G_EXTEND, G_ZWJ, G_REGIONAL_INDICATOR,
    G_PREPEND, G_SPACING_MARK, G_L, G_V, G_T, G_LV, G_LVT
} GraphemeClass;

typedef enum IndicProperty {
    I_NONE, I_CONSONANT, I_EXTEND, I_LINKER
} IndicProperty;

typedef struct GraphemeRange {
    uint32_t first, last;
    uint8_t flags;
} GraphemeRange;

#include "unicode_grapheme.inc"

typedef enum IndicState {
    INDIC_NONE, INDIC_CONSONANT, INDIC_JOINABLE
} IndicState;

typedef struct GraphemeRun {
    GraphemeClass previous;
    IndicState indic;
    bool regional_odd;
    bool pictograph_chain;
    bool pictograph_zwj;
} GraphemeRun;

static uint8_t grapheme_flags(uint32_t scalar)
{
    size_t begin = 0;
    size_t end = sizeof(grapheme_ranges) / sizeof(grapheme_ranges[0]);
    while (begin < end) {
        const size_t middle = begin + (end - begin) / 2;
        const GraphemeRange entry = grapheme_ranges[middle];
        if (scalar < entry.first) {
            end = middle;
        } else if (scalar > entry.last) {
            begin = middle + 1;
        } else {
            return entry.flags;
        }
    }
    return 0;
}

static GraphemeClass grapheme_class(uint8_t flags)
{
    return (GraphemeClass)(flags & 0x0f);
}

static IndicProperty indic_property(uint8_t flags)
{
    return (IndicProperty)((flags >> 4) & 0x03);
}

static bool is_pictograph(uint8_t flags)
{
    return (flags & 0x40) != 0;
}

static bool is_control(GraphemeClass type)
{
    return type == G_CR || type == G_LF || type == G_CONTROL;
}

/* Apply UAX #29 rules in precedence order. GB1/GB2 are handled by the
 * iterator. The state below describes only the already consumed prefix. */
static bool breaks_before(const GraphemeRun *run, uint8_t flags)
{
    const GraphemeClass left = run->previous;
    const GraphemeClass right = grapheme_class(flags);

    if (left == G_CR && right == G_LF) return false;
    if (is_control(left) || is_control(right)) return true;
    if (left == G_L && (right == G_L || right == G_V || right == G_LV || right == G_LVT))
        return false;
    if ((left == G_LV || left == G_V) && (right == G_V || right == G_T)) return false;
    if ((left == G_LVT || left == G_T) && right == G_T) return false;
    if (right == G_EXTEND || right == G_ZWJ || right == G_SPACING_MARK) return false;
    if (left == G_PREPEND) return false;
    if (run->indic == INDIC_JOINABLE && indic_property(flags) == I_CONSONANT) return false;
    if (run->pictograph_zwj && is_pictograph(flags)) return false;
    if (right == G_REGIONAL_INDICATOR && run->regional_odd) return false;
    return true;
}

static void consume(GraphemeRun *run, uint8_t flags)
{
    const GraphemeClass type = grapheme_class(flags);
    const IndicProperty indic = indic_property(flags);
    run->pictograph_zwj = type == G_ZWJ && run->pictograph_chain;
    run->pictograph_chain = is_pictograph(flags) || (type == G_EXTEND && run->pictograph_chain);
    run->regional_odd = type == G_REGIONAL_INDICATOR && !run->regional_odd;
    run->previous = type;

    if (indic == I_CONSONANT)
        run->indic = INDIC_CONSONANT;
    else if (indic == I_LINKER && run->indic != INDIC_NONE)
        run->indic = INDIC_JOINABLE;
    else if (indic == I_NONE)
        run->indic = INDIC_NONE;
}

bool nt_grapheme_separates(NtByteView left, NtByteView right)
{
    if (!left.count || !right.count) return true;
    GraphemeRun run = {0};
    uint32_t scalar;
    for (size_t offset = 0; offset < left.count;) {
        if (nt_utf8_decode(left, &offset, &scalar) != NT_RESULT_OK) return false;
        consume(&run, grapheme_flags(scalar));
    }
    size_t offset = 0;
    return nt_utf8_decode(right, &offset, &scalar) == NT_RESULT_OK &&
           breaks_before(&run, grapheme_flags(scalar));
}

const char *nt_unicode_version(void)
{
    return grapheme_unicode_version;
}

NtResult nt_grapheme_iterator_init(NtGraphemeIterator *iterator, NtByteView input)
{
    if (!iterator) return NT_RESULT_INVALID_ARGUMENT;
    const NtResult result = nt_utf8_validate(input, NULL);
    if (result == NT_RESULT_OK) *iterator = (NtGraphemeIterator){ input, 0 };
    return result;
}

NtResult nt_grapheme_iterator_next(NtGraphemeIterator *iterator,
                                   NtByteSpan *span, bool *available)
{
    if (!iterator || !span || !available || (!iterator->input.data && iterator->input.count))
        return NT_RESULT_INVALID_ARGUMENT;
    const NtByteView input = iterator->input;
    const size_t begin = iterator->offset;
    if (begin > input.count) return NT_RESULT_OUT_OF_RANGE;
    if (begin == input.count) {
        *available = false;
        return NT_RESULT_OK;
    }

    size_t end = begin;
    uint32_t scalar;
    NtResult result = nt_utf8_decode(input, &end, &scalar);
    if (result != NT_RESULT_OK) return result;
    GraphemeRun run = {0};
    consume(&run, grapheme_flags(scalar));
    while (end < input.count) {
        size_t next = end;
        result = nt_utf8_decode(input, &next, &scalar);
        if (result != NT_RESULT_OK) return result;
        const uint8_t flags = grapheme_flags(scalar);
        if (breaks_before(&run, flags)) break;
        consume(&run, flags);
        end = next;
    }

    iterator->offset = end;
    *span = (NtByteSpan){ begin, end };
    *available = true;
    return NT_RESULT_OK;
}
