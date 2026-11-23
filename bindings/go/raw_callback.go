package nocturne

/*
#include <stdint.h>
#include <string.h>
#include <nocturne/base.h>

uintptr_t nt_binding_callback_address(uint32_t id);

static void go_raw_copy(uintptr_t out, uintptr_t input, size_t size)
{
    memmove((void *)out, (const void *)input, size);
}

static void go_raw_callback_result(uintptr_t out, uint32_t result)
{
    const NtResult status = (NtResult)result;
    memcpy((void *)out, &status, sizeof(status));
}
*/
import "C"

import (
	"runtime/cgo"
	"strings"
	"sync"
	"sync/atomic"
	"unsafe"
)

type RawCallback func(*RawCallbackFrame) error

type rawCallbackEntry struct {
	signature rawFunction
	call      RawCallback
}

// RawCallbacks owns C callback registrations sharing one opaque context.
// Close it after the last native object that borrows a callback is destroyed.
type RawCallbacks struct {
	api     *RawAPI
	handle  cgo.Handle
	entries map[uint32]rawCallbackEntry
	closed  atomic.Bool
	mu      sync.Mutex
	errors  []error
}

func (api *RawAPI) NewCallbacks(handlers map[string]RawCallback) (*RawCallbacks, error) {
	if len(handlers) == 0 {
		return nil, InvalidArgument
	}
	context := &RawCallbacks{api: api, entries: make(map[uint32]rawCallbackEntry, len(handlers))}
	for name, callback := range handlers {
		signature, ok := api.callbacks[name]
		if !ok {
			return nil, UnsupportedCapability
		}
		if callback == nil {
			return nil, InvalidArgument
		}
		context.entries[signature.ID] = rawCallbackEntry{signature, callback}
	}
	context.handle = cgo.NewHandle(context)
	return context, nil
}

func (context *RawCallbacks) Context() RawAddress {
	if context.closed.Load() {
		return 0
	}
	return RawAddress(context.handle)
}

func (context *RawCallbacks) Function(name string) (RawAddress, error) {
	if context.closed.Load() {
		return 0, StaleReference
	}
	signature := context.api.callbacks[name]
	if _, ok := context.entries[signature.ID]; !ok {
		return 0, UnsupportedCapability
	}
	return RawAddress(C.nt_binding_callback_address(C.uint32_t(signature.ID))), nil
}

func (context *RawCallbacks) Close() {
	if context.closed.CompareAndSwap(false, true) {
		context.handle.Delete()
	}
}

// TakeErrors drains callback errors and recovered panics. Native NtResult
// callbacks also return CallbackFailed or the supplied native Result.
func (context *RawCallbacks) TakeErrors() []error {
	context.mu.Lock()
	defer context.mu.Unlock()
	errors := context.errors
	context.errors = nil
	return errors
}

func (context *RawCallbacks) save(err error) {
	context.mu.Lock()
	defer context.mu.Unlock()
	context.errors = append(context.errors, err)
}

// RawCallbackFrame borrows the input and result slots while a callback runs.
type RawCallbackFrame struct {
	*RawArena
	signature rawFunction
	args      []C.uintptr_t
	result    RawAddress
	returned  bool
}

// Argument copies an incoming C value into a slot in the callback arena.
// Pointer values still borrow their targets under the callback's contract.
func (frame *RawCallbackFrame) Argument(index int) (RawAddress, error) {
	if frame.closed {
		return 0, StaleReference
	}
	if index < 0 || index >= len(frame.args) {
		return 0, OutOfRange
	}
	size, err := frame.api.Size(frame.signature.Args[index].Type)
	if err != nil {
		return 0, err
	}
	out, err := frame.Allocate(size)
	if err != nil {
		return 0, err
	}
	C.go_raw_copy(C.uintptr_t(out), frame.args[index], C.size_t(size))
	return out, nil
}

// WriteOutput copies an arena slot into the live target of a mutable
// pointer argument. The native callback contract governs the target size.
func (frame *RawCallbackFrame) WriteOutput(index int, value RawAddress, size int) error {
	if frame.closed {
		return StaleReference
	}
	if index < 0 || index >= len(frame.args) || size < 0 || !frame.owns(value, size) {
		return OutOfRange
	}
	kind := frame.signature.Args[index].Type
	if !strings.HasSuffix(kind, "*") || strings.HasPrefix(kind, "const ") {
		return InvalidArgument
	}
	pointer, err := frame.Pointer(RawAddress(frame.args[index]))
	if err != nil {
		return err
	}
	if pointer == 0 {
		return InvalidArgument
	}
	C.go_raw_copy(C.uintptr_t(pointer), C.uintptr_t(value), C.size_t(size))
	return nil
}

func (frame *RawCallbackFrame) Return(value RawAddress) error {
	if frame.closed {
		return StaleReference
	}
	if frame.result == 0 {
		return InvalidArgument
	}
	size, err := frame.api.Size(frame.signature.Result)
	if err != nil {
		return err
	}
	if !frame.owns(value, size) {
		return OutOfRange
	}
	C.go_raw_copy(C.uintptr_t(frame.result), C.uintptr_t(value), C.size_t(size))
	frame.returned = true
	return nil
}

func rawCallbackContext(token C.uintptr_t) (context *RawCallbacks) {
	// A released callback token can still arrive after an application error.
	defer func() { _ = recover() }()
	context, _ = cgo.Handle(token).Value().(*RawCallbacks)
	return
}

//export nt_binding_host_callback
func nt_binding_host_callback(token C.uintptr_t, id C.uint32_t, result C.uintptr_t, args *C.uintptr_t, count C.uint32_t) {
	context := rawCallbackContext(token)
	if context == nil || context.closed.Load() {
		return
	}
	entry, ok := context.entries[uint32(id)]
	if !ok || int(count) != len(entry.signature.Args) {
		return
	}
	arena := &RawArena{api: context.api}
	defer arena.close()
	frame := &RawCallbackFrame{RawArena: arena, signature: entry.signature,
		result: RawAddress(result), args: unsafe.Slice(args, int(count))}
	defer func() {
		frame.args = nil
		if cause := recover(); cause != nil {
			context.save(&PanicError{Value: cause})
			if entry.signature.Result == "NtResult" {
				C.go_raw_callback_result(result, C.uint32_t(CallbackFailed))
			}
		}
	}()
	err := entry.call(frame)
	status, native := err.(Result)
	if err != nil && (!native || status != OK) {
		context.save(err)
		if !native {
			status = CallbackFailed
		}
	} else if frame.returned {
		return
	}
	if entry.signature.Result == "NtResult" {
		C.go_raw_callback_result(result, C.uint32_t(status))
	}
}
