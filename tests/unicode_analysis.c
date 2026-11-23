#include <nocturne/text_analysis.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define BYTES(s) ((NtByteView){s, sizeof(s) - 1})
#define SPAN(a, b) ((NtByteSpan){a, b})

static const char *fixture;
static size_t line_number;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%zu (%s:%d): %s\n", fixture, line_number, \
                __FILE__, __LINE__, #condition); \
        return false; \
    } \
} while (0)

enum { MAX_SCALARS = 256, MAX_BYTES = MAX_SCALARS * 4 };

typedef struct Sample {
    char bytes[MAX_BYTES];
    size_t byte_count, length;
    size_t offsets[MAX_SCALARS + 1];
} Sample;

static bool append(Sample *sample, uint32_t scalar)
{
    if (sample->length >= MAX_SCALARS || scalar > 0x10ffff ||
        (scalar >= 0xd800 && scalar <= 0xdfff)) return false;
    char *out = sample->bytes + sample->byte_count;
    size_t n;
    if (scalar <= 0x7f) {
        out[0] = (char)scalar;
        n = 1;
    } else if (scalar <= 0x7ff) {
        out[0] = (char)(0xc0u | (scalar >> 6));
        out[1] = (char)(0x80u | (scalar & 0x3fu));
        n = 2;
    } else if (scalar <= 0xffff) {
        out[0] = (char)(0xe0u | (scalar >> 12));
        out[1] = (char)(0x80u | ((scalar >> 6) & 0x3fu));
        out[2] = (char)(0x80u | (scalar & 0x3fu));
        n = 3;
    } else {
        out[0] = (char)(0xf0u | (scalar >> 18));
        out[1] = (char)(0x80u | ((scalar >> 12) & 0x3fu));
        out[2] = (char)(0x80u | ((scalar >> 6) & 0x3fu));
        out[3] = (char)(0x80u | (scalar & 0x3fu));
        n = 4;
    }
    sample->byte_count += n;
    sample->offsets[++sample->length] = sample->byte_count;
    return true;
}

static bool nonblank(const char *line)
{
    while (isspace((unsigned char)*line)) ++line;
    return *line != '\0';
}

static bool number(const char *token, int base, unsigned long maximum, uint32_t *out)
{
    while (isspace((unsigned char)*token)) ++token;
    if (!isxdigit((unsigned char)*token)) return false;
    char *end;
    errno = 0;
    unsigned long value = strtoul(token, &end, base);
    if (end == token || errno || value > maximum || nonblank(end)) return false;
    *out = (uint32_t)value;
    return true;
}

static bool split(char *row, char **fields, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        fields[i] = row;
        char *next = strchr(row, ';');
        if (i + 1 == count) return !next;
        if (!next) return false;
        *next = '\0';
        row = next + 1;
    }
    return false;
}

static bool fixture_line(char *row, bool final)
{
    CHECK(final || strchr(row, '\n') != NULL);
    char *comment = strchr(row, '#');
    if (comment) *comment = '\0';
    return true;
}

static bool hard_break(uint32_t scalar)
{
    return (scalar >= 0x0a && scalar <= 0x0d) || scalar == 0x85 ||
           scalar == 0x2028 || scalar == 0x2029;
}

static bool line_break_row(char *row)
{
    Sample sample = {0};
    NtLineBreak expected[MAX_SCALARS + 1], actual[MAX_SCALARS + 1];
    size_t expected_count = 0;
    uint32_t previous = 0;
    bool marker = true, final_break = false;
    for (char *tok = strtok(row, " \t\r\n"); tok; tok = strtok(NULL, " \t\r\n")) {
        if (marker) {
            final_break = strcmp(tok, "\xc3\xb7") == 0;
            CHECK(final_break || strcmp(tok, "\xc3\x97") == 0);
            if (final_break) {
                CHECK(expected_count < ARRAY_COUNT(expected));
                expected[expected_count++] = (NtLineBreak){sample.byte_count, hard_break(previous)};
            }
        } else {
            CHECK(number(tok, 16, 0x10ffff, &previous));
            CHECK(append(&sample, previous));
        }
        marker = !marker;
    }
    CHECK(!marker && final_break && expected_count);
    expected[expected_count - 1].mandatory = true;
    size_t count = 0;
    const NtByteView input = {sample.bytes, sample.byte_count};
    CHECK(nt_line_breaks(NULL, input, actual, ARRAY_COUNT(actual), &count) == NT_RESULT_OK);
    if (count != expected_count) {
        fprintf(stderr, "Mapped %zu breaks, expected %zu\n", count, expected_count);
        CHECK(count == expected_count);
    }
    for (size_t i = 0; i < count; ++i) {
        CHECK(actual[i].offset == expected[i].offset);
        CHECK(actual[i].mandatory == expected[i].mandatory);
    }
    return true;
}

typedef struct BidiExpect {
    size_t level_count, visual_count;
    uint8_t levels[MAX_SCALARS];
    size_t order[MAX_SCALARS];
} BidiExpect;

static bool read_levels(char *text, BidiExpect *expect)
{
    expect->level_count = 0;
    for (char *tok = strtok(text, " \t\r\n"); tok; tok = strtok(NULL, " \t\r\n")) {
        uint32_t level = NT_BIDI_REMOVED_LEVEL;
        if (strcmp(tok, "x") != 0) CHECK(number(tok, 10, 126, &level));
        CHECK(expect->level_count < ARRAY_COUNT(expect->levels));
        expect->levels[expect->level_count++] = (uint8_t)level;
    }
    return true;
}

static bool read_order(char *text, BidiExpect *expect)
{
    expect->visual_count = 0;
    for (char *tok = strtok(text, " \t\r\n"); tok; tok = strtok(NULL, " \t\r\n")) {
        uint32_t index;
        CHECK(number(tok, 10, MAX_SCALARS - 1, &index));
        CHECK(expect->visual_count < ARRAY_COUNT(expect->order));
        expect->order[expect->visual_count++] = index;
    }
    return true;
}

/* BidiTest supplies character classes directly. These representatives are
 * drawn from DerivedBidiClass and leave the paired-bracket property unset. */
static bool class_scalar(const char *name, uint32_t *out)
{
    static const struct { const char *name; uint32_t scalar; } classes[] = {
        {"L", 'a'}, {"R", 0x05d0}, {"AL", 0x0627}, {"EN", '0'}, {"AN", 0x0661},
        {"ES", '+'}, {"ET", '$'}, {"CS", ','}, {"NSM", 0x0300}, {"BN", 0x00ad},
        {"B", '\n'}, {"S", '\t'}, {"WS", ' '}, {"ON", '!'}, {"LRE", 0x202a},
        {"LRO", 0x202d}, {"RLE", 0x202b}, {"RLO", 0x202e}, {"PDF", 0x202c},
        {"LRI", 0x2066}, {"RLI", 0x2067}, {"FSI", 0x2068}, {"PDI", 0x2069},
    };
    for (size_t i = 0; i < ARRAY_COUNT(classes); ++i) {
        if (strcmp(name, classes[i].name) != 0) continue;
        *out = classes[i].scalar;
        return true;
    }
    return false;
}

static bool read_sample(char *row, bool classes, Sample *sample)
{
    for (char *tok = strtok(row, " \t\r\n"); tok; tok = strtok(NULL, " \t\r\n")) {
        uint32_t scalar;
        CHECK(classes ? class_scalar(tok, &scalar) : number(tok, 16, 0x10ffff, &scalar));
        CHECK(append(sample, scalar));
    }
    return true;
}

static bool check_resolution(const Sample *input, const BidiExpect *expect,
                             NtDirection direction, int base)
{
    NtBidi *analysis = NULL;
    CHECK(input->length == expect->level_count);
    const NtByteView bytes = {input->bytes, input->byte_count};
    CHECK(nt_bidi_create(NULL, bytes, direction, &analysis) == NT_RESULT_OK);

    NtBidiVisual visual[MAX_SCALARS];
    size_t count = 0;
    bool success = nt_bidi_count(analysis) == input->length &&
        nt_bidi_reorder_line(analysis, SPAN(0, input->byte_count), visual,
                             ARRAY_COUNT(visual), &count) == NT_RESULT_OK;
    success = success && count == expect->visual_count;
    for (size_t i = 0; success && i < input->length; ++i) {
        NtBidiScalar scalar;
        success = nt_bidi_scalar(analysis, i, &scalar) == NT_RESULT_OK &&
            scalar.source.begin == input->offsets[i] && scalar.source.end == input->offsets[i + 1] &&
            ((scalar.level == NT_BIDI_REMOVED_LEVEL) ==
             (expect->levels[i] == NT_BIDI_REMOVED_LEVEL)) &&
            (base < 0 || scalar.paragraph_level == base);
    }
    for (size_t i = 0; success && i < count; ++i) {
        const size_t index = visual[i].logical_index;
        success = index < input->length && index == expect->order[i] &&
            visual[i].level == expect->levels[index] &&
            visual[i].source.begin == input->offsets[index] &&
            visual[i].source.end == input->offsets[index + 1];
        if (!success)
            fprintf(stderr, "Direction %d visual position %zu got index %zu level %u, expected %zu level %u\n",
                    direction, i, index, visual[i].level, expect->order[i],
                    expect->levels[expect->order[i]]);
    }
    nt_bidi_destroy(analysis);
    CHECK(success);
    return true;
}

static bool character_row(char *row)
{
    char *fields[5];
    uint32_t direction, base;
    Sample sample = {0};
    BidiExpect expect;
    CHECK(split(row, fields, ARRAY_COUNT(fields)));
    CHECK(number(fields[1], 10, 2, &direction) && number(fields[2], 10, 1, &base));
    CHECK(read_sample(fields[0], false, &sample));
    CHECK(read_levels(fields[3], &expect) && read_order(fields[4], &expect));
    const NtDirection directions[] = {NT_DIRECTION_LTR, NT_DIRECTION_RTL, NT_DIRECTION_AUTO};
    return check_resolution(&sample, &expect, directions[direction], (int)base);
}

static bool class_row(char *row, const BidiExpect *expect, size_t *variants)
{
    char *fields[2];
    Sample sample = {0};
    uint32_t flags;
    CHECK(split(row, fields, ARRAY_COUNT(fields)));
    CHECK(number(fields[1], 16, 7, &flags) && flags);
    CHECK(read_sample(fields[0], true, &sample));
    const NtDirection directions[] = {NT_DIRECTION_AUTO, NT_DIRECTION_LTR, NT_DIRECTION_RTL};
    for (size_t i = 0; i < ARRAY_COUNT(directions); ++i) {
        if (!(flags & (1u << i))) continue;
        CHECK(check_resolution(&sample, expect, directions[i], -1));
        ++*variants;
    }
    return true;
}

typedef enum FixtureKind { FIX_LINE, FIX_BIDI_CLASS, FIX_BIDI_CODEPOINT } FixtureKind;

static bool conformance(const char *path, FixtureKind kind)
{
    fixture = path;
    line_number = 0;
    FILE *stream = fopen(path, "r");
    if (!stream) { perror(path); return false; }
    char row[16384];
    size_t rows = 0, variants = 0;
    BidiExpect expect = {0};
    bool ok = true;
    while (ok && fgets(row, sizeof(row), stream)) {
        ++line_number;
        if (!(ok = fixture_line(row, feof(stream) != 0)) || !nonblank(row)) continue;
        if (kind == FIX_BIDI_CLASS && strncmp(row, "@Levels:", 8) == 0)
            ok = read_levels(row + 8, &expect);
        else if (kind == FIX_BIDI_CLASS && strncmp(row, "@Reorder:", 9) == 0)
            ok = read_order(row + 9, &expect);
        else {
            ++rows;
            if (kind == FIX_LINE)
                ok = line_break_row(row);
            else if (kind == FIX_BIDI_CODEPOINT)
                ok = character_row(row);
            else
                ok = class_row(row, &expect, &variants);
        }
    }
    const bool readable = !ferror(stream);
    fclose(stream);
    CHECK(readable && ok);
    const size_t expected[] = {19338, 490846, 91707};
    if (rows != expected[kind]) {
        fprintf(stderr, "%s: expected %zu Unicode 17 rows, read %zu\n", path, expected[kind], rows);
        return false;
    }
    if (kind == FIX_BIDI_CLASS) {
        CHECK(variants == 770241);
        printf("Unicode 17 bidi class fixture: %zu rows, %zu direction variants passed\n",
               rows, variants);
    } else {
        printf("Unicode 17 %s fixture: %zu rows passed\n",
               kind == FIX_LINE ? "line break" : "bidi character", rows);
    }
    return true;
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: %s LineBreakTest.txt BidiTest.txt BidiCharacterTest.txt\n", argv[0]);
        return 2;
    }
    return conformance(argv[1], FIX_LINE) && conformance(argv[2], FIX_BIDI_CLASS) &&
           conformance(argv[3], FIX_BIDI_CODEPOINT) ? 0 : 1;
}
