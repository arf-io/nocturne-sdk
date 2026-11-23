package nocturne

/*
#include <nocturne/binding.h>

extern NtResult nocturneGoBindingCompute(uintptr_t handle, NtValue *values, size_t count, NtValue *out);

static NtResult go_binding_compute(void *context, const NtValue *values, size_t count, NtValue *out)
{
    return nocturneGoBindingCompute((uintptr_t)context, (NtValue *)values, count, out);
}

static void go_binding_property(NtBindingValue *value, NtPropertyRef property)
{
    value->from.property = property;
}

static void go_binding_computed(NtBindingValue *value, const size_t *dependencies, size_t count, uintptr_t handle)
{
    value->from.computed.dependencies = dependencies;
    value->from.computed.count = count;
    value->from.computed.evaluate = go_binding_compute;
    value->from.computed.context = (void *)handle;
}
*/
import "C"

import (
	"runtime/cgo"
	"unsafe"
)

type BindingSource uint32

const (
	BindingInput    BindingSource = C.NT_BINDING_INPUT
	BindingProperty BindingSource = C.NT_BINDING_PROPERTY
	BindingComputed BindingSource = C.NT_BINDING_COMPUTED
)

// BindingCompute receives copies of its dependencies in declaration order.
// It runs synchronously while the owner tree is busy.
type BindingCompute func(inputs []PropertyValue) (PropertyValue, error)

// BindingValue declares a value. Property selects a read for BindingProperty.
// Dependencies and Compute belong to BindingComputed.
type BindingValue struct {
	Kind         PropertyKind
	Source       BindingSource
	Property     PropertyRef
	Dependencies []uint
	Compute      BindingCompute
}

type BindingTarget struct {
	Property PropertyRef
	Value    uint
}

// BindingGraph owns copied native descriptors and retained compute functions.
// Close it when done, even when the owner tree has already closed.
type BindingGraph struct{ state *bindingState }

type bindingState struct {
	tree    Tree
	native  *C.NtBindingGraph
	handles []cgo.Handle
	arena   *RawArena
}

type bindingEvaluator struct {
	graph   *bindingState
	compute BindingCompute
}

//export nocturneGoBindingCompute
func nocturneGoBindingCompute(handle C.uintptr_t, values *C.NtValue, count C.size_t, out *C.NtValue) (status C.NtResult) {
	evaluator := cgo.Handle(handle).Value().(*bindingEvaluator)
	graph := evaluator.graph
	defer func() {
		if cause := recover(); cause != nil {
			graph.tree.state.recordCallbackFailure("bindingCompute", &PanicError{Value: cause})
			status = C.NT_RESULT_CALLBACK_FAILED
		}
	}()

	inputs, err := propertyValuesFromNative(values, count)
	if err != nil {
		return callbackResult(err)
	}
	result, err := evaluator.compute(inputs)
	if err != nil {
		graph.tree.state.recordCallbackFailure("bindingCompute", err)
		return callbackResult(err)
	}
	if err = result.writeNative(graph.arena, out); err != nil {
		return callbackResult(err)
	}
	return C.NT_RESULT_OK
}

func (graph *bindingState) releaseCallbacks() {
	for _, handle := range graph.handles {
		handle.Delete()
	}
	graph.handles = nil
}

func (graph *bindingState) writeValue(arena *RawArena, value BindingValue, out *C.NtBindingValue) error {
	out.kind = C.NtValueKind(value.Kind)
	out.source = C.NtBindingSource(value.Source)
	switch value.Source {
	case BindingProperty:
		property, err := value.Property.native(graph.tree.state)
		if err != nil {
			return err
		}
		C.go_binding_property(out, property)
	case BindingComputed:
		if value.Compute == nil {
			return InvalidArgument
		}
		dependencies, err := nativeSlice[C.size_t](arena, len(value.Dependencies))
		if err != nil {
			return err
		}
		for i, dependency := range value.Dependencies {
			dependencies[i] = C.size_t(dependency)
		}
		handle := cgo.NewHandle(&bindingEvaluator{graph: graph, compute: value.Compute})
		graph.handles = append(graph.handles, handle)
		C.go_binding_computed(out, unsafe.SliceData(dependencies), C.size_t(len(dependencies)), C.uintptr_t(handle))
	}
	return nil
}

// NewBindingGraph validates the sources, dependency order, and writable targets.
func (tree *Tree) NewBindingGraph(values []BindingValue, targets []BindingTarget) (*BindingGraph, error) {
	state, err := tree.acquire()
	if err != nil {
		return nil, err
	}
	defer state.release()
	arena := RawArena{}
	defer arena.close()
	graph := &bindingState{tree: *tree}
	defer func() {
		if graph.native == nil {
			graph.releaseCallbacks()
		}
	}()
	nativeValues, err := nativeSlice[C.NtBindingValue](&arena, len(values))
	if err != nil {
		return nil, err
	}
	for i, value := range values {
		if err = graph.writeValue(&arena, value, &nativeValues[i]); err != nil {
			return nil, err
		}
	}
	nativeTargets, err := nativeSlice[C.NtBindingTarget](&arena, len(targets))
	if err != nil {
		return nil, err
	}
	for i, target := range targets {
		property, err := target.Property.native(state)
		if err != nil {
			return nil, err
		}
		nativeTargets[i] = C.NtBindingTarget{property: property, value: C.size_t(target.Value)}
	}
	var handle *C.NtBindingGraph
	if err = check(C.nt_binding_graph_create(state.native, unsafe.SliceData(nativeValues), C.size_t(len(nativeValues)),
		unsafe.SliceData(nativeTargets), C.size_t(len(nativeTargets)), &handle)); err != nil {
		return nil, err
	}
	graph.native = handle
	return &BindingGraph{state: graph}, nil
}

// Apply supplies input values in declaration order and atomically commits all
// targets. Property sources read the pre-commit state.
func (graph *BindingGraph) Apply(inputs []PropertyValue) error {
	if graph == nil || graph.state == nil {
		return StaleReference
	}
	binding := graph.state
	state, err := binding.tree.acquire()
	if err != nil {
		return err
	}
	defer state.release()
	if binding.native == nil {
		return StaleReference
	}
	arena := RawArena{}
	defer arena.close()
	native, err := nativeSlice[C.NtValue](&arena, len(inputs))
	if err != nil {
		return err
	}
	for i, value := range inputs {
		if err = value.writeNative(&arena, &native[i]); err != nil {
			return err
		}
	}
	binding.arena = &arena
	defer func() { binding.arena = nil }()
	return state.finish(C.nt_binding_graph_apply(binding.native, unsafe.SliceData(native), C.size_t(len(native))))
}

// Close releases the graph and its callbacks. Copies share the same owner.
func (graph *BindingGraph) Close() error {
	if graph == nil || graph.state == nil {
		return nil
	}
	binding := graph.state
	// Synchronize with graph operations and tree destruction, including the
	// detached graph case after Tree.Close.
	lock := &binding.tree.state.locked
	if !lock.CompareAndSwap(false, true) {
		return WrongPhase
	}
	defer lock.Store(false)
	if binding.native == nil {
		return nil
	}
	if err := check(C.nt_binding_graph_destroy(binding.native)); err != nil {
		return err
	}
	binding.native = nil
	binding.releaseCallbacks()
	return nil
}
