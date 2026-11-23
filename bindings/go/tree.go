package nocturne

/*
#include "bridge.h"
#include <nocturne/layout.h>
#include <nocturne/widgets.h>

static NtResult go_label_mount(NtTree *tree, NtNodeId parent, NtMountOptions options,
                              NtLabelOptions label, NtNodeId *out)
{
    return nt_label_mount(tree, parent, options, &label, out);
}

static NtResult go_label_set(NtTree *tree, NtNodeId node, NtLabelOptions label)
{
    return nt_label_set(tree, node, &label);
}
*/
import "C"

import (
	"runtime"
	"runtime/cgo"
	"sync/atomic"
)

// Tree owns a native component tree. Close it when done. A concurrent or
// reentrant operation returns WrongPhase. Copies share one native owner.
type Tree struct{ state *treeState }

type treeState struct {
	locked          atomic.Bool
	native          *C.NtTree
	revision        uint64
	failures        []CallbackFailure
	editorViews     map[C.NtNodeId]editorViewLease
	editorResources map[editorViewResource]int
	receivers       map[ScheduleID]scheduledReceiver
}

type Node struct {
	owner *treeState
	id    C.NtNodeId
}

func (node Node) IsZero() bool { return node.owner == nil }

type MountOptions struct {
	Enabled, Focusable, HitTestable bool
}

func (options MountOptions) native() C.NtMountOptions {
	return C.NtMountOptions{enabled: C.bool(options.Enabled), focusable: C.bool(options.Focusable),
		hit_testable: C.bool(options.HitTestable)}
}

type Geometry struct {
	Parent          Node
	Bounds, Clip    Rect
	Placed, Enabled bool
}

type Intent struct {
	Source  Node
	Kind    uint32
	Payload string
}

// NewTree creates an empty native tree.
func NewTree() (*Tree, error) {
	if err := ensureABI(); err != nil {
		return nil, err
	}
	var native *C.NtTree
	if err := check(C.nt_tree_create(nil, &native)); err != nil {
		return nil, err
	}
	return &Tree{state: &treeState{native: native}}, nil
}

func (tree *Tree) acquire() (*treeState, error) {
	if tree == nil || tree.state == nil {
		return nil, StaleReference
	}
	state := tree.state
	if !state.locked.CompareAndSwap(false, true) {
		return nil, WrongPhase
	}
	if state.native == nil {
		state.release()
		return nil, StaleReference
	}
	for resource := range state.editorResources {
		if !resource.borrowTree(state) {
			state.release()
			return nil, WrongPhase
		}
	}
	return state, nil
}

func (state *treeState) release() {
	for resource := range state.editorResources {
		resource.releaseTree(state)
	}
	state.locked.Store(false)
}

func (state *treeState) finish(status C.NtResult) error {
	native := check(status)
	if len(state.failures) == 0 {
		return native
	}
	failure := &CallError{Native: native, Callbacks: state.failures}
	state.failures = nil
	return failure
}

func (state *treeState) node(id C.NtNodeId) Node {
	if id.tree == nil {
		return Node{}
	}
	return Node{owner: state, id: id}
}

func (state *treeState) reference(node Node) (C.NtNodeId, error) {
	if node.IsZero() || node.owner == state {
		return node.id, nil
	}
	return C.NtNodeId{}, InvalidArgument
}

type nativeMount func(*C.NtTree, C.NtNodeId, *C.NtNodeId) C.NtResult

func (tree *Tree) mountNative(parent Node, mount nativeMount) (Node, error) {
	state, err := tree.acquire()
	if err != nil {
		return Node{}, err
	}
	defer state.release()
	id, err := state.reference(parent)
	if err != nil {
		return Node{}, err
	}
	var mounted C.NtNodeId
	status := mount(state.native, id, &mounted)
	return state.node(mounted), state.finish(status)
}

func (tree *Tree) Mount(parent Node, options MountOptions, component Component) (Node, error) {
	return tree.mountHostedComponent(parent, component, func(tree *C.NtTree, parent C.NtNodeId, handle C.uintptr_t, out *C.NtNodeId) C.NtResult {
		return C.nt_go_mount(tree, parent, handle, options.native(), out)
	})
}

type hostedMount func(*C.NtTree, C.NtNodeId, C.uintptr_t, *C.NtNodeId) C.NtResult

func (tree *Tree) mountHostedComponent(parent Node, component Component, mount hostedMount) (Node, error) {
	if component == nil {
		return Node{}, InvalidArgument
	}
	state, err := tree.acquire()
	if err != nil {
		return Node{}, err
	}
	defer state.release()
	parentID, err := state.reference(parent)
	if err != nil {
		return Node{}, err
	}
	handle := cgo.NewHandle(&mounted{tree: state, component: component})
	var id C.NtNodeId
	status := mount(state.native, parentID, C.uintptr_t(handle), &id)
	if status != C.NT_RESULT_OK {
		handle.Delete()
	}
	return state.node(id), state.finish(status)
}

type BoxKind uint32

const (
	BoxRow    BoxKind = C.NT_BOX_LAYOUT_ROW
	BoxColumn BoxKind = C.NT_BOX_LAYOUT_COLUMN
	BoxStack  BoxKind = C.NT_BOX_LAYOUT_STACK
)

type Insets struct{ Left, Top, Right, Bottom uint32 }

func (insets Insets) native() C.NtBoxInsets {
	return C.NtBoxInsets{
		left: C.uint32_t(insets.Left), top: C.uint32_t(insets.Top),
		right: C.uint32_t(insets.Right), bottom: C.uint32_t(insets.Bottom),
	}
}

type Box struct {
	Kind    BoxKind
	Padding Insets
	Gap     uint32
}

func (tree *Tree) MountBox(parent Node, options MountOptions, box Box) (Node, error) {
	native := C.NtBoxLayout{kind: C.NtBoxLayoutKind(box.Kind), gap: C.uint32_t(box.Gap),
		padding: box.Padding.native()}
	return tree.mountNative(parent, func(tree *C.NtTree, parent C.NtNodeId, out *C.NtNodeId) C.NtResult {
		return C.nt_box_layout_mount(tree, parent, options.native(), &native, out)
	})
}

func labelOptions(text string, palette Style) C.NtLabelOptions {
	return C.NtLabelOptions{text: nativeBytes(text), style: C.NtLabelStyle{
		width: C.NtWidthPolicy{ambiguous: C.NT_WIDTH_ONE, emoji: C.NT_WIDTH_TWO},
		wrap:  C.NT_WRAP_NONE, align: C.NT_TEXT_ALIGN_START, direction: C.NT_DIRECTION_AUTO,
		tab_stop: 4, color: palette.native(),
	}}
}

func (tree *Tree) MountLabel(parent Node, options MountOptions, text string, palette Style) (Node, error) {
	return tree.mountNative(parent, func(tree *C.NtTree, parent C.NtNodeId, out *C.NtNodeId) C.NtResult {
		status := C.go_label_mount(tree, parent, options.native(), labelOptions(text, palette), out)
		runtime.KeepAlive(text)
		return status
	})
}

func (tree *Tree) SetLabel(node Node, text string, palette Style) error {
	return tree.callNode(node, func(tree *C.NtTree, node C.NtNodeId) C.NtResult {
		status := C.go_label_set(tree, node, labelOptions(text, palette))
		runtime.KeepAlive(text)
		return status
	})
}

func (tree *Tree) callNode(node Node, operation func(*C.NtTree, C.NtNodeId) C.NtResult) error {
	state, err := tree.acquire()
	if err != nil {
		return err
	}
	defer state.release()
	id, err := state.reference(node)
	if err != nil {
		return err
	}
	return state.finish(operation(state.native, id))
}

func (tree *Tree) Remove(node Node) error {
	return tree.callNode(node, func(native *C.NtTree, node C.NtNodeId) C.NtResult {
		result := C.nt_tree_remove(native, node)
		tree.state.pruneEditorViews()
		tree.state.pruneScheduleReceivers()
		return result
	})
}

func (tree *Tree) Move(node, parent Node) error {
	return tree.callNode(node, func(native *C.NtTree, node C.NtNodeId) C.NtResult {
		next, err := tree.state.reference(parent)
		if err != nil {
			return C.NT_RESULT_INVALID_ARGUMENT
		}
		return C.nt_tree_move(native, node, next)
	})
}

func (tree *Tree) Invalidate(node Node, reason Invalidation) error {
	return tree.callNode(node, func(tree *C.NtTree, node C.NtNodeId) C.NtResult {
		return C.nt_tree_invalidate(tree, node, C.NtInvalidation(reason))
	})
}

func (tree *Tree) SetFocus(node Node) error {
	return tree.callNode(node, func(tree *C.NtTree, node C.NtNodeId) C.NtResult {
		return C.nt_tree_set_focus(tree, node)
	})
}

// FocusAdvance selects the next published focusable node in traversal order.
func (tree *Tree) FocusAdvance(reverse bool) error {
	state, err := tree.acquire()
	if err != nil {
		return err
	}
	defer state.release()
	return state.finish(C.nt_tree_focus_advance(state.native, C.bool(reverse)))
}

func (tree *Tree) Focus() (Node, error) {
	state, err := tree.acquire()
	if err != nil {
		return Node{}, err
	}
	defer state.release()
	return state.node(C.nt_tree_focus(state.native)), nil
}

func (tree *Tree) Capture(pointer uint64) (Node, error) {
	state, err := tree.acquire()
	if err != nil {
		return Node{}, err
	}
	defer state.release()
	return state.node(C.nt_tree_capture(state.native, C.NtPointerId(pointer))), nil
}

func (tree *Tree) Geometry(node Node) (Geometry, error) {
	state, err := tree.acquire()
	if err != nil {
		return Geometry{}, err
	}
	defer state.release()
	id, err := state.reference(node)
	if err != nil {
		return Geometry{}, err
	}
	var out C.NtNodeGeometry
	if err := check(C.nt_tree_geometry(state.native, id, &out)); err != nil {
		return Geometry{}, err
	}
	return state.geometry(out), nil
}

func (state *treeState) geometry(native C.NtNodeGeometry) Geometry {
	return Geometry{Parent: state.node(native.parent), Bounds: rectFromNative(native.bounds), Clip: rectFromNative(native.clip),
		Placed: bool(native.placed), Enabled: bool(native.enabled)}
}

// Hit returns the topmost enabled, hit-testable node at a published point.
// A zero node means the point hit no node.
func (tree *Tree) Hit(point Point) (Node, error) {
	state, err := tree.acquire()
	if err != nil {
		return Node{}, err
	}
	defer state.release()
	var out C.NtNodeId
	if err := check(C.nt_tree_hit(state.native, point.native(), &out)); err != nil {
		return Node{}, err
	}
	return state.node(out), nil
}

func (tree *Tree) Tick(now uint64, motion Motion) error {
	state, err := tree.acquire()
	if err != nil {
		return err
	}
	defer state.release()
	return state.finish(C.nt_tree_tick(state.native, C.NtTimeUs(now), C.NtMotionPolicy(motion)))
}

func (tree *Tree) NextUpdate() (Deadline, error) {
	state, err := tree.acquire()
	if err != nil {
		return Deadline{}, err
	}
	defer state.release()
	next := C.nt_tree_next_update(state.native)
	return Deadline{Scheduled: bool(next.scheduled), At: uint64(next.at)}, nil
}

func (tree *Tree) Publish(size Size) error {
	state, err := tree.acquire()
	if err != nil {
		return err
	}
	defer state.release()
	status := C.nt_tree_publish(state.native, size.native())
	if status == C.NT_RESULT_OK {
		state.revision++
	}
	return state.finish(status)
}

func (tree *Tree) PeekIntent() (Intent, bool, error) {
	state, err := tree.acquire()
	if err != nil {
		return Intent{}, false, err
	}
	defer state.release()
	var out C.NtIntentView
	if !bool(C.nt_tree_peek_intent(state.native, &out)) {
		return Intent{}, false, nil
	}
	payload, err := copyBytes(out.payload)
	if err != nil {
		return Intent{}, false, err
	}
	return Intent{Source: state.node(out.source), Kind: uint32(out.kind), Payload: payload}, true, nil
}

func (tree *Tree) PopIntent() error {
	state, err := tree.acquire()
	if err != nil {
		return err
	}
	defer state.release()
	C.nt_tree_pop_intent(state.native)
	return nil
}

// Close destroys the tree and reports any disposal failures from this call.
func (tree *Tree) Close() error {
	state, err := tree.acquire()
	if err == StaleReference {
		return nil
	}
	if err != nil {
		return err
	}
	defer state.release()
	C.nt_tree_destroy(state.native)
	state.native = nil
	state.pruneEditorViews()
	state.pruneScheduleReceivers()
	return state.finish(C.NT_RESULT_OK)
}
