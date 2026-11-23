#ifndef NOCTURNE_LAYOUT_H
#define NOCTURNE_LAYOUT_H

#include <nocturne/component.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum NtBoxLayoutKind {
    NT_BOX_LAYOUT_ROW, NT_BOX_LAYOUT_COLUMN, NT_BOX_LAYOUT_STACK
} NtBoxLayoutKind;

typedef enum NtBoxAlign { NT_BOX_START, NT_BOX_CENTER, NT_BOX_END } NtBoxAlign;
typedef struct NtBoxInsets { uint32_t left, top, right, bottom; } NtBoxInsets;

typedef struct NtBoxItem {
    NtExtentRange columns, rows;
    bool grow_columns, grow_rows;
} NtBoxItem;

typedef struct NtBoxLayout {
    NtBoxLayoutKind kind;
    NtBoxInsets padding;
    uint32_t gap;                 /* Cells between adjacent row or column children. */
    NtBoxAlign horizontal, vertical;
    NtBoxItem fallback;
    const NtBoxItem *items;       /* Overrides by direct-child insertion index. */
    size_t item_count;
} NtBoxLayout;

/* Mount a container with owned copies of layout and its item overrides.
 * A zero-initialized layout is an intrinsic row aligned to the upper left.
 * The fallback applies after the last override. Bounded min == max fixes
 * that extent. Space is shared equally among eligible children, with
 * earlier children taking leftover cells. Padding clips child paint/hits. */
NtResult nt_box_layout_mount(NtTree *tree, NtNodeId parent, NtMountOptions options,
                             const NtBoxLayout *layout, NtNodeId *out);
/* Replace the configuration of a mounted box layout and invalidate layout.
 * On failure the existing configuration and published frame stay valid. */
NtResult nt_box_layout_set(NtTree *tree, NtNodeId node, const NtBoxLayout *layout);

#ifdef __cplusplus
}
#endif
#endif
