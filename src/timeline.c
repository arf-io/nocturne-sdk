#include "allocator.h"
#include "animation_internal.h"
#include "timeline_internal.h"

#include <string.h>

typedef struct CompiledClip {
    NtTimelineClip spec;
    NtTimeUs start, end;
    size_t order;
} CompiledClip;

typedef struct TimelineChannel {
    uint64_t id;
    size_t first, end;
} TimelineChannel;

struct NtTimeline {
    NtAllocator allocator;
    NtTimelineOptions options;
    NtTimeUs duration;
    CompiledClip *clips;
    size_t count;
    TimelineChannel *channels;
    size_t channel_count;
};

NT_DECLARE_ALIGNMENT(NtTimeline);
NT_DECLARE_ALIGNMENT(CompiledClip);
NT_DECLARE_ALIGNMENT(TimelineChannel);
NT_DECLARE_ALIGNMENT(NtKeyframe);
NT_DECLARE_ALIGNMENT(NtTrackEdit);

static NtResult source_value(const NtTimelineClip *clip, NtTimeUs source_time, double *out)
{
    if (clip->kind == NT_TRACK_RANDOM)
        return nt_random_track_sample(&clip->source.random,source_time,out);
    NtAnimationSample sample;
    const NtStatus result = clip->kind == NT_TRACK_TWEEN ?
        nt_tween_sample(&clip->source.tween,source_time,&sample) :
        nt_keyframes_sample(&clip->source.keyframes,source_time,&sample);
    if (result == NT_INVALID_ARGUMENT) return NT_RESULT_INVALID_ARGUMENT;
    if (result != NT_OK) return NT_RESULT_OUT_OF_RANGE;
    *out = sample.value;
    return NT_RESULT_OK;
}

static NtResult validate_clip(const NtTimelineClip *clip)
{
    if (clip->kind < NT_TRACK_TWEEN || clip->kind > NT_TRACK_RANDOM)
        return NT_RESULT_INVALID_ARGUMENT;
    if (clip->kind == NT_TRACK_KEYFRAMES &&
        clip->source.keyframes.count > SIZE_MAX / sizeof(NtKeyframe)) return NT_RESULT_OUT_OF_RANGE;
    NtTimeUs at;
    NtResult result = nt_track_time(&clip->timing,clip->span_us,&at);
    double value;
    if (result == NT_RESULT_OK) result = source_value(clip,at,&value);
    return result;
}

static NtResult copy_clip(const NtAllocator *allocator, CompiledClip *target, const NtTimelineClip *clip,
                           NtTimeUs start, NtTimeUs end, size_t order)
{
    *target = (CompiledClip){*clip,start,end,order};
    target->spec.timing.edits = NULL;
    if (clip->kind == NT_TRACK_KEYFRAMES) target->spec.source.keyframes.keys = NULL;
    if (clip->timing.count) {
        const size_t bytes = clip->timing.count * sizeof(NtTrackEdit);
        NtTrackEdit *edits = nt_allocate(allocator,bytes,NT_ALIGNMENT(NtTrackEdit));
        if (!edits) return NT_RESULT_ALLOCATION_FAILED;
        memcpy(edits,clip->timing.edits,bytes);
        target->spec.timing.edits = edits;
    }
    if (clip->kind == NT_TRACK_KEYFRAMES) {
        const size_t bytes = clip->source.keyframes.count * sizeof(NtKeyframe);
        NtKeyframe *keys = nt_allocate(allocator,bytes,NT_ALIGNMENT(NtKeyframe));
        if (!keys) return NT_RESULT_ALLOCATION_FAILED;
        memcpy(keys,clip->source.keyframes.keys,bytes);
        target->spec.source.keyframes.keys = keys;
    }
    return NT_RESULT_OK;
}

/* The same walk measures a validated description and then copies it. No
 * pointer to a group or caller-owned source survives compilation. */
static NtResult walk(const NtTimelineNode *node, NtTimeUs base, unsigned depth,
                     NtTimeline *timeline, size_t *count, NtTimeUs *end)
{
    if (depth == 128 || base > UINT64_MAX - node->delay_us) return NT_RESULT_OUT_OF_RANGE;
    const NtTimeUs start = base + node->delay_us;
    if (node->kind == NT_TIMELINE_CLIP) {
        const NtTimelineClip *clip = &node->data.clip;
        if (*count == SIZE_MAX / sizeof(CompiledClip) || start > UINT64_MAX - clip->span_us)
            return NT_RESULT_OUT_OF_RANGE;
        *end = start + clip->span_us;
        const NtResult result = timeline ?
            copy_clip(&timeline->allocator,&timeline->clips[*count],clip,start,*end,*count) :
            validate_clip(clip);
        ++*count;
        return result;
    }

    if ((node->kind != NT_TIMELINE_SEQUENCE && node->kind != NT_TIMELINE_PARALLEL) ||
        !node->data.group.count || !node->data.group.children) return NT_RESULT_INVALID_ARGUMENT;
    if (node->data.group.count > SIZE_MAX / sizeof(NtTimelineNode)) return NT_RESULT_OUT_OF_RANGE;
    const bool sequence = node->kind == NT_TIMELINE_SEQUENCE;
    NtTimeUs furthest = start;
    for (size_t i = 0; i < node->data.group.count; ++i) {
        NtTimeUs child_end;
        const NtResult result = walk(&node->data.group.children[i],sequence ? furthest : start,
                                     depth+1,timeline,count,&child_end);
        if (result != NT_RESULT_OK) return result;
        if (child_end > furthest) furthest = child_end;
    }
    *end = furthest;
    return NT_RESULT_OK;
}

static int compare_clip(const void *a, const void *b)
{
    const CompiledClip *first = a, *second = b;
    if (first->spec.channel != second->spec.channel)
        return first->spec.channel < second->spec.channel ? -1 : 1;
    if (first->start != second->start) return first->start < second->start ? -1 : 1;
    const bool first_instant = first->start == first->end;
    const bool second_instant = second->start == second->end;
    if (first_instant != second_instant) return first_instant ? -1 : 1;
    return first->order < second->order ? -1 : first->order > second->order;
}

static NtResult index_channels(NtTimeline *timeline)
{
    CompiledClip *clips = timeline->clips;
    qsort(clips,timeline->count,sizeof(*clips),compare_clip);
    size_t count = 1;
    for (size_t i = 1; i < timeline->count; ++i) {
        if (clips[i-1].spec.channel != clips[i].spec.channel) ++count;
        else if (clips[i-1].end > clips[i].start) return NT_RESULT_INVALID_ARGUMENT;
    }
    TimelineChannel *channels = nt_allocate(&timeline->allocator,count * sizeof(*channels),
                                             NT_ALIGNMENT(TimelineChannel));
    if (!channels) return NT_RESULT_ALLOCATION_FAILED;
    timeline->channels = channels;
    timeline->channel_count = count;
    size_t first = 0, channel = 0;
    for (size_t i = 1; i < timeline->count; ++i) {
        if (clips[i-1].spec.channel == clips[i].spec.channel) continue;
        channels[channel++] = (TimelineChannel){clips[first].spec.channel,first,i};
        first = i;
    }
    channels[channel] = (TimelineChannel){clips[first].spec.channel,first,timeline->count};
    return NT_RESULT_OK;
}

void nt_timeline_destroy(NtTimeline *timeline)
{
    if (!timeline) return;
    const NtAllocator allocator = timeline->allocator;
    if (timeline->clips) {
        for (size_t i = 0; i < timeline->count; ++i) {
            const NtTimelineClip *clip = &timeline->clips[i].spec;
            nt_release(&allocator,(void *)clip->timing.edits,clip->timing.count * sizeof(NtTrackEdit),
                       NT_ALIGNMENT(NtTrackEdit));
            if (clip->kind == NT_TRACK_KEYFRAMES)
                nt_release(&allocator,(void *)clip->source.keyframes.keys,
                           clip->source.keyframes.count * sizeof(NtKeyframe),NT_ALIGNMENT(NtKeyframe));
        }
    }
    nt_release(&allocator,timeline->clips,timeline->count * sizeof(CompiledClip),NT_ALIGNMENT(CompiledClip));
    nt_release(&allocator,timeline->channels,timeline->channel_count * sizeof(TimelineChannel),
               NT_ALIGNMENT(TimelineChannel));
    nt_release(&allocator,timeline,sizeof(*timeline),NT_ALIGNMENT(NtTimeline));
}

NtResult nt_timeline_create(const NtAllocator *provided, const NtTimelineNode *root,
                            const NtTimelineOptions *options, NtTimeline **out)
{
    NtAllocator allocator;
    if (!root || !out || !nt_allocator_get(provided,&allocator)) return NT_RESULT_INVALID_ARGUMENT;
    const NtTimelineOptions policy = options ? *options : (NtTimelineOptions){0};
    if (policy.repeat < NT_ONCE || policy.repeat > NT_PING_PONG) return NT_RESULT_INVALID_ARGUMENT;
    size_t count = 0;
    NtTimeUs duration;
    NtResult result = walk(root,0,0,NULL,&count,&duration);
    if (result != NT_RESULT_OK) return result;
    if (!duration && policy.repeat != NT_ONCE) return NT_RESULT_INVALID_ARGUMENT;
    if (duration > UINT64_MAX - policy.delay_us) return NT_RESULT_OUT_OF_RANGE;

    NtTimeline *timeline = nt_allocate(&allocator,sizeof(*timeline),NT_ALIGNMENT(NtTimeline));
    if (!timeline) return NT_RESULT_ALLOCATION_FAILED;
    *timeline = (NtTimeline){.allocator=allocator,.options=policy,.duration=duration,.count=count};
    timeline->clips = nt_allocate(&allocator,count * sizeof(CompiledClip),NT_ALIGNMENT(CompiledClip));
    if (!timeline->clips) {
        nt_timeline_destroy(timeline);
        return NT_RESULT_ALLOCATION_FAILED;
    }
    for (size_t i = 0; i < count; ++i) timeline->clips[i] = (CompiledClip){0};
    size_t used = 0;
    result = walk(root,0,0,timeline,&used,&duration);
    if (result == NT_RESULT_OK) result = index_channels(timeline);
    if (result == NT_RESULT_OK) *out = timeline;
    else nt_timeline_destroy(timeline);
    return result;
}

NtTimeUs nt_timeline_duration(const NtTimeline *timeline)
{
    return timeline ? timeline->duration : 0;
}

size_t nt_timeline_channel_count(const NtTimeline *timeline)
{
    return timeline ? timeline->channel_count : 0;
}

NtResult nt_timeline_channel_at(const NtTimeline *timeline, size_t index, uint64_t *out)
{
    if (!timeline || !out) return NT_RESULT_INVALID_ARGUMENT;
    if (index >= timeline->channel_count) return NT_RESULT_OUT_OF_RANGE;
    *out = timeline->channels[index].id;
    return NT_RESULT_OK;
}

static const TimelineChannel *find_channel(const NtTimeline *timeline, uint64_t id)
{
    size_t first = 0, end = timeline->channel_count;
    while (first < end) {
        const size_t middle = first + (end - first) / 2;
        const TimelineChannel *channel = &timeline->channels[middle];
        if (channel->id == id) return channel;
        if (channel->id < id) first = middle + 1;
        else end = middle;
    }
    return NULL;
}

static const CompiledClip *find_clip(const NtTimeline *timeline, const TimelineChannel *channel,
                                     NtTimeUs playhead)
{
    size_t first = channel->first, end = channel->end;
    if (playhead < timeline->clips[first].start) playhead = timeline->clips[first].start;
    while (first + 1 < end) {
        const size_t middle = first + (end - first) / 2;
        if (timeline->clips[middle].start <= playhead) first = middle;
        else end = middle;
    }
    return &timeline->clips[first];
}

NtResult nt_timeline_sample(const NtTimeline *timeline, uint64_t channel,
                            NtTimeUs elapsed_us, NtTimelineSample *out)
{
    if (!timeline || !out) return NT_RESULT_INVALID_ARGUMENT;
    const TimelineChannel *binding = find_channel(timeline,channel);
    if (!binding) return NT_RESULT_OUT_OF_RANGE;
    const NtTimelineOptions policy = timeline->options;
    NtAnimationSample position;
    NtTimeUs at = nt_animation_timing(elapsed_us,policy.delay_us,timeline->duration,policy.repeat,&position);
    if (policy.reverse) at = timeline->duration - at;
    const CompiledClip *clip = find_clip(timeline,binding,at);
    const NtPhase phase = at < clip->start ? NT_DELAYED : at >= clip->end ? NT_FINISHED : NT_RUNNING;
    const NtTimeUs elapsed = phase == NT_DELAYED ? 0 :
        phase == NT_FINISHED ? clip->spec.span_us : at - clip->start;
    NtTimelineSample sample = {.playhead_us=at,.iteration=position.iteration,.phase=position.phase,
        .clip_phase=phase,.direction=policy.reverse ? -position.direction : position.direction};
    NtResult result = nt_track_time_validated(&clip->spec.timing,elapsed,&sample.source_us);
    if (result == NT_RESULT_OK) result = source_value(&clip->spec,sample.source_us,&sample.value);
    if (result == NT_RESULT_OK) *out = sample;
    return result;
}
