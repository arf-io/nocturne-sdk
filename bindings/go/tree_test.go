package nocturne

import (
	"errors"
	"fmt"
	"testing"
)

const testGlyph = "👩‍💻"

var testInk = Style{Foreground: Color{Red: 230, Green: 240, Blue: 250, Alpha: 255}}

type indicator struct {
	ComponentBase
	tree       *Tree
	focusError error
	updates    int
	disposals  int
	paintLease *Paint
}

func (*indicator) Measure(*Measure, Constraints) (Size, error) {
	return Size{Columns: 2, Rows: 1}, nil
}

func (meter *indicator) Update(info UpdateInfo) (UpdateReply, error) {
	meter.updates++
	return UpdateReply{Invalidate: InvalidatePaint, Next: Deadline{
		Scheduled: meter.updates == 1, At: info.Now + 250,
	}}, nil
}

func (meter *indicator) Paint(paint *Paint, _ Size) error {
	meter.paintLease = paint
	async := make(chan error, 1)
	go func() { async <- paint.Glyph(Point{}, "?", 1, testInk) }()
	if err := <-async; !errors.Is(err, WrongPhase) {
		return fmt.Errorf("foreign goroutine paint = %v", err)
	}
	if _, err := meter.tree.Frame(); !errors.Is(err, WrongPhase) {
		return fmt.Errorf("reentrant tree frame = %v", err)
	}
	if meter.updates == 1 {
		return paint.Glyph(Point{}, testGlyph, 2, testInk)
	}
	return paint.Glyph(Point{}, "!", 1, testInk)
}

func (meter *indicator) Event(event *Event) error {
	switch event.Kind() {
	case FocusGainEvent:
		if err := event.Emit(42, "discard"); err != nil {
			return err
		}
		panic(meter.focusError)
	case KeyEvent:
		key, err := event.Key()
		if err != nil {
			return err
		}
		if key.Code == '!' {
			if err := event.Emit(43, "discard"); err != nil {
				return err
			}
			return OK
		}
	case PointerDownEvent:
		point, present, err := event.LocalPosition()
		if err != nil {
			return err
		}
		if !present || point != (Point{}) {
			return fmt.Errorf("pointer position = %+v, %v", point, present)
		}
		return event.Capture(TakeCapture)
	case PasteEvent:
		content, err := event.Text()
		if err != nil {
			return err
		}
		if err := event.Emit(41, content); err != nil {
			return err
		}
		return event.Consume()
	}
	return nil
}

func (meter *indicator) Dispose() error {
	meter.disposals++
	return nil
}

func must[T any](t *testing.T, result T, err error) T {
	t.Helper()
	if err != nil {
		t.Fatal(err)
	}
	return result
}

func mustOK(t *testing.T, err error) {
	t.Helper()
	if err != nil {
		t.Fatal(err)
	}
}

func TestCustomComponent(t *testing.T) {
	tree, err := NewTree()
	mustOK(t, err)
	t.Cleanup(func() { _ = tree.Close() })
	meter := &indicator{tree: tree, focusError: errors.New("focus handler panicked")}
	root, err := tree.MountBox(Node{}, MountOptions{Enabled: true},
		Box{Kind: BoxStack, Padding: Insets{Left: 1}})
	mustOK(t, err)
	opts := MountOptions{Enabled: true, Focusable: true, HitTestable: true}
	node, err := tree.Mount(root, opts, meter)
	mustOK(t, err)
	mustOK(t, tree.Tick(1000, MotionFull))
	deadline, err := tree.NextUpdate()
	if got := must(t, deadline, err); got != (Deadline{Scheduled: true, At: 1250}) {
		t.Fatalf("deadline = %+v", got)
	}
	size := Size{Columns: 4, Rows: 1}
	mustOK(t, tree.Publish(size))
	geometry, err := tree.Geometry(node)
	if got := must(t, geometry, err); got.Bounds != (Rect{Origin: Point{X: 1}, Size: Size{Columns: 2, Rows: 1}}) {
		t.Fatalf("geometry = %+v", got)
	}
	frame, err := tree.Frame()
	mustOK(t, err)
	cell, err := frame.Cell(Point{X: 2})
	if got := must(t, cell, err); got.Glyph != testGlyph || got.Columns != 2 || got.Lead.X != 1 {
		t.Fatalf("continuation = %+v", got)
	}
	escaped := *meter.paintLease
	if err := escaped.Glyph(Point{}, "?", 1, testInk); !errors.Is(err, StaleReference) {
		t.Fatalf("expired context = %v", err)
	}

	err = tree.SetFocus(node)
	var failure *CallError
	if !errors.As(err, &failure) || failure.Native != nil || !errors.Is(err, meter.focusError) {
		t.Fatalf("focus cause = %v", err)
	}
	focus, err := tree.Focus()
	if got := must(t, focus, err); got != node {
		t.Fatalf("focus did not commit: %+v", got)
	}
	key := Input{Kind: KeyEvent, Key: KeyInput{Code: '!', State: KeyPress}, Text: "!"}
	report, err := tree.Dispatch(1000, key, DispatchPolicy{})
	if !report.Admitted || report.Target != node || !errors.Is(err, CallbackFailed) {
		t.Fatalf("failed key = %+v, %v", report, err)
	}
	if _, found, err := tree.PeekIntent(); found || err != nil {
		t.Fatalf("failed callback published an intent: %v, %v", found, err)
	}
	report, err = tree.Dispatch(1000, key, DispatchPolicy{Reserved: []KeyChord{{Code: '!'}}})
	if got := must(t, report, err); !got.Reserved {
		t.Fatalf("reserved key = %+v", got)
	}

	pointer := PointerInput{ID: 7, Position: Point{X: 1}, HasPosition: true, Button: PrimaryButton, Pressed: PrimaryButton}
	report, err = tree.Dispatch(1000, Input{Kind: PointerUpEvent, Pointer: pointer}, DispatchPolicy{})
	if err == nil || report.Admitted {
		t.Fatalf("invalid pointer admission = %+v, %v", report, err)
	}
	_, err = tree.Dispatch(1000, Input{Kind: PointerDownEvent, Pointer: pointer}, DispatchPolicy{})
	mustOK(t, err)
	capture, err := tree.Capture(7)
	if got := must(t, capture, err); got != node {
		t.Fatalf("capture = %+v", got)
	}
	_, err = tree.Dispatch(1000, Input{Kind: PointerCancelEvent, Cancelled: 7}, DispatchPolicy{})
	mustOK(t, err)
	capture, err = tree.Capture(7)
	if got := must(t, capture, err); !got.IsZero() {
		t.Fatalf("capture after cancel = %+v", got)
	}

	paste := "x\x00" + testGlyph
	report, err = tree.Dispatch(1000, Input{Kind: PasteEvent, Text: paste}, DispatchPolicy{})
	if got := must(t, report, err); !got.Consumed {
		t.Fatalf("paste delivery = %+v", got)
	}
	intent, found, err := tree.PeekIntent()
	if err != nil || !found || intent.Kind != 41 || intent.Payload != paste || intent.Source != node {
		t.Fatalf("paste intent = %+v, %v, %v", intent, found, err)
	}
	mustOK(t, tree.PopIntent())
	mustOK(t, tree.Tick(1250, MotionFull))
	mustOK(t, tree.Publish(size))
	if _, err := frame.Cell(Point{X: 2}); !errors.Is(err, StaleReference) {
		t.Fatalf("stale frame result = %v", err)
	}
	frame, err = tree.Frame()
	mustOK(t, err)
	cell, err = frame.Cell(Point{X: 1})
	if got := must(t, cell, err); got.Glyph != "!" {
		t.Fatalf("updated glyph = %+v", got)
	}

	mustOK(t, tree.Remove(node))
	if _, err := tree.Geometry(node); !errors.Is(err, StaleReference) {
		t.Fatalf("stale node result = %v", err)
	}
	for range 512 {
		repeated, err := tree.Mount(root, opts, meter)
		mustOK(t, err)
		mustOK(t, tree.Remove(repeated))
	}
	_, err = tree.Mount(root, opts, meter)
	mustOK(t, err)
	mustOK(t, tree.Close())
	mustOK(t, tree.Close())
	if meter.disposals != 514 {
		t.Fatalf("disposals = %d", meter.disposals)
	}
}

func TestStockLabel(t *testing.T) {
	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	node, err := tree.MountLabel(Node{}, MountOptions{Enabled: true}, "🌙", testInk)
	mustOK(t, err)
	size := Size{Columns: 4, Rows: 1}
	mustOK(t, tree.Publish(size))
	frame, err := tree.Frame()
	mustOK(t, err)
	cell, err := frame.Cell(Point{X: 1})
	if got := must(t, cell, err); got.Glyph != "🌙" {
		t.Fatalf("label continuation = %+v", got)
	}
	mustOK(t, tree.SetLabel(node, "Q", testInk))
	mustOK(t, tree.Publish(size))
	frame, err = tree.Frame()
	mustOK(t, err)
	cell, err = frame.Cell(Point{})
	if got := must(t, cell, err); got.Glyph != "Q" {
		t.Fatalf("updated label = %+v", got)
	}
}

type panicOnPaste struct{ ComponentBase }

func (panicOnPaste) Event(event *Event) error {
	if event.Kind() == PasteEvent {
		if err := event.Emit(7, "discard"); err != nil {
			return err
		}
		panic(InvalidArgument)
	}
	return nil
}

func TestPanicWithNativeResult(t *testing.T) {
	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	node, err := tree.Mount(Node{}, MountOptions{Enabled: true, Focusable: true}, panicOnPaste{})
	mustOK(t, err)
	mustOK(t, tree.Publish(Size{Columns: 2, Rows: 1}))
	mustOK(t, tree.SetFocus(node))
	delivery, err := tree.Dispatch(1, Input{Kind: PasteEvent, Text: "x"}, DispatchPolicy{})
	var failure *CallError
	var panicCause *PanicError
	if delivery.Target != node || !errors.As(err, &failure) || failure.Native != CallbackFailed ||
		!errors.As(err, &panicCause) || panicCause.Value != InvalidArgument {
		t.Fatalf("paste panic = %+v, %v", delivery, err)
	}
	if _, found, err := tree.PeekIntent(); found || err != nil {
		t.Fatalf("panicking callback published an intent: %v, %v", found, err)
	}
}
