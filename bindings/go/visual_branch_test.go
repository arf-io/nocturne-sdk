package nocturne

import "testing"

func TestTypedVisualBranchScheduleAndPaint(t *testing.T) {
	options := DefaultVisualBranchOptions()
	options.StartUs, options.LifetimeUs, options.RevealUs, options.FlickerUs = 100, 120, 5, 0
	segments, err := options.Sample(160, MotionFull)
	mustOK(t, err)
	if len(segments) != 13 || segments[0].Color.Alpha != 128 || segments[0].ParentID != VisualBranchNoParent {
		t.Fatalf("full-motion segments = %+v", segments)
	}
	for i := 1; i < len(segments); i++ {
		row := segments[i]
		if int(row.ID) != i || int(row.ParentID) >= i || row.Start != segments[row.ParentID].End {
			t.Fatalf("invalid segment %d = %+v", i, row)
		}
	}
	segments, err = options.Sample(100, MotionReduced)
	mustOK(t, err)
	if len(segments) != 19 || segments[6].End != options.End || segments[0].Color.Alpha != 255 {
		t.Fatalf("reduced-motion segments = %+v", segments)
	}
	next, err := options.Deadline(103, MotionFull, 60)
	mustOK(t, err)
	if next != (Deadline{true, 105}) {
		t.Fatalf("full-motion deadline = %+v", next)
	}
	next, err = options.Deadline(103, MotionReduced, 60)
	mustOK(t, err)
	if next != (Deadline{true, 220}) {
		t.Fatalf("reduced-motion deadline = %+v", next)
	}
	options.TrunkSegments = 256
	if _, err := options.Sample(160, MotionFull); err != OutOfRange {
		t.Fatalf("oversized branch = %v", err)
	}

	options = DefaultVisualBranchOptions()
	options.Start, options.End = Point{X: -2147483648, Y: 1}, Point{X: 2147483647, Y: 1}
	options.TrunkSegments, options.BranchCount, options.JitterCells = 2, 0, 0
	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	var saved *Paint
	_, err = tree.Mount(Node{}, MountOptions{Enabled: true}, &panelPaintFixture{draw: func(paint *Paint) error {
		saved = paint
		return paint.DrawVisualBranch(options, "#", WidthPolicy{}, 0, MotionReduced)
	}})
	mustOK(t, err)
	mustOK(t, tree.Publish(Size{6, 3}))
	frame, err := tree.Frame()
	mustOK(t, err)
	for x := int32(0); x < 6; x++ {
		cell, err := frame.Cell(Point{X: x, Y: 1})
		mustOK(t, err)
		if cell.Glyph != "#" || cell.Owner == 0 {
			t.Fatalf("branch cell %d = %+v", x, cell)
		}
	}
	if err := saved.DrawVisualBranch(options, "#", WidthPolicy{}, 0, MotionReduced); err != StaleReference {
		t.Fatalf("escaped paint = %v", err)
	}
}
