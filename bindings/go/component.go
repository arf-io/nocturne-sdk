package nocturne

/*
#include "bridge.h"
*/
import "C"

import (
	"errors"
	"fmt"
	"runtime"
	"runtime/cgo"
	"sync/atomic"
	"unsafe"
)

type Invalidation uint32

const (
	InvalidateNone   Invalidation = C.NT_INVALIDATE_NONE
	InvalidatePaint  Invalidation = C.NT_INVALIDATE_PAINT
	InvalidateLayout Invalidation = C.NT_INVALIDATE_LAYOUT
)

type Motion uint32

const (
	MotionFull    Motion = C.NT_MOTION_FULL
	MotionReduced Motion = C.NT_MOTION_REDUCED
)

type Deadline struct {
	Scheduled bool
	At        uint64
}

type UpdateInfo struct {
	Now, Elapsed uint64
	Motion       Motion
}

type UpdateReply struct {
	Invalidate Invalidation
	Next       Deadline
}

type Extent struct {
	Min, Max uint32
	Bounded  bool
}

type Constraints struct{ Columns, Rows Extent }

func (extent Extent) native() C.NtExtentRange {
	return C.NtExtentRange{min: C.uint32_t(extent.Min), max: C.uint32_t(extent.Max),
		bounded: C.bool(extent.Bounded)}
}

func extentFromNative(extent C.NtExtentRange) Extent {
	return Extent{Min: uint32(extent.min), Max: uint32(extent.max), Bounded: bool(extent.bounded)}
}

func (constraints Constraints) native() C.NtConstraints {
	return C.NtConstraints{columns: constraints.Columns.native(), rows: constraints.Rows.native()}
}

// Component is hosted in a native tree. The tree calls Dispose once per
// successful mount. Embed ComponentBase to implement selected callbacks.
type Component interface {
	Measure(*Measure, Constraints) (Size, error)
	Layout(*Layout, Size) error
	Update(UpdateInfo) (UpdateReply, error)
	Paint(*Paint, Size) error
	Event(*Event) error
	Dispose() error
}

type ComponentBase struct{}

func (ComponentBase) Measure(_ *Measure, constraints Constraints) (Size, error) {
	return Size{Columns: constraints.Columns.Min, Rows: constraints.Rows.Min}, nil
}

func (ComponentBase) Layout(*Layout, Size) error { return nil }
func (ComponentBase) Update(UpdateInfo) (UpdateReply, error) {
	return UpdateReply{}, nil
}
func (ComponentBase) Paint(*Paint, Size) error { return nil }
func (ComponentBase) Event(*Event) error       { return nil }
func (ComponentBase) Dispose() error           { return nil }

type CallbackFailure struct {
	Phase string
	Cause error
}

type CallError struct {
	Native    error
	Callbacks []CallbackFailure
}

func (failure *CallError) Error() string {
	if failure.Native != nil {
		return fmt.Sprintf("%v (%d callback failures)", failure.Native, len(failure.Callbacks))
	}
	return fmt.Sprintf("%d callbacks failed during the native call", len(failure.Callbacks))
}

func (failure *CallError) Unwrap() []error {
	causes := make([]error, 0, 1+len(failure.Callbacks))
	if failure.Native != nil {
		causes = append(causes, failure.Native)
	}
	for _, callback := range failure.Callbacks {
		causes = append(causes, callback.Cause)
	}
	return causes
}

type PanicError struct{ Value any }

func (failure *PanicError) Error() string { return fmt.Sprintf("component panic: %v", failure.Value) }

func (failure *PanicError) Unwrap() error {
	cause, _ := failure.Value.(error)
	return cause
}

type callbackScope struct {
	active         atomic.Bool
	drawing        bool
	evaluatingRule bool
	eventFailure   C.NtResult
}

func beginScope() *callbackScope {
	scope := &callbackScope{}
	scope.active.Store(true)
	return scope
}

func (scope *callbackScope) check(context unsafe.Pointer) error {
	if scope == nil || !scope.active.Load() {
		return StaleReference
	}
	if !bool(C.nt_go_context_current(context)) {
		return WrongPhase
	}
	return nil
}

type Measure struct {
	scope  *callbackScope
	native *C.NtMeasureContext
}

func (context *Measure) ChildCount() (uint, error) {
	if err := context.scope.check(unsafe.Pointer(context.native)); err != nil {
		return 0, err
	}
	return uint(C.nt_measure_child_count(context.native)), nil
}

func (context *Measure) Child(index uint, limits Constraints) (Size, error) {
	if err := context.scope.check(unsafe.Pointer(context.native)); err != nil {
		return Size{}, err
	}
	var child C.NtNodeId
	if err := check(C.nt_measure_child_at(context.native, C.size_t(index), &child)); err != nil {
		return Size{}, err
	}
	var preferred C.NtCellSize
	if err := check(C.nt_measure_child(context.native, child, limits.native(), &preferred)); err != nil {
		return Size{}, err
	}
	return sizeFromNative(preferred), nil
}

type Layout struct {
	scope  *callbackScope
	native *C.NtLayoutContext
}

func (context *Layout) ChildCount() (uint, error) {
	if err := context.scope.check(unsafe.Pointer(context.native)); err != nil {
		return 0, err
	}
	return uint(C.nt_layout_child_count(context.native)), nil
}

func (context *Layout) child(index uint) (C.NtNodeId, error) {
	var child C.NtNodeId
	if err := context.scope.check(unsafe.Pointer(context.native)); err != nil {
		return child, err
	}
	err := check(C.nt_layout_child_at(context.native, C.size_t(index), &child))
	return child, err
}

func (context *Layout) ChildSize(index uint) (Size, error) {
	child, err := context.child(index)
	if err != nil {
		return Size{}, err
	}
	var preferred C.NtCellSize
	if err := check(C.nt_layout_child_size(context.native, child, &preferred)); err != nil {
		return Size{}, err
	}
	return sizeFromNative(preferred), nil
}

func (context *Layout) Place(index uint, bounds Rect, depth int32) error {
	child, err := context.child(index)
	if err != nil {
		return err
	}
	return check(C.nt_layout_place(context.native, child, bounds.native(), C.int32_t(depth), nil))
}

// PlaceClipped assigns the child's bounds and constrains paint and hit testing
// to a parent-local rectangle.
func (context *Layout) PlaceClipped(index uint, bounds Rect, depth int32, clip Rect) error {
	child, err := context.child(index)
	if err != nil {
		return err
	}
	return check(C.nt_layout_place_clipped(context.native, child, bounds.native(), C.int32_t(depth), clip.native()))
}

type Paint struct {
	scope  *callbackScope
	native *C.NtPaintContext
	owner  *treeState
}

func (context *Paint) check() error {
	if err := context.scope.check(unsafe.Pointer(context.native)); err != nil {
		return err
	}
	if context.scope.drawing {
		return WrongPhase
	}
	return nil
}

func (context *Paint) Glyph(at Point, glyph string, columns uint8, style Style) error {
	if err := context.check(); err != nil {
		return err
	}
	result := C.nt_paint_glyph(context.native, at.native(), nativeBytes(glyph), C.uint8_t(columns), style.native())
	runtime.KeepAlive(glyph)
	return check(result)
}

func (context *Paint) Fill(bounds Rect, color Color) error {
	if err := context.check(); err != nil {
		return err
	}
	return check(C.nt_paint_fill(context.native, bounds.native(), color.native()))
}

// Clip returns the paintable rectangle in component-local coordinates.
func (context *Paint) Clip() (Rect, error) {
	if err := context.check(); err != nil {
		return Rect{}, err
	}
	return rectFromNative(C.nt_paint_clip(context.native)), nil
}

func (context *Paint) EraseGlyph(at Point) error {
	if err := context.check(); err != nil {
		return err
	}
	return check(C.nt_paint_erase_glyph(context.native, at.native()))
}

type mounted struct {
	tree      *treeState
	component Component
}

func (state *treeState) recordCallbackFailure(phase string, cause error) {
	state.failures = append(state.failures, CallbackFailure{Phase: phase, Cause: cause})
}

func callbackResult(err error) C.NtResult {
	var native Result
	if errors.As(err, &native) && native != OK {
		return C.NtResult(native)
	}
	return C.NT_RESULT_CALLBACK_FAILED
}

func invoke(handle C.uintptr_t, phase string, call func(Component) error) (status C.NtResult) {
	owned := cgo.Handle(handle).Value().(*mounted)
	defer func() {
		if cause := recover(); cause != nil {
			owned.tree.recordCallbackFailure(phase, &PanicError{Value: cause})
			status = C.NT_RESULT_CALLBACK_FAILED
		}
	}()
	if err := call(owned.component); err != nil {
		owned.tree.recordCallbackFailure(phase, err)
		return callbackResult(err)
	}
	return C.NT_RESULT_OK
}

//export nocturneGoMeasure
func nocturneGoMeasure(handle C.uintptr_t, native *C.NtMeasureContext, limits C.NtConstraints, out *C.NtCellSize) C.NtResult {
	scope := beginScope()
	defer scope.active.Store(false)
	return invoke(handle, "measure", func(component Component) error {
		preferred, err := component.Measure(&Measure{scope: scope, native: native},
			Constraints{Columns: extentFromNative(limits.columns), Rows: extentFromNative(limits.rows)})
		if err == nil {
			*out = preferred.native()
		}
		return err
	})
}

//export nocturneGoLayout
func nocturneGoLayout(handle C.uintptr_t, native *C.NtLayoutContext, assigned C.NtCellSize) C.NtResult {
	scope := beginScope()
	defer scope.active.Store(false)
	return invoke(handle, "layout", func(component Component) error {
		return component.Layout(&Layout{scope: scope, native: native}, sizeFromNative(assigned))
	})
}

//export nocturneGoUpdate
func nocturneGoUpdate(handle C.uintptr_t, info C.NtUpdateInfo, out *C.NtUpdateReply) C.NtResult {
	return invoke(handle, "update", func(component Component) error {
		reply, err := component.Update(UpdateInfo{Now: uint64(info.now), Elapsed: uint64(info.elapsed), Motion: Motion(info.motion)})
		if err == nil {
			*out = C.NtUpdateReply{invalidate: C.NtInvalidation(reply.Invalidate),
				next: C.NtDeadline{scheduled: C.bool(reply.Next.Scheduled), at: C.NtTimeUs(reply.Next.At)}}
		}
		return err
	})
}

//export nocturneGoPaint
func nocturneGoPaint(handle C.uintptr_t, native *C.NtPaintContext, assigned C.NtCellSize) C.NtResult {
	scope := beginScope()
	defer scope.active.Store(false)
	owner := cgo.Handle(handle).Value().(*mounted).tree
	return invoke(handle, "paint", func(component Component) error {
		return component.Paint(&Paint{scope: scope, native: native, owner: owner}, sizeFromNative(assigned))
	})
}

//export nocturneGoEvent
func nocturneGoEvent(handle C.uintptr_t, native *C.NtEventContext, input *C.NtEvent, reply *C.NtEventReply) C.NtResult {
	scope := beginScope()
	defer scope.active.Store(false)
	owner := cgo.Handle(handle).Value().(*mounted).tree
	event := &Event{scope: scope, owner: owner, native: native, input: input, reply: reply,
		kind: EventKind(input.kind), at: uint64(input.at)}
	status := invoke(handle, "event", func(component Component) error { return component.Event(event) })
	if status != C.NT_RESULT_OK {
		return status
	}
	return scope.eventFailure
}

//export nocturneGoDispose
func nocturneGoDispose(handle C.uintptr_t) {
	defer cgo.Handle(handle).Delete()
	invoke(handle, "dispose", func(component Component) error { return component.Dispose() })
}
