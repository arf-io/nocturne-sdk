#include "input_internal.h"

#include <nocturne/text.h>

bool nt_input_printable(uint32_t scalar)
{
    return scalar >= 0x20 && (scalar < 0x7f || scalar >= 0xa0) &&
           scalar <= 0x10ffff && !(scalar >= 0xd800 && scalar <= 0xdfff);
}

static bool valid_modifiers(uint32_t modifiers)
{
    return modifiers <= (NT_MOD_NUM_LOCK | (NT_MOD_NUM_LOCK - 1u));
}

static bool valid_reason(NtCancelReason reason)
{
    return reason >= NT_CANCEL_HOST && reason <= NT_CANCEL_REMOVED;
}

static bool valid_pointer(NtPointerInput pointer, NtEventKind kind)
{
    const uint32_t known = NT_BUTTON_11 | (NT_BUTTON_11 - 1u);
    if (!valid_modifiers(pointer.modifiers) || pointer.pressed > known) return false;
    if (kind == NT_EVENT_POINTER_MOVE) return pointer.button == 0;
    if (!pointer.button || pointer.button > known || (pointer.button & (pointer.button - 1)))
        return false;
    const bool down = (pointer.pressed & pointer.button) != 0;
    return down == (kind == NT_EVENT_POINTER_DOWN);
}

NtResult nt_event_validate(const NtEvent *event)
{
    if (!event) return NT_RESULT_INVALID_ARGUMENT;
    switch (event->kind) {
        case NT_EVENT_KEY: {
            const NtKeyInput key = event->data.key;
            const bool known = nt_input_printable(key.key) ||
                (key.key >= NT_KEY_ESCAPE && key.key <= NT_KEY_F12);
            if (known && valid_modifiers(key.modifiers) &&
                key.state >= NT_KEY_PRESS && key.state <= NT_KEY_RELEASE) return NT_RESULT_OK;
            break;
        }
        case NT_EVENT_TEXT: case NT_EVENT_PASTE:
            return nt_utf8_validate(event->data.text, NULL);
        case NT_EVENT_POINTER_DOWN: case NT_EVENT_POINTER_MOVE: case NT_EVENT_POINTER_UP:
            if (valid_pointer(event->data.pointer, event->kind)) return NT_RESULT_OK;
            break;
        case NT_EVENT_WHEEL:
            if (valid_modifiers(event->data.wheel.modifiers) &&
                (event->data.wheel.dx || event->data.wheel.dy)) return NT_RESULT_OK;
            break;
        case NT_EVENT_POINTER_CANCEL:
            if (valid_reason(event->data.pointer_cancel.reason)) return NT_RESULT_OK;
            break;
        case NT_EVENT_FOCUS_LOSS: case NT_EVENT_HOST_CANCEL:
            if (valid_reason(event->data.reason)) return NT_RESULT_OK;
            break;
        case NT_EVENT_FOCUS_GAIN: return NT_RESULT_OK;
        case NT_EVENT_RESIZE: {
            const uint32_t limit = (uint32_t)INT32_MAX + 1u;
            return event->data.resize.columns <= limit && event->data.resize.rows <= limit ?
                NT_RESULT_OK : NT_RESULT_OUT_OF_RANGE;
        }
    }
    return NT_RESULT_INVALID_ARGUMENT;
}
