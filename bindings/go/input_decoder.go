package nocturne

/*
#include <nocturne/input.h>
#include "bridge.h"

static NtCancelReason go_decoded_cancel_reason(const NtEvent *event)
{
    return event->data.reason;
}
*/
import "C"

import (
	"runtime"
	"sync"
	"unsafe"
)

type KeyboardProtocol uint32

const (
	KeyboardXTerm KeyboardProtocol = C.NT_KEYBOARD_XTERM
	KeyboardKitty KeyboardProtocol = C.NT_KEYBOARD_KITTY
)

// InputDecoderOptions uses literal values. Zero delay expires Escape
// immediately, and a zero paste limit permits only empty pastes.
// Start with DefaultInputDecoderOptions to change just one setting.
type InputDecoderOptions struct {
	EscapeDelayUs uint64
	PasteLimit    uint
	Keyboard      KeyboardProtocol
}

func DefaultInputDecoderOptions() InputDecoderOptions {
	return InputDecoderOptions{EscapeDelayUs: 25000, PasteLimit: 1 << 20, Keyboard: KeyboardXTerm}
}

type InputReadStatus uint32

const (
	InputPending   InputReadStatus = C.NT_INPUT_PENDING
	InputReady     InputReadStatus = C.NT_INPUT_READY
	InputDiscarded InputReadStatus = C.NT_INPUT_DISCARDED
)

// InputRead reports the exact accepted prefix. At and Input are set on InputReady.
type InputRead struct {
	Consumed int
	Status   InputReadStatus
	At       uint64
	Input    Input
}

// InputDecoder owns a terminal input stream. Copies share the same decoder.
type InputDecoder struct{ state *inputDecoderState }

type inputDecoderState struct {
	mu     sync.Mutex
	native *C.NtInputDecoder
}

func NewInputDecoder(options *InputDecoderOptions) (*InputDecoder, error) {
	if err := ensureABI(); err != nil {
		return nil, err
	}
	var native *C.NtInputOptions
	if options != nil {
		native = &C.NtInputOptions{escape_delay: C.NtTimeUs(options.EscapeDelayUs),
			paste_limit: C.size_t(options.PasteLimit), keyboard: C.NtKeyboardProtocol(options.Keyboard)}
	}
	state := &inputDecoderState{}
	if err := check(C.nt_input_create(nil, native, &state.native)); err != nil {
		return nil, err
	}
	return &InputDecoder{state}, nil
}

func (decoder *InputDecoder) call(operation func(*C.NtInputDecoder) C.NtResult) error {
	if decoder == nil || decoder.state == nil {
		return StaleReference
	}
	state := decoder.state
	state.mu.Lock()
	defer state.mu.Unlock()
	if state.native == nil {
		return StaleReference
	}
	return check(operation(state.native))
}

func (decoder *InputDecoder) Close() {
	if decoder == nil || decoder.state == nil {
		return
	}
	state := decoder.state
	state.mu.Lock()
	defer state.mu.Unlock()
	C.nt_input_destroy(state.native)
	state.native = nil
}

func decodedText(view C.NtByteView) string {
	return string(unsafe.Slice((*byte)(unsafe.Pointer(view.data)), int(view.count)))
}

func inputFromDecoder(event *C.NtEvent, keyText C.NtByteView) Input {
	input := Input{Kind: EventKind(event.kind)}
	switch input.Kind {
	case KeyEvent:
		key := C.nt_go_event_key(event)
		input.Key = KeyInput{Code: uint32(key.key), Modifiers: uint32(key.modifiers), State: KeyState(key.state)}
		input.Text = decodedText(keyText)
	case TextEvent, PasteEvent:
		input.Text = decodedText(C.nt_go_event_text(event))
	case PointerDownEvent, PointerMoveEvent, PointerUpEvent:
		pointer := C.nt_go_event_pointer(event)
		input.Pointer = PointerInput{ID: uint64(pointer.id), Position: pointFromNative(pointer.position),
			Button: uint32(pointer.button), Pressed: uint32(pointer.pressed), HasPosition: bool(pointer.has_position),
			Modifiers: uint32(pointer.modifiers)}
	case WheelEvent:
		wheel := C.nt_go_event_wheel(event)
		input.Wheel = WheelInput{Position: pointFromNative(wheel.position), DX: int32(wheel.dx), DY: int32(wheel.dy),
			Modifiers: uint32(wheel.modifiers)}
	case PointerCancelEvent:
		cancel := C.nt_go_event_cancel(event)
		input.Cancelled, input.Reason = uint64(cancel.id), CancelReason(cancel.reason)
	case HostCancelEvent:
		input.Reason = CancelReason(C.go_decoded_cancel_reason(event))
	case ResizeEvent:
		input.Size = sizeFromNative(C.nt_go_event_size(event))
	}
	return input
}

// Read decodes at most one event and copies any associated text. Resume at
// bytes[result.Consumed:], including after a ready record with zero consumption.
// AllocationFailed can accompany a nonzero accepted prefix.
func (decoder *InputDecoder) Read(bytes []byte, now uint64) (InputRead, error) {
	var result InputRead
	err := decoder.call(func(native *C.NtInputDecoder) C.NtResult {
		input := C.NtByteView{data: (*C.char)(unsafe.Pointer(unsafe.SliceData(bytes))), count: C.size_t(len(bytes))}
		var out C.NtInputRead
		status := C.nt_input_read(native, input, C.NtTimeUs(now), &out)
		runtime.KeepAlive(bytes)
		if status == C.NT_RESULT_OK || status == C.NT_RESULT_ALLOCATION_FAILED {
			result.Consumed, result.Status = int(out.consumed), InputReadStatus(out.status)
			if result.Status == InputReady {
				result.At, result.Input = uint64(out.event.at), inputFromDecoder(&out.event, out.key_text)
			}
		}
		return status
	})
	return result, err
}

func (decoder *InputDecoder) Deadline() (Deadline, error) {
	var result Deadline
	err := decoder.call(func(native *C.NtInputDecoder) C.NtResult {
		var at C.NtTimeUs
		if C.nt_input_deadline(native, &at) {
			result = Deadline{Scheduled: true, At: uint64(at)}
		}
		return C.NT_RESULT_OK
	})
	return result, err
}

func (decoder *InputDecoder) ExpectCursorReport(expected bool) error {
	return decoder.call(func(native *C.NtInputDecoder) C.NtResult {
		return C.nt_input_expect_cursor_report(native, C.bool(expected))
	})
}

func (decoder *InputDecoder) Cancel(now uint64, reason CancelReason) (Input, error) {
	var result Input
	err := decoder.call(func(native *C.NtInputDecoder) C.NtResult {
		var out C.NtEvent
		status := C.nt_input_cancel(native, C.NtTimeUs(now), C.NtCancelReason(reason), &out)
		if status == C.NT_RESULT_OK {
			result = inputFromDecoder(&out, C.NtByteView{})
		}
		return status
	})
	return result, err
}
