package nocturne

import "testing"

func TestStockBoxLayoutRetainsChildrenAndCopiesOverrides(t *testing.T) {
	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()

	enabled := MountOptions{Enabled: true}
	layout := BoxLayout{Box: Box{Kind: BoxRow, Padding: Insets{Left: 1}, Gap: 1},
		Items: []BoxItem{{Columns: Extent{Min: 2, Max: 2, Bounded: true}}},
	}
	box, err := tree.MountBoxLayout(Node{}, enabled, layout)
	mustOK(t, err)
	label, err := tree.MountLabel(box, enabled, "A", testInk)
	mustOK(t, err)
	rule, err := tree.MountRule(box, enabled, RuleOptions{ASCII: true, Style: testInk})
	mustOK(t, err)
	layout.Items[0].Columns.Max = 1

	size := Size{Columns: 8, Rows: 5}
	mustOK(t, tree.Publish(size))
	geometry, err := tree.Geometry(label)
	mustOK(t, err)
	if geometry.Bounds != (Rect{Origin: Point{X: 1}, Size: Size{Columns: 2, Rows: 1}}) {
		t.Fatalf("row label bounds: %+v", geometry.Bounds)
	}
	if err := tree.SetBoxLayout(box, layout); err != InvalidArgument {
		t.Fatalf("invalid extents returned %v", err)
	}

	next := BoxLayout{Box: Box{Kind: BoxColumn, Padding: Insets{Left: 2, Top: 1}}}
	mustOK(t, tree.SetBoxLayout(box, next))
	mustOK(t, tree.SetRule(rule, RuleOptions{Vertical: true, ASCII: true, Style: testInk}))
	mustOK(t, tree.Publish(size))
	geometry, err = tree.Geometry(label)
	mustOK(t, err)
	if geometry.Bounds.Origin != (Point{X: 2, Y: 1}) {
		t.Fatalf("column label bounds: %+v", geometry.Bounds)
	}
	frame, err := tree.Frame()
	mustOK(t, err)
	cell, err := frame.Cell(Point{X: 2, Y: 2})
	mustOK(t, err)
	if cell.Glyph != "|" {
		t.Fatalf("vertical rule cell: %+v", cell)
	}
}

type clippedLayout struct {
	ComponentBase
	held *Layout
}

func (parent *clippedLayout) Measure(measure *Measure, limits Constraints) (Size, error) {
	return measure.Child(0, limits)
}

func (parent *clippedLayout) Layout(layout *Layout, _ Size) error {
	parent.held = layout
	return layout.PlaceClipped(0,
		Rect{Origin: Point{X: 1}, Size: Size{Columns: 4, Rows: 2}}, 0,
		Rect{Origin: Point{X: 2}, Size: Size{Columns: 1, Rows: 1}})
}

type clippedPaint struct {
	ComponentBase
	clip Rect
}

func (child *clippedPaint) Paint(paint *Paint, _ Size) error {
	var err error
	child.clip, err = paint.Clip()
	if err != nil {
		return err
	}
	if err := paint.Glyph(Point{X: 1}, "X", 1, testInk); err != nil {
		return err
	}
	return paint.EraseGlyph(Point{X: 1})
}

func TestClippedLayoutUsesLocalPaintClip(t *testing.T) {
	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	parent, child := &clippedLayout{}, &clippedPaint{}
	root, err := tree.Mount(Node{}, MountOptions{Enabled: true}, parent)
	mustOK(t, err)
	node, err := tree.Mount(root, MountOptions{Enabled: true, HitTestable: true}, child)
	mustOK(t, err)
	mustOK(t, tree.Publish(Size{Columns: 6, Rows: 2}))
	if child.clip != (Rect{Origin: Point{X: 1}, Size: Size{Columns: 1, Rows: 1}}) {
		t.Fatalf("local clip: %+v", child.clip)
	}
	geometry, err := tree.Geometry(node)
	mustOK(t, err)
	if geometry.Clip.Origin != (Point{X: 2}) || geometry.Bounds.Size.Columns != 4 {
		t.Fatalf("assigned geometry: %+v", geometry)
	}
	hit, err := tree.Hit(Point{X: 2})
	mustOK(t, err)
	if hit != node {
		t.Fatalf("hit inside clip: %+v", hit)
	}
	hit, err = tree.Hit(Point{X: 1})
	mustOK(t, err)
	if hit == node {
		t.Fatal("hit outside clip reached the child")
	}
	if err := parent.held.PlaceClipped(0, Rect{}, 0, Rect{}); err != StaleReference {
		t.Fatalf("escaped placement: %v", err)
	}
	frame, err := tree.Frame()
	mustOK(t, err)
	cell, err := frame.Cell(Point{X: 2})
	mustOK(t, err)
	if cell.Glyph == "X" {
		t.Fatalf("erased glyph remained visible: %+v", cell)
	}
}

func TestEditorCaretAndViewportGeometry(t *testing.T) {
	editor, err := NewEditor("A界B", EditorOptions{})
	mustOK(t, err)
	defer editor.Close()
	layout, err := LayoutText("A界B", TextLayoutOptions{})
	mustOK(t, err)
	defer layout.Close()
	cell := Point{X: 2}
	expectedCaret, err := layout.Hit(cell)
	mustOK(t, err)
	mustOK(t, editor.SelectAt(layout, cell, false))
	selection, err := editor.Selection()
	mustOK(t, err)
	if selection != (Selection{Anchor: expectedCaret, Active: expectedCaret}) {
		t.Fatalf("layout hit selected %+v", selection)
	}
	expectedPosition, err := layout.CaretPosition(expectedCaret)
	mustOK(t, err)
	position, err := editor.CaretPosition(layout)
	mustOK(t, err)
	if position != expectedPosition {
		t.Fatalf("visual caret position: %+v", position)
	}
	mustOK(t, editor.ReplaceSelection("x"))
	if err := editor.SelectAt(layout, cell, false); err != StaleReference {
		t.Fatalf("stale layout returned %v", err)
	}

	viewport, child := Size{Columns: 3, Rows: 3}, Size{Columns: 8, Rows: 7}
	scroll, err := ResolveScroll(ViewportOptions{
		ScrollColumns: true, Requested: Size{Columns: 10, Rows: 10},
	}, viewport, child)
	mustOK(t, err)
	if scroll != (Size{Columns: 5}) {
		t.Fatalf("effective scroll: %+v", scroll)
	}
	rect, err := AnchorRect(viewport, child, BoxCenter, BoxEnd, Point{X: 1, Y: -1})
	mustOK(t, err)
	if rect != (Rect{Origin: Point{X: -2, Y: -5}, Size: child}) {
		t.Fatalf("anchored child: %+v", rect)
	}
}
