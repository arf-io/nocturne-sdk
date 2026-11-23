#include <nocturne/editor.h>

#include "allocator.h"
#include "editor_internal.h"
#include "text_internal.h"

#include <string.h>

typedef struct NtEditorEdit {
    size_t offset, removed, inserted;
    NtEditorSelection before, after;
} NtEditorEdit;
typedef NtEditorEdit *NtEditorEditPointer;

typedef struct NtEditJournal {
    NtEditorEdit **entries;
    size_t limit, first, count, cursor;
} NtEditJournal;

typedef struct NtEditorHistoryEntry {
    struct NtEditorHistoryEntry *older, *newer;
    size_t count;
} NtEditorHistoryEntry;

typedef struct NtEditorHistory {
    NtEditorHistoryEntry *newest, *oldest, *current;
    size_t limit, count;
    NtByteView draft;
    NtEditorSelection draft_selection;
} NtEditorHistory;

struct NtEditor {
    NtAllocator allocator;
    NtText *text;
    NtEditorOptions options;
    NtEditorSelection selection;
    uint64_t change_serial;
    NtEditJournal journal;
    NtEditorHistory history;
};

NT_DECLARE_ALIGNMENT(NtEditor);
NT_DECLARE_ALIGNMENT(NtEditorEdit);
NT_DECLARE_ALIGNMENT(NtEditorEditPointer);
NT_DECLARE_ALIGNMENT(NtEditorHistoryEntry);

static size_t journal_slot(const NtEditJournal *journal, size_t index)
{
    const size_t first_run = journal->limit - journal->first;
    return index < first_run ? journal->first + index : index - first_run;
}

static void release_edit(NtEditor *editor, NtEditorEdit *edit)
{
    nt_release(&editor->allocator, edit, sizeof(*edit) + edit->removed + edit->inserted,
               NT_ALIGNMENT(NtEditorEdit));
}

static void finish_history_browse(NtEditor *editor)
{
    NtEditorHistory *history = &editor->history;
    nt_release(&editor->allocator, (void *)history->draft.data, history->draft.count, 1);
    history->draft = (NtByteView){0};
    history->current = NULL;
}

static void discard_oldest_history(NtEditor *editor)
{
    NtEditorHistory *history = &editor->history;
    NtEditorHistoryEntry *entry = history->oldest;
    history->oldest = entry->newer;
    if (history->oldest) history->oldest->older = NULL;
    else history->newest = NULL;
    --history->count;
    nt_release(&editor->allocator, entry, sizeof(*entry) + entry->count, NT_ALIGNMENT(NtEditorHistoryEntry));
}

static NtResult validate_lines(NtEditorLineMode mode, NtByteView bytes)
{
    if (mode == NT_EDITOR_MULTILINE) return NT_RESULT_OK;
    for (size_t offset = 0; offset < bytes.count;) {
        uint32_t scalar;
        const NtResult result = nt_utf8_decode(bytes, &offset, &scalar);
        if (result != NT_RESULT_OK) return result;
        if (nt_text_hard_break(scalar)) return NT_RESULT_INVALID_ARGUMENT;
    }
    return NT_RESULT_OK;
}

NtResult nt_editor_create(const NtAllocator *provided, NtByteView initial,
                          const NtEditorOptions *options, NtEditor **out)
{
    NtAllocator allocator;
    if (!out || !nt_allocator_get(provided, &allocator)) return NT_RESULT_INVALID_ARGUMENT;
    const NtEditorOptions selected = options ? *options : (NtEditorOptions){0};
    if (selected.lines != NT_EDITOR_MULTILINE && selected.lines != NT_EDITOR_SINGLE_LINE)
        return NT_RESULT_INVALID_ARGUMENT;
    NtResult result = validate_lines(selected.lines, initial);
    if (result != NT_RESULT_OK) return result;

    NtText *text;
    result = nt_text_create(provided, initial, &text);
    if (result != NT_RESULT_OK) return result;
    NtEditor *editor = nt_allocate(&allocator, sizeof(*editor), NT_ALIGNMENT(NtEditor));
    if (!editor) {
        nt_text_destroy(text);
        return NT_RESULT_ALLOCATION_FAILED;
    }
    *editor = (NtEditor){.allocator = allocator, .text = text, .options = selected};
    *out = editor;
    return NT_RESULT_OK;
}

void nt_editor_destroy(NtEditor *editor)
{
    if (!editor) return;
    const NtAllocator allocator = editor->allocator;
    (void)nt_editor_set_undo_limit(editor, 0);
    (void)nt_editor_set_history_limit(editor, 0);
    nt_text_destroy(editor->text);
    nt_release(&allocator, editor, sizeof(*editor), NT_ALIGNMENT(NtEditor));
}

const NtText *nt_editor_text(const NtEditor *editor)
{
    return editor ? editor->text : NULL;
}

bool nt_editor_is_read_only(const NtEditor *editor)
{
    return editor->options.read_only;
}

uint64_t nt_editor_change_serial(const NtEditor *editor)
{
    return editor->change_serial;
}

NtEditorSelection nt_editor_selection(const NtEditor *editor)
{
    return editor ? editor->selection : (NtEditorSelection){0};
}

static NtByteSpan selected_span(NtEditorSelection selection)
{
    const size_t anchor = selection.anchor.offset, active = selection.active.offset;
    return anchor <= active ? (NtByteSpan){anchor, active} : (NtByteSpan){active, anchor};
}

NtByteView nt_editor_selected_text(const NtEditor *editor)
{
    if (!editor) return (NtByteView){0};
    const NtByteSpan span = selected_span(editor->selection);
    if (span.begin == span.end) return (NtByteView){0};
    return (NtByteView){nt_text_bytes(editor->text).data + span.begin, span.end - span.begin};
}

/* The final boundary is represented by the cluster count. */
static size_t boundary_at_or_after(const NtText *text, size_t offset)
{
    size_t first = 0, end = nt_text_cluster_count(text);
    while (first < end) {
        const size_t middle = first + (end - first) / 2;
        NtTextCluster cluster;
        (void)nt_text_cluster(text, middle, &cluster);
        if (cluster.source.begin < offset) first = middle + 1;
        else end = middle;
    }
    return first;
}

static size_t boundary_offset(const NtText *text, size_t index)
{
    NtTextCluster cluster;
    return nt_text_cluster(text, index, &cluster) == NT_RESULT_OK ?
        cluster.source.begin : nt_text_bytes(text).count;
}

static NtResult validate_caret(const NtText *text, NtCaret caret)
{
    if (caret.affinity != NT_CARET_LEADING && caret.affinity != NT_CARET_TRAILING)
        return NT_RESULT_INVALID_ARGUMENT;
    if (caret.offset > nt_text_bytes(text).count) return NT_RESULT_OUT_OF_RANGE;
    return boundary_offset(text, boundary_at_or_after(text, caret.offset)) == caret.offset ?
        NT_RESULT_OK : NT_RESULT_INVALID_ARGUMENT;
}

NtResult nt_editor_set_selection(NtEditor *editor, NtEditorSelection selection)
{
    if (!editor) return NT_RESULT_INVALID_ARGUMENT;
    NtResult result = validate_caret(editor->text, selection.anchor);
    if (result == NT_RESULT_OK) result = validate_caret(editor->text, selection.active);
    if (result == NT_RESULT_OK) editor->selection = selection;
    return result;
}

static void place_caret(NtEditor *editor, NtCaret caret, bool extend)
{
    if (!extend) editor->selection.anchor = caret;
    editor->selection.active = caret;
}

static bool line_break_at(const NtText *text, size_t index)
{
    size_t offset = boundary_offset(text, index);
    uint32_t scalar;
    return nt_utf8_decode(nt_text_bytes(text), &offset, &scalar) == NT_RESULT_OK &&
           nt_text_hard_break(scalar);
}

NtResult nt_editor_move(NtEditor *editor, NtEditorMove move, bool extend)
{
    if (!editor || move < NT_EDITOR_PREVIOUS_CLUSTER || move > NT_EDITOR_DOCUMENT_END)
        return NT_RESULT_INVALID_ARGUMENT;
    const NtEditorSelection selection = editor->selection;
    const bool previous = move == NT_EDITOR_PREVIOUS_CLUSTER;
    const bool next = move == NT_EDITOR_NEXT_CLUSTER;
    if (!extend && (previous || next) && selection.anchor.offset != selection.active.offset) {
        const bool active_is_earlier = selection.active.offset < selection.anchor.offset;
        place_caret(editor, previous == active_is_earlier ? selection.active : selection.anchor, false);
        return NT_RESULT_OK;
    }

    const NtText *text = editor->text;
    const size_t count = nt_text_cluster_count(text);
    const size_t current = boundary_at_or_after(text, selection.active.offset);
    size_t index = current;
    switch (move) {
        case NT_EDITOR_PREVIOUS_CLUSTER:
            if (index > 0) --index;
            break;
        case NT_EDITOR_NEXT_CLUSTER:
            if (index < count) ++index;
            break;
        case NT_EDITOR_LINE_START:
            while (index > 0 && !line_break_at(text, index - 1)) --index;
            break;
        case NT_EDITOR_LINE_END:
            while (index < count && !line_break_at(text, index)) ++index;
            break;
        case NT_EDITOR_DOCUMENT_START: index = 0; break;
        case NT_EDITOR_DOCUMENT_END: index = count; break;
    }
    const bool leading = previous || move == NT_EDITOR_LINE_START || move == NT_EDITOR_DOCUMENT_START;
    const NtCaret caret = (previous || next) && index == current ? selection.active :
        (NtCaret){boundary_offset(text, index), leading ? NT_CARET_LEADING : NT_CARET_TRAILING};
    place_caret(editor, caret, extend);
    return NT_RESULT_OK;
}

static NtResult check_layout(const NtEditor *editor, const NtTextLayout *layout)
{
    if (!editor || !layout) return NT_RESULT_INVALID_ARGUMENT;
    const NtByteView document = nt_text_bytes(editor->text), source = nt_text_layout_source(layout);
    if (document.count != source.count ||
        (document.count && memcmp(document.data, source.data, document.count) != 0))
        return NT_RESULT_STALE_REFERENCE;
    return NT_RESULT_OK;
}

NtResult nt_editor_select_at(NtEditor *editor, const NtTextLayout *layout, NtCellPoint cell,
                            bool extend)
{
    NtResult result = check_layout(editor, layout);
    if (result != NT_RESULT_OK) return result;
    NtCaret caret;
    result = nt_text_layout_hit(layout, cell, &caret);
    if (result == NT_RESULT_OK) result = validate_caret(editor->text, caret);
    if (result == NT_RESULT_OK) place_caret(editor, caret, extend);
    return result;
}

NtResult nt_editor_caret_position(const NtEditor *editor, const NtTextLayout *layout,
                                 NtCellPoint *out)
{
    if (!out) return NT_RESULT_INVALID_ARGUMENT;
    const NtResult result = check_layout(editor, layout);
    if (result != NT_RESULT_OK) return result;
    return nt_text_layout_caret_position(layout, editor->selection.active, out);
}

NtResult nt_editor_set_undo_limit(NtEditor *editor, size_t limit)
{
    if (!editor) return NT_RESULT_INVALID_ARGUMENT;
    NtEditJournal *journal = &editor->journal;
    if (journal->limit == limit) return NT_RESULT_OK;
    if (limit > SIZE_MAX / sizeof(*journal->entries)) return NT_RESULT_OUT_OF_RANGE;
    NtEditorEdit **entries = NULL;
    if (limit) {
        entries = nt_allocate(&editor->allocator, limit * sizeof(*entries), NT_ALIGNMENT(NtEditorEditPointer));
        if (!entries) return NT_RESULT_ALLOCATION_FAILED;
    }

    const size_t undo = journal->cursor < limit ? journal->cursor : limit;
    const size_t available_redo = journal->count - journal->cursor;
    const size_t redo = available_redo < limit - undo ? available_redo : limit - undo;
    const size_t begin = journal->cursor - undo, end = journal->cursor + redo;
    for (size_t i = 0; i < journal->count; ++i) {
        NtEditorEdit *edit = journal->entries[journal_slot(journal, i)];
        if (i >= begin && i < end) entries[i - begin] = edit;
        else release_edit(editor, edit);
    }
    nt_release(&editor->allocator, journal->entries, journal->limit * sizeof(*entries),
               NT_ALIGNMENT(NtEditorEditPointer));
    *journal = (NtEditJournal){.entries = entries, .limit = limit, .count = undo + redo, .cursor = undo};
    return NT_RESULT_OK;
}

NtEditorUndoState nt_editor_undo_state(const NtEditor *editor)
{
    if (!editor) return (NtEditorUndoState){0};
    const NtEditJournal *journal = &editor->journal;
    return (NtEditorUndoState){journal->cursor, journal->count - journal->cursor};
}

static void journal_append(NtEditor *editor, NtEditorEdit *edit)
{
    NtEditJournal *journal = &editor->journal;
    while (journal->count > journal->cursor)
        release_edit(editor, journal->entries[journal_slot(journal, --journal->count)]);
    if (journal->count == journal->limit) {
        release_edit(editor, journal->entries[journal->first]);
        journal->first = (journal->first + 1) % journal->limit;
        --journal->count;
    }
    journal->entries[journal_slot(journal, journal->count)] = edit;
    ++journal->count;
    journal->cursor = journal->count;
}

static NtResult replace_range(NtEditor *editor, NtByteSpan span, NtByteView bytes,
                               const NtEditorSelection *selection, bool *changed)
{
    const NtByteView document = nt_text_bytes(editor->text);
    const size_t removed = span.end - span.begin, retained = document.count - removed;
    if (bytes.count > SIZE_MAX - retained) return NT_RESULT_OUT_OF_RANGE;
    if (bytes.count && !bytes.data) return NT_RESULT_INVALID_ARGUMENT;
    NtResult result = validate_lines(editor->options.lines, bytes);
    if (result != NT_RESULT_OK) return result;

    const bool replaces_bytes = removed != bytes.count ||
        (removed && memcmp(document.data + span.begin, bytes.data, removed) != 0);
    NtEditorEdit *edit = NULL;
    if (replaces_bytes && editor->journal.limit) {
        if (editor->options.lines == NT_EDITOR_MULTILINE) {
            result = nt_utf8_validate(bytes, NULL);
            if (result != NT_RESULT_OK) return result;
        }
        if (removed > SIZE_MAX - sizeof(*edit) || bytes.count > SIZE_MAX - sizeof(*edit) - removed)
            return NT_RESULT_OUT_OF_RANGE;
        edit = nt_allocate(&editor->allocator, sizeof(*edit) + removed + bytes.count, NT_ALIGNMENT(NtEditorEdit));
        if (!edit) return NT_RESULT_ALLOCATION_FAILED;
        *edit = (NtEditorEdit){.offset = span.begin, .removed = removed, .inserted = bytes.count,
                               .before = editor->selection};
        char *saved = (char *)(edit + 1);
        if (removed) memcpy(saved, document.data + span.begin, removed);
        if (bytes.count) memcpy(saved + removed, bytes.data, bytes.count);
    }
    if (replaces_bytes) {
        result = nt_text_replace(editor->text, span, bytes);
        if (result != NT_RESULT_OK) {
            if (edit) release_edit(editor, edit);
            return result;
        }
        ++editor->change_serial;
    }
    if (selection) editor->selection = *selection;
    else if (removed || bytes.count) {
        const size_t after = span.begin + bytes.count;
        const size_t offset = boundary_offset(editor->text, boundary_at_or_after(editor->text, after));
        place_caret(editor, (NtCaret){offset, offset ? NT_CARET_TRAILING : NT_CARET_LEADING}, false);
    }
    if (edit) {
        edit->after = editor->selection;
        journal_append(editor, edit);
    }
    if (changed) *changed = replaces_bytes;
    return NT_RESULT_OK;
}

static NtResult edit_document(NtEditor *editor, NtByteSpan span, NtByteView bytes)
{
    bool changed;
    const NtResult result = replace_range(editor, span, bytes, NULL, &changed);
    if (result == NT_RESULT_OK && changed) finish_history_browse(editor);
    return result;
}

NtResult nt_editor_replace_selection(NtEditor *editor, NtByteView replacement)
{
    if (!editor) return NT_RESULT_INVALID_ARGUMENT;
    if (editor->options.read_only) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    return edit_document(editor, selected_span(editor->selection), replacement);
}

NtResult nt_editor_delete(NtEditor *editor, bool backward)
{
    if (!editor) return NT_RESULT_INVALID_ARGUMENT;
    if (editor->options.read_only) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    NtByteSpan span = selected_span(editor->selection);
    if (span.begin == span.end) {
        const size_t index = boundary_at_or_after(editor->text, span.begin);
        if (backward && index > 0) span.begin = boundary_offset(editor->text, index - 1);
        if (!backward && index < nt_text_cluster_count(editor->text))
            span.end = boundary_offset(editor->text, index + 1);
    }
    return edit_document(editor, span, (NtByteView){0});
}

static NtResult replay_edit(NtEditor *editor, bool undo, bool *changed)
{
    if (!editor) return NT_RESULT_INVALID_ARGUMENT;
    if (editor->options.read_only) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    NtEditJournal *journal = &editor->journal;
    if (undo ? journal->cursor == 0 : journal->cursor == journal->count) {
        if (changed) *changed = false;
        return NT_RESULT_OK;
    }

    const size_t index = undo ? journal->cursor - 1 : journal->cursor;
    const NtEditorEdit *edit = journal->entries[journal_slot(journal, index)];
    const NtByteSpan span = {edit->offset, edit->offset + (undo ? edit->inserted : edit->removed)};
    const char *saved = (const char *)(edit + 1);
    const NtByteView bytes = undo ? (NtByteView){saved, edit->removed} :
        (NtByteView){saved + edit->removed, edit->inserted};
    /* An inverse range can touch the interior of a resegmented grapheme. */
    const NtResult result = nt_text_replace(editor->text, span, bytes);
    if (result != NT_RESULT_OK) return result;
    ++editor->change_serial;
    editor->selection = undo ? edit->before : edit->after;
    journal->cursor = undo ? index : index + 1;
    finish_history_browse(editor);
    if (changed) *changed = true;
    return NT_RESULT_OK;
}

NtResult nt_editor_undo(NtEditor *editor, bool *changed)
{
    return replay_edit(editor, true, changed);
}

NtResult nt_editor_redo(NtEditor *editor, bool *changed)
{
    return replay_edit(editor, false, changed);
}

NtResult nt_editor_set_history_limit(NtEditor *editor, size_t limit)
{
    if (!editor) return NT_RESULT_INVALID_ARGUMENT;
    NtEditorHistory *history = &editor->history;
    if (history->limit == limit) return NT_RESULT_OK;
    finish_history_browse(editor);
    history->limit = limit;
    while (history->count > limit) discard_oldest_history(editor);
    return NT_RESULT_OK;
}

NtResult nt_editor_history_add(NtEditor *editor, NtByteView bytes)
{
    if (!editor) return NT_RESULT_INVALID_ARGUMENT;
    NtEditorHistory *history = &editor->history;
    if (!history->limit) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    NtEditorHistoryEntry *entry;
    if (bytes.count > SIZE_MAX - sizeof(*entry)) return NT_RESULT_OUT_OF_RANGE;
    const NtResult result = editor->options.lines == NT_EDITOR_SINGLE_LINE ?
        validate_lines(editor->options.lines, bytes) : nt_utf8_validate(bytes, NULL);
    if (result != NT_RESULT_OK) return result;
    entry = nt_allocate(&editor->allocator, sizeof(*entry) + bytes.count, NT_ALIGNMENT(NtEditorHistoryEntry));
    if (!entry) return NT_RESULT_ALLOCATION_FAILED;
    *entry = (NtEditorHistoryEntry){.count = bytes.count};
    if (bytes.count) memcpy(entry + 1, bytes.data, bytes.count);

    finish_history_browse(editor);
    if (history->count == history->limit) discard_oldest_history(editor);
    entry->older = history->newest;
    if (history->newest) history->newest->newer = entry;
    else history->oldest = entry;
    history->newest = entry;
    ++history->count;
    return NT_RESULT_OK;
}

size_t nt_editor_history_count(const NtEditor *editor)
{
    return editor ? editor->history.count : 0;
}

static NtByteView history_bytes(const NtEditorHistoryEntry *entry)
{
    return (NtByteView){entry->count ? (const char *)(entry + 1) : NULL, entry->count};
}

NtResult nt_editor_history_entry(const NtEditor *editor, size_t index, NtByteView *out)
{
    if (!editor || !out) return NT_RESULT_INVALID_ARGUMENT;
    if (index >= editor->history.count) return NT_RESULT_OUT_OF_RANGE;
    const NtEditorHistoryEntry *entry = editor->history.newest;
    for (size_t i = 0; i < index; ++i) entry = entry->older;
    *out = history_bytes(entry);
    return NT_RESULT_OK;
}

NtResult nt_editor_history_step(NtEditor *editor, bool older, bool *changed)
{
    if (!editor) return NT_RESULT_INVALID_ARGUMENT;
    if (editor->options.read_only) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    NtEditorHistory *history = &editor->history;
    const NtEditorHistoryEntry *current = history->current;
    NtEditorHistoryEntry *target = older ? history->newest : NULL;
    if (current) target = older ? current->older : current->newer;
    if (!target && (older || !current)) {
        if (changed) *changed = false;
        return NT_RESULT_OK;
    }

    const NtByteView document = nt_text_bytes(editor->text);
    const NtEditorSelection before = editor->selection;
    char *draft = NULL;
    if (!current && document.count) {
        draft = nt_allocate(&editor->allocator, document.count, 1);
        if (!draft) return NT_RESULT_ALLOCATION_FAILED;
        memcpy(draft, document.data, document.count);
    }
    const NtByteView bytes = target ? history_bytes(target) : history->draft;
    const NtCaret end = {bytes.count, bytes.count ? NT_CARET_TRAILING : NT_CARET_LEADING};
    const NtEditorSelection selection = target ? (NtEditorSelection){end, end} : history->draft_selection;
    const NtResult result = replace_range(editor, (NtByteSpan){0, document.count}, bytes, &selection, NULL);
    if (result != NT_RESULT_OK) {
        nt_release(&editor->allocator, draft, document.count, 1);
        return result;
    }
    if (!current) {
        history->draft = (NtByteView){draft, document.count};
        history->draft_selection = before;
    }
    history->current = target;
    if (!target) finish_history_browse(editor);
    if (changed) *changed = true;
    return NT_RESULT_OK;
}
