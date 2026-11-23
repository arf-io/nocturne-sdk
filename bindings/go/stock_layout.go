package nocturne

/*
#include <nocturne/widgets.h>
*/
import "C"

import "unsafe"

type BoxAlign uint32

const (
	BoxStart  BoxAlign = C.NT_BOX_START
	BoxCenter BoxAlign = C.NT_BOX_CENTER
	BoxEnd    BoxAlign = C.NT_BOX_END
)

type BoxItem struct {
	Columns, Rows         Extent
	GrowColumns, GrowRows bool
}

func (item BoxItem) native() C.NtBoxItem {
	return C.NtBoxItem{columns: item.Columns.native(), rows: item.Rows.native(),
		grow_columns: C.bool(item.GrowColumns), grow_rows: C.bool(item.GrowRows)}
}

// BoxLayout extends a box with child alignment and extent overrides.
// Items address children by insertion index. Fallback applies to later children.
type BoxLayout struct {
	Box
	Horizontal, Vertical BoxAlign
	Fallback             BoxItem
	Items                []BoxItem
}

func (box BoxLayout) native(arena *RawArena) (C.NtBoxLayout, error) {
	items, err := nativeSlice[C.NtBoxItem](arena, len(box.Items))
	if err != nil {
		return C.NtBoxLayout{}, err
	}
	for index, item := range box.Items {
		items[index] = item.native()
	}
	return C.NtBoxLayout{
		kind: C.NtBoxLayoutKind(box.Kind), gap: C.uint32_t(box.Gap), padding: box.Padding.native(),
		horizontal: C.NtBoxAlign(box.Horizontal), vertical: C.NtBoxAlign(box.Vertical),
		fallback: box.Fallback.native(), items: unsafe.SliceData(items), item_count: C.size_t(len(items)),
	}, nil
}

func (tree *Tree) MountBoxLayout(parent Node, options MountOptions, box BoxLayout) (Node, error) {
	var arena RawArena
	defer arena.close()
	layout, err := box.native(&arena)
	if err != nil {
		return Node{}, err
	}
	return tree.mountNative(parent, func(tree *C.NtTree, parent C.NtNodeId, out *C.NtNodeId) C.NtResult {
		return C.nt_box_layout_mount(tree, parent, options.native(), &layout, out)
	})
}

// SetBoxLayout replaces the configuration and retains the mount and children.
func (tree *Tree) SetBoxLayout(node Node, box BoxLayout) error {
	var arena RawArena
	defer arena.close()
	layout, err := box.native(&arena)
	if err != nil {
		return err
	}
	return tree.callNode(node, func(tree *C.NtTree, node C.NtNodeId) C.NtResult {
		return C.nt_box_layout_set(tree, node, &layout)
	})
}

// RuleOptions describes a native rule. Supply an opaque foreground in Style
// to show the glyph. A zero Style has transparent ink.
type RuleOptions struct {
	Vertical, ASCII bool
	Style           Style
}

func (rule RuleOptions) native() C.NtRuleOptions {
	return C.NtRuleOptions{vertical: C.bool(rule.Vertical), ascii: C.bool(rule.ASCII), color: rule.Style.native()}
}

func (tree *Tree) MountRule(parent Node, options MountOptions, rule RuleOptions) (Node, error) {
	return tree.mountNative(parent, func(tree *C.NtTree, parent C.NtNodeId, out *C.NtNodeId) C.NtResult {
		return C.nt_rule_mount(tree, parent, options.native(), rule.native(), out)
	})
}

func (tree *Tree) SetRule(node Node, rule RuleOptions) error {
	return tree.callNode(node, func(tree *C.NtTree, node C.NtNodeId) C.NtResult {
		return C.nt_rule_set(tree, node, rule.native())
	})
}
