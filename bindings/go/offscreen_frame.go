package nocturne

/*
#include <nocturne/frame.h>
*/
import "C"

import (
	"runtime"
	"sync/atomic"
)

// OffscreenFrame owns a mutable frame. Close releases it.
// Copies share one owner. Access during a borrow returns WrongPhase.
type OffscreenFrame struct{ state *offscreenState }

type offscreenState struct {
	locked atomic.Bool
	native *C.NtFrame
}

func NewOffscreenFrame(size Size) (*OffscreenFrame, error) {
	if err := ensureABI(); err != nil {
		return nil, err
	}
	state := &offscreenState{}
	if err := check(C.nt_frame_create(nil, size.native(), &state.native)); err != nil {
		return nil, err
	}
	return &OffscreenFrame{state: state}, nil
}

func (state *offscreenState) acquire() (*C.NtFrame, error) {
	if state == nil {
		return nil, StaleReference
	}
	if !state.locked.CompareAndSwap(false, true) {
		return nil, WrongPhase
	}
	if state.native == nil {
		state.release()
		return nil, StaleReference
	}
	return state.native, nil
}

func (state *offscreenState) release() { state.locked.Store(false) }

// Frame returns a readable reference to the current offscreen cells.
func (frame *OffscreenFrame) Frame() Frame {
	if frame == nil {
		return Frame{}
	}
	return Frame{offscreen: frame.state}
}

func (frame *OffscreenFrame) call(operation func(*C.NtFrame) C.NtResult) error {
	native, release, err := frame.Frame().borrow()
	if err != nil {
		return err
	}
	defer release()
	return check(operation(native))
}

func (frame *OffscreenFrame) Resize(size Size) error {
	return frame.call(func(native *C.NtFrame) C.NtResult { return C.nt_frame_resize(native, size.native()) })
}

func (frame *OffscreenFrame) SetGlyph(at Point, glyph string, columns uint8, style Style, owner uint64) error {
	return frame.call(func(native *C.NtFrame) C.NtResult {
		result := C.nt_frame_set_glyph(native, at.native(), nativeBytes(glyph), C.uint8_t(columns),
			style.native(), C.NtOwnerToken(owner))
		runtime.KeepAlive(glyph)
		return result
	})
}

func (frame *OffscreenFrame) ClearGlyph(at Point) error {
	return frame.call(func(native *C.NtFrame) C.NtResult { return C.nt_frame_clear_glyph(native, at.native()) })
}

func (frame *OffscreenFrame) Close() error {
	state := frame.Frame().offscreen
	native, err := state.acquire()
	if err == StaleReference {
		return nil
	}
	if err != nil {
		return err
	}
	defer state.release()
	C.nt_frame_destroy(native)
	state.native = nil
	return nil
}
