package nocturne

/*
#include "bridge.h"

static NtCancelReason go_event_host_reason(const NtEvent *event) { return event->data.reason; }
*/
import "C"

import (
	"runtime"
	"unsafe"
)

type EventKind uint32

const (
	KeyEvent           EventKind = C.NT_EVENT_KEY
	TextEvent          EventKind = C.NT_EVENT_TEXT
	PasteEvent         EventKind = C.NT_EVENT_PASTE
	PointerDownEvent   EventKind = C.NT_EVENT_POINTER_DOWN
	PointerMoveEvent   EventKind = C.NT_EVENT_POINTER_MOVE
	PointerUpEvent     EventKind = C.NT_EVENT_POINTER_UP
	WheelEvent         EventKind = C.NT_EVENT_WHEEL
	PointerCancelEvent EventKind = C.NT_EVENT_POINTER_CANCEL
	FocusGainEvent     EventKind = C.NT_EVENT_FOCUS_GAIN
	FocusLossEvent     EventKind = C.NT_EVENT_FOCUS_LOSS
	HostCancelEvent    EventKind = C.NT_EVENT_HOST_CANCEL
	ResizeEvent        EventKind = C.NT_EVENT_RESIZE
)

type KeyState uint32

const (
	KeyPress   KeyState = C.NT_KEY_PRESS
	KeyRepeat  KeyState = C.NT_KEY_REPEAT
	KeyRelease KeyState = C.NT_KEY_RELEASE
)

const (
	KeyEscape    uint32 = C.NT_KEY_ESCAPE
	KeyEnter     uint32 = C.NT_KEY_ENTER
	KeyTab       uint32 = C.NT_KEY_TAB
	KeyBackspace uint32 = C.NT_KEY_BACKSPACE
	KeyInsert    uint32 = C.NT_KEY_INSERT
	KeyDelete    uint32 = C.NT_KEY_DELETE
	KeyHome      uint32 = C.NT_KEY_HOME
	KeyEnd       uint32 = C.NT_KEY_END
	KeyPageUp    uint32 = C.NT_KEY_PAGE_UP
	KeyPageDown  uint32 = C.NT_KEY_PAGE_DOWN
	KeyUp        uint32 = C.NT_KEY_UP
	KeyDown      uint32 = C.NT_KEY_DOWN
	KeyLeft      uint32 = C.NT_KEY_LEFT
	KeyRight     uint32 = C.NT_KEY_RIGHT
	KeyF1        uint32 = C.NT_KEY_F1
	KeyF2        uint32 = C.NT_KEY_F2
	KeyF3        uint32 = C.NT_KEY_F3
	KeyF4        uint32 = C.NT_KEY_F4
	KeyF5        uint32 = C.NT_KEY_F5
	KeyF6        uint32 = C.NT_KEY_F6
	KeyF7        uint32 = C.NT_KEY_F7
	KeyF8        uint32 = C.NT_KEY_F8
	KeyF9        uint32 = C.NT_KEY_F9
	KeyF10       uint32 = C.NT_KEY_F10
	KeyF11       uint32 = C.NT_KEY_F11
	KeyF12       uint32 = C.NT_KEY_F12
)

const (
	ModShift    uint32 = C.NT_MOD_SHIFT
	ModAlt      uint32 = C.NT_MOD_ALT
	ModControl  uint32 = C.NT_MOD_CONTROL
	ModSuper    uint32 = C.NT_MOD_SUPER
	ModHyper    uint32 = C.NT_MOD_HYPER
	ModMeta     uint32 = C.NT_MOD_META
	ModCapsLock uint32 = C.NT_MOD_CAPS_LOCK
	ModNumLock  uint32 = C.NT_MOD_NUM_LOCK
)

const (
	PrimaryButton uint32 = C.NT_BUTTON_PRIMARY
)

type KeyInput struct {
	Code, Modifiers uint32
	State           KeyState
}

type PointerInput struct {
	ID              uint64
	Position        Point
	Button, Pressed uint32
	HasPosition     bool
	Modifiers       uint32
}

type WheelInput struct {
	Position  Point
	DX, DY    int32
	Modifiers uint32
}

type CancelReason uint32

const (
	HostCancel       CancelReason = C.NT_CANCEL_HOST
	ReleaseCancel    CancelReason = C.NT_CANCEL_RELEASE
	InterruptedRoute CancelReason = C.NT_CANCEL_ROUTE_INTERRUPTED
	FocusLossCancel  CancelReason = C.NT_CANCEL_FOCUS_LOSS
	IneligibleCancel CancelReason = C.NT_CANCEL_INELIGIBLE
	RemovedCancel    CancelReason = C.NT_CANCEL_REMOVED
)

// Input is a normalized host event. Text also carries associated key text.
type Input struct {
	Kind      EventKind
	Key       KeyInput
	Text      string
	Pointer   PointerInput
	Wheel     WheelInput
	Size      Size
	Cancelled uint64
	Reason    CancelReason
}

type KeyChord struct{ Code, Modifiers uint32 }

type DispatchPolicy struct {
	Reserved      []KeyChord
	TraverseFocus bool
}

type Dispatch struct {
	Target                                      Node
	Admitted, Reserved, Consumed, TextDelivered bool
}

func (input Input) native(at uint64) C.NtGoInput {
	return C.NtGoInput{kind: C.NtEventKind(input.Kind), at: C.NtTimeUs(at),
		key: C.NtKeyInput{key: C.uint32_t(input.Key.Code), modifiers: C.uint32_t(input.Key.Modifiers),
			state: C.NtKeyState(input.Key.State)},
		pointer: C.NtPointerInput{id: C.NtPointerId(input.Pointer.ID), position: input.Pointer.Position.native(),
			button: C.uint32_t(input.Pointer.Button), pressed: C.uint32_t(input.Pointer.Pressed),
			has_position: C.bool(input.Pointer.HasPosition), modifiers: C.uint32_t(input.Pointer.Modifiers)},
		cancel: C.NtPointerCancel{id: C.NtPointerId(input.Cancelled), reason: C.NtCancelReason(input.Reason)},
		wheel: C.NtWheelInput{position: input.Wheel.Position.native(), dx: C.int32_t(input.Wheel.DX),
			dy: C.int32_t(input.Wheel.DY), modifiers: C.uint32_t(input.Wheel.Modifiers)},
		size: input.Size.native()}
}

// Dispatch returns the native delivery report even when an admitted event fails.
func (tree *Tree) Dispatch(at uint64, input Input, policy DispatchPolicy) (Dispatch, error) {
	state, err := tree.acquire()
	if err != nil {
		return Dispatch{}, err
	}
	defer state.release()
	reserved := make([]C.NtKeyChord, len(policy.Reserved))
	for index, chord := range policy.Reserved {
		reserved[index] = C.NtKeyChord{key: C.uint32_t(chord.Code), modifiers: C.uint32_t(chord.Modifiers)}
	}
	value := input.native(at)
	out := C.NtDispatchResult{target: C.NtNodeId{slot: C.UINT32_MAX}}
	status := C.nt_go_dispatch(state.native, &value, nativeBytes(input.Text), unsafe.SliceData(reserved),
		C.size_t(len(reserved)), C.bool(policy.TraverseFocus), &out)
	runtime.KeepAlive(input.Text)
	delivery := Dispatch{}
	if out.target.slot != C.UINT32_MAX {
		delivery = Dispatch{Target: state.node(out.target), Admitted: true, Reserved: bool(out.reserved),
			Consumed: bool(out.consumed), TextDelivered: bool(out.text_delivered)}
	}
	return delivery, state.finish(status)
}

type EventPhase uint32

const (
	PreviewPhase EventPhase = C.NT_EVENT_PREVIEW
	TargetPhase  EventPhase = C.NT_EVENT_TARGET
	BubblePhase  EventPhase = C.NT_EVENT_BUBBLE
)

type CaptureRequest uint32

const (
	KeepCapture    CaptureRequest = C.NT_CAPTURE_KEEP
	TakeCapture    CaptureRequest = C.NT_CAPTURE_TAKE
	ReleaseCapture CaptureRequest = C.NT_CAPTURE_RELEASE
)

// Event exposes a callback's normalized input and reply while it is active.
type Event struct {
	scope  *callbackScope
	owner  *treeState
	native *C.NtEventContext
	input  *C.NtEvent
	reply  *C.NtEventReply
	kind   EventKind
	at     uint64
}

func (event *Event) Kind() EventKind { return event.kind }
func (event *Event) At() uint64      { return event.at }

func (event *Event) check() error {
	return event.scope.check(unsafe.Pointer(event.native))
}

func (event *Event) writable() error {
	if err := event.check(); err != nil {
		return err
	}
	if event.scope.evaluatingRule {
		return WrongPhase
	}
	return nil
}

func (event *Event) stageReady() error {
	if err := event.writable(); err != nil {
		return err
	}
	return check(event.scope.eventFailure)
}

// rememberFailure covers managed marshalling failures as well as failures
// that the native event context already holds.
func (event *Event) rememberFailure(err error) error {
	if err != nil && event.scope.eventFailure == C.NT_RESULT_OK {
		event.scope.eventFailure = callbackResult(err)
	}
	return err
}

func (event *Event) Phase() (EventPhase, error) {
	if err := event.check(); err != nil {
		return PreviewPhase, err
	}
	return EventPhase(C.nt_event_phase(event.native)), nil
}

func (event *Event) Key() (KeyInput, error) {
	if err := event.check(); err != nil {
		return KeyInput{}, err
	}
	if event.kind != KeyEvent {
		return KeyInput{}, InvalidArgument
	}
	key := C.nt_go_event_key(event.input)
	return KeyInput{Code: uint32(key.key), Modifiers: uint32(key.modifiers), State: KeyState(key.state)}, nil
}

func (event *Event) Text() (string, error) {
	if err := event.check(); err != nil {
		return "", err
	}
	if event.kind != TextEvent && event.kind != PasteEvent {
		return "", InvalidArgument
	}
	return copyBytes(C.nt_go_event_text(event.input))
}

func (event *Event) Pointer() (PointerInput, error) {
	if err := event.check(); err != nil {
		return PointerInput{}, err
	}
	if event.kind != PointerDownEvent && event.kind != PointerMoveEvent && event.kind != PointerUpEvent {
		return PointerInput{}, InvalidArgument
	}
	input := C.nt_go_event_pointer(event.input)
	return PointerInput{ID: uint64(input.id), Position: pointFromNative(input.position), Button: uint32(input.button),
		Pressed: uint32(input.pressed), HasPosition: bool(input.has_position), Modifiers: uint32(input.modifiers)}, nil
}

func (event *Event) Wheel() (WheelInput, error) {
	if err := event.check(); err != nil {
		return WheelInput{}, err
	}
	if event.kind != WheelEvent {
		return WheelInput{}, InvalidArgument
	}
	wheel := C.nt_go_event_wheel(event.input)
	return WheelInput{Position: pointFromNative(wheel.position), DX: int32(wheel.dx), DY: int32(wheel.dy),
		Modifiers: uint32(wheel.modifiers)}, nil
}

func (event *Event) CancelledPointer() (uint64, CancelReason, error) {
	if err := event.check(); err != nil {
		return 0, 0, err
	}
	if event.kind != PointerCancelEvent {
		return 0, 0, InvalidArgument
	}
	cancellation := C.nt_go_event_cancel(event.input)
	return uint64(cancellation.id), CancelReason(cancellation.reason), nil
}

func (event *Event) HostCancelReason() (CancelReason, error) {
	if err := event.check(); err != nil {
		return 0, err
	}
	if event.kind != HostCancelEvent {
		return 0, InvalidArgument
	}
	return CancelReason(C.go_event_host_reason(event.input)), nil
}

func (event *Event) Resize() (Size, error) {
	if err := event.check(); err != nil {
		return Size{}, err
	}
	if event.kind != ResizeEvent {
		return Size{}, InvalidArgument
	}
	return sizeFromNative(C.nt_go_event_size(event.input)), nil
}

func (event *Event) LocalPosition() (Point, bool, error) {
	if err := event.check(); err != nil {
		return Point{}, false, err
	}
	var point C.NtCellPoint
	present := C.nt_event_local_position(event.native, &point)
	return pointFromNative(point), bool(present), nil
}

func (event *Event) Emit(kind uint32, payload string) error {
	if err := event.stageReady(); err != nil {
		return err
	}
	status := C.nt_event_emit(event.native, C.uint32_t(kind), nativeBytes(payload))
	runtime.KeepAlive(payload)
	return event.rememberFailure(check(status))
}

func (event *Event) Consume() error {
	if err := event.writable(); err != nil {
		return err
	}
	event.reply.consume = true
	return nil
}

func (event *Event) Invalidate(reason Invalidation) error {
	if err := event.writable(); err != nil {
		return err
	}
	event.reply.invalidate = C.NtInvalidation(reason)
	return nil
}

func (event *Event) Capture(request CaptureRequest) error {
	if err := event.writable(); err != nil {
		return err
	}
	event.reply.capture = C.NtCaptureRequest(request)
	return nil
}

func (event *Event) RequestFocus() error {
	if err := event.writable(); err != nil {
		return err
	}
	event.reply.focus = true
	return nil
}
