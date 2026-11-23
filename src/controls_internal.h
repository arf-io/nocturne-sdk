#ifndef NOCTURNE_CONTROLS_INTERNAL_H
#define NOCTURNE_CONTROLS_INTERNAL_H

#include <nocturne/controls.h>

#include "component_internal.h"

typedef struct NtControlPress {
    NtPointerId pointer;
    bool tracking, inside;
} NtControlPress;

NtResult nt_control_validate_mount(const NtTree *tree, NtNodeId parent, const NtNodeId *out);
NtResult nt_control_lookup(const NtTree *tree, NtNodeId node, const NtComponentOps *ops, void **out);
NtResult nt_control_leaf(const void *state, NtLayoutContext *context, NtCellSize assigned);
NtCellSize nt_control_fit(NtCellSize size, NtConstraints constraints);
NtCellStyle nt_control_colors(NtNodeId node, NtControlStyle style, bool engaged);
bool nt_control_style_valid(NtControlStyle style);
const NtAllocator *nt_control_allocator(const NtAllocator *allocator);
NtResult nt_control_text_layout(const NtAllocator *allocator, const NtText *text, NtWidthPolicy width,
                                NtWrapMode wrap, NtConstraints bounds, bool owned, NtTextLayout **out);
NtResult nt_control_draw_label(const NtAllocator *allocator, const NtText *text, NtWidthPolicy width,
                                NtPaintContext *paint, uint32_t x, uint32_t y, uint32_t columns,
                                NtCellStyle colors);
NtResult nt_control_emit(NtEventContext *context, NtControlEventKind kind, uint64_t value);
bool nt_control_pointer_inside(NtNodeId node, const NtPointerInput *pointer);
bool nt_control_pressed(NtNodeId node, NtControlPress press);
bool nt_control_press(NtNodeId node, NtControlPress *press, const NtEvent *event, NtEventReply *reply);
uint32_t nt_control_key(const NtEvent *event, bool repeat);

#endif
