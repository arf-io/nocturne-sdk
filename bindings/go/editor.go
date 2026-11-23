package nocturne

/*
#include <nocturne/editor.h>
*/
import "C"

import "runtime"

type EditorOptions struct {
	SingleLine bool
	ReadOnly   bool
}

type CaretAffinity uint32

const (
	CaretLeading  CaretAffinity = C.NT_CARET_LEADING
	CaretTrailing CaretAffinity = C.NT_CARET_TRAILING
)

// Caret identifies a grapheme boundary by its UTF-8 byte offset.
type Caret struct {
	Offset   uint
	Affinity CaretAffinity
}

type Selection struct{ Anchor, Active Caret }

func (caret Caret) native() C.NtCaret {
	return C.NtCaret{offset: C.size_t(caret.Offset), affinity: C.NtCaretAffinity(caret.Affinity)}
}

func caretFromNative(caret C.NtCaret) Caret {
	return Caret{Offset: uint(caret.offset), Affinity: CaretAffinity(caret.affinity)}
}

type EditorMove uint32

const (
	PreviousCluster EditorMove = C.NT_EDITOR_PREVIOUS_CLUSTER
	NextCluster     EditorMove = C.NT_EDITOR_NEXT_CLUSTER
	LineStart       EditorMove = C.NT_EDITOR_LINE_START
	LineEnd         EditorMove = C.NT_EDITOR_LINE_END
	DocumentStart   EditorMove = C.NT_EDITOR_DOCUMENT_START
	DocumentEnd     EditorMove = C.NT_EDITOR_DOCUMENT_END
)

// Editor owns native Unicode text, selection and undo history.
// Copies refer to the same editor. Close invalidates every copy. Mounted
// views retain the native document until they are removed.
type Editor struct{ state *editorState }

type editorState struct {
	editorResource[*C.NtEditor]
}

func NewEditor(value string, options EditorOptions) (*Editor, error) {
	if err := ensureABI(); err != nil {
		return nil, err
	}
	nativeOptions := C.NtEditorOptions{lines: C.NT_EDITOR_MULTILINE, read_only: C.bool(options.ReadOnly)}
	if options.SingleLine {
		nativeOptions.lines = C.NT_EDITOR_SINGLE_LINE
	}
	var native *C.NtEditor
	status := C.nt_editor_create(nil, nativeBytes(value), &nativeOptions, &native)
	runtime.KeepAlive(value)
	if err := check(status); err != nil {
		return nil, err
	}
	state := &editorState{editorResource[*C.NtEditor]{
		native: native, destroy: func(handle *C.NtEditor) { C.nt_editor_destroy(handle) },
	}}
	runtime.SetFinalizer(state, (*editorState).close)
	return &Editor{state: state}, nil
}

func (editor *Editor) borrow() (*editorState, error) {
	if editor == nil || editor.state == nil {
		return nil, StaleReference
	}
	state := editor.state
	if err := state.lockOpen(); err != nil {
		return nil, err
	}
	return state, nil
}

func (editor *Editor) Value() (string, error) {
	state, err := editor.borrow()
	if err != nil {
		return "", err
	}
	defer state.mu.Unlock()
	return copyBytes(C.nt_text_bytes(C.nt_editor_text(state.native)))
}

func (editor *Editor) Selection() (Selection, error) {
	state, err := editor.borrow()
	if err != nil {
		return Selection{}, err
	}
	defer state.mu.Unlock()
	value := C.nt_editor_selection(state.native)
	return Selection{Anchor: caretFromNative(value.anchor), Active: caretFromNative(value.active)}, nil
}

// SelectedText returns an owned copy of the selected UTF-8 bytes.
func (editor *Editor) SelectedText() (string, error) {
	state, err := editor.borrow()
	if err != nil {
		return "", err
	}
	defer state.mu.Unlock()
	return copyBytes(C.nt_editor_selected_text(state.native))
}

func (editor *Editor) SetSelection(selection Selection) error {
	state, err := editor.borrow()
	if err != nil {
		return err
	}
	defer state.mu.Unlock()
	return check(C.nt_editor_set_selection(state.native, C.NtEditorSelection{
		anchor: selection.Anchor.native(), active: selection.Active.native(),
	}))
}

func (editor *Editor) Move(move EditorMove, extend bool) error {
	state, err := editor.borrow()
	if err != nil {
		return err
	}
	defer state.mu.Unlock()
	return check(C.nt_editor_move(state.native, C.NtEditorMove(move), C.bool(extend)))
}

// SelectAt maps a displayed cell to a caret in a layout of the editor's
// current bytes. With extend enabled only the active caret moves.
func (editor *Editor) SelectAt(layout *TextLayout, cell Point, extend bool) error {
	state, err := editor.borrow()
	if err != nil {
		return err
	}
	defer state.mu.Unlock()
	native, release, err := layout.borrow()
	if err != nil {
		return err
	}
	defer release()
	return check(C.nt_editor_select_at(state.native, native, cell.native(), C.bool(extend)))
}

// CaretPosition returns the active caret's cell in a matching layout snapshot.
func (editor *Editor) CaretPosition(layout *TextLayout) (Point, error) {
	state, err := editor.borrow()
	if err != nil {
		return Point{}, err
	}
	defer state.mu.Unlock()
	native, release, err := layout.borrow()
	if err != nil {
		return Point{}, err
	}
	defer release()
	var out C.NtCellPoint
	if err := check(C.nt_editor_caret_position(state.native, native, &out)); err != nil {
		return Point{}, err
	}
	return pointFromNative(out), nil
}

func (editor *Editor) ReplaceSelection(value string) error {
	state, err := editor.borrow()
	if err != nil {
		return err
	}
	defer state.mu.Unlock()
	status := C.nt_editor_replace_selection(state.native, nativeBytes(value))
	runtime.KeepAlive(value)
	return check(status)
}

func (editor *Editor) Delete(backward bool) error {
	state, err := editor.borrow()
	if err != nil {
		return err
	}
	defer state.mu.Unlock()
	return check(C.nt_editor_delete(state.native, C.bool(backward)))
}

func (editor *Editor) SetUndoLimit(limit uint) error {
	state, err := editor.borrow()
	if err != nil {
		return err
	}
	defer state.mu.Unlock()
	return check(C.nt_editor_set_undo_limit(state.native, C.size_t(limit)))
}

type EditorUndoState struct{ UndoCount, RedoCount uint }

func (editor *Editor) UndoState() (EditorUndoState, error) {
	state, err := editor.borrow()
	if err != nil {
		return EditorUndoState{}, err
	}
	defer state.mu.Unlock()
	value := C.nt_editor_undo_state(state.native)
	return EditorUndoState{UndoCount: uint(value.undo_count), RedoCount: uint(value.redo_count)}, nil
}

// SetHistoryLimit sets the separate submitted-history capacity. Zero disables it.
func (editor *Editor) SetHistoryLimit(limit uint) error {
	state, err := editor.borrow()
	if err != nil {
		return err
	}
	defer state.mu.Unlock()
	return check(C.nt_editor_set_history_limit(state.native, C.size_t(limit)))
}

// AddHistory copies an entry without replacing the visible document.
func (editor *Editor) AddHistory(value string) error {
	state, err := editor.borrow()
	if err != nil {
		return err
	}
	defer state.mu.Unlock()
	status := C.nt_editor_history_add(state.native, nativeBytes(value))
	runtime.KeepAlive(value)
	return check(status)
}

func (editor *Editor) HistoryCount() (uint, error) {
	state, err := editor.borrow()
	if err != nil {
		return 0, err
	}
	defer state.mu.Unlock()
	return uint(C.nt_editor_history_count(state.native)), nil
}

// HistoryEntry returns a copy of an entry, indexed newest first.
func (editor *Editor) HistoryEntry(index uint) (string, error) {
	state, err := editor.borrow()
	if err != nil {
		return "", err
	}
	defer state.mu.Unlock()
	var value C.NtByteView
	if err := check(C.nt_editor_history_entry(state.native, C.size_t(index), &value)); err != nil {
		return "", err
	}
	return copyBytes(value)
}

// StepHistory moves toward older or newer entries. Moving forward past the
// newest entry restores the draft and its selection.
func (editor *Editor) StepHistory(older bool) (bool, error) {
	state, err := editor.borrow()
	if err != nil {
		return false, err
	}
	defer state.mu.Unlock()
	var changed C.bool
	err = check(C.nt_editor_history_step(state.native, C.bool(older), &changed))
	return bool(changed), err
}

func (editor *Editor) undoOrRedo(redo bool) (bool, error) {
	state, err := editor.borrow()
	if err != nil {
		return false, err
	}
	defer state.mu.Unlock()
	var changed C.bool
	if redo {
		err = check(C.nt_editor_redo(state.native, &changed))
	} else {
		err = check(C.nt_editor_undo(state.native, &changed))
	}
	return bool(changed), err
}

func (editor *Editor) Undo() (bool, error) { return editor.undoOrRedo(false) }
func (editor *Editor) Redo() (bool, error) { return editor.undoOrRedo(true) }

func (editor *Editor) Close() {
	if editor != nil && editor.state != nil {
		editor.state.close()
	}
}
