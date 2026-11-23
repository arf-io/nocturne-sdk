#ifndef NOCTURNE_GO_BRIDGE_H
#define NOCTURNE_GO_BRIDGE_H

#include <nocturne/component.h>
#include <nocturne/paint.h>
#include <nocturne/present.h>

NtResult nt_go_mount(NtTree *tree, NtNodeId parent, uintptr_t handle,
                     NtMountOptions options, NtNodeId *out);
NtResult nt_go_mount_panel_content(NtTree *tree, NtNodeId parent, uintptr_t handle,
                                   NtMountOptions options, NtCellSize preferred,
                                   bool has_update, bool has_event, NtNodeId *out);
NtResult nt_go_present_ansi(const NtFrame *frame, uintptr_t handle, NtTerminalCapabilities capabilities,
                            NtRgba matte, NtPaletteView palette, NtTimeUs now);
bool nt_go_context_current(const void *context);
const void *nt_go_swap_context(const void *context);
NtKeyInput nt_go_event_key(const NtEvent *event);
NtByteView nt_go_event_text(const NtEvent *event);

NtPointerInput nt_go_event_pointer(const NtEvent *event);
NtPointerCancel nt_go_event_cancel(const NtEvent *event);
NtWheelInput nt_go_event_wheel(const NtEvent *event);
NtCellSize nt_go_event_size(const NtEvent *event);

typedef struct NtGoInput {
    NtEventKind kind;
    NtTimeUs at;
    NtKeyInput key;
    NtPointerInput pointer;
    NtPointerCancel cancel;
    NtWheelInput wheel;
    NtCellSize size;
} NtGoInput;

NtResult nt_go_dispatch(NtTree *tree, const NtGoInput *input, NtByteView text,
                        const NtKeyChord *reserved, size_t reserved_count,
                        bool traverse_focus, NtDispatchResult *out);

#endif
