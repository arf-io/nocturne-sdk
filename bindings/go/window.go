package nocturne

/*
#include <nocturne/window.h>
#include "bridge.h"

static NtResult go_window_mount(NtTree *tree, NtNodeId workspace, NtMountOptions mount,
                                NtWindowOptions options, NtNodeId *out)
{
    return nt_window_mount(tree, workspace, mount, &options, out);
}
static NtResult go_window_set_panel(NtTree *tree, NtNodeId node, NtPanelOptions panel)
{
    return nt_window_set_panel(tree, node, &panel);
}
*/
import "C"

import "runtime"

type WindowMode uint32

const (
	WindowNormal    WindowMode = C.NT_WINDOW_NORMAL
	WindowMinimized WindowMode = C.NT_WINDOW_MINIMIZED
	WindowMaximized WindowMode = C.NT_WINDOW_MAXIMIZED
)

type WindowDock uint32

const (
	WindowFloating WindowDock = C.NT_WINDOW_FLOAT
	WindowLeft     WindowDock = C.NT_WINDOW_DOCK_LEFT
	WindowRight    WindowDock = C.NT_WINDOW_DOCK_RIGHT
	WindowTop      WindowDock = C.NT_WINDOW_DOCK_TOP
	WindowBottom   WindowDock = C.NT_WINDOW_DOCK_BOTTOM
)

type WindowPlacement struct {
	Floating   Rect
	Mode       WindowMode
	Dock       WindowDock
	DockExtent uint32
}

func (placement WindowPlacement) native() C.NtWindowPlacement {
	return C.NtWindowPlacement{floating: placement.Floating.native(), mode: C.NtWindowMode(placement.Mode),
		dock: C.NtWindowDock(placement.Dock), dock_extent: C.uint32_t(placement.DockExtent)}
}

func windowPlacementFromNative(placement C.NtWindowPlacement) WindowPlacement {
	return WindowPlacement{Floating: rectFromNative(placement.floating), Mode: WindowMode(placement.mode),
		Dock: WindowDock(placement.dock), DockExtent: uint32(placement.dock_extent)}
}

type WindowOptions struct {
	Placement                  *WindowPlacement
	Panel                      *PanelOptions
	Title                      string
	DisableDrag, DisableResize bool
}

type WindowAnimation struct {
	Destination         Rect
	StartUs, DurationUs uint64
	Easing              Easing
}

type WindowState struct {
	Requested WindowPlacement
	Published Geometry
	Animating bool
}

func (tree *Tree) MountWindowWorkspace(parent Node, options MountOptions, preferred Size) (Node, error) {
	return tree.mountNative(parent, func(tree *C.NtTree, parent C.NtNodeId, out *C.NtNodeId) C.NtResult {
		return C.nt_window_workspace_mount(tree, parent, options.native(), preferred.native(), out)
	})
}

func (tree *Tree) MountWindow(workspace Node, options MountOptions, window WindowOptions) (Node, error) {
	return tree.mountNative(workspace, func(tree *C.NtTree, parent C.NtNodeId, out *C.NtNodeId) C.NtResult {
		native := C.nt_window_options_default()
		if window.Placement != nil {
			native.placement = window.Placement.native()
		}
		if window.Panel != nil {
			native.panel = window.Panel.native()
		}
		if window.Title != "" {
			native.panel.title.text = nativeBytes(window.Title)
		}
		native.draggable = C.bool(!window.DisableDrag)
		native.resizable = C.bool(!window.DisableResize)
		result := C.go_window_mount(tree, parent, options.native(), native, out)
		runtime.KeepAlive(window)
		return result
	})
}

func (tree *Tree) SetWindowPlacement(node Node, placement WindowPlacement) error {
	return tree.callNode(node, func(native *C.NtTree, id C.NtNodeId) C.NtResult {
		return C.nt_window_set(native, id, placement.native())
	})
}

// SetWindowPanel copies the chrome and padding without replacing the window body.
func (tree *Tree) SetWindowPanel(node Node, panel PanelOptions) error {
	return tree.callNode(node, func(native *C.NtTree, id C.NtNodeId) C.NtResult {
		result := C.go_window_set_panel(native, id, panel.native())
		runtime.KeepAlive(panel)
		return result
	})
}

// FocusWindow raises a published window and focuses it or the given descendant.
func (tree *Tree) FocusWindow(node, target Node) error {
	state, err := tree.acquire()
	if err != nil {
		return err
	}
	defer state.release()
	id, err := state.reference(node)
	if err != nil {
		return err
	}
	focus, err := state.reference(target)
	if err != nil {
		return err
	}
	return state.finish(C.nt_window_focus(state.native, id, focus))
}

func (tree *Tree) AnimateWindow(node Node, animation WindowAnimation) error {
	return tree.callNode(node, func(native *C.NtTree, id C.NtNodeId) C.NtResult {
		return C.nt_window_animate(native, id, C.NtWindowAnimation{destination: animation.Destination.native(),
			start: C.NtTimeUs(animation.StartUs), duration: C.NtTimeUs(animation.DurationUs),
			easing: C.NtEasing(animation.Easing)})
	})
}

func (tree *Tree) Window(node Node) (WindowState, error) {
	state, err := tree.acquire()
	if err != nil {
		return WindowState{}, err
	}
	defer state.release()
	id, err := state.reference(node)
	if err != nil {
		return WindowState{}, err
	}
	var out C.NtWindowState
	if err := check(C.nt_window_get(state.native, id, &out)); err != nil {
		return WindowState{}, err
	}
	return WindowState{Requested: windowPlacementFromNative(out.requested), Published: state.geometry(out.published),
		Animating: bool(out.animating)}, nil
}

// PanelContentProvider supplies a preferred size and scoped callbacks for a leaf.
type PanelContentProvider struct {
	Preferred Size
	Paint     func(*Paint, Size) error
	Update    func(UpdateInfo) (UpdateReply, error)
	Event     func(*Event) error
	Dispose   func() error
}

type panelContentAdapter struct {
	ComponentBase
	provider PanelContentProvider
}

func (adapter panelContentAdapter) Paint(context *Paint, assigned Size) error {
	return adapter.provider.Paint(context, assigned)
}

func (adapter panelContentAdapter) Update(info UpdateInfo) (UpdateReply, error) {
	return adapter.provider.Update(info)
}

func (adapter panelContentAdapter) Event(event *Event) error {
	return adapter.provider.Event(event)
}

func (adapter panelContentAdapter) Dispose() error {
	if adapter.provider.Dispose != nil {
		return adapter.provider.Dispose()
	}
	return nil
}

// MountPanelContent copies the provider. A successful mount owns its callbacks
// until removal. Paint is required, and all other callbacks are optional.
func (tree *Tree) MountPanelContent(parent Node, mount MountOptions, provider PanelContentProvider) (Node, error) {
	if provider.Paint == nil {
		return Node{}, InvalidArgument
	}
	return tree.mountHostedComponent(parent, panelContentAdapter{provider: provider},
		func(tree *C.NtTree, parent C.NtNodeId, handle C.uintptr_t, out *C.NtNodeId) C.NtResult {
			return C.nt_go_mount_panel_content(tree, parent, handle, mount.native(), provider.Preferred.native(),
				C.bool(provider.Update != nil), C.bool(provider.Event != nil), out)
		})
}
