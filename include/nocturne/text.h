#ifndef NOCTURNE_TEXT_H
#define NOCTURNE_TEXT_H

#include <nocturne/base.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NtByteSpan {
    size_t begin, end;
} NtByteSpan;

/* The implementation's pinned Unicode version, e.g. "17.0.0".
 * The returned string has static lifetime. */
const char *nt_unicode_version(void);

/* Decode one strict UTF-8 scalar at *offset. Advance to the next byte on
 * success. Invalid input or pointers return INVALID_ARGUMENT. An offset
 * at or beyond input.count returns OUT_OF_RANGE. Outputs stay unchanged
 * on any error. */
NtResult nt_utf8_decode(NtByteView input, size_t *offset, uint32_t *scalar);

/* Accepts an empty view with data == NULL, and counts Unicode scalars
 * (including U+0000). scalar_count may be NULL. It stays unchanged on error. */
NtResult nt_utf8_validate(NtByteView input, size_t *scalar_count);

typedef struct NtGraphemeIterator {
    NtByteView input;
    size_t offset;
} NtGraphemeIterator;

/* Validate the full input before initializing the iterator. It borrows input
 * for all future calls, so the bytes must stay alive and immutable. The
 * iterator is caller-owned. Initialize before use and access its fields
 * through these functions. It may be copied to fork iteration. */
NtResult nt_grapheme_iterator_init(NtGraphemeIterator *iterator, NtByteView input);

/* Visit the next extended grapheme cluster under Unicode UAX #29. Each span
 * consists of half-open UTF-8 byte offsets in the borrowed input. At the
 * end, set *available to false and leave the span unchanged. All pointers
 * are required. The iterator and outputs stay unchanged on error. */
NtResult nt_grapheme_iterator_next(NtGraphemeIterator *iterator,
                                    NtByteSpan *span, bool *available);

typedef struct NtText NtText;
typedef uint64_t NtClusterId;

typedef struct NtTextCluster {
    NtClusterId id;
    NtByteSpan source;
} NtTextCluster;

/* Create an owned copy of validated UTF-8. The output is changed on success
 * only. An empty view is valid. The caller serializes access to each object. */
NtResult nt_text_create(const NtAllocator *allocator, NtByteView input, NtText **out);
void nt_text_destroy(NtText *text);

/* A counted borrow, valid until the next successful content change or
 * destruction. An empty or null object returns a null, zero-length view. */
NtByteView nt_text_bytes(const NtText *text);
size_t nt_text_cluster_count(const NtText *text);
NtResult nt_text_cluster(const NtText *text, size_t index, NtTextCluster *out);

/* Replace a half-open byte range at scalar boundaries. Replacement may
 * borrow from this text. Byte-identical replacement is a no-op. IDs are
 * nonzero and local to this text. An untouched cluster retains its ID when
 * the same source bytes still form exactly one cluster after the edit.
 * New or changed clusters get fresh IDs. Failure leaves the object intact. */
NtResult nt_text_replace(NtText *text, NtByteSpan source, NtByteView replacement);

typedef enum NtGlyphPresentation {
    NT_GLYPH_TEXT,
    NT_GLYPH_EMOJI,
    NT_GLYPH_ISOLATED_MARK,
    NT_GLYPH_REPLACEMENT,
    NT_GLYPH_INVISIBLE,
    NT_GLYPH_TAB,
    NT_GLYPH_LINE_BREAK
} NtGlyphPresentation;

typedef struct NtGlyphMetrics {
    NtGlyphPresentation presentation;
    uint8_t columns;
} NtGlyphMetrics;

/* Measure exactly one complete extended grapheme cluster under the given
 * width policy. Tabs and line breaks have zero intrinsic columns. Controls
 * requiring a visible replacement and isolated marks each reserve one cell.
 * See CONTRACTS.md for the Unicode data and presentation rules. Malformed,
 * empty or multi-cluster input, an invalid policy, or a null output returns
 * INVALID_ARGUMENT. The output stays unchanged on failure. */
NtResult nt_glyph_measure(NtByteView glyph, NtWidthPolicy policy, NtGlyphMetrics *out);

#ifdef __cplusplus
}
#endif
#endif
