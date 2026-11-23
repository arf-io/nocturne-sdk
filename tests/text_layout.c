#include <nocturne/frame.h>
#include <nocturne/text_layout.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BYTES(s) ((NtByteView){s, sizeof(s) - 1})
#define POINT(x, y) ((NtCellPoint){x, y})
#define CARET(i, side) ((NtCaret){i, NT_CARET_ ## side})
#define CJK "\xe4\xb8\xad"
#define EMOJI "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb"
#define ALEF "\xd7\x90"
#define BET "\xd7\x91"
#define ACCENT "\xcc\x81"
#define MARKER "\xe2\x80\xa6"
#define CHECK(expr) do { \
    if (!(expr)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); return false; } \
} while (0)

static NtTextLayoutOptions options(void)
{
    return (NtTextLayoutOptions){.width = {NT_WIDTH_ONE, NT_WIDTH_TWO},
        .wrap = NT_WRAP_WORD, .align = NT_TEXT_ALIGN_START,
        .base_direction = NT_DIRECTION_AUTO, .tab_stop = 4};
}

static bool cell_size(const NtTextLayout *layout, uint32_t columns, uint32_t rows)
{
    NtCellSize size = nt_text_layout_size(layout);
    return columns == size.columns && rows == size.rows;
}

static bool glyph(const NtTextLayout *layout, size_t i, NtByteView bytes, size_t begin,
                   size_t end, int32_t x, int32_t y, uint32_t columns)
{
    NtClusterView cluster;
    CHECK(nt_text_layout_cluster(layout, i, &cluster) == NT_RESULT_OK);
    CHECK(cluster.source.begin == begin && cluster.source.end == end);
    CHECK(cluster.position.x == x && cluster.position.y == y && cluster.columns == columns);
    CHECK(cluster.glyph.count == bytes.count && !memcmp(cluster.glyph.data, bytes.data, bytes.count));
    return true;
}

static bool position(const NtTextLayout *layout, NtCaret caret, int32_t x, int32_t y)
{
    NtCellPoint at;
    CHECK(nt_text_layout_caret_position(layout, caret, &at) == NT_RESULT_OK);
    CHECK(at.x == x && at.y == y);
    return true;
}

static bool hit(const NtTextLayout *layout, int32_t x, int32_t y, NtCaret expected)
{
    NtCaret actual;
    CHECK(nt_text_layout_hit(layout, POINT(x, y), &actual) == NT_RESULT_OK);
    CHECK(actual.offset == expected.offset && actual.affinity == expected.affinity);
    return true;
}

static bool mixed_text_round_trip(void)
{
    const NtByteView input = BYTES("e" ACCENT CJK EMOJI "\tQ\r\n" ALEF BET);
    NtTextLayoutOptions o = options();
    NtTextLayout *layout;
    CHECK(nt_text_layout_create_borrowed(NULL, input, &o, &layout) == NT_RESULT_OK);
    CHECK(cell_size(layout, 9, 2) && nt_text_layout_cluster_count(layout) == 7);
    CHECK(glyph(layout, 0, BYTES("e" ACCENT), 0, 3, 0, 0, 1));
    CHECK(glyph(layout, 1, BYTES(CJK), 3, 6, 1, 0, 2));
    CHECK(glyph(layout, 2, BYTES(EMOJI), 6, 17, 3, 0, 2));
    CHECK(glyph(layout, 3, BYTES(" "), 17, 18, 5, 0, 3));
    CHECK(glyph(layout, 4, BYTES("Q"), 18, 19, 8, 0, 1));
    CHECK(glyph(layout, 5, BYTES(BET), 23, 25, 7, 1, 1));
    CHECK(glyph(layout, 6, BYTES(ALEF), 21, 23, 8, 1, 1));
    NtClusterView cluster;
    CHECK(nt_text_layout_cluster(layout, 0, &cluster) == NT_RESULT_OK);
    CHECK(cluster.glyph.data == input.data && cluster.id == 1 && cluster.bidi_level == 0);
    CHECK(nt_text_layout_cluster(layout, 6, &cluster) == NT_RESULT_OK);
    CHECK(cluster.bidi_level == 1 && cluster.id == 7);

    CHECK(hit(layout, 0, 0, CARET(0, LEADING)));
    CHECK(hit(layout, 2, 0, CARET(6, TRAILING)));
    CHECK(position(layout, CARET(6, TRAILING), 3, 0));
    CHECK(hit(layout, 3, 0, CARET(6, TRAILING)));
    CHECK(hit(layout, 4, 0, CARET(17, TRAILING)));
    CHECK(position(layout, CARET(17, TRAILING), 5, 0));
    CHECK(hit(layout, 7, 0, CARET(18, TRAILING)));
    CHECK(position(layout, CARET(18, TRAILING), 8, 0));
    CHECK(hit(layout, 9, 0, CARET(19, TRAILING)));
    CHECK(position(layout, CARET(19, LEADING), 9, 0));
    CHECK(position(layout, CARET(21, TRAILING), 9, 1));
    CHECK(hit(layout, 7, 1, CARET(25, TRAILING)));
    CHECK(position(layout, CARET(25, TRAILING), 7, 1));
    CHECK(hit(layout, 9, 1, CARET(21, LEADING)));
    CHECK(position(layout, CARET(21, LEADING), 9, 1));
    CHECK(position(layout, CARET(23, TRAILING), 8, 1));

    NtCellPoint untouched = POINT(77, 88);
    CHECK(nt_text_layout_caret_position(layout, CARET(1, LEADING), &untouched) == NT_RESULT_OUT_OF_RANGE);
    CHECK(nt_text_layout_caret_position(layout, CARET(20, TRAILING), &untouched) == NT_RESULT_OUT_OF_RANGE);
    CHECK(untouched.x == 77 && untouched.y == 88);
    nt_text_layout_destroy(layout);
    return true;
}

static bool wrapping_and_bounds(void)
{
    NtTextLayoutOptions o = options();
    o.bounds.columns = (NtExtentRange){0, 4, true};
    NtTextLayout *layout;
    CHECK(nt_text_layout_create_borrowed(NULL, BYTES("ab cd"), &o, &layout) == NT_RESULT_OK);
    CHECK(cell_size(layout, 3, 2) && nt_text_layout_cluster_count(layout) == 5);
    CHECK(glyph(layout, 2, BYTES(" "), 2, 3, 2, 0, 1));
    CHECK(glyph(layout, 3, BYTES("c"), 3, 4, 0, 1, 1));
    CHECK(position(layout, CARET(3, LEADING), 0, 1));
    CHECK(position(layout, CARET(3, TRAILING), 3, 0));
    CHECK(hit(layout, 0, 1, CARET(3, LEADING)));
    CHECK(hit(layout, 3, 0, CARET(3, TRAILING)));
    nt_text_layout_destroy(layout);

    o.wrap = NT_WRAP_CLUSTER;
    CHECK(nt_text_layout_create_borrowed(NULL, BYTES("ab cd"), &o, &layout) == NT_RESULT_OK);
    CHECK(glyph(layout, 3, BYTES("c"), 3, 4, 3, 0, 1));
    CHECK(glyph(layout, 4, BYTES("d"), 4, 5, 0, 1, 1));
    nt_text_layout_destroy(layout);

    /* Break after SPACE is internal to SPACE + combining mark in this input.
     * WORD may wrap after the full grapheme, never after only its first byte. */
    o.wrap = NT_WRAP_WORD;
    o.bounds.columns.max = 1;
    CHECK(nt_text_layout_create_borrowed(NULL, BYTES(" " ACCENT "a"), &o, &layout) == NT_RESULT_OK);
    CHECK(glyph(layout, 0, BYTES(" " ACCENT), 0, 3, 0, 0, 1));
    CHECK(glyph(layout, 1, BYTES("a"), 3, 4, 0, 1, 1));
    nt_text_layout_destroy(layout);

    CHECK(nt_text_layout_create_borrowed(NULL, BYTES(CJK "x"), &o, &layout) == NT_RESULT_OK);
    CHECK(cell_size(layout, 1, 2) && nt_text_layout_cluster_count(layout) == 1);
    CHECK(glyph(layout, 0, BYTES("x"), 3, 4, 0, 1, 1));
    NtCaret miss = CARET(99, LEADING);
    CHECK(nt_text_layout_hit(layout, POINT(0, 0), &miss) == NT_RESULT_OUT_OF_RANGE);
    CHECK(miss.offset == 99);
    nt_text_layout_destroy(layout);

    o.bounds.columns.max = 4;
    o.wrap = NT_WRAP_CLUSTER;
    CHECK(nt_text_layout_create_borrowed(NULL, BYTES("xxxxx\tz"), &o, &layout) == NT_RESULT_OK);
    CHECK(glyph(layout, 5, BYTES(" "), 5, 6, 1, 1, 3));
    CHECK(glyph(layout, 6, BYTES("z"), 6, 7, 0, 2, 1));
    nt_text_layout_destroy(layout);

    o.bounds.columns = (NtExtentRange){0, 0, true};
    CHECK(nt_text_layout_create_borrowed(NULL, BYTES(CJK "\r\nx"), &o, &layout) == NT_RESULT_OK);
    CHECK(cell_size(layout, 0, 2) && nt_text_layout_cluster_count(layout) == 0);
    CHECK(nt_text_layout_hit(layout, POINT(0, 0), &miss) == NT_RESULT_OUT_OF_RANGE);
    nt_text_layout_destroy(layout);

    o.bounds.columns = (NtExtentRange){3, 4, true};
    o.bounds.rows = (NtExtentRange){0, 0, true};
    CHECK(nt_text_layout_create_borrowed(NULL, BYTES("a"), &o, &layout) == NT_RESULT_OK);
    CHECK(cell_size(layout, 3, 0) && nt_text_layout_cluster_count(layout) == 0);
    nt_text_layout_destroy(layout);
    return true;
}

static bool truncation_and_alignment(void)
{
    NtTextLayoutOptions o = options();
    o.bounds.columns = (NtExtentRange){3, 3, true};
    o.wrap = NT_WRAP_NONE;
    o.ellipsis = true;
    NtTextLayout *layout;
    CHECK(nt_text_layout_create_borrowed(NULL, BYTES("a" CJK "z"), &o, &layout) == NT_RESULT_OK);
    CHECK(cell_size(layout, 3, 1) && nt_text_layout_cluster_count(layout) == 2);
    CHECK(glyph(layout, 0, BYTES("a"), 0, 1, 0, 0, 1));
    CHECK(glyph(layout, 1, BYTES(MARKER), 1, 1, 1, 0, 1));
    NtClusterView mark;
    CHECK(nt_text_layout_cluster(layout, 1, &mark) == NT_RESULT_OK && mark.id == 0);
    CHECK(hit(layout, 2, 0, CARET(1, LEADING)));
    CHECK(position(layout, CARET(1, LEADING), 1, 0));
    nt_text_layout_destroy(layout);

    o.base_direction = NT_DIRECTION_RTL;
    CHECK(nt_text_layout_create_borrowed(NULL, BYTES(ALEF BET ALEF BET), &o, &layout) == NT_RESULT_OK);
    CHECK(glyph(layout, 0, BYTES(MARKER), 4, 4, 0, 0, 1));
    CHECK(glyph(layout, 1, BYTES(BET), 2, 4, 1, 0, 1));
    CHECK(glyph(layout, 2, BYTES(ALEF), 0, 2, 2, 0, 1));
    CHECK(hit(layout, 0, 0, CARET(4, LEADING)));
    CHECK(position(layout, CARET(4, LEADING), 1, 0));
    nt_text_layout_destroy(layout);

    o.base_direction = NT_DIRECTION_LTR;
    o.bounds.columns = (NtExtentRange){0, 1, true};
    o.width.ambiguous = NT_WIDTH_TWO;
    CHECK(nt_text_layout_create_borrowed(NULL, BYTES("ab"), &o, &layout) == NT_RESULT_OK);
    CHECK(nt_text_layout_cluster_count(layout) == 1 && glyph(layout, 0, BYTES("a"), 0, 1, 0, 0, 1));
    nt_text_layout_destroy(layout);

    o = options();
    o.align = NT_TEXT_ALIGN_CENTER;
    o.ellipsis = true;
    o.bounds.columns = (NtExtentRange){6, 6, true};
    o.bounds.rows = (NtExtentRange){0, 1, true};
    CHECK(nt_text_layout_create_borrowed(NULL, BYTES("ab\ncd"), &o, &layout) == NT_RESULT_OK);
    CHECK(cell_size(layout, 6, 1));
    CHECK(glyph(layout, 0, BYTES("a"), 0, 1, 1, 0, 1));
    CHECK(glyph(layout, 2, BYTES(MARKER), 2, 2, 3, 0, 1));
    CHECK(hit(layout, 6, 0, CARET(2, LEADING)));
    CHECK(position(layout, CARET(2, LEADING), 3, 0));
    NtCellPoint untouched = POINT(77, 88);
    CHECK(nt_text_layout_caret_position(layout, CARET(3, LEADING), &untouched) == NT_RESULT_OUT_OF_RANGE);
    CHECK(untouched.x == 77 && untouched.y == 88);
    nt_text_layout_destroy(layout);

    o.align = NT_TEXT_ALIGN_END;
    o.bounds.rows = (NtExtentRange){4, 0, false};
    CHECK(nt_text_layout_create_borrowed(NULL, BYTES("a\n\n"), &o, &layout) == NT_RESULT_OK);
    CHECK(cell_size(layout, 6, 4));
    CHECK(glyph(layout, 0, BYTES("a"), 0, 1, 5, 0, 1));
    CHECK(position(layout, CARET(2, TRAILING), 6, 1));
    CHECK(position(layout, CARET(3, TRAILING), 6, 2));
    CHECK(hit(layout, 0, 2, CARET(3, LEADING)));
    CHECK(position(layout, CARET(3, LEADING), 6, 2));
    NtCaret miss = CARET(99, LEADING);
    CHECK(nt_text_layout_hit(layout, POINT(0, 3), &miss) == NT_RESULT_OUT_OF_RANGE);
    nt_text_layout_destroy(layout);
    o.align = NT_TEXT_ALIGN_START;
    CHECK(nt_text_layout_create_borrowed(NULL, BYTES(ALEF "\xe2\x80\xa8"), &o, &layout) ==
          NT_RESULT_OK);
    CHECK(position(layout, CARET(5, LEADING), 6, 1));
    nt_text_layout_destroy(layout);
    CHECK(nt_text_layout_create_borrowed(NULL, BYTES(ALEF "\n"), &o, &layout) == NT_RESULT_OK);
    CHECK(position(layout, CARET(3, LEADING), 0, 1));
    nt_text_layout_destroy(layout);
    return true;
}

static bool bidi_projection_and_fallbacks(void)
{
    NtTextLayoutOptions o = options();
    NtTextLayout *layout;
    CHECK(nt_text_layout_create_borrowed(NULL, BYTES("(" ALEF ACCENT BET ")"), &o, &layout) == NT_RESULT_OK);
    CHECK(cell_size(layout, 4, 1) && nt_text_layout_cluster_count(layout) == 4);
    CHECK(glyph(layout, 0, BYTES("("), 7, 8, 0, 0, 1));
    CHECK(glyph(layout, 1, BYTES(BET), 5, 7, 1, 0, 1));
    CHECK(glyph(layout, 2, BYTES(ALEF ACCENT), 1, 5, 2, 0, 1));
    CHECK(glyph(layout, 3, BYTES(")"), 0, 1, 3, 0, 1));
    nt_text_layout_destroy(layout);

    CHECK(nt_text_layout_create_borrowed(NULL, BYTES(ACCENT "\0A\x1c" ALEF BET), &o, &layout) ==
          NT_RESULT_OK);
    CHECK(cell_size(layout, 6, 1) && nt_text_layout_cluster_count(layout) == 6);
    CHECK(glyph(layout, 0, BYTES("\xe2\x97\x8c" ACCENT), 0, 2, 0, 0, 1));
    CHECK(glyph(layout, 1, BYTES("\xef\xbf\xbd"), 2, 3, 1, 0, 1));
    CHECK(glyph(layout, 2, BYTES("A"), 3, 4, 2, 0, 1));
    CHECK(glyph(layout, 3, BYTES("\xef\xbf\xbd"), 4, 5, 3, 0, 1));
    CHECK(glyph(layout, 4, BYTES(BET), 7, 9, 4, 0, 1));
    CHECK(glyph(layout, 5, BYTES(ALEF), 5, 7, 5, 0, 1));
    nt_text_layout_destroy(layout);
    /* The Myanmar mark participates as a spacing cluster. Adding a dotted
     * circle would create two clusters and be rejected by the frame. */
    CHECK(nt_text_layout_create_borrowed(NULL, BYTES("\xe1\x80\xab"), &o, &layout) == NT_RESULT_OK);
    CHECK(glyph(layout, 0, BYTES("\xe1\x80\xab"), 0, 3, 0, 0, 1));
    NtFrame *frame;
    NtClusterView cluster;
    CHECK(nt_frame_create(NULL, nt_text_layout_size(layout), &frame) == NT_RESULT_OK);
    CHECK(nt_text_layout_cluster(layout, 0, &cluster) == NT_RESULT_OK);
    CHECK(nt_frame_set_glyph(frame, cluster.position, cluster.glyph, (uint8_t)cluster.columns,
                              (NtCellStyle){0}, cluster.id) == NT_RESULT_OK);
    nt_frame_destroy(frame);
    nt_text_layout_destroy(layout);
    return true;
}

static bool mirror_width_policy(void)
{
    const NtByteView reverse_solidus = BYTES("\xe2\xa7\xb5"); /* U+29F5 */
    const NtByteView division_slash = BYTES("\xe2\x88\x95"); /* U+2215 */
    NtTextLayoutOptions o = options();
    o.base_direction = NT_DIRECTION_RTL;
    o.wrap = NT_WRAP_NONE;
    o.width.ambiguous = NT_WIDTH_TWO;
    o.bounds.columns = (NtExtentRange){0, 1, true};
    NtTextLayout *layout;
    CHECK(nt_text_layout_create_borrowed(NULL, reverse_solidus, &o, &layout) == NT_RESULT_OK);
    CHECK(glyph(layout, 0, reverse_solidus, 0, 3, 0, 0, 1));
    NtClusterView cluster;
    CHECK(nt_text_layout_cluster(layout, 0, &cluster) == NT_RESULT_OK && cluster.bidi_level == 1);
    nt_text_layout_destroy(layout);
    CHECK(nt_text_layout_create_borrowed(NULL, division_slash, &o, &layout) == NT_RESULT_OK);
    CHECK(nt_text_layout_cluster_count(layout) == 0);
    nt_text_layout_destroy(layout);

    o.bounds.columns.max = 2;
    CHECK(nt_text_layout_create_borrowed(NULL, division_slash, &o, &layout) == NT_RESULT_OK);
    CHECK(glyph(layout, 0, division_slash, 0, 3, 0, 0, 2));
    nt_text_layout_destroy(layout);
    o.width.ambiguous = NT_WIDTH_ONE;
    o.bounds.columns.max = 1;
    CHECK(nt_text_layout_create_borrowed(NULL, reverse_solidus, &o, &layout) == NT_RESULT_OK);
    CHECK(glyph(layout, 0, division_slash, 0, 3, 0, 0, 1));
    nt_text_layout_destroy(layout);
    CHECK(nt_text_layout_create_borrowed(NULL, division_slash, &o, &layout) == NT_RESULT_OK);
    CHECK(glyph(layout, 0, reverse_solidus, 0, 3, 0, 0, 1));
    nt_text_layout_destroy(layout);
    return true;
}

typedef struct Allocations {
    size_t calls, fail_at, live;
} Allocations;

static void *allocate(void *context, size_t size, size_t alignment)
{
    Allocations *a = context;
    if (++a->calls == a->fail_at) return NULL;
    void *bytes = malloc(size);
    if (bytes) ++a->live;
    if (bytes && (uintptr_t)bytes % alignment) abort();
    return bytes;
}

static void release(void *context, void *bytes, size_t size, size_t alignment)
{
    Allocations *a = context;
    if (!a->live || !size || !alignment) abort();
    --a->live;
    free(bytes);
}

static bool ownership_and_failure(void)
{
    NtText *text;
    CHECK(nt_text_create(NULL, BYTES("a" CJK), &text) == NT_RESULT_OK);
    CHECK(nt_text_replace(text, (NtByteSpan){0, 1}, BYTES("b")) == NT_RESULT_OK);
    NtTextCluster stored;
    CHECK(nt_text_cluster(text, 1, &stored) == NT_RESULT_OK);

    NtTextLayoutOptions o = options();
    NtTextLayout *layout;
    CHECK(nt_text_layout_create(NULL, text, &o, &layout) == NT_RESULT_OK);
    CHECK(nt_text_replace(text, (NtByteSpan){0, 4}, BYTES("elsewhere")) == NT_RESULT_OK);
    nt_text_destroy(text);
    NtClusterView cluster;
    CHECK(nt_text_layout_cluster(layout, 1, &cluster) == NT_RESULT_OK);
    CHECK(cluster.id == stored.id && glyph(layout, 1, BYTES(CJK), 1, 4, 1, 0, 2));
    nt_text_layout_destroy(layout);

    CHECK(nt_text_create(NULL, BYTES(ACCENT "(" ALEF ")"), &text) == NT_RESULT_OK);
    Allocations a = {0};
    NtAllocator allocator = {&a, allocate, release};
    CHECK(nt_text_layout_create(&allocator, text, &o, &layout) == NT_RESULT_OK);
    const size_t calls = a.calls;
    nt_text_layout_destroy(layout);
    CHECK(a.live == 0 && calls >= 15);

    for (size_t i = 1; i <= calls; ++i) {
        a = (Allocations){.fail_at = i};
        layout = NULL;
        CHECK(nt_text_layout_create(&allocator, text, &o, &layout) == NT_RESULT_ALLOCATION_FAILED);
        CHECK(!layout && !a.live);
    }
    nt_text_destroy(text);

    layout = NULL;
    CHECK(nt_text_layout_create_borrowed(NULL, BYTES("\xff"), &o, &layout) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(!layout);
    o.tab_stop = 0;
    CHECK(nt_text_layout_create_borrowed(NULL, BYTES("a"), &o, &layout) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(!layout);
    printf("Layout ownership and every allocation failure passed (%zu sites).\n", calls);
    return true;
}

static uint32_t random_value(uint32_t *state)
{
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

static bool generated_round_trips(void)
{
    const NtByteView tokens[] = {BYTES("a"), BYTES(CJK), BYTES(EMOJI), BYTES(ALEF), BYTES(BET),
        BYTES(ACCENT), BYTES(" "), BYTES("\t"), BYTES("\r\n"), BYTES("\x1c"), BYTES("\0"),
        BYTES("\xe2\x80\xab"), BYTES("\xe2\x81\xa7"), BYTES("\xe2\x81\xa9"), BYTES("("),
        BYTES(")"), BYTES("\xe2\x81\xa8"), BYTES("\xe2\x80\xac"), BYTES("\xe2\x80\xa8")};
    const NtCellStyle style = {0};
    uint32_t rng = 0xde57c1a3;
    for (size_t trial = 0; trial < 1000; ++trial) {
        char bytes[384];
        NtByteView source = {bytes, 0};
        const size_t parts = random_value(&rng) % 30;
        for (size_t k = 0; k < parts; ++k) {
            NtByteView token = tokens[random_value(&rng) % (sizeof(tokens) / sizeof(*tokens))];
            memcpy(bytes + source.count, token.data, token.count);
            source.count += token.count;
        }
        NtTextLayoutOptions o = options();
        o.wrap = (NtWrapMode)(trial % 3);
        o.align = (NtTextAlign)(trial / 3 % 3);
        o.base_direction = (NtDirection)(trial / 9 % 3);
        o.ellipsis = (trial / 27 % 2) != 0;
        o.width.ambiguous = (trial / 54 % 2) ? NT_WIDTH_ONE : NT_WIDTH_TWO;
        o.bounds.columns = (NtExtentRange){0, random_value(&rng) % 14, (trial % 4) != 0};
        o.bounds.rows = (NtExtentRange){0, random_value(&rng) % 9, (trial % 5) != 0};
        NtTextLayout *layout;
        CHECK(nt_text_layout_create_borrowed(NULL, source, &o, &layout) == NT_RESULT_OK);
        const NtCellSize size = nt_text_layout_size(layout);
        NtFrame *frame;
        CHECK(nt_frame_create(NULL, size, &frame) == NT_RESULT_OK);
        NtCellPoint last = POINT(0, -1);
        uint32_t right = 0;
        const size_t count = nt_text_layout_cluster_count(layout);
        for (size_t i = 0; i < count; ++i) {
            NtClusterView c;
            CHECK(nt_text_layout_cluster(layout, i, &c) == NT_RESULT_OK);
            CHECK(c.position.x >= 0 && c.position.y >= 0 && c.position.y >= last.y);
            CHECK((uint32_t)c.position.y < size.rows && c.columns > 0 &&
                  (uint32_t)c.position.x + c.columns <= size.columns);
            if (c.position.y == last.y) CHECK((uint32_t)c.position.x == right);
            last = c.position;
            right = (uint32_t)c.position.x + c.columns;
            CHECK(c.source.begin <= c.source.end && c.source.end <= source.count);
            if (c.id) {
                const NtCaret a = CARET(c.source.begin, LEADING), b = CARET(c.source.end, TRAILING);
                const int32_t x = c.position.x, edge = (int32_t)right, y = c.position.y;
                CHECK(position(layout, a, (c.bidi_level & 1u) ? edge : x, y));
                CHECK(position(layout, b, (c.bidi_level & 1u) ? x : edge, y));
            }
            for (uint32_t k = 0; k < c.columns;) {
                const uint8_t width = c.columns > 2 ? 1 : (uint8_t)c.columns;
                CHECK(nt_frame_set_glyph(frame, POINT(c.position.x + (int32_t)k, c.position.y),
                                          c.glyph, width, style, c.id) == NT_RESULT_OK);
                k += width;
            }
        }
        for (uint32_t y = 0; y < size.rows; ++y) {
            for (uint32_t x = 0; x <= size.columns; ++x) {
                NtCaret found;
                if (nt_text_layout_hit(layout, POINT((int32_t)x, (int32_t)y), &found) != NT_RESULT_OK)
                    continue;
                NtCellPoint edge;
                CHECK(nt_text_layout_caret_position(layout, found, &edge) == NT_RESULT_OK);
                CHECK(edge.x >= 0 && (uint32_t)edge.x <= size.columns && edge.y == (int32_t)y);
            }
        }
        nt_frame_destroy(frame);
        nt_text_layout_destroy(layout);
    }
    puts("Generated layout/frame round trips passed (1000 mixed Unicode cases).");
    return true;
}

int main(void)
{
    if (!mixed_text_round_trip() || !wrapping_and_bounds() || !truncation_and_alignment() ||
        !bidi_projection_and_fallbacks() || !mirror_width_policy() || !ownership_and_failure() || !generated_round_trips()) return 1;
    puts("Layout source spans, visual order, caret hits, wrapping and truncation passed.");
    return 0;
}
