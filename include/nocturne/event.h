#ifndef NOCTURNE_EVENT_H
#define NOCTURNE_EVENT_H

#include <nocturne/base.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NtTree NtTree;
typedef struct NtEventContext NtEventContext;
typedef struct NtNodeId {
    const NtTree *tree;
    uint32_t slot, generation;
} NtNodeId;

typedef enum NtInvalidation { NT_INVALIDATE_NONE, NT_INVALIDATE_PAINT, NT_INVALIDATE_LAYOUT } NtInvalidation;

typedef uint64_t NtPointerId;
typedef enum NtEventKind {
    NT_EVENT_KEY, NT_EVENT_TEXT, NT_EVENT_PASTE,
    NT_EVENT_POINTER_DOWN, NT_EVENT_POINTER_MOVE, NT_EVENT_POINTER_UP, NT_EVENT_WHEEL,
    NT_EVENT_POINTER_CANCEL, NT_EVENT_FOCUS_GAIN, NT_EVENT_FOCUS_LOSS,
    NT_EVENT_HOST_CANCEL, NT_EVENT_RESIZE
} NtEventKind;

/* Printable keys use Unicode scalar identities. Named keys occupy a
 * separate range beyond Unicode. */
typedef enum NtNamedKey {
    NT_KEY_ESCAPE = 0x110000, NT_KEY_ENTER, NT_KEY_TAB, NT_KEY_BACKSPACE,
    NT_KEY_INSERT, NT_KEY_DELETE, NT_KEY_HOME, NT_KEY_END,
    NT_KEY_PAGE_UP, NT_KEY_PAGE_DOWN, NT_KEY_UP, NT_KEY_DOWN, NT_KEY_LEFT, NT_KEY_RIGHT,
    NT_KEY_F1, NT_KEY_F2, NT_KEY_F3, NT_KEY_F4, NT_KEY_F5, NT_KEY_F6,
    NT_KEY_F7, NT_KEY_F8, NT_KEY_F9, NT_KEY_F10, NT_KEY_F11, NT_KEY_F12
} NtNamedKey;

typedef enum NtModifier {
    NT_MOD_SHIFT = 1u << 0, NT_MOD_ALT = 1u << 1, NT_MOD_CONTROL = 1u << 2,
    NT_MOD_SUPER = 1u << 3, NT_MOD_HYPER = 1u << 4, NT_MOD_META = 1u << 5,
    NT_MOD_CAPS_LOCK = 1u << 6, NT_MOD_NUM_LOCK = 1u << 7
} NtModifier;

typedef enum NtKeyState { NT_KEY_PRESS, NT_KEY_REPEAT, NT_KEY_RELEASE } NtKeyState;
typedef struct NtKeyInput { uint32_t key, modifiers; NtKeyState state; } NtKeyInput;
typedef enum NtPointerButton {
    NT_BUTTON_PRIMARY = 1u << 0, NT_BUTTON_MIDDLE = 1u << 1, NT_BUTTON_SECONDARY = 1u << 2,
    NT_BUTTON_8 = 1u << 3, NT_BUTTON_9 = 1u << 4, NT_BUTTON_10 = 1u << 5, NT_BUTTON_11 = 1u << 6
} NtPointerButton;
typedef struct NtPointerInput {
    NtPointerId id;
    NtCellPoint position;
    uint32_t button, pressed;
    bool has_position;
    uint32_t modifiers;
} NtPointerInput;
typedef struct NtWheelInput { NtCellPoint position; int32_t dx, dy; uint32_t modifiers; } NtWheelInput;
typedef enum NtCancelReason {
    NT_CANCEL_HOST, NT_CANCEL_RELEASE, NT_CANCEL_ROUTE_INTERRUPTED,
    NT_CANCEL_FOCUS_LOSS, NT_CANCEL_INELIGIBLE, NT_CANCEL_REMOVED
} NtCancelReason;
typedef struct NtPointerCancel { NtPointerId id; NtCancelReason reason; } NtPointerCancel;

typedef struct NtEvent {
    NtEventKind kind;
    NtTimeUs at;
    union {
        NtKeyInput key;
        NtByteView text;
        NtPointerInput pointer;
        NtWheelInput wheel;
        NtCellSize resize;
        NtPointerCancel pointer_cancel;
        NtCancelReason reason;
    } data;
} NtEvent;

/* Check a host-created event before dispatch using the normalized contract. */
NtResult nt_event_validate(const NtEvent *event);

typedef struct NtKeyChord { uint32_t key, modifiers; } NtKeyChord;
typedef struct NtDispatchPolicy {
    const NtKeyChord *reserved;
    size_t reserved_count;
    bool traverse_focus;
} NtDispatchPolicy;

typedef struct NtDispatchResult {
    NtNodeId target;
    bool reserved, consumed, text_delivered;
} NtDispatchResult;

typedef enum NtEventPhase { NT_EVENT_PREVIEW, NT_EVENT_TARGET, NT_EVENT_BUBBLE } NtEventPhase;
typedef enum NtCaptureRequest { NT_CAPTURE_KEEP, NT_CAPTURE_TAKE, NT_CAPTURE_RELEASE } NtCaptureRequest;

typedef struct NtEventReply {
    NtInvalidation invalidate;
    NtCaptureRequest capture;
    bool consume, focus;
} NtEventReply;

typedef struct NtIntentView {
    NtNodeId source;
    uint32_t kind;
    NtByteView payload;
} NtIntentView;

NtEventPhase nt_event_phase(const NtEventContext *context);
bool nt_event_local_position(const NtEventContext *context, NtCellPoint *out);
NtResult nt_event_emit(NtEventContext *context, uint32_t kind, NtByteView payload);

#ifdef __cplusplus
}
#endif
#endif
