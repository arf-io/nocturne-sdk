#include <nocturne/component.h>
#include <nocturne/editor.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "Editor history consumer line %d: %s\n", __LINE__, #condition); \
    exit(EXIT_FAILURE); \
} } while (0)
#define BYTES(s) ((NtByteView){s, sizeof(s) - 1})
#define RI_A "\xf0\x9f\x87\xa6"
#define RI_B "\xf0\x9f\x87\xa7"
#define ACCENT "e\xcc\x81"

static NtEditor *create(const NtAllocator *allocator, NtByteView text, const NtEditorOptions *options)
{
    NtEditor *editor;
    CHECK(nt_editor_create(allocator, text, options, &editor) == NT_RESULT_OK);
    return editor;
}

static void equal_bytes(NtByteView bytes, NtByteView expected)
{
    CHECK(bytes.count == expected.count);
    CHECK(!bytes.count || memcmp(bytes.data, expected.data, bytes.count) == 0);
}

static void equal_document(NtEditor *editor, NtByteView expected)
{
    equal_bytes(nt_text_bytes(nt_editor_text(editor)), expected);
}

static void equal_selection(NtEditor *editor, NtEditorSelection expected)
{
    const NtEditorSelection got = nt_editor_selection(editor);
    CHECK(got.anchor.offset == expected.anchor.offset && got.anchor.affinity == expected.anchor.affinity);
    CHECK(got.active.offset == expected.active.offset && got.active.affinity == expected.active.affinity);
}

static void journal_counts(NtEditor *editor, size_t undo, size_t redo)
{
    const NtEditorUndoState state = nt_editor_undo_state(editor);
    CHECK(state.undo_count == undo && state.redo_count == redo);
}

static void replay(NtEditor *editor, bool backward, bool expected)
{
    bool changed = !expected;
    CHECK((backward ? nt_editor_undo(editor, &changed) : nt_editor_redo(editor, &changed)) == NT_RESULT_OK);
    CHECK(changed == expected);
}

static void exact_undo_after_grapheme_join(void)
{
    NtEditor *editor = create(NULL, BYTES(RI_B "!"), NULL);
    replay(editor, true, false);
    CHECK(nt_editor_set_undo_limit(editor, 3) == NT_RESULT_OK);
    const NtEditorSelection before = {{0,NT_CARET_TRAILING},{0,NT_CARET_LEADING}};
    CHECK(nt_editor_set_selection(editor, before) == NT_RESULT_OK);
    CHECK(nt_editor_replace_selection(editor, BYTES(RI_A)) == NT_RESULT_OK);
    const NtEditorSelection after = {{8,NT_CARET_TRAILING},{8,NT_CARET_TRAILING}};
    equal_selection(editor, after);
    NtTextCluster old_mark, new_mark;
    CHECK(nt_text_cluster(nt_editor_text(editor), 1, &old_mark) == NT_RESULT_OK);
    CHECK(nt_editor_move(editor, NT_EDITOR_DOCUMENT_END, true) == NT_RESULT_OK);
    replay(editor, true, true);
    equal_selection(editor, before);
    equal_document(editor, BYTES(RI_B "!"));
    CHECK(nt_text_cluster(nt_editor_text(editor), 1, &new_mark) == NT_RESULT_OK && old_mark.id == new_mark.id);
    replay(editor, false, true);
    equal_document(editor, BYTES(RI_A RI_B "!"));
    equal_selection(editor, after);
    nt_editor_destroy(editor);

    editor = create(NULL, BYTES(RI_A "-" RI_B), NULL);
    CHECK(nt_editor_set_undo_limit(editor, 1) == NT_RESULT_OK);
    const NtEditorSelection reversed = {{5,NT_CARET_LEADING},{4,NT_CARET_TRAILING}};
    CHECK(nt_editor_set_selection(editor, reversed) == NT_RESULT_OK);
    CHECK(nt_editor_delete(editor, true) == NT_RESULT_OK);
    equal_document(editor, BYTES(RI_A RI_B));
    replay(editor, true, true);
    equal_document(editor, BYTES(RI_A "-" RI_B));
    equal_selection(editor, reversed);
    replay(editor, false, true);
    equal_document(editor, BYTES(RI_A RI_B));
    equal_selection(editor, after);
    nt_editor_destroy(editor);
    puts("PASS undo and redo across joined graphemes restore exact bytes, oriented selection and affinity");
}

static void capacity_and_branching(void)
{
    NtEditor *editor = create(NULL, (NtByteView){0}, NULL);
    CHECK(nt_editor_set_undo_limit(editor, 3) == NT_RESULT_OK);
    for (char letter = 'A'; letter <= 'D'; ++letter)
        CHECK(nt_editor_replace_selection(editor, (NtByteView){&letter,1}) == NT_RESULT_OK);
    journal_counts(editor, 3, 0);
    for (size_t i = 0; i < 3; ++i) replay(editor, true, true);
    replay(editor, true, false);
    equal_document(editor, BYTES("A"));
    CHECK(nt_editor_set_undo_limit(editor, 2) == NT_RESULT_OK);
    journal_counts(editor, 0, 2);
    replay(editor, false, true);
    equal_document(editor, BYTES("AB"));
    CHECK(nt_editor_set_undo_limit(editor, 4) == NT_RESULT_OK);
    journal_counts(editor, 1, 1);
    const NtEditorSelection tail = {{2,NT_CARET_TRAILING},{1,NT_CARET_LEADING}};
    CHECK(nt_editor_set_selection(editor, tail) == NT_RESULT_OK);
    CHECK(nt_editor_replace_selection(editor, nt_editor_selected_text(editor)) == NT_RESULT_OK);
    journal_counts(editor, 1, 1);
    CHECK(nt_editor_replace_selection(editor, BYTES("x")) == NT_RESULT_OK);
    journal_counts(editor, 2, 0);
    equal_document(editor, BYTES("ABx"));
    replay(editor, false, false);
    CHECK(nt_editor_set_undo_limit(editor, SIZE_MAX) == NT_RESULT_OUT_OF_RANGE);
    journal_counts(editor, 2, 0);
    CHECK(nt_editor_set_undo_limit(editor, 0) == NT_RESULT_OK);
    journal_counts(editor, 0, 0);
    replay(editor, true, false);
    nt_editor_destroy(editor);
    puts("PASS bounded journal wrap, resize, closest redo preservation and branch behavior");
}

static void navigate(NtEditor *editor, bool older, bool expected)
{
    bool changed = !expected;
    CHECK(nt_editor_history_step(editor, older, &changed) == NT_RESULT_OK && changed == expected);
}

static void history_round_trip_and_edits(void)
{
    NtEditor *editor = create(NULL, BYTES("draft\0" ACCENT), NULL);
    CHECK(nt_editor_history_add(editor, BYTES("command")) == NT_RESULT_UNSUPPORTED_CAPABILITY);
    navigate(editor, true, false);
    CHECK(nt_editor_set_undo_limit(editor, 10) == NT_RESULT_OK);
    CHECK(nt_editor_set_history_limit(editor, 2) == NT_RESULT_OK);
    CHECK(nt_editor_history_add(editor, BYTES("discard")) == NT_RESULT_OK);
    CHECK(nt_editor_history_add(editor, BYTES("old\nentry")) == NT_RESULT_OK);
    CHECK(nt_editor_history_add(editor, BYTES("new")) == NT_RESULT_OK);
    CHECK(nt_editor_history_count(editor) == 2);
    NtByteView entry;
    CHECK(nt_editor_history_entry(editor, 1, &entry) == NT_RESULT_OK);
    equal_bytes(entry, BYTES("old\nentry"));
    CHECK(nt_editor_history_entry(editor, 2, &entry) == NT_RESULT_OUT_OF_RANGE);
    equal_bytes(entry, BYTES("old\nentry"));

    const NtEditorSelection draft = {{9,NT_CARET_LEADING},{5,NT_CARET_TRAILING}};
    CHECK(nt_editor_set_selection(editor, draft) == NT_RESULT_OK);
    navigate(editor, false, false);
    navigate(editor, true, true);
    equal_document(editor, BYTES("new"));
    navigate(editor, true, true);
    equal_document(editor, BYTES("old\nentry"));
    navigate(editor, true, false);
    navigate(editor, false, true);
    equal_document(editor, BYTES("new"));
    navigate(editor, false, true);
    equal_document(editor, BYTES("draft\0" ACCENT));
    equal_selection(editor, draft);
    journal_counts(editor, 4, 0);
    replay(editor, true, true);
    equal_document(editor, BYTES("new"));
    replay(editor, false, true);
    equal_selection(editor, draft);

    navigate(editor, true, true);
    CHECK(nt_editor_replace_selection(editor, BYTES("!")) == NT_RESULT_OK);
    navigate(editor, false, false);
    equal_document(editor, BYTES("new!"));
    navigate(editor, true, true);
    navigate(editor, false, true);
    equal_document(editor, BYTES("new!"));
    CHECK(nt_editor_history_add(editor, entry) == NT_RESULT_OK);
    CHECK(nt_editor_history_entry(editor, 0, &entry) == NT_RESULT_OK);
    equal_bytes(entry, BYTES("old\nentry"));
    CHECK(nt_editor_set_history_limit(editor, 0) == NT_RESULT_OK);
    CHECK(nt_editor_history_count(editor) == 0);
    nt_editor_destroy(editor);

    const NtEditorOptions single = {NT_EDITOR_SINGLE_LINE, false};
    editor = create(NULL, (NtByteView){0}, &single);
    CHECK(nt_editor_set_history_limit(editor, 2) == NT_RESULT_OK);
    CHECK(nt_editor_history_add(editor, BYTES("invalid\n")) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(nt_editor_history_add(editor, (NtByteView){0}) == NT_RESULT_OK);
    navigate(editor, true, true);
    navigate(editor, false, true);
    journal_counts(editor, 0, 0);
    nt_editor_destroy(editor);
    puts("PASS submitted history preserves exact draft bytes and carets, with eviction and edit detachment");
}

typedef struct AuditAllocator { size_t calls, fail, live; } AuditAllocator;

static void *allocate(void *context, size_t bytes, size_t alignment)
{
    AuditAllocator *audit = context;
    CHECK(bytes && alignment);
    if (++audit->calls == audit->fail) return NULL;
    void *result = malloc(bytes);
    if (result) ++audit->live;
    return result;
}

static void release(void *context, void *bytes, size_t count, size_t alignment)
{
    AuditAllocator *audit = context;
    CHECK(count && alignment && audit->live);
    --audit->live;
    free(bytes);
}

static void allocations_preserve_journal_and_draft(void)
{
    AuditAllocator audit = {0};
    const NtAllocator allocator = {&audit,allocate,release};
    NtEditor *editor = create(&allocator, BYTES("draft"), NULL);
    CHECK(nt_editor_set_undo_limit(editor, 4) == NT_RESULT_OK);
    CHECK(nt_editor_set_history_limit(editor, 2) == NT_RESULT_OK);
    CHECK(nt_editor_history_add(editor, BYTES("old")) == NT_RESULT_OK);
    CHECK(nt_editor_replace_selection(editor, BYTES("x")) == NT_RESULT_OK);
    replay(editor, true, true);
    const NtByteView original = nt_text_bytes(nt_editor_text(editor));
    const size_t live = audit.live;
    for (size_t site = 1; site <= 3; ++site) {
        audit.fail = audit.calls + site;
        CHECK(nt_editor_replace_selection(editor, BYTES(ACCENT)) == NT_RESULT_ALLOCATION_FAILED);
        CHECK(nt_text_bytes(nt_editor_text(editor)).data == original.data && audit.live == live);
        journal_counts(editor, 0, 1);
    }
    for (size_t site = 1; site <= 4; ++site) {
        audit.fail = audit.calls + site;
        bool changed = true;
        CHECK(nt_editor_history_step(editor, true, &changed) == NT_RESULT_ALLOCATION_FAILED);
        CHECK(changed && nt_text_bytes(nt_editor_text(editor)).data == original.data && audit.live == live);
        journal_counts(editor, 0, 1);
    }
    audit.fail = audit.calls + 1;
    CHECK(nt_editor_set_undo_limit(editor, 2) == NT_RESULT_ALLOCATION_FAILED);
    journal_counts(editor, 0, 1);
    audit.fail = 0;
    navigate(editor, true, true);
    for (size_t site = 1; site <= 2; ++site) {
        audit.fail = audit.calls + site;
        bool changed = false;
        CHECK(nt_editor_undo(editor, &changed) == NT_RESULT_ALLOCATION_FAILED);
        CHECK(!changed);
        equal_document(editor, BYTES("old"));
        journal_counts(editor, 1, 0);
    }
    audit.fail = audit.calls + 1;
    CHECK(nt_editor_history_add(editor, BYTES("new")) == NT_RESULT_ALLOCATION_FAILED);
    CHECK(nt_editor_history_count(editor) == 1);
    audit.fail = 0;
    navigate(editor, false, true);
    equal_document(editor, BYTES("draft"));
    nt_editor_destroy(editor);

    NtEditorRegisters *registers;
    CHECK(nt_editor_registers_create(&allocator, 2, &registers) == NT_RESULT_OK);
    CHECK(nt_editor_register_write(registers, 0, BYTES(ACCENT)) == NT_RESULT_OK);
    NtEditorRegisterValue entry;
    CHECK(nt_editor_register_read(registers, 0, &entry) == NT_RESULT_OK && entry.occupied);
    audit.fail = audit.calls + 1;
    CHECK(nt_editor_register_write(registers, 0, entry.text) == NT_RESULT_OK);
    CHECK(nt_editor_register_write(registers, 0, BYTES("fail")) == NT_RESULT_ALLOCATION_FAILED);
    NtEditorRegisterValue intact;
    CHECK(nt_editor_register_read(registers, 0, &intact) == NT_RESULT_OK && intact.text.data == entry.text.data);
    equal_bytes(intact.text, BYTES(ACCENT));
    nt_editor_registers_destroy(registers);
    CHECK(audit.live == 0);
    puts("PASS allocation refusal preserves redo, draft browsing, and occupied register borrows");
}

typedef struct Consumer { NtEditor *editor; const NtEditorKeyMap *map; } Consumer;

static NtResult measure(const void *state, NtMeasureContext *context, NtConstraints limits, NtCellSize *size)
{
    (void)state;
    (void)context;
    *size = (NtCellSize){limits.columns.min,limits.rows.min};
    return NT_RESULT_OK;
}

static NtResult layout(const void *state, NtLayoutContext *context, NtCellSize size)
{
    (void)state;
    (void)context;
    (void)size;
    return NT_RESULT_OK;
}

static NtResult paint(const void *state, NtPaintContext *context, NtCellSize size)
{
    (void)state;
    (void)context;
    (void)size;
    return NT_RESULT_OK;
}

static NtResult event(void *state, NtEventContext *context, const NtEvent *input, NtEventReply *reply)
{
    Consumer *consumer = state;
    CHECK(nt_event_phase(context) == NT_EVENT_TARGET);
    return nt_editor_input(consumer->editor, input, consumer->map, &reply->consume);
}

static void key(NtEditor *editor, const NtEditorKeyMap *map, NtKeyInput input, bool expected)
{
    const NtEvent event = {.kind=NT_EVENT_KEY,.data.key=input};
    bool handled = !expected;
    CHECK(nt_editor_input(editor, &event, map, &handled) == NT_RESULT_OK && handled == expected);
}

static void opt_in_key_map_and_registers(void)
{
    Consumer consumer = {create(NULL, BYTES("ab"), NULL),NULL};
    NtTree *tree;
    CHECK(nt_tree_create(NULL, &tree) == NT_RESULT_OK);
    const NtComponentOps ops = {measure,layout,NULL,paint,event,NULL};
    NtNodeId root;
    CHECK(nt_tree_mount(tree, (NtNodeId){0}, &ops, &consumer, (NtMountOptions){true,true,true}, &root)
          == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree, (NtCellSize){5,2}) == NT_RESULT_OK);
    CHECK(nt_tree_set_focus(tree, root) == NT_RESULT_OK);

    NtDispatchResult dispatch;
    const NtEvent printable = {.kind=NT_EVENT_KEY,.data.key={'x',0,NT_KEY_PRESS}};
    CHECK(nt_tree_dispatch(tree, &printable, BYTES("x"), NULL, &dispatch) == NT_RESULT_OK);
    CHECK(!dispatch.reserved && dispatch.text_delivered);
    equal_document(consumer.editor, BYTES("xab"));

    NtEditorRegisters *registers;
    CHECK(nt_editor_registers_create(NULL, 2, &registers) == NT_RESULT_OK);
    const NtEditorBinding bindings[] = {
        {.chord={'x',0},.action=NT_EDITOR_ACTION_DELETE_NEXT},
        {.chord={'c',NT_MOD_CONTROL},.action=NT_EDITOR_ACTION_REGISTER_COPY,.slot=0},
        {.chord={'v',NT_MOD_CONTROL},.action=NT_EDITOR_ACTION_REGISTER_PASTE,.slot=0,.repeat=true},
        {.chord={'p',NT_MOD_CONTROL},.action=NT_EDITOR_ACTION_REGISTER_PASTE,.slot=1},
        {.chord={NT_KEY_ENTER,0},.action=NT_EDITOR_ACTION_NEWLINE},
        {.chord={NT_KEY_LEFT,NT_MOD_SHIFT},.action=NT_EDITOR_ACTION_MOVE,
         .move=NT_EDITOR_PREVIOUS_CLUSTER,.extend=true},
        {.chord={'z',NT_MOD_CONTROL},.action=NT_EDITOR_ACTION_UNDO},
        {.chord={'y',NT_MOD_CONTROL},.action=NT_EDITOR_ACTION_REDO},
        {.chord={NT_KEY_UP,0},.action=NT_EDITOR_ACTION_HISTORY_OLDER},
        {.chord={NT_KEY_DOWN,0},.action=NT_EDITOR_ACTION_HISTORY_NEWER},
    };
    const NtEditorKeyMap map = {bindings, sizeof(bindings) / sizeof(*bindings), registers};
    consumer.map = &map;
    const NtKeyChord chord = {'x',0};
    const NtDispatchPolicy policy = {&chord,1,false};
    CHECK(nt_tree_dispatch(tree, &printable, BYTES("x"), &policy, &dispatch) == NT_RESULT_OK);
    CHECK(dispatch.reserved && !dispatch.text_delivered);
    equal_document(consumer.editor, BYTES("xab"));
    CHECK(nt_tree_dispatch(tree, &printable, BYTES("x"), NULL, &dispatch) == NT_RESULT_OK);
    CHECK(dispatch.consumed && !dispatch.text_delivered);
    equal_document(consumer.editor, BYTES("xb"));

    const NtEditorSelection selected = {{2,NT_CARET_TRAILING},{0,NT_CARET_LEADING}};
    CHECK(nt_editor_set_selection(consumer.editor, selected) == NT_RESULT_OK);
    const NtEvent copy = {.kind=NT_EVENT_KEY,.data.key={'c',NT_MOD_CONTROL,NT_KEY_PRESS}};
    bool handled;
    CHECK(nt_editor_input(consumer.editor, &copy, &map, &handled) == NT_RESULT_OK && handled);
    NtEditorRegisterValue value;
    CHECK(nt_editor_register_read(registers, 0, &value) == NT_RESULT_OK && value.occupied);
    equal_bytes(value.text, BYTES("xb"));
    equal_selection(consumer.editor, selected);
    CHECK(nt_editor_move(consumer.editor, NT_EDITOR_DOCUMENT_END, false) == NT_RESULT_OK);
    NtEvent paste = {.kind=NT_EVENT_KEY,.data.key={'v',NT_MOD_CONTROL,NT_KEY_REPEAT}};
    CHECK(nt_editor_input(consumer.editor, &paste, &map, &handled) == NT_RESULT_OK && handled);
    equal_document(consumer.editor, BYTES("xbxb"));
    paste.data.key.state = NT_KEY_RELEASE;
    CHECK(nt_editor_input(consumer.editor, &paste, &map, &handled) == NT_RESULT_OK && !handled);
    paste.data.key.state = NT_KEY_PRESS;
    paste.data.key.modifiers |= NT_MOD_ALT;
    CHECK(nt_editor_input(consumer.editor, &paste, &map, &handled) == NT_RESULT_OK && !handled);
    const NtEvent payload = {.kind=NT_EVENT_PASTE,.data.text=BYTES("\0" ACCENT)};
    CHECK(nt_editor_input(consumer.editor, &payload, NULL, &handled) == NT_RESULT_OK && handled);
    equal_document(consumer.editor, BYTES("xbxb\0" ACCENT));
    const NtEvent enter = {.kind=NT_EVENT_KEY,.data.key={NT_KEY_ENTER,0,NT_KEY_PRESS}};
    CHECK(nt_editor_input(consumer.editor, &enter, NULL, &handled) == NT_RESULT_OK && !handled);
    CHECK(nt_editor_input(consumer.editor, &enter, &map, &handled) == NT_RESULT_OK && handled);
    equal_document(consumer.editor, BYTES("xbxb\0" ACCENT "\n"));

    CHECK(nt_editor_set_undo_limit(consumer.editor, 4) == NT_RESULT_OK);
    CHECK(nt_editor_set_history_limit(consumer.editor, 2) == NT_RESULT_OK);
    CHECK(nt_editor_history_add(consumer.editor, BYTES("recent")) == NT_RESULT_OK);
    key(consumer.editor, &map, (NtKeyInput){NT_KEY_LEFT,NT_MOD_SHIFT,NT_KEY_PRESS}, true);
    equal_bytes(nt_editor_selected_text(consumer.editor), BYTES("\n"));
    key(consumer.editor, &map, (NtKeyInput){NT_KEY_LEFT,NT_MOD_SHIFT,NT_KEY_REPEAT}, false);
    equal_bytes(nt_editor_selected_text(consumer.editor), BYTES("\n"));
    key(consumer.editor, &map, (NtKeyInput){'p',NT_MOD_CONTROL,NT_KEY_PRESS}, true);
    equal_bytes(nt_editor_selected_text(consumer.editor), BYTES("\n"));
    CHECK(nt_editor_register_write(registers, 1, (NtByteView){0}) == NT_RESULT_OK);
    key(consumer.editor, &map, (NtKeyInput){'p',NT_MOD_CONTROL,NT_KEY_PRESS}, true);
    equal_document(consumer.editor, BYTES("xbxb\0" ACCENT));
    key(consumer.editor, &map, (NtKeyInput){'z',NT_MOD_CONTROL,NT_KEY_PRESS}, true);
    equal_bytes(nt_editor_selected_text(consumer.editor), BYTES("\n"));
    key(consumer.editor, &map, (NtKeyInput){'y',NT_MOD_CONTROL,NT_KEY_PRESS}, true);
    equal_document(consumer.editor, BYTES("xbxb\0" ACCENT));
    key(consumer.editor, &map, (NtKeyInput){NT_KEY_UP,0,NT_KEY_PRESS}, true);
    equal_document(consumer.editor, BYTES("recent"));
    key(consumer.editor, &map, (NtKeyInput){NT_KEY_DOWN,0,NT_KEY_PRESS}, true);
    equal_document(consumer.editor, BYTES("xbxb\0" ACCENT));
    nt_tree_destroy(tree);
    nt_editor_registers_destroy(registers);
    nt_editor_destroy(consumer.editor);
    puts("PASS opt-in chords, host reservations and associated text, register copy and paste, repeat and release");
}

int main(void)
{
    exact_undo_after_grapheme_join();
    capacity_and_branching();
    history_round_trip_and_edits();
    allocations_preserve_journal_and_draft();
    opt_in_key_map_and_registers();
    return EXIT_SUCCESS;
}
