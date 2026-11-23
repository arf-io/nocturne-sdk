package nocturne

import (
	"errors"
	"testing"
)

func TestTypedWindowCompositionAndAnimation(t *testing.T) {
	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	visible := MountOptions{Enabled: true}
	size := Size{Columns: 20, Rows: 8}
	workspace, err := tree.MountWindowWorkspace(Node{}, visible, size)
	mustOK(t, err)
	placement := WindowPlacement{Floating: Rect{Size: Size{Columns: 12, Rows: 5}}}
	window, err := tree.MountWindow(workspace, visible, WindowOptions{Placement: &placement, Title: "Notes"})
	mustOK(t, err)
	_, err = tree.MountLabel(window, visible, "Status", testInk)
	mustOK(t, err)
	mustOK(t, tree.Publish(size))
	destination := Rect{Origin: Point{X: 4, Y: 2}, Size: placement.Floating.Size}
	mustOK(t, tree.AnimateWindow(window, WindowAnimation{Destination: destination, DurationUs: 100}))
	mustOK(t, tree.Tick(50, MotionFull))
	mustOK(t, tree.Publish(size))
	state, err := tree.Window(window)
	if state := must(t, state, err); !state.Animating || state.Published.Parent != workspace ||
		state.Published.Bounds.Origin != (Point{X: 2, Y: 1}) {
		t.Fatalf("halfway window = %+v", state)
	}
	mustOK(t, tree.Tick(100, MotionFull))
	mustOK(t, tree.Publish(size))
	state, err = tree.Window(window)
	if state := must(t, state, err); state.Animating || state.Published.Bounds != destination {
		t.Fatalf("finished window = %+v", state)
	}
}

func TestWindowPanelReplacementAndFocus(t *testing.T) {
	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	enabled := MountOptions{Enabled: true, Focusable: true, HitTestable: true}
	size := Size{Columns: 30, Rows: 12}
	workspace, err := tree.MountWindowWorkspace(Node{}, enabled, size)
	mustOK(t, err)
	placement := WindowPlacement{Floating: Rect{Origin: Point{X: 2, Y: 1}, Size: Size{Columns: 16, Rows: 6}}}
	panel := PanelOptions{Framed: true, Border: testInk, TitleStyle: testInk}
	window, err := tree.MountWindow(workspace, enabled, WindowOptions{Placement: &placement, Panel: &panel})
	mustOK(t, err)
	body, err := tree.MountLabel(window, enabled, "Body", testInk)
	mustOK(t, err)
	other, err := tree.MountWindow(workspace, enabled, WindowOptions{Placement: &placement, Panel: &panel})
	mustOK(t, err)
	mustOK(t, tree.Publish(size))
	hit, err := tree.Hit(placement.Floating.Origin)
	mustOK(t, err)
	if hit != other {
		t.Fatalf("initial overlapping window hit = %+v", hit)
	}
	mustOK(t, tree.FocusWindow(window, body))
	focus, err := tree.Focus()
	mustOK(t, err)
	if focus != body {
		t.Fatalf("window descendant focus = %+v", focus)
	}
	if err := tree.FocusWindow(window, other); err != InvalidArgument {
		t.Fatalf("sibling focus = %v", err)
	}
	mustOK(t, tree.Publish(size))
	hit, err = tree.Hit(placement.Floating.Origin)
	mustOK(t, err)
	if hit != window {
		t.Fatalf("raised overlapping window hit = %+v", hit)
	}
	before, err := tree.Geometry(body)
	mustOK(t, err)
	panel.Padding.Left = 3
	panel.Title = "Updated"
	mustOK(t, tree.SetWindowPanel(window, panel))
	mustOK(t, tree.Publish(size))
	after, err := tree.Geometry(body)
	mustOK(t, err)
	if after.Parent != window || after.Bounds.Origin.X != before.Bounds.Origin.X+3 {
		t.Fatalf("replaced panel layout = %+v, previous %+v", after, before)
	}
	mustOK(t, tree.FocusWindow(window, Node{}))
	focus, err = tree.Focus()
	mustOK(t, err)
	if focus != window {
		t.Fatalf("window focus = %+v", focus)
	}
}

func TestPanelContentProviderLifecycle(t *testing.T) {
	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	enabled := MountOptions{Enabled: true, Focusable: true}
	size := Size{Columns: 12, Rows: 6}
	panel, err := tree.MountPanel(Node{}, enabled, PanelOptions{})
	mustOK(t, err)
	var paint *Paint
	var updates, focused, disposed int
	var fail bool
	cause := errors.New("cannot paint provider")
	provider := PanelContentProvider{
		Preferred: Size{Columns: 3, Rows: 2},
		Paint: func(context *Paint, _ Size) error {
			paint = context
			if fail {
				return cause
			}
			return context.Glyph(Point{}, "P", 1, testInk)
		},
		Update: func(UpdateInfo) (UpdateReply, error) {
			updates++
			return UpdateReply{Invalidate: InvalidatePaint}, nil
		},
		Event: func(event *Event) error {
			if event.Kind() == FocusGainEvent {
				focused++
			}
			return nil
		},
		Dispose: func() error { disposed++; return nil },
	}
	body, err := tree.MountPanelContent(panel, enabled, provider)
	mustOK(t, err)
	if _, err := tree.MountPanelContent(Node{}, enabled, provider); err != InvalidArgument || disposed != 0 {
		t.Fatalf("refused mount = %v, disposals = %d", err, disposed)
	}
	mustOK(t, tree.Tick(1, MotionFull))
	mustOK(t, tree.Publish(size))
	mustOK(t, tree.SetFocus(body))
	if updates != 1 || focused != 1 {
		t.Fatalf("provider updates = %d, focus gains = %d", updates, focused)
	}
	frame, err := tree.Frame()
	mustOK(t, err)
	cell, err := frame.Cell(Point{})
	mustOK(t, err)
	if cell.Glyph != "P" || cell.Owner == 0 {
		t.Fatalf("provider glyph = %+v", cell)
	}
	if err := paint.Glyph(Point{}, "!", 1, testInk); err != StaleReference {
		t.Fatalf("retained paint = %v", err)
	}
	fail = true
	mustOK(t, tree.Invalidate(body, InvalidatePaint))
	var failure *CallError
	if err := tree.Publish(size); !errors.As(err, &failure) || failure.Native != CallbackFailed || !errors.Is(err, cause) {
		t.Fatalf("provider paint failure = %v", err)
	}
	current, err := tree.Frame()
	mustOK(t, err)
	kept, err := current.Cell(Point{})
	mustOK(t, err)
	if kept != cell {
		t.Fatalf("failed paint changed cell to %+v", kept)
	}
	mustOK(t, tree.Remove(body))
	if disposed != 1 {
		t.Fatalf("successful mount disposals = %d", disposed)
	}
}
