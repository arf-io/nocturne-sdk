#ifndef NOCTURNE_TIMELINE_H
#define NOCTURNE_TIMELINE_H

#include <nocturne/animation.h>
#include <nocturne/base.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A nonnegative rational rate. Zero initialization gives unit rate.
 * A zero numerator with positive denominator freezes source time. */
typedef struct NtTrackRate { uint32_t numerator, denominator; } NtTrackRate;
NtResult nt_track_rate_limit(NtTrackRate requested, NtTrackRate minimum,
                             NtTrackRate maximum, NtTrackRate *out);

typedef enum NtTrackEditKind { NT_TRACK_DWELL, NT_TRACK_SKIP } NtTrackEditKind;
typedef struct NtTrackEdit {
    NtTrackEditKind kind;
    NtTimeUs at_us, length_us;   /* Source time and positive length. */
} NtTrackEdit;
typedef struct NtTrackTime {
    NtTrackRate rate;
    const NtTrackEdit *edits;
    size_t count;
    NtTimeUs quantum_us;         /* Zero disables quantization. */
} NtTrackTime;
/* Scale time, round down to the clock grid, then apply ordered source edits.
 * NULL timing is the identity. Edits are borrowed for the call. */
NtResult nt_track_time(const NtTrackTime *timing, NtTimeUs elapsed_us, NtTimeUs *out);

typedef struct NtRandomTrack {
    double from, to;
    NtTimeUs interval_us;
    uint64_t seed, stream;
    bool smooth;
} NtRandomTrack;
NtResult nt_random_track_sample(const NtRandomTrack *track, NtTimeUs at_us, double *out);

typedef struct NtTrackSchedule {
    NtTimeUs stagger_us, jitter_us, duration_variation_us;
    uint64_t batch_size, seed;    /* Zero batch size schedules one item per batch. */
} NtTrackSchedule;
typedef struct NtTrackSlot { NtTimeUs delay_us, span_us; } NtTrackSlot;
/* The caller supplies a rank (or index) and a separately stable item identity. */
NtResult nt_track_schedule(const NtTrackSchedule *schedule, uint64_t rank, uint64_t item,
                           NtTimeUs span_us, NtTrackSlot *out);

typedef enum NtTrackKind { NT_TRACK_TWEEN, NT_TRACK_KEYFRAMES, NT_TRACK_RANDOM } NtTrackKind;
typedef struct NtTimelineClip {
    uint64_t channel;
    NtTimeUs span_us;            /* Clip length on the timeline, before source mapping. */
    NtTrackTime timing;
    NtTrackKind kind;
    union {
        NtTween tween;
        NtKeyframeTrack keyframes;
        NtRandomTrack random;
    } source;
} NtTimelineClip;

typedef enum NtTimelineKind { NT_TIMELINE_CLIP, NT_TIMELINE_SEQUENCE, NT_TIMELINE_PARALLEL } NtTimelineKind;
typedef struct NtTimelineNode NtTimelineNode;
struct NtTimelineNode {
    NtTimelineKind kind;
    NtTimeUs delay_us;           /* Offset from the start of this node's assigned slot. */
    union {
        NtTimelineClip clip;
        struct { const NtTimelineNode *children; size_t count; } group;
    } data;
};

typedef struct NtTimelineOptions { NtTimeUs delay_us; NtRepeat repeat; bool reverse; } NtTimelineOptions;
typedef struct NtTimeline NtTimeline;
typedef struct NtTimelineSample {
    double value;
    NtTimeUs playhead_us, source_us;
    uint64_t iteration;
    NtPhase phase, clip_phase;
    int direction;
} NtTimelineSample;

/* Own an immutable copy. A group has at least one child. Positive spans
 * for one channel must be disjoint. An instantaneous clip can occur at a
 * boundary. At equal starts a positive span takes precedence, otherwise
 * the last instantaneous clip in declaration order wins.
 * NULL options select a forward, once-only timeline without outer delay. */
NtResult nt_timeline_create(const NtAllocator *allocator, const NtTimelineNode *root,
                            const NtTimelineOptions *options, NtTimeline **out);
void nt_timeline_destroy(NtTimeline *timeline);
NtTimeUs nt_timeline_duration(const NtTimeline *timeline); /* One leg, excluding outer delay. */
size_t nt_timeline_channel_count(const NtTimeline *timeline);
NtResult nt_timeline_channel_at(const NtTimeline *timeline, size_t index, uint64_t *out);
/* Pure sampling. Resolve a channel using the oriented timeline playhead.
 * Before the first scheduled start hold the winner at that coordinate.
 * After any clip hold its endpoint until the next one starts. */
NtResult nt_timeline_sample(const NtTimeline *timeline, uint64_t channel,
                            NtTimeUs elapsed_us, NtTimelineSample *out);

#ifdef __cplusplus
}
#endif
#endif
