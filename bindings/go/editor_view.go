package nocturne

/*
#include <nocturne/controls.h>

static NtResult go_editor_view_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount,
                                     NtEditorViewOptions options, const NtEditorBinding *bindings,
                                     size_t count, NtEditorRegisters *registers,
                                     const NtControlStyle *style, NtNodeId *out)
{
    const NtEditorKeyMap keys = {bindings, count, registers};
    options.keys = &keys;
    options.style = style;
    return nt_editor_view_mount(tree, parent, mount, &options, out);
}
*/
import "C"

import "unsafe"

type WrapMode uint32

const (
	WrapNone    WrapMode = C.NT_WRAP_NONE
	WrapWord    WrapMode = C.NT_WRAP_WORD
	WrapCluster WrapMode = C.NT_WRAP_CLUSTER
)

// EditorViewOptions composes an editor or read-only reader into a tree.
// The mount retains the editor and bank and copies keys and style.
type EditorViewOptions struct {
	Editor                    *Editor
	Keys                      *EditorKeyMap
	Style                     *ControlStyle
	Wrap                      WrapMode
	FollowCaret, ShowCaret    bool
	SubmitOnEnter, ScrollKeys bool
}

type editorViewResource interface {
	retainView(*treeState) error
	borrowTree(*treeState) bool
	releaseTree(*treeState)
	dropView(lastForTree bool)
}

type editorViewLease [2]editorViewResource

func (state *treeState) retainEditorView(lease editorViewLease) error {
	for index, resource := range lease {
		if resource == nil {
			continue
		}
		if err := resource.retainView(state); err != nil {
			for _, acquired := range lease[:index] {
				state.dropEditorResource(acquired)
			}
			return err
		}
		if state.editorResources == nil {
			state.editorResources = make(map[editorViewResource]int)
		}
		state.editorResources[resource]++
	}
	return nil
}

func (state *treeState) dropEditorResource(resource editorViewResource) {
	if resource == nil {
		return
	}
	count := state.editorResources[resource] - 1
	if count == 0 {
		delete(state.editorResources, resource)
	} else {
		state.editorResources[resource] = count
	}
	resource.dropView(count == 0)
}

func (state *treeState) pruneEditorViews() {
	for id, lease := range state.editorViews {
		if state.native != nil {
			var geometry C.NtNodeGeometry
			if C.nt_tree_geometry(state.native, id, &geometry) != C.NT_RESULT_STALE_REFERENCE {
				continue
			}
		}
		delete(state.editorViews, id)
		for _, resource := range lease {
			state.dropEditorResource(resource)
		}
	}
}

// MountEditorView adds an editable or read-only view. Mutating a shared
// editor directly requires invalidation of each affected view.
func (tree *Tree) MountEditorView(parent Node, mount MountOptions, options EditorViewOptions) (Node, error) {
	if options.Editor == nil {
		return Node{}, InvalidArgument
	}
	editor := options.Editor.state
	if editor == nil {
		return Node{}, StaleReference
	}
	var bank *editorRegisterState
	var bindings []C.NtEditorBinding
	if options.Keys != nil {
		bindings = nativeEditorBindings(options.Keys.Bindings)
		if options.Keys.Registers != nil {
			bank = options.Keys.Registers.state
			if bank == nil {
				return Node{}, StaleReference
			}
		}
	}

	state, err := tree.acquire()
	if err != nil {
		return Node{}, err
	}
	defer state.release()
	id, err := state.reference(parent)
	if err != nil {
		return Node{}, err
	}
	lease := editorViewLease{editor}
	if bank != nil {
		lease[1] = bank
	}
	if err := state.retainEditorView(lease); err != nil {
		return Node{}, err
	}
	var registers *C.NtEditorRegisters
	if bank != nil {
		registers = bank.native
	}
	native := C.NtEditorViewOptions{editor: editor.native, wrap: C.NtWrapMode(options.Wrap),
		follow_caret: C.bool(options.FollowCaret), show_caret: C.bool(options.ShowCaret),
		submit_on_enter: C.bool(options.SubmitOnEnter), scroll_keys: C.bool(options.ScrollKeys)}
	var out C.NtNodeId
	status := C.go_editor_view_mount(state.native, id, mount.native(), native, unsafe.SliceData(bindings),
		C.size_t(len(bindings)), registers, options.Style.native(), &out)
	if status == C.NT_RESULT_OK {
		if state.editorViews == nil {
			state.editorViews = make(map[C.NtNodeId]editorViewLease)
		}
		state.editorViews[out] = lease
	} else {
		for _, resource := range lease {
			state.dropEditorResource(resource)
		}
	}
	return state.node(out), state.finish(status)
}

// ScrollEditorView requests an offset for the next successful publication.
func (tree *Tree) ScrollEditorView(node Node, offset Size) error {
	return tree.callNode(node, func(native *C.NtTree, id C.NtNodeId) C.NtResult {
		return C.nt_editor_view_scroll(native, id, offset.native())
	})
}

// EditorViewOffset reads the clamped offset from the published view.
func (tree *Tree) EditorViewOffset(node Node) (Size, error) {
	var offset C.NtCellSize
	err := tree.callNode(node, func(native *C.NtTree, id C.NtNodeId) C.NtResult {
		return C.nt_editor_view_offset(native, id, &offset)
	})
	return sizeFromNative(offset), err
}
