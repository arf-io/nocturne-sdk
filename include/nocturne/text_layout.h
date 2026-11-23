#ifndef NOCTURNE_TEXT_LAYOUT_H
#define NOCTURNE_TEXT_LAYOUT_H

#include <nocturne/text_analysis.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NtTextLayout NtTextLayout;

typedef enum NtCaretAffinity { NT_CARET_LEADING, NT_CARET_TRAILING } NtCaretAffinity;
typedef struct NtCaret { size_t offset; NtCaretAffinity affinity; } NtCaret;
typedef enum NtWrapMode { NT_WRAP_NONE, NT_WRAP_WORD, NT_WRAP_CLUSTER } NtWrapMode;
typedef enum NtTextAlign { NT_TEXT_ALIGN_START, NT_TEXT_ALIGN_CENTER, NT_TEXT_ALIGN_END } NtTextAlign;

typedef struct NtTextLayoutOptions {
    NtWidthPolicy width;
    NtConstraints bounds;
    NtWrapMode wrap;
    NtTextAlign align;
    NtDirection base_direction;
    uint32_t tab_stop;
    bool ellipsis;
} NtTextLayoutOptions;

typedef struct NtClusterView {
    NtClusterId id;
    NtByteSpan source;
    NtByteView glyph;
    NtCellPoint position;
    uint32_t columns;
    uint8_t bidi_level;
} NtClusterView;

/* Construct an immutable terminal layout. The owned form copies text bytes
 * and preserves cluster IDs independently of later edits and destruction.
 * The borrowed form keeps a reference to the caller's unchanged bytes until
 * the layout is destroyed. Its IDs are local to this layout. Both require
 * options and a nonzero tab stop. Errors leave the output unchanged. */
NtResult nt_text_layout_create(const NtAllocator *allocator, const NtText *text,
                               const NtTextLayoutOptions *options, NtTextLayout **out);
NtResult nt_text_layout_create_borrowed(const NtAllocator *allocator, NtByteView text,
                                        const NtTextLayoutOptions *options, NtTextLayout **out);
void nt_text_layout_destroy(NtTextLayout *layout);
NtCellSize nt_text_layout_size(const NtTextLayout *layout);
/* Borrow the immutable logical source for the lifetime of the layout. */
NtByteView nt_text_layout_source(const NtTextLayout *layout);
/* Enumerate drawable entries in visual row-major order. A tab has a single
 * space glyph and its full column count. An ellipsis has ID zero and a
 * zero-length source span at the omission boundary. Glyph views expire
 * when the layout is destroyed. */
size_t nt_text_layout_cluster_count(const NtTextLayout *layout);
NtResult nt_text_layout_cluster(const NtTextLayout *layout, size_t index, NtClusterView *out);
/* Hit a caret grid position. X is an edge column (zero through the layout
 * width, inclusive). Y is a row. Padding snaps to the nearest edge. Shared
 * edges prefer the glyph on the left, and an interior tie snaps right. */
NtResult nt_text_layout_hit(const NtTextLayout *layout, NtCellPoint cell, NtCaret *out);
/* LEADING refers to a cluster start and TRAILING to a cluster end,
 * both in logical byte order. Map a visible caret to its visual grid edge.
 * Interior bytes and clipped content do not have caret positions. */
NtResult nt_text_layout_caret_position(const NtTextLayout *layout, NtCaret caret,
                                       NtCellPoint *out);

#ifdef __cplusplus
}
#endif
#endif
