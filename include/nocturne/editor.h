#ifndef NOCTURNE_EDITOR_H
#define NOCTURNE_EDITOR_H

#include <nocturne/event.h>
#include <nocturne/text_layout.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NtEditor NtEditor;

typedef enum NtEditorLineMode { NT_EDITOR_MULTILINE, NT_EDITOR_SINGLE_LINE } NtEditorLineMode;
typedef struct NtEditorOptions { NtEditorLineMode lines; bool read_only; } NtEditorOptions;

typedef struct NtEditorSelection { NtCaret anchor, active; } NtEditorSelection;
typedef enum NtEditorMove {
    NT_EDITOR_PREVIOUS_CLUSTER, NT_EDITOR_NEXT_CLUSTER,
    NT_EDITOR_LINE_START, NT_EDITOR_LINE_END,
    NT_EDITOR_DOCUMENT_START, NT_EDITOR_DOCUMENT_END
} NtEditorMove;

/* Copy strict UTF-8 into an editor with a collapsed selection at byte zero.
 * NULL options select an editable multiline document. An editor accepts
 * the same allocator as NtText. The caller serializes access. */
NtResult nt_editor_create(const NtAllocator *allocator, NtByteView initial,
                          const NtEditorOptions *options, NtEditor **out);
void nt_editor_destroy(NtEditor *editor);

/* Borrow the read-only text object. Its byte and cluster borrows follow
 * the normal NtText lifetime. Use nt_text_layout_create for a snapshot. */
const NtText *nt_editor_text(const NtEditor *editor);
NtEditorSelection nt_editor_selection(const NtEditor *editor);
NtByteView nt_editor_selected_text(const NtEditor *editor);

/* Endpoints use logical UTF-8 byte offsets at grapheme boundaries. Motion
 * operates in logical order. A hard line break is one cluster, including
 * CRLF. Extending keeps the anchor. A previous or next move clamped to
 * the active offset preserves its affinity. */
NtResult nt_editor_set_selection(NtEditor *editor, NtEditorSelection selection);
NtResult nt_editor_move(NtEditor *editor, NtEditorMove move, bool extend);
/* Use the layout of the displayed frame. Its source must match the current
 * document bytes or the operation returns STALE_REFERENCE. */
NtResult nt_editor_select_at(NtEditor *editor, const NtTextLayout *layout, NtCellPoint cell,
                            bool extend);
NtResult nt_editor_caret_position(const NtEditor *editor, const NtTextLayout *layout,
                                 NtCellPoint *out);

/* Replace the selection with strict UTF-8, including paste. The replacement
 * may borrow from this editor. A single-line document rejects hard breaks.
 * Collapse to the first grapheme boundary at or after the inserted bytes. */
NtResult nt_editor_replace_selection(NtEditor *editor, NtByteView replacement);
/* Delete the selection, or one whole adjacent cluster when collapsed.
 * Pass true to delete backward. Read-only editors reject content edits. */
NtResult nt_editor_delete(NtEditor *editor, bool backward);

typedef struct NtEditorUndoState { size_t undo_count, redo_count; } NtEditorUndoState;

/* Undo is disabled until a positive record limit is set. Zero clears the
 * journal. Limit changes and failed edits preserve the visible document. */
NtResult nt_editor_set_undo_limit(NtEditor *editor, size_t limit);
NtEditorUndoState nt_editor_undo_state(const NtEditor *editor);
/* Restore exact bytes and selection. The optional changed output is
 * false when there is no entry in that direction. */
NtResult nt_editor_undo(NtEditor *editor, bool *changed);
NtResult nt_editor_redo(NtEditor *editor, bool *changed);

/* Submitted history is separate from the undo journal. A zero limit
 * disables history. Add copies text, and entries are indexed newest first. */
NtResult nt_editor_set_history_limit(NtEditor *editor, size_t limit);
NtResult nt_editor_history_add(NtEditor *editor, NtByteView text);
size_t nt_editor_history_count(const NtEditor *editor);
NtResult nt_editor_history_entry(const NtEditor *editor, size_t index, NtByteView *out);
/* On first backward navigation save the draft and both carets. Moving
 * forward past the newest entry restores that exact draft. */
NtResult nt_editor_history_step(NtEditor *editor, bool older, bool *changed);

typedef struct NtEditorRegisters NtEditorRegisters;
typedef struct NtEditorRegisterValue { NtByteView text; bool occupied; } NtEditorRegisterValue;

/* Caller-owned optional bank of UTF-8 slots. The bank copies writes.
 * A read borrows bytes until that slot changes or the bank is destroyed.
 * Byte-identical writes preserve the borrow. */
NtResult nt_editor_registers_create(const NtAllocator *allocator, size_t slots, NtEditorRegisters **out);
void nt_editor_registers_destroy(NtEditorRegisters *registers);
NtResult nt_editor_register_read(const NtEditorRegisters *registers, size_t slot, NtEditorRegisterValue *out);
NtResult nt_editor_register_write(NtEditorRegisters *registers, size_t slot, NtByteView text);

typedef enum NtEditorAction {
    NT_EDITOR_ACTION_MOVE, NT_EDITOR_ACTION_DELETE_PREVIOUS, NT_EDITOR_ACTION_DELETE_NEXT,
    NT_EDITOR_ACTION_NEWLINE, NT_EDITOR_ACTION_UNDO, NT_EDITOR_ACTION_REDO,
    NT_EDITOR_ACTION_HISTORY_OLDER, NT_EDITOR_ACTION_HISTORY_NEWER,
    NT_EDITOR_ACTION_REGISTER_COPY, NT_EDITOR_ACTION_REGISTER_PASTE
} NtEditorAction;

typedef struct NtEditorBinding {
    NtKeyChord chord;
    NtEditorAction action;
    NtEditorMove move;
    size_t slot;
    bool extend, repeat;
} NtEditorBinding;

typedef struct NtEditorKeyMap {
    const NtEditorBinding *bindings;
    size_t count;
    NtEditorRegisters *registers;
} NtEditorKeyMap;

/* Handle normalized input after host shortcut reservation. TEXT and PASTE
 * insert their payload. KEY matches an exact caller binding on press and
 * on repeat when allowed. NULL map enables no key commands. */
NtResult nt_editor_input(NtEditor *editor, const NtEvent *event, const NtEditorKeyMap *map,
                          bool *handled);

#ifdef __cplusplus
}
#endif
#endif
