/* Line-breaking rules adapted from native/src/linebreak.c at 53191655ec77.
 * Copyright (c) 2025-2026 Damien Heiser. MIT, see LICENSE. */
#include "text_analysis_internal.h"
#include "allocator.h"

#include "unicode_linebreak.inc"

enum {
    LINE_MARK = 1, LINE_INITIAL_QUOTE = 2, LINE_FINAL_QUOTE = 4,
    LINE_UNASSIGNED = 8, LINE_EAST_ASIAN = 16, LINE_PICTOGRAPH = 32
};

typedef struct LineUnit {
    uint32_t scalar;
    size_t offset;
    size_t nonspace;
    uint8_t kind, flags;
    bool last_zwj, odd_regional, numeric_before;
} LineUnit;

NT_DECLARE_ALIGNMENT(LineUnit);

static bool hard_break(LineClass kind)
{
    return kind == LB_BK || kind == LB_CR || kind == LB_LF || kind == LB_NL;
}

static bool letter(LineClass kind)
{
    return kind == LB_AL || kind == LB_HL;
}

static bool hangul(LineClass kind)
{
    return kind == LB_JL || kind == LB_JV || kind == LB_JT || kind == LB_H2 || kind == LB_H3;
}

static bool aksara(const LineUnit *unit)
{
    return unit->kind == LB_AK || unit->kind == LB_AS || unit->scalar == 0x25cc;
}

static bool initial_quote_context(LineClass kind)
{
    return hard_break(kind) || kind == LB_OP || kind == LB_QU || kind == LB_GL ||
           kind == LB_SP || kind == LB_ZW;
}

static bool final_quote_context(LineClass kind)
{
    return (kind != LB_OP && initial_quote_context(kind)) || kind == LB_WJ ||
           kind == LB_CL || kind == LB_CP || kind == LB_EX || kind == LB_IS || kind == LB_SY;
}

static bool hyphen_context(LineClass kind)
{
    return hard_break(kind) || kind == LB_SP || kind == LB_ZW || kind == LB_CB || kind == LB_GL;
}

static bool number_prefix(const LineUnit *right, LineClass following, LineClass after)
{
    return right->kind == LB_NU ||
           (right->kind == LB_OP && (following == LB_NU || (following == LB_IS && after == LB_NU)));
}

/* UAX #14 rule order, over units after LB9 and LB10. The caller handles
 * start/end, and nonspace/numeric/RI context is accumulated in one pass. */
static bool permits_break(const LineUnit *units, size_t count, size_t index)
{
    const LineUnit *left = &units[index - 1], *right = &units[index];
    const LineClass a = left->kind, b = right->kind;
    const LineClass before = index >= 2 ? units[index - 2].kind : LB_XX;
    const LineClass after = index + 1 < count ? units[index + 1].kind : LB_XX;
    if (a == LB_BK) return true;                                     /* LB4 */
    if (a == LB_CR && b == LB_LF) return false;                       /* LB5 */
    if (hard_break(a)) return true;
    if (hard_break(b)) return false;                                 /* LB6 */
    if (b == LB_SP || b == LB_ZW) return false;                       /* LB7 */
    const size_t prior = left->nonspace;
    const LineClass c = prior != SIZE_MAX ? units[prior].kind : LB_XX;
    if (c == LB_ZW) return true;                                    /* LB8 */
    if (left->last_zwj) return false;                                /* LB8a */
    if (a == LB_WJ || b == LB_WJ || a == LB_GL) return false;          /* LB11–12 */
    if (b == LB_GL && a != LB_SP && a != LB_BA && a != LB_HY && a != LB_HH)
        return false;                                               /* LB12a */
    if (b == LB_CL || b == LB_CP || b == LB_EX || b == LB_SY) return false; /* LB13 */
    if (c == LB_OP) return false;                                    /* LB14 */
    if (c == LB_QU && (units[prior].flags & LINE_INITIAL_QUOTE) &&
        (!prior || initial_quote_context(units[prior - 1].kind)))
        return false;                                               /* LB15a */
    if (b == LB_QU && (right->flags & LINE_FINAL_QUOTE) &&
        (index + 1 == count || final_quote_context(after)))
        return false;                                               /* LB15b */
    if (a == LB_SP && b == LB_IS && after == LB_NU) return true;       /* LB15c */
    if (b == LB_IS) return false;                                    /* LB15d */
    if ((c == LB_CL || c == LB_CP) && b == LB_NS) return false;        /* LB16 */
    if (c == LB_B2 && b == LB_B2) return false;                        /* LB17 */
    if (a == LB_SP) return true;                                     /* LB18 */
    if ((b == LB_QU && !(right->flags & LINE_INITIAL_QUOTE)) ||
        (a == LB_QU && !(left->flags & LINE_FINAL_QUOTE))) return false; /* LB19 */
    if (b == LB_QU && (!(left->flags & LINE_EAST_ASIAN) || index + 1 == count ||
                       !(units[index + 1].flags & LINE_EAST_ASIAN))) return false; /* LB19a */
    if (a == LB_QU && (!(right->flags & LINE_EAST_ASIAN) || index == 1 ||
                       !(units[index - 2].flags & LINE_EAST_ASIAN))) return false;
    if (a == LB_CB || b == LB_CB) return true;                         /* LB20 */
    if ((a == LB_HY || a == LB_HH) && letter(b) &&
        (index == 1 || hyphen_context(before))) return false;         /* LB20a */
    if (b == LB_BA || b == LB_HH || b == LB_HY || b == LB_NS || a == LB_BB) return false; /* LB21 */
    if ((a == LB_HY || a == LB_HH) && before == LB_HL && b != LB_HL) return false; /* LB21a */
    if (a == LB_SY && b == LB_HL) return false;                        /* LB21b */
    if (b == LB_IN) return false;                                     /* LB22 */
    if ((letter(a) && b == LB_NU) || (a == LB_NU && letter(b))) return false; /* LB23 */
    if ((a == LB_PR && (b == LB_ID || b == LB_EB || b == LB_EM)) ||
        ((a == LB_ID || a == LB_EB || a == LB_EM) && b == LB_PO)) return false; /* LB23a */
    if (((a == LB_PR || a == LB_PO) && letter(b)) ||
        (letter(a) && (b == LB_PR || b == LB_PO))) return false;       /* LB24 */

    const bool closes_number = a == LB_CL || a == LB_CP
        ? index >= 2 && units[index - 2].numeric_before : left->numeric_before;
    if (closes_number && (b == LB_PO || b == LB_PR)) return false;     /* LB25 */
    const LineClass after_next = count - index > 2 ? units[index + 2].kind : LB_XX;
    if ((a == LB_PO || a == LB_PR) && number_prefix(right, after, after_next)) return false;
    if ((a == LB_HY || a == LB_IS || left->numeric_before) && b == LB_NU) return false;

    if (a == LB_JL && (b == LB_JL || b == LB_JV || b == LB_H2 || b == LB_H3)) return false; /* LB26 */
    if ((a == LB_JV || a == LB_H2) && (b == LB_JV || b == LB_JT)) return false;
    if ((a == LB_JT || a == LB_H3) && b == LB_JT) return false;
    if ((hangul(a) && b == LB_PO) || (a == LB_PR && hangul(b))) return false; /* LB27 */
    if (letter(a) && letter(b)) return false;                         /* LB28 */
    if (a == LB_AP && aksara(right)) return false;                    /* LB28a */
    if (aksara(left) && (b == LB_VF || b == LB_VI)) return false;
    if (a == LB_VI && index >= 2 && aksara(&units[index - 2]) &&
        (b == LB_AK || right->scalar == 0x25cc)) return false;
    if (aksara(left) && aksara(right) && after == LB_VF) return false;
    if (a == LB_IS && letter(b)) return false;                        /* LB29 */
    if ((letter(a) || a == LB_NU) && b == LB_OP && !(right->flags & LINE_EAST_ASIAN))
        return false;                                               /* LB30 */
    if (a == LB_CP && !(left->flags & LINE_EAST_ASIAN) && (letter(b) || b == LB_NU))
        return false;
    if (a == LB_RI && b == LB_RI && left->odd_regional) return false;  /* LB30a */
    const uint8_t potential_emoji = LINE_UNASSIGNED | LINE_PICTOGRAPH;
    if (b == LB_EM && (a == LB_EB || (left->flags & potential_emoji) == potential_emoji))
        return false;                                               /* LB30b */
    return true;                                                     /* LB31 */
}

static size_t build_units(NtByteView text, LineUnit *units)
{
    size_t cursor = 0, used = 0;
    while (cursor < text.count) {
        const size_t at = cursor;
        uint32_t scalar = 0;
        (void)nt_utf8_decode(text, &cursor, &scalar);
        const LineClass raw = nt_unicode_property(line_classes, NT_ARRAY_COUNT(line_classes),
                                                   scalar, LB_XX);
        uint8_t flags = (uint8_t)nt_unicode_property(line_flags, NT_ARRAY_COUNT(line_flags),
                                                     scalar, 0);
        LineClass kind = raw;
        if (kind == LB_AI || kind == LB_SG || kind == LB_XX) kind = LB_AL;
        else if (kind == LB_SA) kind = flags & LINE_MARK ? LB_CM : LB_AL;
        else if (kind == LB_CJ) kind = LB_NS;

        LineUnit *prev = used ? &units[used - 1] : NULL;
        if (kind == LB_CM || kind == LB_ZWJ) {
            if (prev && !hard_break(prev->kind) && prev->kind != LB_SP && prev->kind != LB_ZW) {
                prev->last_zwj = raw == LB_ZWJ;
                continue;
            }
            kind = LB_AL;
            flags = 0;
            scalar = 'A';
        }
        size_t nonspace = kind != LB_SP ? used : prev ? prev->nonspace : SIZE_MAX;
        bool odd_regional = kind == LB_RI && (!prev || !prev->odd_regional);
        bool numeric_before = kind == LB_NU ||
            ((kind == LB_IS || kind == LB_SY) && prev && prev->numeric_before);
        units[used++] = (LineUnit){scalar, at, nonspace, (uint8_t)kind, flags,
                                  raw == LB_ZWJ, odd_regional, numeric_before};
    }
    return used;
}

static size_t find_breaks(const LineUnit *units, size_t used, size_t byte_count, NtLineBreak *out)
{
    size_t count = 0;
    for (size_t i = 1; i < used; ++i) {
        if (!permits_break(units, used, i)) continue;
        if (out) out[count] = (NtLineBreak){units[i].offset, hard_break(units[i - 1].kind)};
        ++count;
    }
    if (out) out[count] = (NtLineBreak){byte_count, true};
    return count + 1;
}

NtResult nt_line_breaks(const NtAllocator *allocator, NtByteView input,
                        NtLineBreak *breaks, size_t capacity, size_t *count)
{
    NtAllocator resolved;
    if (!count || (!breaks && capacity) || !nt_allocator_get(allocator, &resolved))
        return NT_RESULT_INVALID_ARGUMENT;
    size_t scalars;
    NtResult result = nt_utf8_validate(input, &scalars);
    if (result != NT_RESULT_OK) return result;
    if (scalars > SIZE_MAX / sizeof(LineUnit)) return NT_RESULT_OUT_OF_RANGE;

    const size_t bytes = scalars * sizeof(LineUnit);
    LineUnit *units = bytes ? nt_allocate(&resolved, bytes, NT_ALIGNMENT(LineUnit)) : NULL;
    if (bytes && !units) return NT_RESULT_ALLOCATION_FAILED;

    const size_t used = units ? build_units(input, units) : 0;
    const size_t required = find_breaks(units, used, input.count, NULL);
    if (breaks && capacity >= required)
        (void)find_breaks(units, used, input.count, breaks);
    else if (!nt_is_count_query(breaks, capacity))
        result = NT_RESULT_INSUFFICIENT_BUFFER;
    nt_release(&resolved, units, bytes, NT_ALIGNMENT(LineUnit));
    *count = required;
    return result;
}
