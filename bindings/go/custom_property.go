package nocturne

/*
#include "bridge.h"
#include <nocturne/property.h>

extern NtResult nocturneGoPropertyRead(uintptr_t handle, NtPropertyId id, NtPropertyOutput *out);
extern NtResult nocturneGoPropertyPrepare(uintptr_t handle, NtPropertyId id, NtValue *value, void **prepared);
extern void nocturneGoPropertyFinish(uintptr_t handle, NtPropertyId id, NtValue *value, uintptr_t prepared, bool commit);
extern void nocturneGoPropertyRelease(uintptr_t handle);

static NtResult go_property_read(void *context, NtPropertyId id, NtPropertyOutput *out)
{
    const void *previous = nt_go_swap_context(NULL);
    const NtResult status = nocturneGoPropertyRead((uintptr_t)context, id, out);
    nt_go_swap_context(previous);
    return status;
}

static NtResult go_property_prepare(void *context, NtPropertyId id, NtValue value, void **prepared)
{
    const void *previous = nt_go_swap_context(NULL);
    const NtResult status = nocturneGoPropertyPrepare((uintptr_t)context, id, &value, prepared);
    nt_go_swap_context(previous);
    return status;
}

static void go_property_finish(void *context, NtPropertyId id, NtValue value, void *prepared, bool commit)
{
    const void *previous = nt_go_swap_context(NULL);
    nocturneGoPropertyFinish((uintptr_t)context, id, &value, (uintptr_t)prepared, commit);
    nt_go_swap_context(previous);
}

static void go_property_release(void *context)
{
    nocturneGoPropertyRelease((uintptr_t)context);
}

static void go_property_prepared_token(void **prepared, uintptr_t handle)
{
    *prepared = (void *)handle;
}

static NtResult go_define_properties(NtTree *tree, NtNodeId node, const NtPropertyInfo *info,
                                     size_t count, uintptr_t handle)
{
    const NtPropertyProvider provider = {(void *)handle, go_property_read, go_property_prepare,
                                         go_property_finish, go_property_release};
    return nt_tree_define_property_provider(tree, node, info, count, &provider);
}
*/
import "C"

import (
	"runtime/cgo"
	"unsafe"
)

// PropertyInfo describes a declared property in registration order.
type PropertyInfo struct {
	ID           PropertyID
	Kind         PropertyKind
	Writable     bool
	Invalidation Invalidation
}

// PropertyDescriptor supplies a read and optional transaction callbacks.
// Read and Prepare preserve state. Finish commits when commit is true and
// discards prepared resources otherwise. Finish must complete without error.
type PropertyDescriptor struct {
	ID           PropertyID
	Kind         PropertyKind
	Invalidation Invalidation
	Read         func() (PropertyValue, error)
	Prepare      func(value PropertyValue) (any, error)
	Finish       func(value PropertyValue, prepared any, commit bool)
}

type propertyCallbacks struct {
	tree        *treeState
	descriptors map[PropertyID]PropertyDescriptor
}

func (callbacks *propertyCallbacks) invoke(phase string, work func() error) (status C.NtResult) {
	defer func() {
		if cause := recover(); cause != nil {
			callbacks.tree.recordCallbackFailure(phase, &PanicError{Value: cause})
			status = C.NT_RESULT_CALLBACK_FAILED
		}
	}()
	if err := work(); err != nil {
		callbacks.tree.recordCallbackFailure(phase, err)
		return callbackResult(err)
	}
	return C.NT_RESULT_OK
}

//export nocturneGoPropertyRead
func nocturneGoPropertyRead(handle C.uintptr_t, id C.NtPropertyId, out *C.NtPropertyOutput) C.NtResult {
	callbacks := cgo.Handle(handle).Value().(*propertyCallbacks)
	return callbacks.invoke("propertyRead", func() error {
		value, err := callbacks.descriptors[PropertyID(id)].Read()
		if err != nil {
			return err
		}
		var arena RawArena
		defer arena.close()
		var native C.NtValue
		if err = value.writeNative(&arena, &native); err != nil {
			return err
		}
		return check(C.nt_property_output_set(out, native))
	})
}

//export nocturneGoPropertyPrepare
func nocturneGoPropertyPrepare(handle C.uintptr_t, id C.NtPropertyId, value *C.NtValue, out *unsafe.Pointer) C.NtResult {
	callbacks := cgo.Handle(handle).Value().(*propertyCallbacks)
	prepare := callbacks.descriptors[PropertyID(id)].Prepare
	if prepare == nil {
		return C.NT_RESULT_OK
	}
	return callbacks.invoke("propertyPrepare", func() error {
		input, err := propertyFromNative(value)
		if err != nil {
			return err
		}
		prepared, err := prepare(input)
		if err == nil && prepared != nil {
			C.go_property_prepared_token(out, C.uintptr_t(cgo.NewHandle(prepared)))
		}
		return err
	})
}

//export nocturneGoPropertyFinish
func nocturneGoPropertyFinish(handle C.uintptr_t, id C.NtPropertyId, value *C.NtValue, prepared C.uintptr_t, commit C.bool) {
	callbacks := cgo.Handle(handle).Value().(*propertyCallbacks)
	callbacks.invoke("propertyFinish", func() error {
		var resource any
		if prepared != 0 {
			token := cgo.Handle(prepared)
			defer token.Delete()
			resource = token.Value()
		}
		input, err := propertyFromNative(value)
		if err != nil {
			return err
		}
		callbacks.descriptors[PropertyID(id)].Finish(input, resource, bool(commit))
		return nil
	})
}

//export nocturneGoPropertyRelease
func nocturneGoPropertyRelease(handle C.uintptr_t) {
	cgo.Handle(handle).Delete()
}

// DefineProperties installs one descriptor table on the mount. The tree
// owns the callbacks after successful registration. A nil Finish declares
// a read-only property.
func (tree *Tree) DefineProperties(node Node, descriptors []PropertyDescriptor) error {
	state, err := tree.acquire()
	if err != nil {
		return err
	}
	defer state.release()
	id, err := state.reference(node)
	if err != nil {
		return err
	}
	var arena RawArena
	defer arena.close()
	info, err := nativeSlice[C.NtPropertyInfo](&arena, len(descriptors))
	if err != nil {
		return err
	}
	owned := &propertyCallbacks{tree: state, descriptors: make(map[PropertyID]PropertyDescriptor, len(info))}
	for index, descriptor := range descriptors {
		if descriptor.Read == nil || (descriptor.Prepare != nil && descriptor.Finish == nil) {
			return InvalidArgument
		}
		info[index] = C.NtPropertyInfo{id: C.NtPropertyId(descriptor.ID), kind: C.NtValueKind(descriptor.Kind),
			writable: C.bool(descriptor.Finish != nil), invalidation: C.NtInvalidation(descriptor.Invalidation)}
		owned.descriptors[descriptor.ID] = descriptor
	}
	handle := cgo.NewHandle(owned)
	err = check(C.go_define_properties(state.native, id, unsafe.SliceData(info), C.size_t(len(info)), C.uintptr_t(handle)))
	if err != nil {
		handle.Delete()
	}
	return err
}

// Properties returns the declared metadata for a stock or custom mount.
func (tree *Tree) Properties(node Node) ([]PropertyInfo, error) {
	var result []PropertyInfo
	err := tree.callNode(node, func(native *C.NtTree, id C.NtNodeId) C.NtResult {
		var count C.size_t
		if status := C.nt_tree_property_count(native, id, &count); status != C.NT_RESULT_OK {
			return status
		}
		if uint64(count) > uint64(^uint(0)>>1) {
			return C.NT_RESULT_OUT_OF_RANGE
		}
		result = make([]PropertyInfo, int(count))
		for index := range result {
			var info C.NtPropertyInfo
			if status := C.nt_tree_property_at(native, id, C.size_t(index), &info); status != C.NT_RESULT_OK {
				return status
			}
			result[index] = PropertyInfo{ID: PropertyID(info.id), Kind: PropertyKind(info.kind),
				Writable: bool(info.writable), Invalidation: Invalidation(info.invalidation)}
		}
		return C.NT_RESULT_OK
	})
	if err != nil {
		return nil, err
	}
	return result, nil
}
