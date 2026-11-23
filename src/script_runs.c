/* Script-run policy adapted from native/src/script_runs.c at 53191655ec77.
 * Copyright (c) 2025-2026 Damien Heiser. MIT, see LICENSE. */
#include "text_analysis_internal.h"
#include "allocator.h"

#include <string.h>

typedef struct ScriptSet { uint64_t words[4]; } ScriptSet;
typedef struct ScriptRange {
    uint32_t first, last;
    ScriptSet candidates;
} ScriptRange;

#include "unicode_scripts.inc"

typedef struct ScriptCluster {
    NtByteSpan source;
    ScriptSet candidates;
    uint16_t script, following;
} ScriptCluster;

NT_DECLARE_ALIGNMENT(ScriptCluster);

static const ScriptSet all_scripts = {{UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX}};

static bool strong_script(uint16_t script)
{
    return script < SCRIPT_COUNT && script != SCRIPT_COMMON &&
           script != SCRIPT_INHERITED && script != SCRIPT_UNKNOWN;
}

static bool matches(ScriptSet candidates, uint16_t script)
{
    return script < SCRIPT_COUNT &&
           (candidates.words[script / 64] & (UINT64_C(1) << (script % 64))) != 0;
}

static ScriptSet script_candidates(uint32_t scalar, uint16_t primary)
{
    size_t begin = 0, end = NT_ARRAY_COUNT(script_extensions);
    while (begin < end) {
        const size_t middle = begin + (end - begin) / 2;
        const ScriptRange *entry = &script_extensions[middle];
        if (scalar < entry->first)
            end = middle;
        else if (scalar > entry->last)
            begin = middle + 1;
        else
            return entry->candidates;
    }
    if (primary == SCRIPT_COMMON || primary == SCRIPT_INHERITED) return all_scripts;
    ScriptSet set = {{0}};
    set.words[primary / 64] = UINT64_C(1) << (primary % 64);
    return set;
}

static ScriptCluster collect_cluster(NtByteView input, NtByteSpan span)
{
    ScriptCluster cluster = {span, all_scripts, SCRIPT_COUNT, SCRIPT_COUNT};
    bool unknown = false;
    for (size_t cursor = span.begin; cursor < span.end;) {
        uint32_t scalar = 0;
        (void)nt_utf8_decode(input, &cursor, &scalar);
        const uint16_t primary = nt_unicode_property(script_primary, NT_ARRAY_COUNT(script_primary),
                                                      scalar, SCRIPT_UNKNOWN);
        if (cluster.script == SCRIPT_COUNT && strong_script(primary)) cluster.script = primary;
        if (primary == SCRIPT_UNKNOWN) unknown = true;
        const ScriptSet candidates = script_candidates(scalar, primary);
        for (size_t i = 0; i < NT_ARRAY_COUNT(candidates.words); ++i)
            cluster.candidates.words[i] &= candidates.words[i];
    }
    if (cluster.script == SCRIPT_COUNT && unknown) cluster.script = SCRIPT_UNKNOWN;
    return cluster;
}

static uint16_t fallback_script(ScriptSet candidates)
{
    bool unrestricted = true;
    for (size_t i = 0; i < NT_ARRAY_COUNT(candidates.words); ++i)
        if (candidates.words[i] != UINT64_MAX) unrestricted = false;
    if (unrestricted) return SCRIPT_COMMON;
    for (uint16_t code = 0; code < SCRIPT_COUNT; ++code)
        if (strong_script(code) && matches(candidates, code)) return code;
    return SCRIPT_COMMON;
}

static size_t assign_scripts(ScriptCluster *clusters, size_t count)
{
    uint16_t following = SCRIPT_COUNT;
    for (size_t i = count; i > 0;) {
        ScriptCluster *cluster = &clusters[--i];
        cluster->following = following;
        if (strong_script(cluster->script)) following = cluster->script;
    }
    uint16_t prior = SCRIPT_COUNT;
    size_t runs = 0;
    for (size_t i = 0; i < count; ++i) {
        ScriptCluster *cluster = &clusters[i];
        if (cluster->script == SCRIPT_COUNT) {
            if (strong_script(prior) && matches(cluster->candidates, prior))
                cluster->script = prior;
            else if (matches(cluster->candidates, cluster->following))
                cluster->script = cluster->following;
            else
                cluster->script = fallback_script(cluster->candidates);
        }
        if (prior != cluster->script) ++runs;
        prior = cluster->script;
    }
    return runs;
}

static void write_runs(const ScriptCluster *clusters, size_t count, NtScriptRun *runs)
{
    size_t next = 0;
    for (size_t i = 0; i < count;) {
        const size_t begin = i;
        const uint16_t script = clusters[i++].script;
        while (i < count && clusters[i].script == script) ++i;
        NtScriptRun *run = &runs[next++];
        run->source = (NtByteSpan){clusters[begin].source.begin, clusters[i - 1].source.end};
        memcpy(run->script, script_tags[script], sizeof(run->script));
    }
}

NtResult nt_script_runs(const NtAllocator *allocator, NtByteView input,
                         NtScriptRun *runs, size_t capacity, size_t *count)
{
    NtAllocator resolved;
    if (!count || (!runs && capacity) || !nt_allocator_get(allocator, &resolved))
        return NT_RESULT_INVALID_ARGUMENT;
    NtGraphemeIterator iterator;
    NtResult result = nt_grapheme_iterator_init(&iterator, input);
    if (result != NT_RESULT_OK) return result;
    const NtGraphemeIterator initial = iterator;
    NtByteSpan span;
    bool available;
    size_t length = 0;
    while ((result = nt_grapheme_iterator_next(&iterator, &span, &available)) == NT_RESULT_OK &&
           available) ++length;
    if (result != NT_RESULT_OK) return result;
    if (length > SIZE_MAX / sizeof(ScriptCluster)) return NT_RESULT_OUT_OF_RANGE;

    const size_t bytes = length * sizeof(ScriptCluster);
    ScriptCluster *clusters = bytes ? nt_allocate(&resolved, bytes, NT_ALIGNMENT(ScriptCluster)) : NULL;
    if (bytes && !clusters) return NT_RESULT_ALLOCATION_FAILED;
    iterator = initial;
    for (size_t i = 0; i < length; ++i) {
        (void)nt_grapheme_iterator_next(&iterator, &span, &available);
        clusters[i] = collect_cluster(input, span);
    }
    const size_t required = assign_scripts(clusters, length);
    if (runs && capacity >= required)
        write_runs(clusters, length, runs);
    else if (!nt_is_count_query(runs, capacity))
        result = NT_RESULT_INSUFFICIENT_BUFFER;
    nt_release(&resolved, clusters, bytes, NT_ALIGNMENT(ScriptCluster));
    *count = required;
    return result;
}
