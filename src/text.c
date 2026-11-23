#include <nocturne/text.h>

#include "allocator.h"
#include <string.h>

struct NtText {
    NtAllocator allocator;
    char *bytes;
    size_t byte_count;
    NtTextCluster *clusters;
    size_t cluster_count;
    NtClusterId last_id;
};

NT_DECLARE_ALIGNMENT(NtText);
NT_DECLARE_ALIGNMENT(NtTextCluster);

static void release_contents(NtText *text)
{
    nt_release(&text->allocator, text->bytes, text->byte_count, 1);
    nt_release(&text->allocator, text->clusters, text->cluster_count * sizeof(*text->clusters),
               NT_ALIGNMENT(NtTextCluster));
}

static NtResult allocate_clusters(NtText *text)
{
    NtGraphemeIterator iterator = { nt_text_bytes(text), 0 };
    NtByteSpan span;
    bool available;
    size_t count = 0;
    NtResult result;
    while ((result = nt_grapheme_iterator_next(&iterator, &span, &available)) == NT_RESULT_OK &&
           available)
        ++count;
    if (result != NT_RESULT_OK || count == 0) return result;
    if (count > SIZE_MAX / sizeof(*text->clusters)) return NT_RESULT_OUT_OF_RANGE;

    text->clusters = nt_allocate(&text->allocator, count * sizeof(*text->clusters),
                                 NT_ALIGNMENT(NtTextCluster));
    if (!text->clusters) return NT_RESULT_ALLOCATION_FAILED;
    text->cluster_count = count;
    return NT_RESULT_OK;
}

/* Map a new, untouched span back to its original coordinates. Replaced
 * bytes never inherit an identity from another occurrence of equal text. */
static NtClusterId original_id(const NtText *old, NtByteSpan source, size_t suffix_begin,
                               NtByteSpan candidate, size_t *next)
{
    if (candidate.begin >= suffix_begin) {
        candidate.begin = source.end + (candidate.begin - suffix_begin);
        candidate.end = source.end + (candidate.end - suffix_begin);
    } else if (candidate.end > source.begin) {
        return 0;
    }
    while (*next < old->cluster_count && old->clusters[*next].source.end <= candidate.begin)
        ++*next;
    if (*next == old->cluster_count) return 0;
    const NtTextCluster cluster = old->clusters[*next];
    return cluster.source.begin == candidate.begin && cluster.source.end == candidate.end
           ? cluster.id : 0;
}

static NtResult index_clusters(NtText *text, const NtText *old, NtByteSpan source,
                               size_t suffix_begin)
{
    NtResult result = allocate_clusters(text);
    if (result != NT_RESULT_OK) return result;
    NtGraphemeIterator iterator = { nt_text_bytes(text), 0 };
    size_t next = 0;
    for (size_t i = 0; i < text->cluster_count; ++i) {
        NtByteSpan span;
        bool available;
        result = nt_grapheme_iterator_next(&iterator, &span, &available);
        if (result != NT_RESULT_OK) return result;
        if (!available) return NT_RESULT_INVALID_ARGUMENT;
        NtClusterId id = old ? original_id(old, source, suffix_begin, span, &next) : 0;
        if (!id) {
            if (text->last_id == UINT64_MAX) return NT_RESULT_OUT_OF_RANGE;
            id = ++text->last_id;
        }
        text->clusters[i] = (NtTextCluster){ id, span };
    }
    return NT_RESULT_OK;
}

NtResult nt_text_create(const NtAllocator *allocator, NtByteView input, NtText **out)
{
    NtText initial = {0};
    if (!out || !nt_allocator_get(allocator, &initial.allocator))
        return NT_RESULT_INVALID_ARGUMENT;
    NtResult result = nt_utf8_validate(input, NULL);
    if (result != NT_RESULT_OK) return result;

    NtText *text = nt_allocate(&initial.allocator, sizeof(*text), NT_ALIGNMENT(NtText));
    if (!text) return NT_RESULT_ALLOCATION_FAILED;
    *text = initial;
    if (input.count) {
        text->bytes = nt_allocate(&text->allocator, input.count, 1);
        if (!text->bytes) {
            nt_text_destroy(text);
            return NT_RESULT_ALLOCATION_FAILED;
        }
        memcpy(text->bytes, input.data, input.count);
        text->byte_count = input.count;
    }
    result = index_clusters(text, NULL, (NtByteSpan){0}, 0);
    if (result != NT_RESULT_OK) {
        nt_text_destroy(text);
        return result;
    }
    *out = text;
    return NT_RESULT_OK;
}

void nt_text_destroy(NtText *text)
{
    if (!text) return;
    const NtAllocator allocator = text->allocator;
    release_contents(text);
    nt_release(&allocator, text, sizeof(*text), NT_ALIGNMENT(NtText));
}

NtByteView nt_text_bytes(const NtText *text)
{
    return text ? (NtByteView){ text->bytes, text->byte_count } : (NtByteView){0};
}

size_t nt_text_cluster_count(const NtText *text)
{
    return text ? text->cluster_count : 0;
}

NtResult nt_text_cluster(const NtText *text, size_t index, NtTextCluster *out)
{
    if (!text || !out) return NT_RESULT_INVALID_ARGUMENT;
    if (index >= text->cluster_count) return NT_RESULT_OUT_OF_RANGE;
    *out = text->clusters[index];
    return NT_RESULT_OK;
}

static bool scalar_boundary(const NtText *text, size_t offset)
{
    return offset == text->byte_count || ((unsigned char)text->bytes[offset] & 0xc0) != 0x80;
}

NtResult nt_text_replace(NtText *text, NtByteSpan source, NtByteView replacement)
{
    if (!text) return NT_RESULT_INVALID_ARGUMENT;
    if (source.begin > source.end || source.end > text->byte_count)
        return NT_RESULT_OUT_OF_RANGE;
    if (!scalar_boundary(text, source.begin) || !scalar_boundary(text, source.end))
        return NT_RESULT_INVALID_ARGUMENT;
    const size_t removed = source.end - source.begin;
    const size_t retained = text->byte_count - removed;
    if (replacement.count > SIZE_MAX - retained) return NT_RESULT_OUT_OF_RANGE;

    NtResult result = nt_utf8_validate(replacement, NULL);
    if (result != NT_RESULT_OK) return result;
    if (removed == replacement.count &&
        (!removed || memcmp(text->bytes + source.begin, replacement.data, removed) == 0))
        return NT_RESULT_OK;

    NtText candidate = {0};
    candidate.allocator = text->allocator;
    candidate.last_id = text->last_id;
    candidate.byte_count = retained + replacement.count;
    const size_t suffix_begin = source.begin + replacement.count;
    if (candidate.byte_count) {
        candidate.bytes = nt_allocate(&candidate.allocator, candidate.byte_count, 1);
        if (!candidate.bytes) return NT_RESULT_ALLOCATION_FAILED;
        if (source.begin) memcpy(candidate.bytes, text->bytes, source.begin);
        if (replacement.count)
            memcpy(candidate.bytes + source.begin, replacement.data, replacement.count);
        if (source.end < text->byte_count)
            memcpy(candidate.bytes + suffix_begin, text->bytes + source.end,
                   text->byte_count - source.end);
    }
    result = index_clusters(&candidate, text, source, suffix_begin);
    if (result != NT_RESULT_OK) {
        release_contents(&candidate);
        return result;
    }
    release_contents(text);
    *text = candidate;
    return NT_RESULT_OK;
}
