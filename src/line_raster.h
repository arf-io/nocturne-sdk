#ifndef NOCTURNE_INTERNAL_LINE_RASTER_H
#define NOCTURNE_INTERNAL_LINE_RASTER_H

#include <nocturne/base.h>

typedef NtResult (*NtLinePixel)(const void *context, int64_t x, int64_t y);

static inline uint64_t nt_line_magnitude(int64_t value)
{
    return value < 0 ? (uint64_t)-value : (uint64_t)value;
}

/* Select the visible major-axis interval without moving or rerounding either
 * endpoint. The pixel callback can apply the minor-axis clip or a mask. */
static inline NtResult nt_line_raster(NtCellPoint a, NtCellPoint b, NtCellRect clip,
                                      NtLinePixel pixel, const void *context)
{
    const uint64_t dx = nt_line_magnitude((int64_t)b.x - a.x);
    const uint64_t dy = nt_line_magnitude((int64_t)b.y - a.y);
    const bool vertical = dy > dx;
    int64_t major_start = vertical ? a.y : a.x, major_end = vertical ? b.y : b.x;
    int64_t minor_start = vertical ? a.x : a.y, minor_end = vertical ? b.x : b.y;
    if (major_start > major_end) {
        const int64_t major_old = major_start, minor_old = minor_start;
        major_start = major_end;
        major_end = major_old;
        minor_start = minor_end;
        minor_end = minor_old;
    }
    const uint64_t major = (uint64_t)(major_end - major_start);
    if (!major) return pixel(context, a.x, a.y);
    const uint64_t minor = nt_line_magnitude(minor_end - minor_start);
    const int direction = minor_end < minor_start ? -1 : 1;
    const uint32_t extent = vertical ? clip.size.rows : clip.size.columns;
    const int64_t low = vertical ? clip.origin.y : clip.origin.x;
    const int64_t high = low + extent - 1;
    const int64_t begin = major_start > low ? major_start : low;
    const int64_t end = major_end < high ? major_end : high;
    for (int64_t p = begin; p <= end; ++p) {
        const uint64_t offset = (uint64_t)(p - major_start);
        const int64_t displacement = (int64_t)((offset * minor + major / 2) / major);
        const int64_t q = minor_start + direction * displacement;
        const NtResult result = pixel(context, vertical ? q : p, vertical ? p : q);
        if (result != NT_RESULT_OK) return result;
    }
    return NT_RESULT_OK;
}

#endif
