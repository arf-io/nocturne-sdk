#include <nocturne/editor.h>
#include <nocturne/input.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "Editor consumer line %d: %s\n", __LINE__, #condition); \
    exit(EXIT_FAILURE); \
} } while (0)
#define BYTES(s) ((NtByteView){s, sizeof(s) - 1})
#define ACCENT "e\xcc\x81"
#define WIDE "\xe7\x95\x8c"
#define ALEF "\xd7\x90"
#define BET "\xd7\x91"
#define RI_A "\xf0\x9f\x87\xa6"
#define RI_B "\xf0\x9f\x87\xa7"
#define EMOJI "👩‍💻"

static NtEditor *create(NtByteView bytes, const NtEditorOptions *options)
{
    NtEditor *editor;
    CHECK(nt_editor_create(NULL, bytes, options, &editor) == NT_RESULT_OK);
    return editor;
}

static void equal(NtByteView actual, NtByteView expected)
{
    CHECK(actual.count == expected.count);
    CHECK(!actual.count || memcmp(actual.data, expected.data, actual.count) == 0);
}

static bool same_caret(NtCaret a, NtCaret b)
{
    return a.offset == b.offset && a.affinity == b.affinity;
}

static void selection(const NtEditor *editor, size_t anchor, size_t active)
{
    const NtEditorSelection result = nt_editor_selection(editor);
    CHECK(result.anchor.offset == anchor && result.active.offset == active);
}

static void select_range(NtEditor *editor, size_t anchor, size_t active)
{
    const NtEditorSelection range = {{anchor, NT_CARET_LEADING}, {active, NT_CARET_TRAILING}};
    CHECK(nt_editor_set_selection(editor, range) == NT_RESULT_OK);
}

static NtClusterId identity(const NtEditor *editor, size_t index)
{
    NtTextCluster item;
    CHECK(nt_text_cluster(nt_editor_text(editor), index, &item) == NT_RESULT_OK);
    return item.id;
}

static void selection_replacement_and_aliasing(void)
{
    char source[] = "A" ACCENT EMOJI "Z";
    const size_t text_end = sizeof(source) - 1, emoji_end = text_end - 1;
    NtEditor *editor = create(BYTES(source), NULL);
    source[0] = 'Q';
    equal(nt_text_bytes(nt_editor_text(editor)), BYTES("A" ACCENT EMOJI "Z"));
    selection(editor, 0, 0);

    select_range(editor, emoji_end, 1);
    equal(nt_editor_selected_text(editor), BYTES(ACCENT EMOJI));
    const NtEditorSelection before = nt_editor_selection(editor);
    NtEditorSelection invalid = before;
    invalid.active.offset = 2;
    CHECK(nt_editor_set_selection(editor, invalid) == NT_RESULT_INVALID_ARGUMENT);
    invalid.active.offset = 1;
    invalid.active.affinity = (NtCaretAffinity)7;
    CHECK(nt_editor_set_selection(editor, invalid) == NT_RESULT_INVALID_ARGUMENT);
    invalid = before;
    invalid.anchor.offset = text_end + 1;
    CHECK(nt_editor_set_selection(editor, invalid) == NT_RESULT_OUT_OF_RANGE);
    CHECK(same_caret(nt_editor_selection(editor).anchor, before.anchor));
    CHECK(same_caret(nt_editor_selection(editor).active, before.active));

    CHECK(nt_editor_move(editor, NT_EDITOR_PREVIOUS_CLUSTER, false) == NT_RESULT_OK);
    selection(editor, 1, 1);
    CHECK(same_caret(nt_editor_selection(editor).active, before.active));
    select_range(editor, emoji_end, 1);
    CHECK(nt_editor_move(editor, NT_EDITOR_NEXT_CLUSTER, false) == NT_RESULT_OK);
    selection(editor, emoji_end, emoji_end);
    CHECK(same_caret(nt_editor_selection(editor).active, before.anchor));
    CHECK(nt_editor_move(editor, NT_EDITOR_PREVIOUS_CLUSTER, true) == NT_RESULT_OK);
    selection(editor, emoji_end, 4);
    equal(nt_editor_selected_text(editor), BYTES(EMOJI));
    CHECK(nt_editor_move(editor, NT_EDITOR_PREVIOUS_CLUSTER, true) == NT_RESULT_OK);
    selection(editor, emoji_end, 1);

    const NtClusterId a = identity(editor, 0), z = identity(editor, 3);
    CHECK(nt_editor_replace_selection(editor, BYTES(WIDE)) == NT_RESULT_OK);
    equal(nt_text_bytes(nt_editor_text(editor)), BYTES("A" WIDE "Z"));
    CHECK(identity(editor, 0) == a && identity(editor, 2) == z);
    selection(editor, 4, 4);

    NtByteView text = nt_text_bytes(nt_editor_text(editor));
    CHECK(nt_editor_replace_selection(editor, (NtByteView){text.data + 4, 1}) == NT_RESULT_OK);
    equal(nt_text_bytes(nt_editor_text(editor)), BYTES("A" WIDE "ZZ"));
    selection(editor, 5, 5);
    CHECK(nt_editor_delete(editor, true) == NT_RESULT_OK);
    CHECK(identity(editor, 2) == z);
    selection(editor, 4, 4);
    select_range(editor, 1, 4);
    const NtByteView same_text = nt_editor_selected_text(editor);
    CHECK(nt_editor_replace_selection(editor, same_text) == NT_RESULT_OK);
    CHECK(nt_text_bytes(nt_editor_text(editor)).data + 1 == same_text.data);
    selection(editor, 4, 4);
    CHECK(nt_editor_delete(editor, false) == NT_RESULT_OK);
    CHECK(nt_editor_delete(editor, false) == NT_RESULT_OK);
    equal(nt_text_bytes(nt_editor_text(editor)), BYTES("A" WIDE));
    CHECK(!nt_editor_selected_text(editor).data);
    nt_editor_destroy(editor);
    puts("PASS grapheme selection, both orientations, aliased replacement, whole-cluster deletion and stable identities");
}

static void resegmentation_relocates_the_caret(void)
{
    NtEditor *editor = create(BYTES(RI_B "!"), NULL);
    const NtClusterId punctuation = identity(editor, 1);
    CHECK(nt_editor_replace_selection(editor, BYTES(RI_A)) == NT_RESULT_OK);
    equal(nt_text_bytes(nt_editor_text(editor)), BYTES(RI_A RI_B "!"));
    selection(editor, 8, 8);
    CHECK(nt_text_cluster_count(nt_editor_text(editor)) == 2 && identity(editor, 1) == punctuation);
    CHECK(nt_editor_delete(editor, true) == NT_RESULT_OK);
    equal(nt_text_bytes(nt_editor_text(editor)), BYTES("!"));
    selection(editor, 0, 0);
    nt_editor_destroy(editor);

    editor = create(BYTES(RI_A "-" RI_B), NULL);
    select_range(editor, 4, 5);
    CHECK(nt_editor_delete(editor, false) == NT_RESULT_OK);
    CHECK(nt_text_cluster_count(nt_editor_text(editor)) == 1);
    selection(editor, 8, 8);
    CHECK(nt_editor_move(editor, NT_EDITOR_PREVIOUS_CLUSTER, false) == NT_RESULT_OK);
    selection(editor, 0, 0);
    nt_editor_destroy(editor);

    editor = create(BYTES("\xcc\x81!"), NULL);
    CHECK(nt_editor_replace_selection(editor, BYTES("e")) == NT_RESULT_OK);
    equal(nt_text_bytes(nt_editor_text(editor)), BYTES(ACCENT "!"));
    selection(editor, 3, 3);
    nt_editor_destroy(editor);
    puts("PASS insertion and deletion resnap after regional-indicator and combining-mark joins");
}

static void line_navigation_and_policy(void)
{
    NtEditor *editor = create(BYTES("A\r\nBC\xe2\x80\xa8" "D\n"), NULL);
    CHECK(nt_editor_move(editor, NT_EDITOR_LINE_END, false) == NT_RESULT_OK);
    selection(editor, 1, 1);
    CHECK(nt_editor_move(editor, NT_EDITOR_NEXT_CLUSTER, false) == NT_RESULT_OK);
    selection(editor, 3, 3);
    CHECK(nt_editor_move(editor, NT_EDITOR_LINE_END, true) == NT_RESULT_OK);
    selection(editor, 3, 5);
    equal(nt_editor_selected_text(editor), BYTES("BC"));
    CHECK(nt_editor_move(editor, NT_EDITOR_NEXT_CLUSTER, false) == NT_RESULT_OK);
    CHECK(nt_editor_move(editor, NT_EDITOR_NEXT_CLUSTER, false) == NT_RESULT_OK);
    selection(editor, 8, 8);
    CHECK(nt_editor_move(editor, NT_EDITOR_LINE_END, false) == NT_RESULT_OK);
    selection(editor, 9, 9);
    CHECK(nt_editor_move(editor, NT_EDITOR_LINE_START, false) == NT_RESULT_OK);
    selection(editor, 8, 8);
    CHECK(nt_editor_move(editor, NT_EDITOR_DOCUMENT_END, false) == NT_RESULT_OK);
    CHECK(nt_editor_move(editor, NT_EDITOR_LINE_START, false) == NT_RESULT_OK);
    selection(editor, 10, 10);
    CHECK(nt_editor_move(editor, NT_EDITOR_DOCUMENT_START, true) == NT_RESULT_OK);
    selection(editor, 10, 0);
    nt_editor_destroy(editor);

    const NtEditorOptions single = {NT_EDITOR_SINGLE_LINE, false};
    static const NtByteView breaks[] = {
        {"\r\n",2}, {"\r",1}, {"\n",1}, {"\v",1}, {"\f",1},
        {"\xc2\x85",2}, {"\xe2\x80\xa8",3}, {"\xe2\x80\xa9",3}
    };
    editor = create(BYTES("old"), &single);
    select_range(editor, 3, 0);
    for (size_t i = 0; i < sizeof(breaks) / sizeof(*breaks); ++i) {
        NtEditor *out = editor;
        CHECK(nt_editor_create(NULL, breaks[i], &single, &out) == NT_RESULT_INVALID_ARGUMENT);
        CHECK(out == editor);
        CHECK(nt_editor_replace_selection(editor, breaks[i]) == NT_RESULT_INVALID_ARGUMENT);
        selection(editor, 3, 0);
    }
    CHECK(nt_editor_replace_selection(editor, BYTES("\xc0\xaf")) == NT_RESULT_INVALID_ARGUMENT);
    equal(nt_editor_selected_text(editor), BYTES("old"));
    CHECK(nt_editor_replace_selection(editor, BYTES("new\tvalue")) == NT_RESULT_OK);
    equal(nt_text_bytes(nt_editor_text(editor)), BYTES("new\tvalue"));
    nt_editor_destroy(editor);

    const NtEditorOptions readonly = {NT_EDITOR_MULTILINE, true};
    editor = create(BYTES("read\nonly"), &readonly);
    CHECK(nt_editor_move(editor, NT_EDITOR_DOCUMENT_END, true) == NT_RESULT_OK);
    equal(nt_editor_selected_text(editor), BYTES("read\nonly"));
    CHECK(nt_editor_replace_selection(editor, BYTES("change")) == NT_RESULT_UNSUPPORTED_CAPABILITY);
    CHECK(nt_editor_delete(editor, true) == NT_RESULT_UNSUPPORTED_CAPABILITY);
    selection(editor, 0, 9);
    nt_editor_destroy(editor);
    puts("PASS logical navigation, CRLF as one boundary, single-line rejection and read-only selection");
}

static NtTextLayout *layout(const NtEditor *editor, uint32_t columns)
{
    const NtTextLayoutOptions options = {
        .width = {NT_WIDTH_ONE, NT_WIDTH_TWO}, .bounds.columns = {columns, columns, true},
        .wrap = NT_WRAP_CLUSTER, .base_direction = NT_DIRECTION_LTR, .tab_stop = 4,
    };
    NtTextLayout *result;
    CHECK(nt_text_layout_create(NULL, nt_editor_text(editor), &options, &result) == NT_RESULT_OK);
    return result;
}

static void clamped_motion_keeps_active_affinity(void)
{
    NtEditor *editor = create(BYTES("ab"), NULL);
    const NtCaret start = {0, NT_CARET_TRAILING}, end = {2, NT_CARET_LEADING};
    CHECK(nt_editor_set_selection(editor, (NtEditorSelection){start,start}) == NT_RESULT_OK);
    CHECK(nt_editor_move(editor, NT_EDITOR_PREVIOUS_CLUSTER, false) == NT_RESULT_OK);
    CHECK(same_caret(nt_editor_selection(editor).anchor, start));
    CHECK(same_caret(nt_editor_selection(editor).active, start));
    CHECK(nt_editor_set_selection(editor, (NtEditorSelection){start,end}) == NT_RESULT_OK);
    CHECK(nt_editor_move(editor, NT_EDITOR_NEXT_CLUSTER, true) == NT_RESULT_OK);
    CHECK(same_caret(nt_editor_selection(editor).anchor, start));
    CHECK(same_caret(nt_editor_selection(editor).active, end));
    nt_editor_destroy(editor);
    puts("PASS clamped cluster motion preserves the active edge affinity");
}

static void bidi_hit_paste_and_resize(void)
{
    NtEditor *editor = create(BYTES("X " ALEF BET " Y"), NULL);
    NtTextLayout *wide = layout(editor, 10), *narrow = layout(editor, 4);
    CHECK(nt_text_layout_size(wide).rows == 1 && nt_text_layout_size(narrow).rows == 2);
    CHECK(nt_editor_select_at(editor, wide, (NtCellPoint){3,0}, false) == NT_RESULT_OK);
    selection(editor, 4, 4);
    CHECK(nt_editor_select_at(editor, wide, (NtCellPoint){4,0}, true) == NT_RESULT_OK);
    selection(editor, 4, 2);
    equal(nt_editor_selected_text(editor), BYTES(ALEF));
    NtCellPoint cell;
    CHECK(nt_editor_caret_position(editor, wide, &cell) == NT_RESULT_OK && cell.x == 4 && cell.y == 0);
    CHECK(nt_editor_caret_position(editor, narrow, &cell) == NT_RESULT_OK && cell.x == 4 && cell.y == 0);
    CHECK(nt_editor_select_at(editor, narrow, (NtCellPoint){1,1}, false) == NT_RESULT_OK);
    CHECK(nt_editor_caret_position(editor, narrow, &cell) == NT_RESULT_OK && cell.y == 1);
    selection(editor, 7, 7);
    select_range(editor, 4, 2);

    NtInputDecoder *decoder;
    NtInputRead input;
    CHECK(nt_input_create(NULL, NULL, &decoder) == NT_RESULT_OK);
    CHECK(nt_input_read(decoder, BYTES("\x1b[200~" ACCENT "\n\0" WIDE "\x1b[201~"), 1, &input) == NT_RESULT_OK);
    CHECK(input.status == NT_INPUT_READY && input.event.kind == NT_EVENT_PASTE);
    CHECK(nt_editor_replace_selection(editor, input.event.data.text) == NT_RESULT_OK);
    nt_input_destroy(decoder);
    equal(nt_text_bytes(nt_editor_text(editor)), BYTES("X " ACCENT "\n\0" WIDE BET " Y"));
    selection(editor, 10, 10);
    cell = (NtCellPoint){17,19};
    CHECK(nt_editor_caret_position(editor, wide, &cell) == NT_RESULT_STALE_REFERENCE);
    CHECK(cell.x == 17 && cell.y == 19);
    CHECK(nt_editor_select_at(editor, narrow, (NtCellPoint){1,1}, false) == NT_RESULT_STALE_REFERENCE);
    selection(editor, 10, 10);
    nt_text_layout_destroy(narrow);
    nt_editor_destroy(editor);
    equal(nt_text_layout_source(wide), BYTES("X " ALEF BET " Y"));
    nt_text_layout_destroy(wide);

    editor = create(BYTES("ab"), NULL);
    wide = layout(editor, 10);
    select_range(editor, 0, 1);
    CHECK(nt_editor_replace_selection(editor, BYTES("A")) == NT_RESULT_OK);
    CHECK(nt_editor_select_at(editor, wide, (NtCellPoint){0,0}, false) == NT_RESULT_STALE_REFERENCE);
    selection(editor, 1, 1);
    select_range(editor, 0, 1);
    CHECK(nt_editor_replace_selection(editor, BYTES("a")) == NT_RESULT_OK);
    CHECK(nt_editor_select_at(editor, wide, (NtCellPoint){0,0}, false) == NT_RESULT_OK);
    selection(editor, 0, 0);
    nt_text_layout_destroy(wide);
    nt_editor_destroy(editor);
    puts("PASS bidi hit, layout resize, decoded paste, and stale-layout rejection with owned snapshot lifetime");
}

typedef struct AllocationTrace { size_t calls, fail, live; } AllocationTrace;

static void *allocate(void *context, size_t bytes, size_t alignment)
{
    AllocationTrace *trace = context;
    CHECK(bytes > 0 && alignment > 0);
    if (++trace->calls == trace->fail) return NULL;
    void *memory = malloc(bytes);
    if (memory) ++trace->live;
    return memory;
}

static void release(void *context, void *memory, size_t bytes, size_t alignment)
{
    AllocationTrace *trace = context;
    CHECK(trace->live > 0 && bytes > 0 && alignment > 0);
    --trace->live;
    free(memory);
}

static void allocation_failure_and_long_document(void)
{
    AllocationTrace trace = {0};
    const NtAllocator allocator = {&trace, allocate, release};
    for (size_t site = 1; site <= 4; ++site) {
        trace = (AllocationTrace){.fail=site};
        NtEditor *out = NULL;
        CHECK(nt_editor_create(&allocator, BYTES("ABC"), NULL, &out) == NT_RESULT_ALLOCATION_FAILED);
        CHECK(!out && trace.live == 0);
    }
    trace = (AllocationTrace){0};
    NtEditor *editor = NULL;
    CHECK(nt_editor_create(&allocator, BYTES("ABC"), NULL, &editor) == NT_RESULT_OK);
    select_range(editor, 2, 1);
    const NtByteView before = nt_text_bytes(nt_editor_text(editor));
    const NtClusterId id = identity(editor, 1);
    for (size_t site = 1; site <= 2; ++site) {
        trace.fail = trace.calls + site;
        CHECK(nt_editor_replace_selection(editor, BYTES(ACCENT)) == NT_RESULT_ALLOCATION_FAILED);
        CHECK(nt_text_bytes(nt_editor_text(editor)).data == before.data && identity(editor, 1) == id);
        equal(before, BYTES("ABC"));
        selection(editor, 2, 1);
        CHECK(trace.live == 4);
    }
    trace.fail = 0;
    CHECK(nt_editor_replace_selection(editor, BYTES(ACCENT)) == NT_RESULT_OK);
    equal(nt_text_bytes(nt_editor_text(editor)), BYTES("A" ACCENT "C"));
    nt_editor_destroy(editor);
    CHECK(trace.live == 0);

    editor = create((NtByteView){0}, NULL);
    CHECK(nt_editor_delete(editor, true) == NT_RESULT_OK);
    CHECK(nt_editor_delete(editor, false) == NT_RESULT_OK);
    selection(editor, 0, 0);
    NtTextLayout *empty = layout(editor, 10);
    NtCellPoint cell;
    CHECK(nt_editor_select_at(editor, empty, (NtCellPoint){0,0}, false) == NT_RESULT_OK);
    CHECK(nt_editor_caret_position(editor, empty, &cell) == NT_RESULT_OK && !cell.x && !cell.y);
    nt_text_layout_destroy(empty);
    char *long_text = malloc(32768);
    CHECK(long_text);
    memset(long_text, 'x', 32768);
    CHECK(nt_editor_replace_selection(editor, (NtByteView){long_text,32768}) == NT_RESULT_OK);
    free(long_text);
    selection(editor, 32768, 32768);
    CHECK(nt_editor_move(editor, NT_EDITOR_LINE_START, true) == NT_RESULT_OK);
    selection(editor, 32768, 0);
    CHECK(nt_editor_selected_text(editor).count == 32768);
    CHECK(nt_editor_delete(editor, true) == NT_RESULT_OK);
    selection(editor, 0, 0);
    CHECK(nt_text_cluster_count(nt_editor_text(editor)) == 0);
    nt_editor_destroy(editor);
    puts("PASS allocation rollback and retry, empty and 32,768-byte documents");
}

int main(void)
{
    selection_replacement_and_aliasing();
    resegmentation_relocates_the_caret();
    line_navigation_and_policy();
    clamped_motion_keeps_active_affinity();
    bidi_hit_paste_and_resize();
    allocation_failure_and_long_document();
    return EXIT_SUCCESS;
}
