#include <nocturne/text.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COUNT(array) (sizeof(array) / sizeof((array)[0]))
#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
        return false; \
    } \
} while (0)

static bool utf8_contract(void)
{
    const struct {
        NtByteView code;
        uint32_t scalar;
    } valid[] = {
        {{ "\0", 1 }, 0},
        {{ "\x7f", 1 }, 0x7f},
        {{ "\xc2\x80", 2 }, 0x80},
        {{ "\xdf\xbf", 2 }, 0x7ff},
        {{ "\xe0\xa0\x80", 3 }, 0x800},
        {{ "\xed\x9f\xbf", 3 }, 0xd7ff},
        {{ "\xee\x80\x80", 3 }, 0xe000},
        {{ "\xef\xbf\xbf", 3 }, 0xffff},
        {{ "\xf0\x90\x80\x80", 4 }, 0x10000},
        {{ "\xf4\x8f\xbf\xbf", 4 }, 0x10ffff},
    };
    for (size_t i = 0; i < COUNT(valid); ++i) {
        size_t offset = 0;
        uint32_t scalar = 0x123456;
        CHECK(nt_utf8_decode(valid[i].code, &offset, &scalar) == NT_RESULT_OK);
        CHECK(offset == valid[i].code.count);
        CHECK(scalar == valid[i].scalar);
        CHECK(nt_utf8_decode(valid[i].code, &offset, &scalar) == NT_RESULT_OUT_OF_RANGE);
        CHECK(offset == valid[i].code.count && scalar == valid[i].scalar);
    }

    const NtByteView invalid[] = {
        { "\x80", 1 }, { "\xc0\x80", 2 }, { "\xc1\xbf", 2 },
        { "\xe0\x9f\xbf", 3 }, { "\xed\xa0\x80", 3 }, { "\xed\xbf\xbf", 3 },
        { "\xf0\x8f\xbf\xbf", 4 }, { "\xf4\x90\x80\x80", 4 },
        { "\xf5\x80\x80\x80", 4 }, { "\xff", 1 }, { "\xc2", 1 },
        { "\xe1\x80", 2 }, { "\xf0\x90\x80", 3 }, { "\xe1\x20\x80", 3 },
        { "\xf0\x90\x80\x7f", 4 }, { NULL, 1 },
    };
    for (size_t i = 0; i < COUNT(invalid); ++i) {
        size_t offset = 0, scalars = 999;
        uint32_t scalar = 0xa5a5;
        CHECK(nt_utf8_decode(invalid[i], &offset, &scalar) == NT_RESULT_INVALID_ARGUMENT);
        CHECK(offset == 0 && scalar == 0xa5a5);
        CHECK(nt_utf8_validate(invalid[i], &scalars) == NT_RESULT_INVALID_ARGUMENT);
        CHECK(scalars == 999);
    }

    const NtByteView mixed = { "A\0\xcc\x81\xf0\x9f\x8c\x99", 8 };
    size_t count = 999, offset = 3;
    uint32_t scalar = 42;
    CHECK(nt_utf8_validate(mixed, &count) == NT_RESULT_OK && count == 4);
    CHECK(nt_utf8_validate(mixed, NULL) == NT_RESULT_OK);
    CHECK(nt_utf8_decode(mixed, &offset, &scalar) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(offset == 3 && scalar == 42);
    CHECK(nt_utf8_decode(mixed, NULL, &scalar) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(nt_utf8_decode(mixed, &offset, NULL) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(nt_utf8_validate((NtByteView){ NULL, 0 }, &count) == NT_RESULT_OK && count == 0);
    return true;
}

static bool iterator_contract(void)
{
    NtGraphemeIterator cursor;
    NtByteSpan span = { 99, 100 };
    bool available = true;
    const NtByteView empty = { NULL, 0 };
    CHECK(nt_grapheme_iterator_init(&cursor, empty) == NT_RESULT_OK);
    CHECK(nt_grapheme_iterator_next(&cursor, &span, &available) == NT_RESULT_OK);
    CHECK(!available && span.begin == 99 && span.end == 100);
    CHECK(nt_grapheme_iterator_init(NULL, empty) == NT_RESULT_INVALID_ARGUMENT);

    const NtByteView text = { "e\xcc\x81" "X", 4 };
    const NtByteView malformed = { "a\xed\xa0\x80", 4 };
    CHECK(nt_grapheme_iterator_init(&cursor, text) == NT_RESULT_OK);
    CHECK(nt_grapheme_iterator_next(&cursor, &span, &available) == NT_RESULT_OK);
    CHECK(available && span.begin == 0 && span.end == 3);
    NtGraphemeIterator forked = cursor;
    CHECK(nt_grapheme_iterator_init(&cursor, malformed) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(nt_grapheme_iterator_next(&cursor, &span, NULL) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(nt_grapheme_iterator_next(&cursor, &span, &available) == NT_RESULT_OK);
    CHECK(available && span.begin == 3 && span.end == 4);
    CHECK(nt_grapheme_iterator_next(&forked, &span, &available) == NT_RESULT_OK);
    CHECK(available && span.begin == 3 && span.end == 4);
    CHECK(nt_grapheme_iterator_next(&forked, &span, &available) == NT_RESULT_OK);
    CHECK(!available && span.begin == 3 && span.end == 4);
    return true;
}

static size_t encode(uint32_t scalar, char bytes[4])
{
    if (scalar < 0x80) {
        bytes[0] = (char)scalar;
        return 1;
    }
    if (scalar < 0x800) {
        bytes[0] = (char)(0xc0 | (scalar >> 6));
        bytes[1] = (char)(0x80 | (scalar & 0x3f));
        return 2;
    }
    if (scalar < 0x10000) {
        bytes[0] = (char)(0xe0 | (scalar >> 12));
        bytes[1] = (char)(0x80 | ((scalar >> 6) & 0x3f));
        bytes[2] = (char)(0x80 | (scalar & 0x3f));
        return 3;
    }
    bytes[0] = (char)(0xf0 | (scalar >> 18));
    bytes[1] = (char)(0x80 | ((scalar >> 12) & 0x3f));
    bytes[2] = (char)(0x80 | ((scalar >> 6) & 0x3f));
    bytes[3] = (char)(0x80 | (scalar & 0x3f));
    return 4;
}

/* One row of the pinned test file is a break marker followed by alternating
 * hex scalars and break markers. Record expected boundaries in byte offsets. */
static bool run_fixture_line(char *token)
{
    const char delimiters[] = " \t\r\n";
    const char *boundary = "\xc3\xb7";
    const char *joined = "\xc3\x97";
    char bytes[4096];
    size_t breaks[COUNT(bytes)];
    size_t size = 0, count = 1;
    breaks[0] = 0;

    CHECK(strcmp(token, boundary) == 0);
    while ((token = strtok(NULL, delimiters)) != NULL) {
        char *tail = NULL;
        errno = 0;
        const unsigned long value = strtoul(token, &tail, 16);
        CHECK(tail != token && *tail == 0 && errno == 0 && value <= 0x10fffful);
        CHECK(value < 0xd800 || value > 0xdfff);
        CHECK(size <= COUNT(bytes) - 4 && count < COUNT(breaks));
        size += encode((uint32_t)value, bytes + size);

        token = strtok(NULL, delimiters);
        CHECK(token != NULL);
        if (strcmp(token, boundary) == 0) {
            breaks[count++] = size;
        } else {
            CHECK(strcmp(token, joined) == 0);
        }
    }
    CHECK(size != 0 && count >= 2 && breaks[count - 1] == size);

    NtGraphemeIterator iterator;
    NtByteSpan span;
    bool available;
    CHECK(nt_grapheme_iterator_init(&iterator, (NtByteView){ bytes, size }) == NT_RESULT_OK);
    for (size_t i = 1; i < count; ++i) {
        CHECK(nt_grapheme_iterator_next(&iterator, &span, &available) == NT_RESULT_OK);
        CHECK(available && span.begin == breaks[i - 1] && span.end == breaks[i]);
    }
    CHECK(nt_grapheme_iterator_next(&iterator, &span, &available) == NT_RESULT_OK && !available);
    return true;
}

static bool run_fixture(FILE *fixture)
{
    char line[4096];
    size_t line_number = 0, cases = 0;
    while (fgets(line, sizeof(line), fixture)) {
        ++line_number;
        CHECK(strchr(line, '\n') || feof(fixture));
        char *comment = strchr(line, '#');
        if (comment) *comment = 0;
        char *token = strtok(line, " \t\r\n");
        if (!token) continue;
        if (!run_fixture_line(token)) {
            fprintf(stderr, "GraphemeBreakTest.txt fixture line %zu failed\n", line_number);
            return false;
        }
        ++cases;
    }
    CHECK(!ferror(fixture));
    if (cases != 766) {
        fprintf(stderr, "Expected 766 Unicode 17.0.0 grapheme cases, read %zu\n", cases);
        return false;
    }
    printf("Unicode %s extended grapheme fixture cases: %zu passed\n", nt_unicode_version(), cases);
    return true;
}

int main(int argc, char **argv)
{
    if (argc != 2 || !utf8_contract() || !iterator_contract() ||
        strcmp(nt_unicode_version(), "17.0.0") != 0)
        return EXIT_FAILURE;
    FILE *fixture = fopen(argv[1], "rb");
    if (!fixture) {
        perror(argv[1]);
        return EXIT_FAILURE;
    }
    const bool passed = run_fixture(fixture);
    fclose(fixture);
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
