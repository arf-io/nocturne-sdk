package nocturne

import (
	"errors"
	"runtime"
	"strings"
	"testing"
)

func TestTypedChoiceAndControlIntents(t *testing.T) {
	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	options := MountOptions{Enabled: true, Focusable: true}
	root, err := tree.MountBox(Node{}, options, Box{Kind: BoxColumn})
	mustOK(t, err)
	labels := []ChoiceEntry{{Label: strings.Clone("α")}, {Label: "Unavailable", Disabled: true}, {Label: strings.Clone("β")}}
	choice, err := tree.MountChoice(root, options, ChoiceOptions{Kind: ChoiceSelect,
		Entries: labels, VisibleRows: 2, Style: &ControlStyle{Normal: testInk, Focused: testInk, Engaged: testInk}})
	mustOK(t, err)
	labels[0].Label = "changed"
	runtime.GC()
	item, err := tree.MountItem(root, options, "Confirm", false, nil)
	mustOK(t, err)
	mustOK(t, tree.Publish(Size{Columns: 20, Rows: 6}))
	frame, err := tree.Frame()
	mustOK(t, err)
	cell, err := frame.Cell(Point{X: 2, Y: 0})
	if must(t, cell, err).Glyph != "α" {
		t.Fatal("mounted choice did not retain a copy of its label")
	}
	mustOK(t, tree.SetFocus(choice))
	press := func(now uint64, key uint32) {
		t.Helper()
		_, err := tree.Dispatch(now, Input{Kind: KeyEvent, Key: KeyInput{Code: key}}, DispatchPolicy{})
		mustOK(t, err)
	}
	press(1, KeyEnter)
	state, err := tree.Choice(choice)
	if must(t, state, err) != (ChoiceState{Selected: 0, Expanded: true}) {
		t.Fatalf("expanded select = %v", state)
	}
	press(2, KeyDown)
	intent, found, err := tree.PeekIntent()
	if err != nil || !found || intent.Source != choice || intent.Kind != ControlIntentKind {
		t.Fatalf("choice intent = %v, %v, %v", intent, found, err)
	}
	event, err := DecodeControlIntent(intent)
	if must(t, event, err) != (ControlEvent{Kind: ControlChoiceChanged, Value: 2}) {
		t.Fatalf("decoded selection = %v", event)
	}
	mustOK(t, tree.PopIntent())
	if err := tree.SetChoice(choice, 1); !errors.Is(err, InvalidArgument) {
		t.Fatalf("selected disabled row: %v", err)
	}
	if err := tree.ConfigureChoice(choice, ChoiceOptions{Kind: ChoiceRadio, Entries: []ChoiceEntry{{Label: "\xff"}}}); !errors.Is(err, InvalidArgument) {
		t.Fatalf("replaced choice with malformed UTF-8: %v", err)
	}
	state, err = tree.Choice(choice)
	if must(t, state, err) != (ChoiceState{Selected: 2, Expanded: true}) {
		t.Fatalf("refused replacement changed choice: %v", state)
	}
	press(3, KeyEscape)
	mustOK(t, tree.SetChoice(choice, ChoiceNone))
	state, err = tree.Choice(choice)
	if must(t, state, err) != (ChoiceState{Selected: ChoiceNone}) {
		t.Fatalf("cleared choice = %v", state)
	}
	mustOK(t, tree.ConfigureChoice(choice, ChoiceOptions{Kind: ChoiceRadio, Entries: []ChoiceEntry{{Label: "Ready"}}}))
	state, err = tree.Choice(choice)
	if must(t, state, err) != (ChoiceState{}) {
		t.Fatalf("configured choice = %v", state)
	}
	mustOK(t, tree.SetItem(item, true))
	selected, err := tree.Item(item)
	if !must(t, selected, err) {
		t.Fatal("item did not become selected")
	}
	_, found, err = tree.PeekIntent()
	if err != nil || found {
		t.Fatalf("programmatic changes generated an intent: %v, %v", found, err)
	}
	if _, err := DecodeControlIntent(Intent{Kind: ControlIntentKind, Payload: "\x03"}); !errors.Is(err, InvalidArgument) {
		t.Fatalf("decoded malformed intent: %v", err)
	}
}

func TestTypedStockControls(t *testing.T) {
	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	visible := MountOptions{Enabled: true, Focusable: true}
	root, err := tree.MountBox(Node{}, visible, Box{Kind: BoxColumn})
	mustOK(t, err)
	style := &ControlStyle{Normal: testInk, Focused: testInk, Engaged: testInk}
	button, err := tree.MountButton(root, visible, "Open ☾", style)
	mustOK(t, err)
	checkbox, err := tree.MountCheckbox(root, visible, "Archive", false, style)
	mustOK(t, err)
	bar, err := tree.MountProgress(root, visible, Progress{Value: 5, Maximum: 10, Filled: testInk})
	mustOK(t, err)
	mustOK(t, tree.Publish(Size{Columns: 16, Rows: 4}))
	mustOK(t, tree.SetFocus(checkbox))
	_, err = tree.Dispatch(0, Input{Kind: KeyEvent, Key: KeyInput{Code: ' ', State: KeyPress}}, DispatchPolicy{})
	mustOK(t, err)
	checked, err := tree.Checkbox(checkbox)
	if !must(t, checked, err) {
		t.Fatal("a keyboard activation did not check the checkbox")
	}
	mustOK(t, tree.SetCheckbox(checkbox, false))
	checked, err = tree.Checkbox(checkbox)
	if must(t, checked, err) {
		t.Fatal("SetCheckbox did not clear the checkbox")
	}
	mustOK(t, tree.SetProgress(bar, Progress{Value: 6, Maximum: 10, Filled: testInk}))
	mustOK(t, tree.Remove(button))
	mustOK(t, tree.Remove(checkbox))
	if _, err := tree.Checkbox(checkbox); !errors.Is(err, StaleReference) {
		t.Fatalf("removed checkbox = %v", err)
	}
}

func TestTypedIndicatorsAndPressedState(t *testing.T) {
	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	focusable := MountOptions{Enabled: true, Focusable: true, HitTestable: true}
	root, err := tree.MountBox(Node{}, focusable, Box{Kind: BoxColumn})
	mustOK(t, err)
	const phase = 1<<53 + 1
	cursorOptions := CursorOptions{Style: testInk, PhaseUs: phase, Visible: true}
	cursor, err := tree.MountCursor(root, focusable, cursorOptions)
	mustOK(t, err)
	wait, err := tree.MountWait(root, focusable, WaitOptions{Style: testInk, PhaseUs: phase, ASCII: true})
	mustOK(t, err)
	initialBar := Scrollbar{Content: 10, Viewport: 3, Offset: 99, Vertical: true, Thumb: testInk}
	bar, err := tree.MountScrollbar(root, focusable, initialBar)
	mustOK(t, err)
	actual, err := tree.Scrollbar(bar)
	initialBar.Offset = 7
	if must(t, actual, err) != initialBar {
		t.Fatalf("normalized scrollbar = %+v", actual)
	}
	initialBar.Offset = 2
	mustOK(t, tree.SetScrollbar(bar, initialBar))
	progressOptions := Progress{Value: 3, Maximum: 9, Filled: testInk}
	progress, err := tree.MountProgress(root, focusable, progressOptions)
	mustOK(t, err)
	actualProgress, err := tree.Progress(progress)
	if must(t, actualProgress, err) != progressOptions {
		t.Fatalf("progress state = %+v", actualProgress)
	}
	button, err := tree.MountButton(root, focusable, "Check", nil)
	mustOK(t, err)

	frameSize := Size{Columns: 16, Rows: 16}
	glyph := func(node Node) string {
		t.Helper()
		geometry, err := tree.Geometry(node)
		mustOK(t, err)
		frame, err := tree.Frame()
		mustOK(t, err)
		cell, err := frame.Cell(geometry.Bounds.Origin)
		return must(t, cell, err).Glyph
	}
	mustOK(t, tree.Tick(phase-1, MotionFull))
	mustOK(t, tree.Publish(frameSize))
	if glyph(cursor) != "|" || glyph(wait) != "|" {
		t.Fatal("initial cursor or wait glyph is wrong")
	}
	mustOK(t, tree.Tick(phase, MotionFull))
	mustOK(t, tree.Publish(frameSize))
	if glyph(cursor) == "|" || glyph(wait) != "/" {
		t.Fatal("indicators did not advance at the exact phase")
	}
	deadline, err := tree.NextUpdate()
	if err != nil || !deadline.Scheduled || deadline.At != 2*phase {
		t.Fatalf("indicator deadline = %+v, %v", deadline, err)
	}
	mustOK(t, tree.Tick(2*phase, MotionReduced))
	mustOK(t, tree.Publish(frameSize))
	if glyph(cursor) != "|" || glyph(wait) != "|" {
		t.Fatal("reduced motion did not settle the indicators")
	}
	cursorOptions.Visible = false
	mustOK(t, tree.SetCursor(cursor, cursorOptions))
	mustOK(t, tree.SetWait(wait, WaitOptions{Style: testInk}))
	mustOK(t, tree.Publish(frameSize))
	if glyph(cursor) == "|" || glyph(wait) != "⠋" {
		t.Fatal("updated visibility and Unicode wait mode were not published")
	}

	mustOK(t, tree.SetFocus(bar))
	_, err = tree.Dispatch(2*phase+1, Input{Kind: KeyEvent, Key: KeyInput{Code: KeyDown}}, DispatchPolicy{})
	mustOK(t, err)
	actual, err = tree.Scrollbar(bar)
	if must(t, actual, err).Offset != 3 {
		t.Fatalf("keyboard scrollbar offset = %d", actual.Offset)
	}
	intent, pending, err := tree.PeekIntent()
	if err != nil || !pending || intent.Source != bar {
		t.Fatalf("scrollbar intent = %+v, %t, %v", intent, pending, err)
	}
	event, err := DecodeControlIntent(intent)
	if must(t, event, err) != (ControlEvent{Kind: ControlScrollChanged, Value: 3}) {
		t.Fatalf("scroll changed state = %+v", event)
	}
	geometry, err := tree.Geometry(button)
	mustOK(t, err)
	pointer := PointerInput{ID: 1, Button: PrimaryButton, Pressed: PrimaryButton,
		Position: geometry.Bounds.Origin, HasPosition: true}
	_, err = tree.Dispatch(2*phase+2, Input{Kind: PointerDownEvent, Pointer: pointer}, DispatchPolicy{})
	mustOK(t, err)
	pressed, err := tree.ButtonPressed(button)
	if !must(t, pressed, err) {
		t.Fatal("button press was not observed during capture")
	}
	pointer.Pressed = 0
	_, err = tree.Dispatch(2*phase+3, Input{Kind: PointerUpEvent, Pointer: pointer}, DispatchPolicy{})
	mustOK(t, err)
	pressed, err = tree.ButtonPressed(button)
	if must(t, pressed, err) {
		t.Fatal("button remained pressed after release")
	}
}

func TestTypedTreeHitAndFocusOrder(t *testing.T) {
	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	label, err := tree.MountLabel(Node{}, MountOptions{Enabled: true, Focusable: true, HitTestable: true}, "A", testInk)
	mustOK(t, err)
	mustOK(t, tree.Publish(Size{Columns: 2, Rows: 1}))
	hit, err := tree.Hit(Point{})
	if must(t, hit, err) != label {
		t.Fatal("the published label should receive the hit")
	}
	hit, err = tree.Hit(Point{X: -1})
	if !must(t, hit, err).IsZero() {
		t.Fatal("a point outside the frame must miss")
	}
	mustOK(t, tree.FocusAdvance(false))
	focus, err := tree.Focus()
	if must(t, focus, err) != label {
		t.Fatal("focus traversal did not select the label")
	}
}
