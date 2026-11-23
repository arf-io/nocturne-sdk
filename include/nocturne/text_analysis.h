#ifndef NOCTURNE_TEXT_ANALYSIS_H
#define NOCTURNE_TEXT_ANALYSIS_H

#include <nocturne/text.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum NtDirection {
    NT_DIRECTION_AUTO,
    NT_DIRECTION_LTR,
    NT_DIRECTION_RTL
} NtDirection;

typedef struct NtLineBreak {
    size_t offset;
    bool mandatory;
} NtLineBreak;

/* Unicode 17 default UAX #14 opportunities in increasing UTF-8 byte order.
 * The last opportunity is mandatory, including offset zero for empty text.
 * Use a null array and zero capacity to query count. An insufficient array
 * leaves its elements unchanged and reports the required count. Other errors
 * preserve all outputs. A terminal layout intersects these opportunities
 * with its grapheme boundaries. */
NtResult nt_line_breaks(const NtAllocator *allocator, NtByteView input,
                        NtLineBreak *breaks, size_t capacity, size_t *count);

typedef struct NtBidi NtBidi;

/* Resolve paragraphs using Unicode 17 UAX #9. The result owns decoded scalars
 * and byte offsets, so input bytes may be released when this call returns.
 * AUTO uses each paragraph's first strong type outside isolates, with LTR
 * as fallback. The output is unchanged on failure. */
NtResult nt_bidi_create(const NtAllocator *allocator, NtByteView input,
                        NtDirection direction, NtBidi **out);
void nt_bidi_destroy(NtBidi *bidi);
size_t nt_bidi_count(const NtBidi *bidi);

#define NT_BIDI_REMOVED_LEVEL UINT8_MAX

typedef struct NtBidiScalar {
    NtByteSpan source;
    uint32_t scalar;
    uint8_t level;
    uint8_t paragraph_level;
    bool paragraph_separator; /* Unicode bidi class B. */
} NtBidiScalar;

/* Inspect a logical scalar. Its level precedes line-level resets and is
 * NT_BIDI_REMOVED_LEVEL for the controls excluded by rule X9. A separator
 * belongs to the paragraph it ends. The text end also ends its paragraph. */
NtResult nt_bidi_scalar(const NtBidi *bidi, size_t index, NtBidiScalar *out);

typedef struct NtBidiVisual {
    size_t logical_index;
    NtByteSpan source;
    uint32_t display_scalar;
    uint8_t level;
} NtBidiVisual;

/* Apply UAX #9 L1 and L2 to a line within one paragraph. Both ends must
 * be scalar boundaries. Return records in visual order with resolved levels
 * and Unicode mirror substitutions where available. X9 controls are omitted.
 * Empty lines are valid. Query and insufficient-buffer rules match
 * nt_line_breaks. The caller may reuse the immutable analysis across lines. */
NtResult nt_bidi_reorder_line(const NtBidi *bidi, NtByteSpan line,
                              NtBidiVisual *visual, size_t capacity, size_t *count);

typedef struct NtScriptRun {
    NtByteSpan source;
    char script[5]; /* NUL-terminated ISO 15924 code, e.g. "Latn". */
} NtScriptRun;

/* Resolve Unicode 17 Script and Script_Extensions over complete graphemes
 * and return adjoining runs in logical order. Query and insufficient-buffer
 * rules match nt_line_breaks. Empty text has zero runs. */
NtResult nt_script_runs(const NtAllocator *allocator, NtByteView input,
                        NtScriptRun *runs, size_t capacity, size_t *count);

#ifdef __cplusplus
}
#endif
#endif
