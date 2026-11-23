package nocturne

import (
	"errors"
	"testing"
)

func TestTypedEditorGraphemesAndUndo(t *testing.T) {
	const original = "👩‍💻Z"
	editor, err := NewEditor(original, EditorOptions{SingleLine: true})
	mustOK(t, err)
	defer editor.Close()
	mustOK(t, editor.SetUndoLimit(2))
	mustOK(t, editor.Move(NextCluster, true))
	selection, err := editor.Selection()
	if must(t, selection, err).Active.Offset != uint(len("👩‍💻")) {
		t.Fatalf("selection end = %d", selection.Active.Offset)
	}
	mustOK(t, editor.ReplaceSelection("☾"))
	if err := editor.ReplaceSelection("\n"); !errors.Is(err, InvalidArgument) {
		t.Fatalf("single-line replacement = %v", err)
	}
	value, err := editor.Value()
	if must(t, value, err) != "☾Z" {
		t.Fatalf("edited value = %q", value)
	}
	changed, err := editor.Undo()
	if !must(t, changed, err) {
		t.Fatal("edit should be undoable")
	}
	value, err = editor.Value()
	if must(t, value, err) != original {
		t.Fatalf("undo value = %q", value)
	}
	changed, err = editor.Redo()
	if !must(t, changed, err) {
		t.Fatal("edit should be redoable")
	}
	editor.Close()
	if _, err := editor.Value(); !errors.Is(err, StaleReference) {
		t.Fatalf("closed editor = %v", err)
	}
}

func TestTypedEditorHistoryKeysAndRegisters(t *testing.T) {
	const draft = "draft 👩‍💻"
	editor, err := NewEditor(draft, EditorOptions{SingleLine: true})
	mustOK(t, err)
	defer editor.Close()
	selected := Selection{Anchor: Caret{}, Active: Caret{Offset: 5, Affinity: CaretTrailing}}
	mustOK(t, editor.SetSelection(selected))
	text, err := editor.SelectedText()
	if must(t, text, err) != "draft" {
		t.Fatalf("selected text = %q", text)
	}
	if err := editor.AddHistory("disabled"); !errors.Is(err, UnsupportedCapability) {
		t.Fatalf("disabled history = %v", err)
	}
	mustOK(t, editor.SetHistoryLimit(2))
	for _, value := range []string{"discarded", "", "命令"} {
		mustOK(t, editor.AddHistory(value))
	}
	count, err := editor.HistoryCount()
	if must(t, count, err) != 2 {
		t.Fatalf("history count = %d", count)
	}
	text, err = editor.HistoryEntry(0)
	if must(t, text, err) != "命令" {
		t.Fatalf("newest history = %q", text)
	}
	if err := editor.AddHistory("invalid\n"); !errors.Is(err, InvalidArgument) {
		t.Fatalf("single-line history with newline = %v", err)
	}
	for _, step := range []struct {
		older bool
		text  string
	}{{true, "命令"}, {true, ""}, {false, "命令"}, {false, draft}} {
		changed, err := editor.StepHistory(step.older)
		if !must(t, changed, err) {
			t.Fatal("history step did not change the document")
		}
		value, err := editor.Value()
		if must(t, value, err) != step.text {
			t.Fatalf("history step = %q, want %q", value, step.text)
		}
	}
	text, err = editor.Value()
	selection, selectionErr := editor.Selection()
	if must(t, text, err) != draft || must(t, selection, selectionErr) != selected {
		t.Fatalf("restored draft = %q, %+v", text, selection)
	}

	bank, err := NewEditorRegisters(2)
	mustOK(t, err)
	defer bank.Close()
	text, occupied, err := bank.Read(0)
	if err != nil || occupied || text != "" {
		t.Fatalf("empty slot = %q, %t, %v", text, occupied, err)
	}
	mustOK(t, bank.Write(0, ""))
	text, occupied, err = bank.Read(0)
	if err != nil || !occupied || text != "" {
		t.Fatalf("written empty slot = %q, %t, %v", text, occupied, err)
	}
	keymap := &EditorKeyMap{Registers: bank, Bindings: []EditorBinding{
		{Chord: KeyChord{Code: 'c', Modifiers: ModControl}, Action: EditorRegisterCopy, Slot: 1},
		{Chord: KeyChord{Code: 'v', Modifiers: ModControl}, Action: EditorRegisterPaste, Slot: 1},
		{Chord: KeyChord{Code: 'z', Modifiers: ModControl}, Action: EditorUndo},
		{Chord: KeyChord{Code: KeyF12, Modifiers: ModShift | ModControl}, Action: EditorRedo},
	}}
	copyKey := KeyInput{Code: 'c', Modifiers: ModControl}
	handled, err := editor.HandleKey(copyKey, nil)
	if must(t, handled, err) {
		t.Fatal("a key without a map was consumed")
	}
	handled, err = editor.HandleKey(KeyInput{Code: 'c', Modifiers: ModControl, State: KeyRepeat}, keymap)
	if must(t, handled, err) {
		t.Fatal("copy binding consumed a disallowed repeat")
	}
	handled, err = editor.HandleKey(copyKey, keymap)
	if !must(t, handled, err) {
		t.Fatal("copy binding was not handled")
	}
	text, occupied, err = bank.Read(1)
	if err != nil || !occupied || text != "draft" {
		t.Fatalf("copied slot = %q, %t, %v", text, occupied, err)
	}
	mustOK(t, editor.SetUndoLimit(2))
	mustOK(t, editor.Move(DocumentEnd, false))
	handled, err = editor.HandleKey(KeyInput{Code: 'v', Modifiers: ModControl}, keymap)
	if !must(t, handled, err) {
		t.Fatal("paste binding was not handled")
	}
	text, err = editor.Value()
	if must(t, text, err) != draft+"draft" {
		t.Fatalf("pasted document = %q", text)
	}
	journal, err := editor.UndoState()
	if must(t, journal, err) != (EditorUndoState{UndoCount: 1}) {
		t.Fatalf("initial journal = %+v", journal)
	}
	handled, err = editor.HandleKey(KeyInput{Code: 'z', Modifiers: ModControl}, keymap)
	if !must(t, handled, err) {
		t.Fatal("undo binding was not handled")
	}
	journal, err = editor.UndoState()
	if must(t, journal, err) != (EditorUndoState{RedoCount: 1}) {
		t.Fatalf("journal after undo = %+v", journal)
	}
	handled, err = editor.HandleKey(KeyInput{Code: KeyF12, Modifiers: ModShift | ModControl}, keymap)
	if !must(t, handled, err) {
		t.Fatal("the shifted function-key binding was not handled")
	}
	text, err = editor.Value()
	if must(t, text, err) != draft+"draft" {
		t.Fatalf("redone document = %q", text)
	}
	bankCopy := *bank
	bank.Close()
	if _, _, err := bankCopy.Read(1); !errors.Is(err, StaleReference) {
		t.Fatalf("closed register bank alias = %v", err)
	}
	if _, err := editor.HandleKey(copyKey, keymap); !errors.Is(err, StaleReference) {
		t.Fatalf("key map with closed bank = %v", err)
	}
}
