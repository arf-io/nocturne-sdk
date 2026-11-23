package nocturne

/*
#include <nocturne/editor.h>

static NtResult go_editor_key(NtEditor *editor, NtKeyInput key,
                              const NtEditorBinding *bindings, size_t count,
                              NtEditorRegisters *registers, bool *handled)
{
    const NtEvent event = {.kind = NT_EVENT_KEY, .data.key = key};
    const NtEditorKeyMap map = {bindings, count, registers};
    return nt_editor_input(editor, &event, &map, handled);
}
*/
import "C"

import (
	"runtime"
	"unsafe"
)

type EditorAction uint32

const (
	EditorMoveCaret      EditorAction = C.NT_EDITOR_ACTION_MOVE
	EditorDeletePrevious EditorAction = C.NT_EDITOR_ACTION_DELETE_PREVIOUS
	EditorDeleteNext     EditorAction = C.NT_EDITOR_ACTION_DELETE_NEXT
	EditorNewline        EditorAction = C.NT_EDITOR_ACTION_NEWLINE
	EditorUndo           EditorAction = C.NT_EDITOR_ACTION_UNDO
	EditorRedo           EditorAction = C.NT_EDITOR_ACTION_REDO
	EditorHistoryOlder   EditorAction = C.NT_EDITOR_ACTION_HISTORY_OLDER
	EditorHistoryNewer   EditorAction = C.NT_EDITOR_ACTION_HISTORY_NEWER
	EditorRegisterCopy   EditorAction = C.NT_EDITOR_ACTION_REGISTER_COPY
	EditorRegisterPaste  EditorAction = C.NT_EDITOR_ACTION_REGISTER_PASTE
)

type EditorBinding struct {
	Chord          KeyChord
	Action         EditorAction
	Move           EditorMove
	Slot           uint
	Extend, Repeat bool
}

type EditorKeyMap struct {
	Bindings  []EditorBinding
	Registers *EditorRegisters
}

func nativeEditorBindings(bindings []EditorBinding) []C.NtEditorBinding {
	entries := make([]C.NtEditorBinding, len(bindings))
	for index, binding := range bindings {
		entries[index] = C.NtEditorBinding{
			chord:  C.NtKeyChord{key: C.uint32_t(binding.Chord.Code), modifiers: C.uint32_t(binding.Chord.Modifiers)},
			action: C.NtEditorAction(binding.Action), move: C.NtEditorMove(binding.Move),
			slot: C.size_t(binding.Slot), extend: C.bool(binding.Extend), repeat: C.bool(binding.Repeat),
		}
	}
	return entries
}

// HandleKey admits a normalized key and runs the first exact matching
// binding. With a nil map the editor handles no keys.
func (editor *Editor) HandleKey(key KeyInput, keymap *EditorKeyMap) (bool, error) {
	state, err := editor.borrow()
	if err != nil {
		return false, err
	}
	defer state.mu.Unlock()
	var entries []C.NtEditorBinding
	var registers *C.NtEditorRegisters
	if keymap != nil {
		entries = nativeEditorBindings(keymap.Bindings)
		if keymap.Registers != nil {
			bank, err := keymap.Registers.borrow()
			if err != nil {
				return false, err
			}
			defer bank.mu.Unlock()
			registers = bank.native
		}
	}
	var handled C.bool
	native := C.NtKeyInput{key: C.uint32_t(key.Code), modifiers: C.uint32_t(key.Modifiers), state: C.NtKeyState(key.State)}
	err = check(C.go_editor_key(state.native, native, unsafe.SliceData(entries), C.size_t(len(entries)), registers, &handled))
	return bool(handled), err
}

// EditorRegisters owns a bank of UTF-8 slots. Copies share one native owner.
type EditorRegisters struct{ state *editorRegisterState }

type editorRegisterState struct {
	editorResource[*C.NtEditorRegisters]
}

func NewEditorRegisters(slots uint) (*EditorRegisters, error) {
	if err := ensureABI(); err != nil {
		return nil, err
	}
	var native *C.NtEditorRegisters
	if err := check(C.nt_editor_registers_create(nil, C.size_t(slots), &native)); err != nil {
		return nil, err
	}
	state := &editorRegisterState{editorResource[*C.NtEditorRegisters]{
		native: native, destroy: func(handle *C.NtEditorRegisters) { C.nt_editor_registers_destroy(handle) },
	}}
	runtime.SetFinalizer(state, (*editorRegisterState).close)
	return &EditorRegisters{state: state}, nil
}

func (registers *EditorRegisters) borrow() (*editorRegisterState, error) {
	if registers == nil || registers.state == nil {
		return nil, StaleReference
	}
	state := registers.state
	if err := state.lockOpen(); err != nil {
		return nil, err
	}
	return state, nil
}

// Read returns an owned copy and whether the slot has been written.
func (registers *EditorRegisters) Read(slot uint) (string, bool, error) {
	state, err := registers.borrow()
	if err != nil {
		return "", false, err
	}
	defer state.mu.Unlock()
	var value C.NtEditorRegisterValue
	if err := check(C.nt_editor_register_read(state.native, C.size_t(slot), &value)); err != nil {
		return "", false, err
	}
	content, err := copyBytes(value.text)
	return content, bool(value.occupied), err
}

func (registers *EditorRegisters) Write(slot uint, value string) error {
	state, err := registers.borrow()
	if err != nil {
		return err
	}
	defer state.mu.Unlock()
	status := C.nt_editor_register_write(state.native, C.size_t(slot), nativeBytes(value))
	runtime.KeepAlive(value)
	return check(status)
}

func (registers *EditorRegisters) Close() {
	if registers != nil && registers.state != nil {
		registers.state.close()
	}
}
