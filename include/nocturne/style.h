#ifndef NOCTURNE_STYLE_H
#define NOCTURNE_STYLE_H

#include <nocturne/property.h>
#include <nocturne/text_paint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t NtStyleTag;
typedef struct NtStyleElement {
    NtStyleTag role, region;
    uint64_t states;
} NtStyleElement;
typedef struct NtStyleCondition {
    NtStyleTag role, region;     /* Zero is a wildcard. */
    uint64_t require, forbid;   /* Disjoint caller-defined state masks. */
} NtStyleCondition;
typedef enum NtStyleRelation { NT_STYLE_SELF, NT_STYLE_PARENT, NT_STYLE_ANCESTOR } NtStyleRelation;
typedef struct NtStyleSelector {
    NtStyleCondition subject, ancestor;
    NtStyleRelation relation;
    NtStyleTag text_region;     /* Zero is a wildcard. */
    NtClusterId cluster_id;     /* Zero is a wildcard. */
    bool match_bytes, match_characters;
    NtByteSpan bytes;           /* Match the first logical byte in [begin, end). */
    uint32_t first_scalar, last_scalar; /* Inclusive Unicode scalar interval. */
} NtStyleSelector;

typedef struct NtStyleRule {
    NtStyleSelector selector;
    NtPropertyId property;
    NtValue value;
    bool override;
} NtStyleRule;
typedef struct NtStyleSheet NtStyleSheet;
typedef struct NtStyleSheets {
    const NtStyleSheet *const *items; /* Earlier sheets have earlier declaration order. */
    size_t count;
} NtStyleSheets;

/* Copy rules and TEXT payloads. Sheet storage is immutable and shareable. */
NtResult nt_style_sheet_create(const NtAllocator *allocator, const NtStyleRule *rules,
                                size_t count, NtStyleSheet **out);
void nt_style_sheet_destroy(NtStyleSheet *sheet);

typedef struct NtStylePath {
    NtStyleElement element;
    const NtStyleElement *ancestors; /* Immediate parent first. */
    size_t ancestor_count;
} NtStylePath;
typedef struct NtStyleRegion { NtStyleTag tag; NtByteSpan bytes; } NtStyleRegion;
typedef struct NtStyleSubject {
    NtStylePath path;
    NtByteView source;
    NtByteSpan text;             /* Empty for a component or a synthetic glyph. */
    NtClusterId cluster_id;
    const NtStyleRegion *regions;
    size_t region_count;
} NtStyleSubject;

/* Highest priority tier, then specificity, then latest declaration.
 * No match writes false to matched and leaves out unchanged. A selected
 * TEXT value borrows from the winning sheet until it is destroyed. */
NtResult nt_style_resolve(NtStyleSheets sheets, const NtStyleSubject *subject, NtPropertyId property,
                           bool *matched, NtValue *out);

typedef struct NtStyleMount { NtNodeId node; NtStyleElement element; } NtStyleMount;
/* Style only the listed writable properties, using current tree ancestry.
 * Mount annotations are unique and optional, defaulting to zero for omitted
 * nodes. A property with no match is untouched. All matched writes commit
 * together. The caller keeps sheets and their views live for the call. */
NtResult nt_tree_apply_styles(NtTree *tree, NtStyleSheets sheets, const NtStyleMount *mounts,
                               size_t mount_count, const NtPropertyRef *targets, size_t target_count);

typedef struct NtTextStyles {
    NtStyleSheets sheets;
    NtStylePath path;
    const NtStyleRegion *regions;
    size_t region_count;
} NtTextStyles;

/* Overlay the winning RGBA foreground and background declarations on each
 * visible cluster's existing span/base style. Other properties are ignored.
 * A matched channel replaces its procedural paint. Ownership is preserved
 * on a surface, or assigned to the mounted node in a paint callback. */
NtResult nt_style_text_paint(const NtSurfaceView *view, NtCellPoint position, const NtTextLayout *layout,
                              const NtTextPaintOptions *paint, const NtTextStyles *styles);
NtResult nt_paint_styled_text(NtPaintContext *paint, NtCellPoint position, const NtTextLayout *layout,
                              const NtTextPaintOptions *options, const NtTextStyles *styles);

#ifdef __cplusplus
}
#endif
#endif
