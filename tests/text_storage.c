#include <nocturne/frame.h>
#include <nocturne/text.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COUNT(array) (sizeof(array) / sizeof((array)[0]))
#define BYTES(literal) ((NtByteView){ (literal), sizeof(literal) - 1 })
#define RANGE(first, last) ((NtByteSpan){ first, last })
#define CELL(column, row) ((NtCellPoint){ column, row })
#define SIZE(columns, rows) ((NtCellSize){ columns, rows })
#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
        return false; \
    } \
} while (0)

static bool equal_bytes(NtByteView a, NtByteView b)
{
    return a.count == b.count && (!a.count || memcmp(a.data, b.data, a.count) == 0);
}

static NtClusterId cluster_id(const NtText *text, size_t index, size_t start, size_t end)
{
    NtTextCluster cluster;
    if (nt_text_cluster(text, index, &cluster) != NT_RESULT_OK ||
        cluster.source.begin != start || cluster.source.end != end)
        return 0;
    return cluster.id;
}

static bool owned_text(void)
{
    char source[] = "aa\0e\xcc\x81!";
    NtText *text = NULL;
    CHECK(nt_text_create(NULL, BYTES(source), &text) == NT_RESULT_OK);
    source[1] = 'X';
    CHECK(equal_bytes(nt_text_bytes(text), BYTES("aa\0e\xcc\x81!")));
    CHECK(nt_text_cluster_count(text) == 5);
    const NtClusterId a = cluster_id(text, 0, 0, 1);
    const NtClusterId duplicate = cluster_id(text, 1, 1, 2);
    const NtClusterId nul = cluster_id(text, 2, 2, 3);
    const NtClusterId accent = cluster_id(text, 3, 3, 6);
    const NtClusterId suffix = cluster_id(text, 4, 6, 7);
    CHECK(a && duplicate && a != duplicate && nul && accent && suffix);
    const NtByteView before = nt_text_bytes(text);
    CHECK(nt_text_replace(text, RANGE(0, 1), BYTES("a")) == NT_RESULT_OK);
    CHECK(nt_text_bytes(text).data == before.data);
    CHECK(nt_text_replace(text, RANGE(5, 6), BYTES("")) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(nt_text_replace(text, RANGE(7, 8), BYTES("")) == NT_RESULT_OUT_OF_RANGE);
    CHECK(nt_text_replace(text, RANGE(2, 1), BYTES("")) == NT_RESULT_OUT_OF_RANGE);
    CHECK(nt_text_replace(text, RANGE(0, 1), BYTES("\xed\xa0\x80")) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(nt_text_replace(text, RANGE(0, 0), (NtByteView){ before.data, SIZE_MAX }) ==
          NT_RESULT_OUT_OF_RANGE);
    CHECK(nt_text_bytes(text).data == before.data && cluster_id(text, 3, 3, 6) == accent);
    NtTextCluster untouched = { 42, { 500, 600 } };
    CHECK(nt_text_cluster(text, 5, &untouched) == NT_RESULT_OUT_OF_RANGE);
    CHECK(untouched.id == 42 && untouched.source.begin == 500);
    CHECK(nt_text_replace(text, RANGE(0, 1), BYTES("")) == NT_RESULT_OK);
    CHECK(cluster_id(text, 0, 0, 1) == duplicate);
    CHECK(cluster_id(text, 1, 1, 2) == nul);
    CHECK(nt_text_replace(text, RANGE(3, 5), BYTES("")) == NT_RESULT_OK);
    const NtClusterId unaccented = cluster_id(text, 2, 2, 3);
    CHECK(unaccented > accent && cluster_id(text, 3, 3, 4) == suffix);

    const NtByteView alias = nt_text_bytes(text);
    CHECK(nt_text_replace(text, RANGE(4, 4), alias) == NT_RESULT_OK);
    CHECK(equal_bytes(nt_text_bytes(text), BYTES("a\0e!a\0e!")));
    const NtClusterId copied = cluster_id(text, 4, 4, 5);
    CHECK(copied > unaccented && cluster_id(text, 0, 0, 1) == duplicate);
    CHECK(nt_text_replace(text, RANGE(0, 8), BYTES("")) == NT_RESULT_OK);
    CHECK(nt_text_bytes(text).data == NULL && nt_text_cluster_count(text) == 0);
    CHECK(nt_text_replace(text, RANGE(0, 0), BYTES("a")) == NT_RESULT_OK);
    CHECK(cluster_id(text, 0, 0, 1) > copied);
    nt_text_destroy(text);
    nt_text_destroy(NULL);
    return true;
}

#define RI_A "\xf0\x9f\x87\xa6"
#define RI_B "\xf0\x9f\x87\xa7"
#define RI_C "\xf0\x9f\x87\xa8"
#define RI_D "\xf0\x9f\x87\xa9"
#define RI_E "\xf0\x9f\x87\xaa"
#define ZWJ "\xe2\x80\x8d"
#define VS15 "\xef\xb8\x8e"
#define VS16 "\xef\xb8\x8f"

static bool resegmentation(void)
{
    NtText *text = NULL;
    CHECK(nt_text_create(NULL, BYTES(RI_A RI_B RI_C RI_D "!"), &text) == NT_RESULT_OK);
    const NtClusterId first = cluster_id(text, 0, 0, 8);
    const NtClusterId second = cluster_id(text, 1, 8, 16);
    const NtClusterId suffix = cluster_id(text, 2, 16, 17);
    CHECK(first && second && suffix);
    CHECK(nt_text_replace(text, RANGE(0, 0), BYTES(RI_E)) == NT_RESULT_OK);
    CHECK(nt_text_cluster_count(text) == 4);
    CHECK(cluster_id(text, 0, 0, 8) > suffix && cluster_id(text, 1, 8, 16) > suffix);
    CHECK(cluster_id(text, 2, 16, 20) > suffix && cluster_id(text, 3, 20, 21) == suffix);
    CHECK(nt_text_replace(text, RANGE(0, 4), BYTES("")) == NT_RESULT_OK);
    CHECK(cluster_id(text, 0, 0, 8) > suffix && cluster_id(text, 1, 8, 16) > suffix);
    CHECK(cluster_id(text, 2, 16, 17) == suffix);
    nt_text_destroy(text);

    CHECK(nt_text_create(NULL, BYTES("a\nx"), &text) == NT_RESULT_OK);
    const NtClusterId a = cluster_id(text, 0, 0, 1);
    const NtClusterId lf = cluster_id(text, 1, 1, 2);
    const NtClusterId x = cluster_id(text, 2, 2, 3);
    CHECK(a && lf && x);
    CHECK(nt_text_replace(text, RANGE(1, 1), BYTES("\r")) == NT_RESULT_OK);
    CHECK(cluster_id(text, 0, 0, 1) == a && cluster_id(text, 1, 1, 3) > x);
    CHECK(cluster_id(text, 2, 3, 4) == x);
    nt_text_destroy(text);
    return true;
}

#define ACCENT "\xcc\x81"
#define HEART "\xe2\x99\xa5"
#define TWO_HEARTS "\xf0\x9f\x92\x95"
#define KEYCAP "\xe2\x83\xa3"
#define THUMB "\xf0\x9f\x91\x8d"
#define TONE "\xf0\x9f\x8f\xbb"
#define POINT_UP "\xe2\x98\x9d"
#define CJK "\xe7\x95\x8c"

static bool width_policy(void)
{
    const struct {
        NtByteView glyph;
        NtGlyphPresentation kind;
        uint8_t columns[4]; /* narrow/narrow, narrow/wide, wide/narrow, wide/wide */
    } cases[] = {
        { BYTES("a" ACCENT), NT_GLYPH_TEXT, {1, 1, 1, 1} },
        { BYTES(CJK ACCENT), NT_GLYPH_TEXT, {2, 2, 2, 2} },
        { BYTES("\xef\xbc\xa1"), NT_GLYPH_TEXT, {2, 2, 2, 2} }, /* Fullwidth A */
        { BYTES("\xc2\xb7"), NT_GLYPH_TEXT, {1, 1, 2, 2} }, /* Middle dot */
        { BYTES("\xd8\x80\xd9\xa1"), NT_GLYPH_TEXT, {1, 1, 1, 1} }, /* Visible prepend */
        { BYTES("\xe1\x80\xab"), NT_GLYPH_TEXT, {1, 1, 1, 1} }, /* UAX #29 spacing exception */
        { BYTES("\xe0\xb8\xb3"), NT_GLYPH_ISOLATED_MARK, {1, 1, 1, 1} }, /* Thai SARA AM */
        { BYTES(ACCENT ACCENT), NT_GLYPH_ISOLATED_MARK, {1, 1, 1, 1} },
        { BYTES("\xe0\xa4\xbe"), NT_GLYPH_ISOLATED_MARK, {1, 1, 1, 1} }, /* Mc */
        { BYTES("\xef\xbe\x9e"), NT_GLYPH_ISOLATED_MARK, {1, 1, 1, 1} }, /* Extend Lm */
        { BYTES(HEART VS15), NT_GLYPH_TEXT, {1, 1, 2, 2} },
        { BYTES(HEART VS16), NT_GLYPH_EMOJI, {1, 2, 1, 2} },
        { BYTES("A" VS16), NT_GLYPH_TEXT, {1, 1, 1, 1} },
        { BYTES("\xe2\x8c\x9a" VS15), NT_GLYPH_TEXT, {2, 2, 2, 2} }, /* Watch */
        { BYTES(TWO_HEARTS VS15), NT_GLYPH_EMOJI, {1, 2, 1, 2} }, /* No registered VS15 */
        { BYTES(HEART ZWJ HEART), NT_GLYPH_EMOJI, {1, 2, 1, 2} },
        { BYTES(HEART VS15 ZWJ HEART), NT_GLYPH_TEXT, {1, 1, 2, 2} },
        { BYTES("1" KEYCAP), NT_GLYPH_EMOJI, {1, 2, 1, 2} },
        { BYTES("\xdb\x9d" "1" KEYCAP), NT_GLYPH_EMOJI, {1, 2, 1, 2} },
        { BYTES("\xdb\x9d" "1" VS16 KEYCAP), NT_GLYPH_EMOJI, {1, 2, 1, 2} },
        { BYTES("#" VS16 KEYCAP), NT_GLYPH_EMOJI, {1, 2, 1, 2} },
        { BYTES("#" VS15 KEYCAP), NT_GLYPH_TEXT, {1, 1, 1, 1} },
        { BYTES(RI_A RI_B), NT_GLYPH_EMOJI, {1, 2, 1, 2} },
        { BYTES(POINT_UP TONE), NT_GLYPH_EMOJI, {1, 2, 1, 2} },
        { BYTES(THUMB TONE ZWJ THUMB), NT_GLYPH_EMOJI, {1, 2, 1, 2} },
        { BYTES(TONE), NT_GLYPH_EMOJI, {1, 2, 1, 2} },
        { BYTES("A" TONE), NT_GLYPH_TEXT, {1, 1, 1, 1} },
        { BYTES("\xe2\x81\xa7"), NT_GLYPH_INVISIBLE, {0, 0, 0, 0} }, /* RLI */
        { BYTES("\xe2\x80\x8b"), NT_GLYPH_INVISIBLE, {0, 0, 0, 0} }, /* ZWSP */
        { BYTES(VS16), NT_GLYPH_INVISIBLE, {0, 0, 0, 0} },
        { BYTES("\0"), NT_GLYPH_REPLACEMENT, {1, 1, 1, 1} },
        { BYTES("\x1b"), NT_GLYPH_REPLACEMENT, {1, 1, 1, 1} },
        { BYTES("\xc2\x9b"), NT_GLYPH_REPLACEMENT, {1, 1, 1, 1} },
        { BYTES("\xef\xbf\xb9"), NT_GLYPH_REPLACEMENT, {1, 1, 1, 1} }, /* Annotation */
        { BYTES("\t"), NT_GLYPH_TAB, {0, 0, 0, 0} },
        { BYTES("\r\n"), NT_GLYPH_LINE_BREAK, {0, 0, 0, 0} },
        { BYTES("\r"), NT_GLYPH_LINE_BREAK, {0, 0, 0, 0} },
        { BYTES("\n"), NT_GLYPH_LINE_BREAK, {0, 0, 0, 0} },
        { BYTES("\xc2\x85"), NT_GLYPH_LINE_BREAK, {0, 0, 0, 0} },
        { BYTES("\xe2\x80\xa8"), NT_GLYPH_LINE_BREAK, {0, 0, 0, 0} },
    };
    const NtWidthPolicy policies[] = {
        {NT_WIDTH_ONE, NT_WIDTH_ONE}, {NT_WIDTH_ONE, NT_WIDTH_TWO},
        {NT_WIDTH_TWO, NT_WIDTH_ONE}, {NT_WIDTH_TWO, NT_WIDTH_TWO},
    };
    for (size_t i = 0; i < COUNT(cases); ++i) {
        for (size_t p = 0; p < COUNT(policies); ++p) {
            NtGlyphMetrics metrics;
            CHECK(nt_glyph_measure(cases[i].glyph, policies[p], &metrics) == NT_RESULT_OK);
            CHECK(metrics.presentation == cases[i].kind && metrics.columns == cases[i].columns[p]);
        }
    }
    const NtByteView bad[] = { {NULL, 0}, {NULL, 5}, BYTES("ab"), BYTES("\r\n\n"), BYTES("\xff") };
    NtGlyphMetrics untouched = { NT_GLYPH_EMOJI, 42 };
    for (size_t i = 0; i < COUNT(bad); ++i) {
        CHECK(nt_glyph_measure(bad[i], policies[0], &untouched) == NT_RESULT_INVALID_ARGUMENT);
        CHECK(untouched.presentation == NT_GLYPH_EMOJI && untouched.columns == 42);
    }
    CHECK(nt_glyph_measure(BYTES("a"), (NtWidthPolicy){0}, &untouched) == NT_RESULT_INVALID_ARGUMENT);
    return true;
}

typedef struct Allocation {
    void *pointer;
    size_t bytes, alignment;
} Allocation;

typedef struct Heap {
    Allocation live[20];
    size_t count, calls, fail_at;
    bool valid;
} Heap;

static void *allocate(void *context, size_t bytes, size_t alignment)
{
    Heap *heap = context;
    if (++heap->calls == heap->fail_at) return NULL;
    void *pointer = malloc(bytes);
    if (!pointer) return NULL;
    if (heap->count == COUNT(heap->live) || !bytes || !alignment ||
        (uintptr_t)pointer % alignment) {
        heap->valid = false;
        free(pointer);
        return NULL;
    }
    heap->live[heap->count++] = (Allocation){ pointer, bytes, alignment };
    return pointer;
}

static void release(void *context, void *pointer, size_t bytes, size_t alignment)
{
    Heap *heap = context;
    for (size_t i = 0; i < heap->count; ++i) {
        if (heap->live[i].pointer != pointer) continue;
        if (heap->live[i].bytes != bytes || heap->live[i].alignment != alignment)
            heap->valid = false;
        heap->live[i] = heap->live[--heap->count];
        free(pointer);
        return;
    }
    heap->valid = false;
}

static bool text_allocation(void)
{
    Heap heap = { .valid = true };
    NtAllocator allocator = { &heap, allocate, release };
    NtText *text = NULL;
    for (size_t i = 1; i <= 3; ++i) {
        heap.fail_at = heap.calls + i;
        CHECK(nt_text_create(&allocator, BYTES("kept"), &text) == NT_RESULT_ALLOCATION_FAILED);
        CHECK(!text && heap.count == 0 && heap.valid);
    }
    heap.fail_at = 0;
    CHECK(nt_text_create(&allocator, BYTES("kept"), &text) == NT_RESULT_OK);
    /* Descriptor is copied. The context itself remains live. */
    allocator.release = NULL;
    const NtByteView original = nt_text_bytes(text);
    const NtClusterId first = cluster_id(text, 0, 0, 1);
    const NtClusterId last = cluster_id(text, 3, 3, 4);
    CHECK(first && last && heap.count == 3);
    for (size_t i = 1; i <= 2; ++i) {
        heap.fail_at = heap.calls + i;
        CHECK(nt_text_replace(text, RANGE(1, 3), BYTES(CJK)) == NT_RESULT_ALLOCATION_FAILED);
        CHECK(heap.count == 3 && heap.valid && nt_text_bytes(text).data == original.data);
        CHECK(equal_bytes(original, BYTES("kept")) && cluster_id(text, 3, 3, 4) == last);
    }
    heap.fail_at = 0;
    CHECK(nt_text_replace(text, RANGE(1, 3), BYTES(CJK)) == NT_RESULT_OK);
    CHECK(cluster_id(text, 0, 0, 1) == first && cluster_id(text, 1, 1, 4) == last + 1);
    CHECK(cluster_id(text, 2, 4, 5) == last && nt_text_cluster_count(text) == 3);
    nt_text_destroy(text);
    CHECK(heap.count == 0 && heap.valid);
    CHECK(nt_text_create(&allocator, BYTES(""), &text) == NT_RESULT_INVALID_ARGUMENT);
    return true;
}

static const NtCellStyle style = { {1, 2, 3, 4}, {5, 6, 7, 8} };

static bool verify_cell(const NtFrame *frame, NtCellPoint cell, int32_t lead_x,
                        uint8_t columns, NtByteView glyph, NtOwnerToken owner)
{
    NtCellView view;
    CHECK(nt_frame_cell(frame, cell, &view) == NT_RESULT_OK);
    CHECK(view.lead.x == lead_x && view.lead.y == cell.y && view.glyph_columns == columns);
    CHECK(view.owner == owner && equal_bytes(view.glyph, glyph));
    if (columns) {
        CHECK(view.style.foreground.red == 1 && view.style.foreground.alpha == 4);
        CHECK(view.style.background.red == 5 && view.style.background.alpha == 8);
    } else {
        CHECK(!view.glyph.data && view.style.foreground.alpha == 0 &&
              view.style.background.alpha == 0);
    }
    return true;
}

static bool cell_storage(void)
{
    Heap heap = { .valid = true };
    const NtAllocator allocator = { &heap, allocate, release };
    NtFrame *frame = NULL;
    for (size_t i = 1; i <= 2; ++i) {
        heap.fail_at = heap.calls + i;
        CHECK(nt_frame_create(&allocator, SIZE(5, 2), &frame) == NT_RESULT_ALLOCATION_FAILED);
        CHECK(!frame && heap.count == 0 && heap.valid);
    }
    heap.fail_at = 0;
    CHECK(nt_frame_create(&allocator, SIZE(5, 2), &frame) == NT_RESULT_OK);
    const NtByteView empty = {0};
    CHECK(verify_cell(frame, CELL(1, 0), 1, 0, empty, 0));
    char glyph[] = CJK;
    CHECK(nt_frame_set_glyph(frame, CELL(1, 0), BYTES(glyph), 2, style, 12) == NT_RESULT_OK);
    glyph[0] = 'X';
    CHECK(verify_cell(frame, CELL(2, 0), 1, 2, BYTES(CJK), 12));
    NtCellView original;
    CHECK(nt_frame_cell(frame, CELL(2, 0), &original) == NT_RESULT_OK);
    CHECK(nt_frame_set_glyph(frame, CELL(2, 0), BYTES("X"), 1, style, 7) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(nt_frame_set_glyph(frame, CELL(4, 0), BYTES(CJK), 2, style, 6) == NT_RESULT_OUT_OF_RANGE);
    CHECK(nt_frame_set_glyph(frame, CELL(-1, 0), BYTES("X"), 1, style, 6) == NT_RESULT_OUT_OF_RANGE);
    const NtByteView bad[] = { BYTES(""), BYTES("\xff"), BYTES("ab"), BYTES("\x1b"),
                               BYTES("\t"), BYTES("\r\n"), BYTES(ACCENT), BYTES(VS16) };
    for (size_t i = 0; i < COUNT(bad); ++i) {
        CHECK(nt_frame_set_glyph(frame, CELL(1, 0), bad[i], 1, style, 8) == NT_RESULT_INVALID_ARGUMENT);
    }
    heap.fail_at = heap.calls + 1;
    CHECK(nt_frame_set_glyph(frame, CELL(0, 0), BYTES(CJK), 2, style, 8) ==
          NT_RESULT_ALLOCATION_FAILED);
    CHECK(heap.count == 3 && verify_cell(frame, CELL(2, 0), 1, 2, BYTES(CJK), 12));
    heap.fail_at = heap.calls + 1;
    CHECK(nt_frame_resize(frame, SIZE(2, 1)) == NT_RESULT_ALLOCATION_FAILED);
    CHECK(nt_frame_size(frame).columns == 5 && nt_frame_size(frame).rows == 2);
    NtCellView still_live;
    CHECK(nt_frame_cell(frame, CELL(2, 0), &still_live) == NT_RESULT_OK);
    CHECK(still_live.glyph.data == original.glyph.data && equal_bytes(original.glyph, BYTES(CJK)));
    heap.fail_at = 0;
    /* Installing from an overlapping borrowed glyph copies before eviction. */
    CHECK(nt_frame_set_glyph(frame, CELL(0, 0), original.glyph, 2, style, 8) == NT_RESULT_OK);
    CHECK(verify_cell(frame, CELL(1, 0), 0, 2, BYTES(CJK), 8));
    CHECK(verify_cell(frame, CELL(2, 0), 2, 0, empty, 0));
    CHECK(nt_frame_set_glyph(frame, CELL(0, 0), BYTES("X"), 1, style, 9) == NT_RESULT_OK);
    CHECK(verify_cell(frame, CELL(1, 0), 1, 0, empty, 0));
    CHECK(nt_frame_set_glyph(frame, CELL(1, 0), BYTES(CJK), 2, style, 4) == NT_RESULT_OK);
    CHECK(nt_frame_clear_glyph(frame, CELL(2, 0)) == NT_RESULT_OK);
    CHECK(verify_cell(frame, CELL(1, 0), 1, 0, empty, 0));
    CHECK(verify_cell(frame, CELL(2, 0), 2, 0, empty, 0));
    CHECK(nt_frame_set_glyph(frame, CELL(1, 0), BYTES(CJK), 2, style, 4) == NT_RESULT_OK);
    CHECK(nt_frame_set_glyph(frame, CELL(0, 1), BYTES(CJK), 2, style, 1) == NT_RESULT_OK);
    CHECK(nt_frame_resize(frame, SIZE(UINT32_MAX, 0)) == NT_RESULT_OUT_OF_RANGE);
    CHECK(nt_frame_resize(frame, SIZE((uint32_t)INT32_MAX + 1, (uint32_t)INT32_MAX + 1)) ==
          NT_RESULT_OUT_OF_RANGE);
    CHECK(nt_frame_resize(frame, SIZE(2, 2)) == NT_RESULT_OK);
    CHECK(verify_cell(frame, CELL(0, 0), 0, 1, BYTES("X"), 9));
    CHECK(verify_cell(frame, CELL(1, 0), 1, 0, empty, 0));
    CHECK(verify_cell(frame, CELL(1, 1), 0, 2, BYTES(CJK), 1));
    CHECK(nt_frame_resize(frame, SIZE(4, 1)) == NT_RESULT_OK);
    CHECK(verify_cell(frame, CELL(0, 0), 0, 1, BYTES("X"), 9));
    CHECK(verify_cell(frame, CELL(2, 0), 2, 0, empty, 0));
    CHECK(nt_frame_cell(frame, CELL(1, 1), &still_live) == NT_RESULT_OUT_OF_RANGE);
    CHECK(still_live.owner == 12 && still_live.lead.x == 1);
    CHECK(nt_frame_resize(frame, SIZE(0, (uint32_t)INT32_MAX + 1)) == NT_RESULT_OK);
    CHECK(nt_frame_size(frame).rows == (uint32_t)INT32_MAX + 1 && heap.count == 1);
    CHECK(nt_frame_cell(frame, CELL(0, 0), &still_live) == NT_RESULT_OUT_OF_RANGE);
    CHECK(nt_frame_resize(frame, SIZE(1, 1)) == NT_RESULT_OK);
    CHECK(verify_cell(frame, CELL(0, 0), 0, 0, empty, 0));
    CHECK(nt_frame_clear_glyph(frame, CELL(0, 0)) == NT_RESULT_OK);
    nt_frame_destroy(frame);
    CHECK(heap.count == 0 && heap.valid);
    return true;
}

int main(void)
{
    if (!owned_text() || !resegmentation() || !width_policy() ||
        !text_allocation() || !cell_storage())
        return 1;
    puts("PASS: owned text, edits and resegmentation, all four width policies, "
         "frame footprints, and transactional allocator failures.");
    return 0;
}
