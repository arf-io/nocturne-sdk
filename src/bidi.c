/* UBA rules adapted from native/src/bidi.c and document_bidi.inc at 53191655ec77.
 * Copyright (c) 2025-2026 Damien Heiser. MIT, see LICENSE. */
#include "text_analysis_internal.h"
#include "allocator.h"

typedef enum BracketType { BRACKET_NONE, BRACKET_OPEN, BRACKET_CLOSE } BracketType;
typedef struct BidiPair {
    uint32_t scalar, pair;
    BracketType type;
} BidiPair;

#include "unicode_bidi.inc"

typedef struct BidiEntry {
    NtBidiScalar result;
    uint8_t original, type, embedding;
} BidiEntry;

struct NtBidi {
    NtAllocator allocator;
    BidiEntry *entries;
    size_t count, bytes;
};

typedef struct BidiWork {
    size_t match, run;
    uint8_t isolate_direction;
} BidiWork;

typedef struct LevelRun {
    size_t begin, end, next;
    bool continued;
} LevelRun;

typedef struct BidiScratch {
    BidiWork *work;
    LevelRun *runs;
    size_t *indices, *order;
} BidiScratch;

NT_DECLARE_ALIGNMENT(NtBidi);
NT_DECLARE_ALIGNMENT(BidiEntry);
NT_DECLARE_ALIGNMENT(BidiWork);
NT_DECLARE_ALIGNMENT(LevelRun);
NT_DECLARE_ALIGNMENT(size_t);

static const BidiPair *pair_for(const BidiPair *table, size_t count, uint32_t scalar)
{
    size_t low = 0;
    while (low < count) {
        const size_t middle = low + (count - low) / 2;
        if (scalar < table[middle].scalar)
            count = middle;
        else if (scalar > table[middle].scalar)
            low = middle + 1;
        else
            return &table[middle];
    }
    return NULL;
}

static uint32_t mirrored(uint32_t scalar)
{
    const BidiPair *pair = pair_for(bidi_mirrors, NT_ARRAY_COUNT(bidi_mirrors), scalar);
    return pair ? pair->pair : scalar;
}

static bool isolate(BidiClass type)
{
    return type == BC_LRI || type == BC_RLI || type == BC_FSI;
}

static bool removed(BidiClass type)
{
    return type == BC_BN || type == BC_LRE || type == BC_RLE ||
           type == BC_LRO || type == BC_RLO || type == BC_PDF;
}

static bool neutral(BidiClass type)
{
    return type == BC_B || type == BC_S || type == BC_WS || type == BC_ON ||
           isolate(type) || type == BC_PDI;
}

static BidiClass strong_type(BidiClass type)
{
    if (type == BC_L) return BC_L;
    if (type == BC_R || type == BC_EN || type == BC_AN) return BC_R;
    return BC_ON;
}

/* Match isolate boundaries and find the first strong type at each isolate
 * depth. The temporary parent link makes P2 and X5c linear even with nesting
 * beyond the explicit embedding stack limit. */
static uint8_t paragraph_context(const BidiEntry *entries, size_t count, NtDirection direction,
                                 BidiWork *work)
{
    size_t active = SIZE_MAX;
    BidiClass first_strong = BC_ON;
    for (size_t i = 0; i < count; ++i) {
        const BidiClass type = entries[i].original;
        work[i] = (BidiWork){SIZE_MAX, SIZE_MAX, BC_ON};
        if (isolate(type)) {
            work[i].run = active;
            active = i;
        } else if (type == BC_PDI && active != SIZE_MAX) {
            work[active].match = i;
            active = work[active].run;
        } else if (type == BC_L || type == BC_R || type == BC_AL) {
            if (active == SIZE_MAX) {
                if (first_strong == BC_ON) first_strong = type;
            } else if (work[active].isolate_direction == BC_ON) {
                work[active].isolate_direction = type;
            }
        }
    }
    return (uint8_t)(direction == NT_DIRECTION_RTL ||
        (direction == NT_DIRECTION_AUTO && (first_strong == BC_R || first_strong == BC_AL)));
}

typedef struct BidiStatus {
    uint8_t level;
    BidiClass override;
    bool isolate;
} BidiStatus;

static void explicit_levels(BidiEntry *entries, size_t count, const BidiWork *work, uint8_t base)
{
    BidiStatus stack[126] = {{base, BC_ON, false}};
    size_t top = 0, overflow_isolates = 0, overflow_embeddings = 0, valid_isolates = 0;
    for (size_t i = 0; i < count; ++i) {
        BidiEntry *entry = &entries[i];
        const BidiClass original = entry->original;
        BidiClass type = original;
        uint8_t level = stack[top].level;
        if (original == BC_LRE || original == BC_RLE || original == BC_LRO || original == BC_RLO) {
            const bool odd = original == BC_RLE || original == BC_RLO;
            const unsigned next = odd ? (level + 1u) | 1u : (level + 2u) & ~1u;
            if (next <= 125 && !overflow_isolates && !overflow_embeddings) {
                const BidiClass override = original == BC_LRO ? BC_L :
                                           original == BC_RLO ? BC_R : BC_ON;
                stack[++top] = (BidiStatus){(uint8_t)next, override, false};
            } else if (!overflow_isolates) {
                ++overflow_embeddings;
            }
        } else if (isolate(original)) {
            const uint8_t first = work[i].isolate_direction;
            const bool odd = original == BC_RLI || (original == BC_FSI &&
                                                     (first == BC_R || first == BC_AL));
            if (stack[top].override != BC_ON) type = stack[top].override;
            const unsigned next = odd ? (level + 1u) | 1u : (level + 2u) & ~1u;
            if (next <= 125 && !overflow_isolates && !overflow_embeddings) {
                stack[++top] = (BidiStatus){(uint8_t)next, BC_ON, true};
                ++valid_isolates;
            } else {
                ++overflow_isolates;
            }
        } else if (original == BC_PDI) {
            if (overflow_isolates) {
                --overflow_isolates;
            } else if (valid_isolates) {
                overflow_embeddings = 0;
                while (!stack[top].isolate) --top;
                --top;
                --valid_isolates;
            }
            level = stack[top].level;
            if (stack[top].override != BC_ON) type = stack[top].override;
        } else if (original == BC_PDF) {
            if (!overflow_isolates) {
                if (overflow_embeddings)
                    --overflow_embeddings;
                else if (top && !stack[top].isolate)
                    --top;
            }
        } else if (original == BC_B) {
            level = base;
        } else if (original != BC_BN && stack[top].override != BC_ON) {
            type = stack[top].override;
        }
        entry->embedding = level;
        entry->type = (uint8_t)type;
        entry->result.paragraph_level = base;
        entry->result.level = removed(original) ? NT_BIDI_REMOVED_LEVEL : level;
    }
}

static void resolve_weak(BidiEntry *entries, const size_t *order, size_t count, BidiClass sos)
{
    BidiClass previous = sos, strong = sos;
    for (size_t k = 0; k < count; ++k) {                              /* W1 */
        BidiEntry *entry = &entries[order[k]];
        if (entry->type == BC_NSM)
            entry->type = isolate(previous) || previous == BC_PDI ? BC_ON : (uint8_t)previous;
        previous = entry->type;
    }
    for (size_t k = 0; k < count; ++k) {                              /* W2 */
        BidiEntry *entry = &entries[order[k]];
        if (entry->type == BC_L || entry->type == BC_R || entry->type == BC_AL)
            strong = entry->type;
        else if (entry->type == BC_EN && strong == BC_AL)
            entry->type = BC_AN;
    }
    for (size_t k = 0; k < count; ++k)                               /* W3 */
        if (entries[order[k]].type == BC_AL) entries[order[k]].type = BC_R;
    for (size_t k = 1; k + 1 < count; ++k) {                          /* W4 */
        BidiEntry *entry = &entries[order[k]];
        const BidiClass a = entries[order[k - 1]].type;
        const BidiClass b = entries[order[k + 1]].type;
        if (a == b && ((entry->type == BC_ES && a == BC_EN) ||
                       (entry->type == BC_CS && (a == BC_EN || a == BC_AN))))
            entry->type = (uint8_t)a;
    }
    for (size_t k = 0; k < count;) {                                  /* W5 */
        if (entries[order[k]].type != BC_ET) { ++k; continue; }
        const size_t first = k;
        while (k < count && entries[order[k]].type == BC_ET) ++k;
        if ((first && entries[order[first - 1]].type == BC_EN) ||
            (k < count && entries[order[k]].type == BC_EN))
            for (size_t j = first; j < k; ++j) entries[order[j]].type = BC_EN;
    }
    strong = sos;
    for (size_t k = 0; k < count; ++k) {                              /* W6, W7 */
        BidiEntry *entry = &entries[order[k]];
        if (entry->type == BC_ES || entry->type == BC_ET || entry->type == BC_CS)
            entry->type = BC_ON;
        if (entry->type == BC_L || entry->type == BC_R)
            strong = entry->type;
        else if (entry->type == BC_EN && strong == BC_L)
            entry->type = BC_L;
    }
}

static uint32_t canonical_bracket(uint32_t scalar)
{
    return scalar == 0x2329 ? 0x3008 : scalar == 0x232a ? 0x3009 : scalar;
}

static bool find_bracket_pairs(const BidiEntry *entries, const size_t *order,
                               size_t count, BidiWork *work)
{
    size_t depth = 0;
    struct {
        size_t position;
        uint32_t closing;
    } stack[63];

    for (size_t k = 0; k < count; ++k) {
        const size_t i = order[k];
        work[i].match = SIZE_MAX;
        if (entries[i].type != BC_ON) continue;
        const uint32_t scalar = entries[i].result.scalar;
        const BidiPair *pair = pair_for(bidi_brackets, NT_ARRAY_COUNT(bidi_brackets), scalar);
        if (!pair) continue;
        if (pair->type == BRACKET_OPEN) {
            if (depth == NT_ARRAY_COUNT(stack)) return false;
            stack[depth].position = k;
            stack[depth++].closing = canonical_bracket(pair->pair);
        } else {
            for (size_t j = depth; j > 0; --j) {
                if (stack[j - 1].closing != canonical_bracket(scalar)) continue;
                work[order[stack[j - 1].position]].match = k;
                depth = j - 1;
                break;
            }
        }
    }
    return true;
}

static void resolve_brackets(BidiEntry *entries, const size_t *order, size_t count,
                             BidiWork *work, BidiClass sos)
{
    if (!find_bracket_pairs(entries, order, count, work)) return;    /* BD16 */
    BidiClass preceding = sos;
    for (size_t k = 0; k < count; ++k) {                              /* N0 */
        BidiEntry *entry = &entries[order[k]];
        const size_t close = work[order[k]].match;
        if (close != SIZE_MAX) {
            const BidiClass embedding = entry->embedding & 1u ? BC_R : BC_L;
            BidiClass resolved = BC_ON;
            for (size_t j = k + 1; j < close; ++j) {
                const BidiClass direction = strong_type(entries[order[j]].type);
                if (direction != BC_ON) resolved = direction;
                if (direction == embedding) break;
            }
            if (resolved != BC_ON) {
                if (resolved != embedding && preceding != resolved) resolved = embedding;
                entry->type = entries[order[close]].type = (uint8_t)resolved;
                for (size_t j = k + 1; j < count && entries[order[j]].original == BC_NSM; ++j)
                    entries[order[j]].type = (uint8_t)resolved;
                for (size_t j = close + 1; j < count && entries[order[j]].original == BC_NSM; ++j)
                    entries[order[j]].type = (uint8_t)resolved;
            }
        }
        const BidiClass strong = strong_type(entry->type);
        if (strong != BC_ON) preceding = strong;
    }
}

static void resolve_neutral_and_implicit(BidiEntry *entries, const size_t *order, size_t count,
                                         BidiClass sos, BidiClass eos)
{
    for (size_t k = 0; k < count;) {                                  /* N1–N2 */
        if (!neutral(entries[order[k]].type)) { ++k; continue; }
        const size_t first = k;
        while (k < count && neutral(entries[order[k]].type)) ++k;
        BidiClass before = first ? strong_type(entries[order[first - 1]].type) : sos;
        BidiClass after = k < count ? strong_type(entries[order[k]].type) : eos;
        for (size_t j = first; j < k; ++j) {
            BidiEntry *entry = &entries[order[j]];
            entry->type = (uint8_t)(before == after ? before : (entry->embedding & 1u ? BC_R : BC_L));
        }
    }
    for (size_t k = 0; k < count; ++k) {                              /* I1–I2 */
        BidiEntry *entry = &entries[order[k]];
        const bool number = entry->type == BC_AN || entry->type == BC_EN;
        if (entry->embedding & 1u) {
            if (entry->type == BC_L || number) ++entry->result.level;
        } else if (entry->type == BC_R) {
            ++entry->result.level;
        } else if (number) {
            entry->result.level += 2;
        }
    }
}

static size_t collect_runs(const BidiEntry *entries, size_t count, BidiScratch scratch,
                           size_t *retained)
{
    size_t used = 0, length = 0;
    for (size_t i = 0; i < count; ++i) {
        if (removed(entries[i].original)) continue;
        if (!used || entries[scratch.indices[length - 1]].embedding != entries[i].embedding)
            scratch.runs[used++] = (LevelRun){length, length, SIZE_MAX, false};
        scratch.runs[used - 1].end = length + 1;
        scratch.work[i].run = used - 1;
        scratch.indices[length++] = i;
    }
    for (size_t r = 0; r < used; ++r) {
        const size_t i = scratch.indices[scratch.runs[r].end - 1];
        if (!isolate(entries[i].original) || scratch.work[i].match == SIZE_MAX) continue;
        const size_t target = scratch.work[scratch.work[i].match].run;
        if (target > r && entries[scratch.indices[scratch.runs[target].begin]].embedding ==
                          entries[i].embedding) {
            scratch.runs[r].next = target;
            scratch.runs[target].continued = true;
        }
    }
    *retained = length;
    return used;
}

static BidiClass context_direction(uint8_t a, uint8_t b)
{
    return ((a > b ? a : b) & 1u) ? BC_R : BC_L;
}

static void resolve_paragraph(BidiEntry *entries, size_t count,
                               NtDirection direction, BidiScratch scratch)
{
    const uint8_t base = paragraph_context(entries, count, direction, scratch.work);
    explicit_levels(entries, count, scratch.work, base);
    size_t retained;
    const size_t runs = collect_runs(entries, count, scratch, &retained);
    for (size_t root = 0; root < runs; ++root) {
        if (scratch.runs[root].continued) continue;
        size_t last = root, used = 0;
        for (;;) {
            const LevelRun run = scratch.runs[last];
            for (size_t k = run.begin; k < run.end; ++k)
                scratch.order[used++] = scratch.indices[k];
            if (run.next == SIZE_MAX) break;
            last = run.next;
        }
        const size_t start = scratch.runs[root].begin;
        const size_t end = scratch.runs[last].end;
        const BidiEntry first_entry = entries[scratch.order[0]];
        const BidiEntry last_entry = entries[scratch.order[used - 1]];
        const uint8_t left = start ? entries[scratch.indices[start - 1]].embedding : base;
        const uint8_t right = end < retained && !isolate(last_entry.original) ?
                              entries[scratch.indices[end]].embedding : base;
        const BidiClass sos = context_direction(first_entry.embedding, left);
        const BidiClass eos = context_direction(last_entry.embedding, right);
        resolve_weak(entries, scratch.order, used, sos);
        resolve_brackets(entries, scratch.order, used, scratch.work, sos);
        resolve_neutral_and_implicit(entries, scratch.order, used, sos, eos);
    }
}

static NtResult resolve_all(NtBidi *bidi, NtDirection direction)
{
    if (!bidi->count) return NT_RESULT_OK;
    const size_t n = bidi->count;
    if (n > SIZE_MAX / sizeof(LevelRun) || n > SIZE_MAX / sizeof(BidiWork) ||
        n > SIZE_MAX / (2 * sizeof(size_t))) return NT_RESULT_OUT_OF_RANGE;
    const NtAllocator *allocator = &bidi->allocator;
    BidiWork *work = nt_allocate(allocator, n * sizeof(*work), NT_ALIGNMENT(BidiWork));
    LevelRun *runs = nt_allocate(allocator, n * sizeof(*runs), NT_ALIGNMENT(LevelRun));
    size_t *indices = nt_allocate(allocator, 2 * n * sizeof(*indices), NT_ALIGNMENT(size_t));
    NtResult result = NT_RESULT_ALLOCATION_FAILED;
    if (work && runs && indices) {
        const BidiScratch scratch = {work, runs, indices, indices + n};
        for (size_t begin = 0; begin < n;) {
            size_t end = begin;
            while (end < n && bidi->entries[end++].original != BC_B) {}
            resolve_paragraph(bidi->entries + begin, end - begin, direction, scratch);
            begin = end;
        }
        result = NT_RESULT_OK;
    }
    nt_release(allocator, indices, 2 * n * sizeof(*indices), NT_ALIGNMENT(size_t));
    nt_release(allocator, runs, n * sizeof(*runs), NT_ALIGNMENT(LevelRun));
    nt_release(allocator, work, n * sizeof(*work), NT_ALIGNMENT(BidiWork));
    return result;
}

NtResult nt_bidi_create(const NtAllocator *allocator, NtByteView input,
                        NtDirection direction, NtBidi **out)
{
    NtAllocator resolved;
    if (!out || !nt_allocator_get(allocator, &resolved) ||
        (direction != NT_DIRECTION_AUTO && direction != NT_DIRECTION_LTR &&
         direction != NT_DIRECTION_RTL)) return NT_RESULT_INVALID_ARGUMENT;

    size_t count;
    NtResult result = nt_utf8_validate(input, &count);
    if (result != NT_RESULT_OK) return result;
    if (count > SIZE_MAX / sizeof(BidiEntry)) return NT_RESULT_OUT_OF_RANGE;
    NtBidi *bidi = nt_allocate(&resolved, sizeof(*bidi), NT_ALIGNMENT(NtBidi));
    if (!bidi) return NT_RESULT_ALLOCATION_FAILED;
    *bidi = (NtBidi){resolved, NULL, count, input.count};
    if (count) {
        bidi->entries = nt_allocate(&resolved, count * sizeof(*bidi->entries), NT_ALIGNMENT(BidiEntry));
        if (!bidi->entries) {
            nt_bidi_destroy(bidi);
            return NT_RESULT_ALLOCATION_FAILED;
        }
    }
    size_t offset = 0;
    for (size_t i = 0; i < count; ++i) {
        const size_t first = offset;
        uint32_t scalar = 0;
        (void)nt_utf8_decode(input, &offset, &scalar);
        const uint8_t type = (uint8_t)nt_unicode_property(bidi_classes, NT_ARRAY_COUNT(bidi_classes),
                                                          scalar, BC_L);
        const NtBidiScalar logical = {{first, offset}, scalar, 0, 0, type == BC_B};
        bidi->entries[i] = (BidiEntry){logical, type, type, 0};
    }
    result = resolve_all(bidi, direction);
    if (result != NT_RESULT_OK) {
        nt_bidi_destroy(bidi);
        return result;
    }
    *out = bidi;
    return NT_RESULT_OK;
}

void nt_bidi_destroy(NtBidi *bidi)
{
    if (!bidi) return;
    const NtAllocator allocator = bidi->allocator;
    nt_release(&allocator, bidi->entries, bidi->count * sizeof(*bidi->entries), NT_ALIGNMENT(BidiEntry));
    nt_release(&allocator, bidi, sizeof(*bidi), NT_ALIGNMENT(NtBidi));
}

size_t nt_bidi_count(const NtBidi *bidi)
{
    return bidi ? bidi->count : 0;
}

NtResult nt_bidi_scalar(const NtBidi *bidi, size_t index, NtBidiScalar *out)
{
    if (!bidi || !out) return NT_RESULT_INVALID_ARGUMENT;
    if (index >= bidi->count) return NT_RESULT_OUT_OF_RANGE;
    *out = bidi->entries[index].result;
    return NT_RESULT_OK;
}

static size_t offset_to_index(const NtBidi *bidi, size_t offset)
{
    if (offset == bidi->bytes) return bidi->count;
    size_t lo = 0, hi = bidi->count;
    while (lo < hi) {
        const size_t middle = lo + (hi - lo) / 2;
        const size_t begin = bidi->entries[middle].result.source.begin;
        if (offset < begin)
            hi = middle;
        else if (offset > begin)
            lo = middle + 1;
        else
            return middle;
    }
    return SIZE_MAX;
}

static void reset_and_reorder(const NtBidi *bidi, NtBidiVisual *visual, size_t count)
{
    const uint8_t base = bidi->entries[visual[0].logical_index].result.paragraph_level;
    bool reset = true;
    int highest = 0, lowest_odd = NT_BIDI_REMOVED_LEVEL;
    for (size_t i = count; i > 0;) {                                  /* L1 */
        NtBidiVisual *item = &visual[--i];
        const BidiClass type = bidi->entries[item->logical_index].original;
        if (type == BC_B || type == BC_S)
            reset = true;
        else if (type != BC_WS && !isolate(type) && type != BC_PDI)
            reset = false;
        if (reset) item->level = base;
        if (item->level & 1u) {
            item->display_scalar = mirrored(item->display_scalar);   /* L4 mapping */
            if (item->level < lowest_odd) lowest_odd = item->level;
        }
        if (item->level > highest) highest = item->level;
    }
    for (int level = highest; level >= lowest_odd; --level) {         /* L2 */
        for (size_t k = 0; k < count;) {
            if (visual[k].level < level) { ++k; continue; }
            size_t first = k;
            while (k < count && visual[k].level >= level) ++k;
            for (size_t last = k - 1; first < last; ++first, --last) {
                const NtBidiVisual swap = visual[first];
                visual[first] = visual[last];
                visual[last] = swap;
            }
        }
    }
}

NtResult nt_bidi_reorder_line(const NtBidi *bidi, NtByteSpan line,
                              NtBidiVisual *visual, size_t capacity, size_t *count)
{
    if (!bidi || !count || (!visual && capacity)) return NT_RESULT_INVALID_ARGUMENT;
    if (line.begin > line.end || line.end > bidi->bytes) return NT_RESULT_OUT_OF_RANGE;
    const size_t first = offset_to_index(bidi, line.begin);
    const size_t last = offset_to_index(bidi, line.end);
    if (first == SIZE_MAX || last == SIZE_MAX) return NT_RESULT_INVALID_ARGUMENT;
    size_t needed = 0;
    for (size_t i = first; i < last; ++i) {
        const BidiEntry *entry = &bidi->entries[i];
        if (entry->original == BC_B && i + 1 < last) return NT_RESULT_INVALID_ARGUMENT;
        if (entry->result.level != NT_BIDI_REMOVED_LEVEL) ++needed;
    }
    *count = needed;
    if (nt_is_count_query(visual, capacity)) return NT_RESULT_OK;
    if (capacity < needed) return NT_RESULT_INSUFFICIENT_BUFFER;
    size_t used = 0;
    for (size_t i = first; i < last; ++i) {
        const NtBidiScalar entry = bidi->entries[i].result;
        if (entry.level != NT_BIDI_REMOVED_LEVEL)
            visual[used++] = (NtBidiVisual){i, entry.source, entry.scalar, entry.level};
    }
    if (used) reset_and_reorder(bidi, visual, used);
    return NT_RESULT_OK;
}
