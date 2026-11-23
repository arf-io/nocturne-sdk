#include "bridge.h"
#include <nocturne/window.h>
#include "_cgo_export.h"
#include <stdlib.h>

typedef struct NtGoState { uintptr_t handle; } NtGoState;

static _Thread_local const void *current_context;

bool nt_go_context_current(const void *context)
{
    return context != NULL && context == current_context;
}

const void *nt_go_swap_context(const void *context)
{
    const void *previous = current_context;
    current_context = context;
    return previous;
}

static NtResult measure(const void *state, NtMeasureContext *context,
                        NtConstraints limits, NtCellSize *preferred)
{
    const NtGoState *owned = state;
    const void *previous = current_context;
    current_context = context;
    const NtResult result = nocturneGoMeasure(owned->handle, context, limits, preferred);
    current_context = previous;
    return result;
}

static NtResult layout(const void *state, NtLayoutContext *context, NtCellSize assigned)
{
    const NtGoState *owned = state;
    const void *previous = current_context;
    current_context = context;
    const NtResult result = nocturneGoLayout(owned->handle, context, assigned);
    current_context = previous;
    return result;
}

static NtResult update(void *state, NtUpdateInfo info, NtUpdateReply *reply)
{
    const NtGoState *owned = state;
    return nocturneGoUpdate(owned->handle, info, reply);
}

static NtResult paint(const void *state, NtPaintContext *context, NtCellSize assigned)
{
    const NtGoState *owned = state;
    const void *previous = current_context;
    current_context = context;
    const NtResult result = nocturneGoPaint(owned->handle, context, assigned);
    current_context = previous;
    return result;
}

static NtResult event(void *state, NtEventContext *context, const NtEvent *input, NtEventReply *reply)
{
    const NtGoState *owned = state;
    const void *previous = current_context;
    current_context = context;
    const NtResult result = nocturneGoEvent(owned->handle, context, (NtEvent *)input, reply);
    current_context = previous;
    return result;
}

static void dispose(void *state)
{
    NtGoState *owned = state;
    nocturneGoDispose(owned->handle);
    free(owned);
}

NtResult nt_go_mount(NtTree *tree, NtNodeId parent, uintptr_t handle,
                     NtMountOptions options, NtNodeId *out)
{
    static const NtComponentOps ops = {measure, layout, update, paint, event, dispose};
    NtGoState *state = malloc(sizeof(*state));
    if (!state) return NT_RESULT_ALLOCATION_FAILED;
    *state = (NtGoState){.handle = handle};
    const NtResult result = nt_tree_mount(tree, parent, &ops, state, options, out);
    if (result != NT_RESULT_OK) free(state);
    return result;
}

NtResult nt_go_mount_panel_content(NtTree *tree, NtNodeId parent, uintptr_t handle,
                                   NtMountOptions options, NtCellSize preferred,
                                   bool has_update, bool has_event, NtNodeId *out)
{
    NtGoState *state = malloc(sizeof(*state));
    if (!state) return NT_RESULT_ALLOCATION_FAILED;
    *state = (NtGoState){.handle = handle};
    const NtPanelContentProvider provider = {
        .context = state, .preferred = preferred, .paint = paint,
        .update = has_update ? update : NULL, .event = has_event ? event : NULL, .dispose = dispose
    };
    const NtResult result = nt_panel_content_mount(tree, parent, options, &provider, out);
    if (result != NT_RESULT_OK) free(state);
    return result;
}

static NtResult write_ansi(void *context, NtByteView bytes)
{
    return nocturneGoANSIWrite((uintptr_t)context, bytes);
}

NtResult nt_go_present_ansi(const NtFrame *frame, uintptr_t handle, NtTerminalCapabilities capabilities,
                            NtRgba matte, NtPaletteView palette, NtTimeUs now)
{
    NtAnsiPresenter ansi = {.capabilities = capabilities, .matte = matte, .palette = palette,
                            .write = write_ansi, .write_context = (void *)handle};
    const NtFrameSink sink = {.context = &ansi, .present = nt_ansi_present};
    return nt_frame_present(&sink, frame, now);
}

NtKeyInput nt_go_event_key(const NtEvent *input)
{
    return input->data.key;
}

NtByteView nt_go_event_text(const NtEvent *input)
{
    return input->data.text;
}

NtPointerInput nt_go_event_pointer(const NtEvent *input) { return input->data.pointer; }
NtPointerCancel nt_go_event_cancel(const NtEvent *input) { return input->data.pointer_cancel; }
NtWheelInput nt_go_event_wheel(const NtEvent *input) { return input->data.wheel; }
NtCellSize nt_go_event_size(const NtEvent *input) { return input->data.resize; }

NtResult nt_go_dispatch(NtTree *tree, const NtGoInput *input, NtByteView text,
                        const NtKeyChord *reserved, size_t reserved_count,
                        bool traverse_focus, NtDispatchResult *out)
{
    NtEvent event = {.kind = input->kind, .at = input->at};
    NtByteView key_text = {0};
    switch (input->kind) {
    case NT_EVENT_KEY:
        event.data.key = input->key;
        key_text = text;
        break;
    case NT_EVENT_TEXT:
    case NT_EVENT_PASTE:
        event.data.text = text;
        break;
    case NT_EVENT_POINTER_DOWN:
    case NT_EVENT_POINTER_MOVE:
    case NT_EVENT_POINTER_UP:
        event.data.pointer = input->pointer;
        break;
    case NT_EVENT_POINTER_CANCEL:
        event.data.pointer_cancel = input->cancel;
        break;
    case NT_EVENT_WHEEL:
        event.data.wheel = input->wheel;
        break;
    case NT_EVENT_RESIZE:
        event.data.resize = input->size;
        break;
    case NT_EVENT_HOST_CANCEL:
        event.data.reason = input->cancel.reason;
        break;
    default:
        break;
    }
    const NtDispatchPolicy policy = {.reserved = reserved, .reserved_count = reserved_count,
                                     .traverse_focus = traverse_focus};
    return nt_tree_dispatch(tree, &event, key_text, &policy, out);
}
