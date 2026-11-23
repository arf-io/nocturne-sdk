package nocturne

import "testing"

func TestTypedPanelAndViewport(t *testing.T) {
	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	visible := MountOptions{Enabled: true}
	panel, err := tree.MountPanel(Node{}, visible, PanelOptions{
		Framed: true, ASCII: true, Border: testInk, Title: "View", TitleStyle: testInk,
	})
	mustOK(t, err)
	view, err := tree.MountViewport(panel, visible, ViewportOptions{ScrollColumns: true})
	mustOK(t, err)
	label, err := tree.MountLabel(view, visible, "abcdef", testInk)
	mustOK(t, err)
	frame := Size{Columns: 5, Rows: 3}
	mustOK(t, tree.Publish(frame))
	mustOK(t, tree.SetViewport(view, ViewportOptions{ScrollColumns: true, Requested: Size{Columns: 4}}))
	state, err := tree.Viewport(view)
	if state := must(t, state, err); !state.Placed || state.Child != label || state.Effective.Columns != 0 {
		t.Fatalf("previous viewport = %+v", state)
	}
	mustOK(t, tree.Publish(frame))
	state, err = tree.Viewport(view)
	if state := must(t, state, err); state.Effective.Columns != 3 || state.Viewport.Columns != 3 || state.Content.Columns != 6 {
		t.Fatalf("published viewport = %+v", state)
	}
}
