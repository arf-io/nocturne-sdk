package nocturne

import (
	"errors"
	"testing"
)

func TestEditorViewInputAndReaderScroll(t *testing.T) {
	editor, err := NewEditor("Draft", EditorOptions{SingleLine: true})
	mustOK(t, err)
	defer editor.Close()
	mustOK(t, editor.Move(DocumentEnd, false))
	registers, err := NewEditorRegisters(1)
	mustOK(t, err)
	defer registers.Close()
	mustOK(t, registers.Write(0, "☾"))

	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	mount := MountOptions{Enabled: true, Focusable: true, HitTestable: true}
	keys := &EditorKeyMap{Registers: registers, Bindings: []EditorBinding{
		{Chord: KeyChord{Code: 'v', Modifiers: ModControl}, Action: EditorRegisterPaste},
	}}
	view, err := tree.MountEditorView(Node{}, mount, EditorViewOptions{
		Editor: editor, Keys: keys, FollowCaret: true, ShowCaret: true, SubmitOnEnter: true,
	})
	mustOK(t, err)
	keys.Bindings[0].Action = EditorDeletePrevious
	if _, err := tree.EditorViewOffset(view); !errors.Is(err, WrongPhase) {
		t.Fatalf("unpublished editor offset = %v", err)
	}
	mustOK(t, tree.Publish(Size{Columns: 12, Rows: 2}))
	mustOK(t, tree.SetFocus(view))
	expectIntent := func(kind ControlEventKind) {
		t.Helper()
		intent, pending, err := tree.PeekIntent()
		if err != nil || !pending || intent.Source != view {
			t.Fatalf("editor intent = %+v, %t, %v", intent, pending, err)
		}
		value, err := DecodeControlIntent(intent)
		if must(t, value, err) != (ControlEvent{Kind: kind}) {
			t.Fatalf("editor event = %+v", value)
		}
		mustOK(t, tree.PopIntent())
	}
	_, err = tree.Dispatch(1, Input{Kind: TextEvent, Text: "水"}, DispatchPolicy{})
	mustOK(t, err)
	expectIntent(ControlEditorChanged)
	_, err = tree.Dispatch(2, Input{Kind: KeyEvent, Key: KeyInput{Code: 'v', Modifiers: ModControl}}, DispatchPolicy{})
	mustOK(t, err)
	expectIntent(ControlEditorChanged)
	value, err := editor.Value()
	if must(t, value, err) != "Draft水☾" {
		t.Fatalf("mounted document = %q", value)
	}
	_, err = tree.Dispatch(3, Input{Kind: KeyEvent, Key: KeyInput{Code: KeyEnter}}, DispatchPolicy{})
	mustOK(t, err)
	expectIntent(ControlEditorSubmitted)
	got, err := tree.Dispatch(4, Input{Kind: KeyEvent, Key: KeyInput{Code: KeyEnter, State: KeyRepeat}}, DispatchPolicy{})
	if !must(t, got, err).Consumed {
		t.Fatal("held Enter did not consume the repeat")
	}
	_, pending, err := tree.PeekIntent()
	if err != nil || pending {
		t.Fatalf("held Enter queued a repeat submission: %t, %v", pending, err)
	}
	mustOK(t, tree.Remove(view))

	const document = "one\ntwo\nthree\nfour"
	reader, err := NewEditor(document, EditorOptions{ReadOnly: true})
	mustOK(t, err)
	defer reader.Close()
	view, err = tree.MountEditorView(Node{}, mount, EditorViewOptions{Editor: reader, ScrollKeys: true})
	mustOK(t, err)
	mustOK(t, tree.Publish(Size{Columns: 8, Rows: 2}))
	mustOK(t, tree.SetFocus(view))
	mustOK(t, tree.ScrollEditorView(view, Size{Columns: 99, Rows: 99}))
	offset, err := tree.EditorViewOffset(view)
	if must(t, offset, err) != (Size{}) {
		t.Fatalf("offset before republish = %+v", offset)
	}
	mustOK(t, tree.Publish(Size{Columns: 8, Rows: 2}))
	offset, err = tree.EditorViewOffset(view)
	if must(t, offset, err) != (Size{Rows: 2}) {
		t.Fatalf("clamped reader offset = %+v", offset)
	}
	got, err = tree.Dispatch(5, Input{Kind: PasteEvent, Text: "!"}, DispatchPolicy{})
	if must(t, got, err).Consumed {
		t.Fatal("read-only view consumed a paste")
	}
	value, err = reader.Value()
	if must(t, value, err) != document {
		t.Fatalf("read-only document = %q", value)
	}
}

type editorViewDisposer struct {
	ComponentBase
	cause error
}

func (disposer editorViewDisposer) Dispose() error { return disposer.cause }

func TestEditorViewSharedLifetimeAndRemoval(t *testing.T) {
	editor, err := NewEditor("a", EditorOptions{})
	mustOK(t, err)
	defer editor.Close()
	mustOK(t, editor.Move(DocumentEnd, false))
	bank, err := NewEditorRegisters(1)
	mustOK(t, err)
	defer bank.Close()
	mustOK(t, bank.Write(0, "β"))
	keys := &EditorKeyMap{Registers: bank, Bindings: []EditorBinding{
		{Chord: KeyChord{Code: 'v'}, Action: EditorRegisterPaste},
	}}
	first, err := NewTree()
	mustOK(t, err)
	defer first.Close()
	second, err := NewTree()
	mustOK(t, err)
	defer second.Close()
	mount := MountOptions{Enabled: true, Focusable: true}
	invalid := EditorViewOptions{Editor: editor, Keys: keys, Wrap: WrapMode(99)}
	if _, err := first.MountEditorView(Node{}, mount, invalid); !errors.Is(err, InvalidArgument) {
		t.Fatalf("invalid wrap accepted: %v", err)
	}
	if editor.state.views != 0 || bank.state.views != 0 || len(first.state.editorResources) != 0 {
		t.Fatal("failed mount retained resources")
	}

	pane, err := first.MountBox(Node{}, mount, Box{Kind: BoxColumn})
	mustOK(t, err)
	view, err := first.MountEditorView(pane, mount, EditorViewOptions{Editor: editor, Keys: keys})
	mustOK(t, err)
	cause := errors.New("disposal fixture")
	_, err = first.Mount(pane, mount, editorViewDisposer{cause: cause})
	mustOK(t, err)
	mirror, err := second.MountEditorView(Node{}, mount, EditorViewOptions{Editor: editor, Keys: keys})
	mustOK(t, err)
	const columns, rows = 12, 2
	size := Size{Columns: columns, Rows: rows}
	mustOK(t, first.Publish(Size{Columns: columns, Rows: rows + 1}))
	mustOK(t, first.SetFocus(view))

	started := make(chan struct{})
	finish := make(chan struct{})
	done := make(chan error, 1)
	go func() {
		done <- first.WithNative(func(*RawTree) error {
			close(started)
			<-finish
			return nil
		})
	}()
	select {
	case <-started:
	case err := <-done:
		t.Fatalf("opening raw borrow: %v", err)
	}
	_, busyEditor := editor.Value()
	_, _, busyBank := bank.Read(0)
	busyView := second.Publish(size)
	close(finish)
	mustOK(t, <-done)
	for _, result := range []error{busyEditor, busyBank, busyView} {
		if !errors.Is(result, WrongPhase) {
			t.Fatalf("shared resource borrow = %v", result)
		}
	}

	editor.Close()
	bank.Close()
	if _, err := editor.Value(); !errors.Is(err, StaleReference) {
		t.Fatalf("closed editor handle = %v", err)
	}
	got, err := first.Dispatch(1, Input{Kind: KeyEvent, Key: KeyInput{Code: 'v'}}, DispatchPolicy{})
	if !must(t, got, err).Consumed {
		t.Fatal("mounted view lost its retained register")
	}
	mustOK(t, second.Publish(size))
	frame, err := second.Frame()
	mustOK(t, err)
	cell, err := frame.Cell(Point{X: 1})
	if must(t, cell, err).Glyph != "β" {
		t.Fatal("mounted mirror did not see the retained editor update")
	}
	if err := first.Remove(pane); !errors.Is(err, cause) {
		t.Fatalf("subtree removal with disposer failure = %v", err)
	}
	if len(first.state.editorViews) != 0 || editor.state.views != 1 || bank.state.views != 1 {
		t.Fatal("subtree removal did not release only its own leases")
	}
	rawFailure := errors.New("raw caller stopped after removal")
	err = second.WithNative(func(scope *RawTree) error {
		handle, err := scope.Handle()
		mustOK(t, err)
		node, err := scope.Node(mirror)
		mustOK(t, err)
		mustOK(t, scope.Result("nt_tree_remove", handle, node))
		return rawFailure
	})
	if !errors.Is(err, rawFailure) {
		t.Fatalf("raw removal error = %v", err)
	}
	if editor.state.native != nil || bank.state.native != nil || len(second.state.editorViews) != 0 {
		t.Fatal("last editor leases survived raw removal")
	}
}
