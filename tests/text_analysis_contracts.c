#include <nocturne/text_analysis.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define BYTES(s) ((NtByteView){s, sizeof(s) - 1})
#define SPAN(a, b) ((NtByteSpan){a, b})
#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return false; \
    } \
} while (0)

#define ALEF "\xd7\x90"
#define BET "\xd7\x91"
#define RLE "\xe2\x80\xab"
#define PDF "\xe2\x80\xac"
#define HIRA "\xe3\x81\x82"
#define KANA "\xe3\x82\xab"
#define PROLONG "\xe3\x83\xbc"
#define ACCENT "\xcc\x81"

static bool line_breaks(void)
{
    NtLineBreak rows[4] = {{42, true}};
    size_t count = 70;
    const NtByteView text = BYTES("A B\r\nC\xe2\x80\xa8" "D");
    CHECK(nt_line_breaks(NULL, text, NULL, 0, &count) == NT_RESULT_OK && count == 4);
    CHECK(nt_line_breaks(NULL, text, rows, 3, &count) == NT_RESULT_INSUFFICIENT_BUFFER);
    CHECK(count == 4 && rows[0].offset == 42 && rows[0].mandatory);
    CHECK(nt_line_breaks(NULL, text, rows, ARRAY_COUNT(rows), &count) == NT_RESULT_OK);
    CHECK(count == 4 && rows[0].offset == 2 && !rows[0].mandatory);
    CHECK(rows[1].offset == 5 && rows[1].mandatory);
    CHECK(rows[2].offset == 9 && rows[2].mandatory);
    CHECK(rows[3].offset == 10 && rows[3].mandatory);

    NtByteView empty = {0};
    CHECK(nt_line_breaks(NULL, empty, rows, ARRAY_COUNT(rows), &count) == NT_RESULT_OK);
    CHECK(count == 1 && rows[0].offset == 0 && rows[0].mandatory);
    CHECK(nt_line_breaks(NULL, BYTES("\0"), rows, ARRAY_COUNT(rows), &count) == NT_RESULT_OK);
    CHECK(count == 1 && rows[0].offset == 1 && rows[0].mandatory);
    CHECK(nt_line_breaks(NULL, BYTES("\xff"), rows, ARRAY_COUNT(rows), &count) ==
          NT_RESULT_INVALID_ARGUMENT);
    CHECK(count == 1 && rows[0].offset == 1);

    /* UAX #14 can permit a break inside an extended grapheme. Terminal
     * wrapping must intersect break opportunities with the grapheme spans. */
    const NtByteView space_mark = BYTES(" " ACCENT);
    CHECK(nt_line_breaks(NULL, space_mark, rows, ARRAY_COUNT(rows), &count) == NT_RESULT_OK);
    CHECK(count == 2 && rows[0].offset == 1 && !rows[0].mandatory);
    NtGraphemeIterator cursor;
    NtByteSpan cluster;
    bool available;
    CHECK(nt_grapheme_iterator_init(&cursor, space_mark) == NT_RESULT_OK);
    CHECK(nt_grapheme_iterator_next(&cursor, &cluster, &available) == NT_RESULT_OK);
    CHECK(available && cluster.begin == 0 && cluster.end == space_mark.count);
    return true;
}

static bool bidi_lines(void)
{
    NtBidi *bidi;
    char source[] = "a" RLE ALEF " " BET PDF "b";
    CHECK(nt_bidi_create(NULL, BYTES(source), NT_DIRECTION_AUTO, &bidi) == NT_RESULT_OK);
    memset(source, 0xff, sizeof(source));
    CHECK(nt_bidi_count(bidi) == 7);

    NtBidiScalar scalar;
    CHECK(nt_bidi_scalar(bidi, 1, &scalar) == NT_RESULT_OK);
    CHECK(scalar.level == NT_BIDI_REMOVED_LEVEL && scalar.scalar == 0x202b);
    CHECK(nt_bidi_scalar(bidi, 3, &scalar) == NT_RESULT_OK);
    CHECK(scalar.level == 1 && scalar.paragraph_level == 0 && scalar.scalar == ' ');

    NtBidiVisual visual[8] = {{77, {77, 88}, '@', 42}};
    size_t count = 55;
    CHECK(nt_bidi_reorder_line(bidi, SPAN(4, 7), NULL, 0, &count) == NT_RESULT_OK);
    CHECK(count == 2);
    CHECK(nt_bidi_reorder_line(bidi, SPAN(5, 7), visual, ARRAY_COUNT(visual), &count) ==
          NT_RESULT_INVALID_ARGUMENT);
    CHECK(count == 2 && visual[0].logical_index == 77);
    CHECK(nt_bidi_reorder_line(bidi, SPAN(4, 7), visual, 1, &count) ==
          NT_RESULT_INSUFFICIENT_BUFFER);
    CHECK(count == 2 && visual[0].logical_index == 77);
    CHECK(nt_bidi_reorder_line(bidi, SPAN(4, 7), visual, ARRAY_COUNT(visual), &count) == NT_RESULT_OK);
    CHECK(count == 2 && visual[0].logical_index == 2 && visual[0].level == 1);
    CHECK(visual[0].source.begin == 4 && visual[0].source.end == 6 && visual[0].display_scalar == 0x05d0);
    CHECK(visual[1].logical_index == 3 && visual[1].level == 0 && visual[1].display_scalar == ' ');
    CHECK(nt_bidi_reorder_line(bidi, SPAN(1, 4), visual, ARRAY_COUNT(visual), &count) == NT_RESULT_OK);
    CHECK(count == 0);
    CHECK(nt_bidi_reorder_line(bidi, SPAN(13, 13), visual, ARRAY_COUNT(visual), &count) == NT_RESULT_OK);
    CHECK(count == 0);
    nt_bidi_destroy(bidi);

    CHECK(nt_bidi_create(NULL, BYTES(ALEF "\n" "b"), NT_DIRECTION_AUTO, &bidi) == NT_RESULT_OK);
    CHECK(nt_bidi_scalar(bidi, 1, &scalar) == NT_RESULT_OK &&
          scalar.paragraph_level == 1 && scalar.paragraph_separator);
    CHECK(nt_bidi_scalar(bidi, 2, &scalar) == NT_RESULT_OK &&
          scalar.paragraph_level == 0 && !scalar.paragraph_separator);
    CHECK(nt_bidi_reorder_line(bidi, SPAN(0, 4), visual, ARRAY_COUNT(visual), &count) ==
          NT_RESULT_INVALID_ARGUMENT);
    CHECK(count == 0);
    CHECK(nt_bidi_reorder_line(bidi, SPAN(0, 3), visual, ARRAY_COUNT(visual), &count) == NT_RESULT_OK);
    CHECK(count == 2);
    CHECK(nt_bidi_reorder_line(bidi, SPAN(3, 4), visual, ARRAY_COUNT(visual), &count) == NT_RESULT_OK);
    CHECK(count == 1 && visual[0].display_scalar == 'b' && visual[0].level == 0);
    nt_bidi_destroy(bidi);

    /* U+001C ends a bidi paragraph but is not a hard UAX #14 line break.
     * Both paragraph levels are LTR, so level alone cannot mark the seam. */
    const NtByteView sections = BYTES("a\x1c" "b");
    CHECK(nt_bidi_create(NULL, sections, NT_DIRECTION_AUTO, &bidi) == NT_RESULT_OK);
    CHECK(nt_bidi_scalar(bidi, 1, &scalar) == NT_RESULT_OK &&
          scalar.paragraph_separator && scalar.paragraph_level == 0);
    CHECK(nt_bidi_scalar(bidi, 2, &scalar) == NT_RESULT_OK &&
          !scalar.paragraph_separator && scalar.paragraph_level == 0);
    NtLineBreak breaks[3];
    CHECK(nt_line_breaks(NULL, sections, breaks, ARRAY_COUNT(breaks), &count) == NT_RESULT_OK);
    CHECK(count == 1 && breaks[0].offset == sections.count && breaks[0].mandatory);
    nt_bidi_destroy(bidi);

    CHECK(nt_bidi_create(NULL, BYTES("(" ALEF BET ")"), NT_DIRECTION_RTL, &bidi) == NT_RESULT_OK);
    CHECK(nt_bidi_reorder_line(bidi, SPAN(0, 6), visual, ARRAY_COUNT(visual), &count) == NT_RESULT_OK);
    CHECK(count == 4 && visual[0].logical_index == 3 && visual[0].display_scalar == '(');
    CHECK(visual[3].logical_index == 0 && visual[3].display_scalar == ')');
    nt_bidi_destroy(bidi);
    return true;
}

static bool script_runs(void)
{
    NtScriptRun runs[4] = {{{77, 88}, "Zzzz"}};
    const NtByteView text = BYTES(".a" ACCENT " " HIRA PROLONG KANA);
    size_t count = 12;
    CHECK(nt_script_runs(NULL, text, NULL, 0, &count) == NT_RESULT_OK && count == 3);
    CHECK(nt_script_runs(NULL, text, runs, 2, &count) == NT_RESULT_INSUFFICIENT_BUFFER);
    CHECK(count == 3 && runs[0].source.begin == 77);
    CHECK(nt_script_runs(NULL, text, runs, ARRAY_COUNT(runs), &count) == NT_RESULT_OK);
    CHECK(count == 3);
    CHECK(runs[0].source.begin == 0 && runs[0].source.end == 5 && !strcmp(runs[0].script, "Latn"));
    CHECK(runs[1].source.begin == 5 && runs[1].source.end == 11 && !strcmp(runs[1].script, "Hira"));
    CHECK(runs[2].source.begin == 11 && runs[2].source.end == 14 && !strcmp(runs[2].script, "Kana"));

    CHECK(nt_script_runs(NULL, BYTES(PROLONG KANA), runs, ARRAY_COUNT(runs), &count) == NT_RESULT_OK);
    CHECK(count == 1 && runs[0].source.end == 6 && !strcmp(runs[0].script, "Kana"));
    CHECK(nt_script_runs(NULL, BYTES("?!"), runs, ARRAY_COUNT(runs), &count) == NT_RESULT_OK);
    CHECK(count == 1 && !strcmp(runs[0].script, "Zyyy"));
    CHECK(nt_script_runs(NULL, BYTES("\xcd\xb8"), runs, ARRAY_COUNT(runs), &count) == NT_RESULT_OK);
    CHECK(count == 1 && !strcmp(runs[0].script, "Zzzz")); /* Unassigned U+0378 */
    CHECK(nt_script_runs(NULL, BYTES("\xcd\xb8" ".a"), runs, ARRAY_COUNT(runs), &count) == NT_RESULT_OK);
    CHECK(count == 2 && runs[0].source.begin == 0 && runs[0].source.end == 2);
    CHECK(!strcmp(runs[0].script, "Zzzz") && !strcmp(runs[1].script, "Latn"));
    CHECK(runs[1].source.begin == 2 && runs[1].source.end == 4);
    CHECK(nt_script_runs(NULL, BYTES(""), runs, ARRAY_COUNT(runs), &count) == NT_RESULT_OK);
    CHECK(count == 0 && !strcmp(runs[0].script, "Zzzz"));
    CHECK(nt_script_runs(NULL, BYTES("\xef"), runs, ARRAY_COUNT(runs), &count) ==
          NT_RESULT_INVALID_ARGUMENT);
    CHECK(count == 0 && !strcmp(runs[0].script, "Zzzz"));
    return true;
}

typedef struct Memory {
    size_t calls, failure, live;
} Memory;

static void *allocate(void *context, size_t bytes, size_t alignment)
{
    Memory *memory = context;
    if (++memory->calls == memory->failure) return NULL;
    void *result = malloc(bytes);
    if (!result) return NULL;
    if (!alignment || (uintptr_t)result % alignment) abort();
    ++memory->live;
    return result;
}

static void release(void *context, void *pointer, size_t bytes, size_t alignment)
{
    Memory *memory = context;
    if (!memory->live || !bytes || !alignment || !pointer) abort();
    --memory->live;
    free(pointer);
}

static bool allocation_and_long_input(void)
{
    Memory memory = {0};
    const NtAllocator allocator = {&memory, allocate, release};
    NtBidi *bidi;
    CHECK(nt_bidi_create(NULL, BYTES(""), NT_DIRECTION_AUTO, &bidi) == NT_RESULT_OK);
    CHECK(nt_bidi_count(bidi) == 0);
    NtBidi *out = bidi;
    for (size_t fail = 1; fail <= 5; ++fail) {
        memory = (Memory){0, fail, 0};
        CHECK(nt_bidi_create(&allocator, BYTES(ALEF "a"), NT_DIRECTION_AUTO, &out) ==
              NT_RESULT_ALLOCATION_FAILED);
        CHECK(out == bidi && memory.live == 0);
    }
    size_t count = 99;
    NtLineBreak row = {77, true};
    memory = (Memory){0, 1, 0};
    CHECK(nt_line_breaks(&allocator, BYTES("ab"), &row, 1, &count) == NT_RESULT_ALLOCATION_FAILED);
    CHECK(count == 99 && row.offset == 77 && memory.live == 0);
    NtScriptRun run = {{77, 88}, "Zzzz"};
    memory = (Memory){0, 1, 0};
    CHECK(nt_script_runs(&allocator, BYTES("ab"), &run, 1, &count) == NT_RESULT_ALLOCATION_FAILED);
    CHECK(count == 99 && run.source.begin == 77 && memory.live == 0);
    CHECK(nt_bidi_create(NULL, BYTES("\xff"), NT_DIRECTION_AUTO, &out) ==
          NT_RESULT_INVALID_ARGUMENT);
    CHECK(out == bidi);
    nt_bidi_destroy(bidi);

    char long_text[16384];
    memset(long_text, 'a', sizeof(long_text));
    const NtByteView input = {long_text, sizeof(long_text)};
    CHECK(nt_bidi_create(NULL, input, NT_DIRECTION_AUTO, &bidi) == NT_RESULT_OK);
    CHECK(nt_bidi_count(bidi) == sizeof(long_text));
    CHECK(nt_bidi_reorder_line(bidi, SPAN(2048, sizeof(long_text)), NULL, 0, &count) ==
          NT_RESULT_OK && count == sizeof(long_text) - 2048);
    nt_bidi_destroy(bidi);
    CHECK(nt_line_breaks(NULL, input, &row, 1, &count) == NT_RESULT_OK && count == 1);
    CHECK(row.offset == sizeof(long_text) && row.mandatory);
    CHECK(nt_script_runs(NULL, input, &run, 1, &count) == NT_RESULT_OK && count == 1);
    CHECK(run.source.end == sizeof(long_text) && !strcmp(run.script, "Latn"));
    return true;
}

int main(void)
{
    if (!line_breaks() || !bidi_lines() || !script_runs() || !allocation_and_long_input())
        return 1;
    puts("PASS: Unicode analysis byte ranges, paragraphs, wrapped lines, mirroring, "
         "script extensions, long inputs and allocation rollback.");
    return 0;
}
