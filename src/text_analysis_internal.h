#ifndef NOCTURNE_INTERNAL_TEXT_ANALYSIS_H
#define NOCTURNE_INTERNAL_TEXT_ANALYSIS_H

#include <nocturne/text_analysis.h>

typedef struct AnalysisRange {
    uint32_t first, last;
    uint16_t value;
} AnalysisRange;

#define NT_ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))

static inline uint16_t nt_unicode_property(const AnalysisRange *ranges, size_t length,
                                           uint32_t scalar, uint16_t fallback)
{
    size_t begin = 0;
    while (begin < length) {
        size_t middle = begin + (length - begin) / 2;
        const AnalysisRange range = ranges[middle];
        if (scalar < range.first)
            length = middle;
        else if (scalar > range.last)
            begin = middle + 1;
        else
            return range.value;
    }
    return fallback;
}

static inline bool nt_is_count_query(const void *data, size_t capacity)
{
    return !data && !capacity;
}

#endif
