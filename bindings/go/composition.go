package nocturne

/*
#include <nocturne/viewport.h>
#include <nocturne/widgets.h>

static NtResult go_panel_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount,
                               NtPanelOptions panel, NtNodeId *out)
{
    return nt_panel_mount(tree, parent, mount, &panel, out);
}

static NtResult go_panel_set(NtTree *tree, NtNodeId node, NtPanelOptions panel)
{
    return nt_panel_set(tree, node, &panel);
}
*/
import "C"

import "runtime"

type PanelOptions struct {
	Fill          Color
	Padding       Insets
	Framed, ASCII bool
	Border        Style
	Title         string
	TitleStyle    Style
}

func (panel PanelOptions) native() C.NtPanelOptions {
	return C.NtPanelOptions{fill: panel.Fill.native(), framed: C.bool(panel.Framed), ascii: C.bool(panel.ASCII),
		border: panel.Border.native(), title: labelOptions(panel.Title, panel.TitleStyle),
		padding: C.NtBoxInsets{left: C.uint32_t(panel.Padding.Left), top: C.uint32_t(panel.Padding.Top),
			right: C.uint32_t(panel.Padding.Right), bottom: C.uint32_t(panel.Padding.Bottom)}}
}

// MountPanel copies the panel options and title. A panel has at most one
// direct child, which can be a layout or another container.
func (tree *Tree) MountPanel(parent Node, options MountOptions, panel PanelOptions) (Node, error) {
	return tree.mountNative(parent, func(tree *C.NtTree, parent C.NtNodeId, out *C.NtNodeId) C.NtResult {
		result := C.go_panel_mount(tree, parent, options.native(), panel.native(), out)
		runtime.KeepAlive(panel)
		return result
	})
}

func (tree *Tree) SetPanel(node Node, panel PanelOptions) error {
	return tree.callNode(node, func(tree *C.NtTree, id C.NtNodeId) C.NtResult {
		result := C.go_panel_set(tree, id, panel.native())
		runtime.KeepAlive(panel)
		return result
	})
}

type ViewportOptions struct {
	ScrollColumns, ScrollRows bool
	Requested                 Size
}

type ViewportState struct {
	Placed                       bool
	Child                        Node
	Viewport, Content, Effective Size
}

func (options ViewportOptions) native() C.NtViewportOptions {
	return C.NtViewportOptions{scroll_columns: C.bool(options.ScrollColumns), scroll_rows: C.bool(options.ScrollRows),
		requested: options.Requested.native()}
}

// ResolveScroll clamps each requested offset to the available distance.
func ResolveScroll(options ViewportOptions, viewport, content Size) (Size, error) {
	if err := ensureABI(); err != nil {
		return Size{}, err
	}
	return sizeFromNative(C.nt_scroll_resolve(options.native(), viewport.native(), content.native())), nil
}

// AnchorRect positions the child in parent coordinates and translates it by offset.
func AnchorRect(parent, child Size, horizontal, vertical BoxAlign, offset Point) (Rect, error) {
	if err := ensureABI(); err != nil {
		return Rect{}, err
	}
	var out C.NtCellRect
	if err := check(C.nt_anchor_rect(parent.native(), child.native(), C.NtBoxAlign(horizontal),
		C.NtBoxAlign(vertical), offset.native(), &out)); err != nil {
		return Rect{}, err
	}
	return rectFromNative(out), nil
}

func (tree *Tree) MountViewport(parent Node, options MountOptions, viewport ViewportOptions) (Node, error) {
	return tree.mountNative(parent, func(tree *C.NtTree, parent C.NtNodeId, out *C.NtNodeId) C.NtResult {
		return C.nt_viewport_mount(tree, parent, options.native(), viewport.native(), out)
	})
}

func (tree *Tree) SetViewport(node Node, viewport ViewportOptions) error {
	return tree.callNode(node, func(tree *C.NtTree, id C.NtNodeId) C.NtResult {
		return C.nt_viewport_set(tree, id, viewport.native())
	})
}

// Viewport returns the scroll and placement from the last published frame.
func (tree *Tree) Viewport(node Node) (ViewportState, error) {
	state, err := tree.acquire()
	if err != nil {
		return ViewportState{}, err
	}
	defer state.release()
	id, err := state.reference(node)
	if err != nil {
		return ViewportState{}, err
	}
	var out C.NtViewportView
	if err := check(C.nt_viewport_view(state.native, id, &out)); err != nil {
		return ViewportState{}, err
	}
	return ViewportState{Placed: bool(out.placed), Child: state.node(out.child),
		Viewport: sizeFromNative(out.viewport), Content: sizeFromNative(out.content),
		Effective: sizeFromNative(out.effective)}, nil
}
