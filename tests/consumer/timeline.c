#include <nocturne/timeline.h>

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr,"timeline line %d: %s\n",__LINE__,#condition); exit(EXIT_FAILURE); \
} } while (0)

static NtTimelineNode tween(uint64_t channel, NtTimeUs delay, NtTimeUs span, double from, double to)
{
    return (NtTimelineNode){.kind=NT_TIMELINE_CLIP,.delay_us=delay,.data.clip={
        .channel=channel,.span_us=span,.source.tween={from,to,span,0,NT_LINEAR,NT_ONCE}}};
}

static NtTimelineSample sample(const NtTimeline *timeline, uint64_t channel, NtTimeUs elapsed)
{
    NtTimelineSample out;
    CHECK(nt_timeline_sample(timeline,channel,elapsed,&out) == NT_RESULT_OK);
    return out;
}

static void at(const NtTimeline *timeline, uint64_t channel, NtTimeUs elapsed,
               NtTimeUs playhead, NtTimeUs source, double value, NtPhase clip_phase)
{
    const NtTimelineSample found = sample(timeline,channel,elapsed);
    CHECK(found.playhead_us == playhead && found.source_us == source);
    CHECK(fabs(found.value-value) <= 1e-12 && found.clip_phase == clip_phase);
}

static void composed_playback(void)
{
    NtKeyframe keys[] = {{0,100,NT_LINEAR},{6,160,NT_LINEAR}};
    NtTrackEdit hold[] = {{NT_TRACK_DWELL,0,1}};
    NtTimelineNode entries[] = {tween(9,0,10,0,10),tween(9,0,0,10,40),tween(9,2,5,50,75)};
    entries[2].data.clip.timing = (NtTrackTime){.edits=hold,.count=1,.quantum_us=2};
    const NtTimelineNode children[] = {
        {.kind=NT_TIMELINE_SEQUENCE,.data.group={entries,3}},
        {.kind=NT_TIMELINE_CLIP,.delay_us=3,.data.clip={
            .channel=3,.span_us=9,.kind=NT_TRACK_KEYFRAMES,.source.keyframes={keys,2,0,NT_ONCE}}},
    };
    const NtTimelineNode root = {.kind=NT_TIMELINE_PARALLEL,.data.group={children,2}};
    NtTimeline *timeline;
    CHECK(nt_timeline_create(NULL,&root,&(NtTimelineOptions){5,NT_PING_PONG,false},&timeline) == NT_RESULT_OK);
    keys[1].value = -1234;
    hold[0].length_us = 900;
    entries[0].data.clip.source.tween.to = -999;
    CHECK(nt_timeline_duration(timeline) == 17 && nt_timeline_channel_count(timeline) == 2);
    uint64_t id;
    CHECK(nt_timeline_channel_at(timeline,0,&id) == NT_RESULT_OK && id == 3);
    CHECK(nt_timeline_channel_at(timeline,1,&id) == NT_RESULT_OK && id == 9);
    CHECK(nt_timeline_channel_at(timeline,2,&id) == NT_RESULT_OUT_OF_RANGE && id == 9);
    NtTimelineSample sentinel = {.value=9000,.playhead_us=918};
    CHECK(nt_timeline_sample(timeline,333,5,&sentinel) == NT_RESULT_OUT_OF_RANGE);
    CHECK(sentinel.value == 9000 && sentinel.playhead_us == 918);

    at(timeline,3,4,0,0,100,NT_DELAYED);
    CHECK(sample(timeline,9,4).phase == NT_DELAYED);
    at(timeline,9,5,0,0,0,NT_RUNNING);
    at(timeline,3,11,6,3,130,NT_RUNNING);
    at(timeline,9,14,9,9,9,NT_RUNNING);
    at(timeline,9,15,10,0,40,NT_FINISHED);
    at(timeline,9,16,11,0,40,NT_FINISHED);
    at(timeline,9,17,12,0,50,NT_RUNNING);
    at(timeline,9,19,14,1,55,NT_RUNNING);
    at(timeline,9,20,15,1,55,NT_RUNNING);
    at(timeline,9,22,17,3,65,NT_FINISHED);
    at(timeline,3,22,17,9,160,NT_FINISHED);
    CHECK(sample(timeline,9,22).direction == -1 && sample(timeline,9,22).iteration == 1);
    at(timeline,9,29,10,0,40,NT_FINISHED);
    CHECK(sample(timeline,9,39).direction == 1 && sample(timeline,9,39).iteration == 2);

    NtPlayback player;
    CHECK(nt_playback_init(&player,500) == NT_OK);
    CHECK(nt_playback_seek(&player,501,14) == NT_OK);
    CHECK(nt_playback_pause(&player,503) == NT_OK);
    NtTimeUs elapsed;
    CHECK(nt_playback_elapsed(&player,600,&elapsed) == NT_OK && elapsed == 16);
    at(timeline,9,elapsed,11,0,40,NT_FINISHED);
    CHECK(nt_playback_resume(&player,601) == NT_OK);
    CHECK(nt_playback_cancel(&player,605) == NT_OK);
    CHECK(nt_playback_elapsed(&player,999,&elapsed) == NT_OK && elapsed == 20);
    CHECK(nt_playback_seek(&player,1000,0) == NT_INVALID_ARGUMENT);
    CHECK(nt_playback_resume(&player,1001) == NT_INVALID_ARGUMENT);
    at(timeline,9,elapsed,15,1,55,NT_RUNNING);
    nt_timeline_destroy(timeline);
    puts("sequence, parallel lanes, held gaps, copied source, repeat, seek, pause and cancel");
}

static void ownership_and_limits(void)
{
    NtTimelineNode nodes[] = {tween(8,0,8,0,8),tween(8,7,2,11,22)};
    NtTimelineNode root = {.kind=NT_TIMELINE_PARALLEL,.data.group={nodes,2}};
    NtTimeline *timeline = NULL;
    CHECK(nt_timeline_create(NULL,&root,NULL,&timeline) == NT_RESULT_INVALID_ARGUMENT && !timeline);
    nodes[1].delay_us = 8;
    CHECK(nt_timeline_create(NULL,&root,NULL,&timeline) == NT_RESULT_OK);
    at(timeline,8,8,8,0,11,NT_RUNNING);
    at(timeline,8,11,10,2,22,NT_FINISHED);
    CHECK(sample(timeline,8,11).phase == NT_FINISHED);
    nt_timeline_destroy(timeline);
    nodes[1] = tween(8,5,0,0,29);
    timeline = NULL;
    CHECK(nt_timeline_create(NULL,&root,NULL,&timeline) == NT_RESULT_INVALID_ARGUMENT && !timeline);
    nodes[1].delay_us = 0;
    CHECK(nt_timeline_create(NULL,&root,NULL,&timeline) == NT_RESULT_OK);
    at(timeline,8,0,0,0,0,NT_RUNNING);
    at(timeline,8,1,1,1,1,NT_RUNNING);
    nt_timeline_destroy(timeline);

    nodes[0].delay_us = nodes[1].delay_us = 3;
    CHECK(nt_timeline_create(NULL,&root,NULL,&timeline) == NT_RESULT_OK);
    at(timeline,8,2,2,0,0,NT_DELAYED);
    at(timeline,8,3,3,0,0,NT_RUNNING);
    nt_timeline_destroy(timeline);
    nodes[0] = tween(8,3,0,0,5);
    CHECK(nt_timeline_create(NULL,&root,NULL,&timeline) == NT_RESULT_OK);
    at(timeline,8,2,2,0,29,NT_DELAYED);
    at(timeline,8,3,3,0,29,NT_FINISHED);
    nt_timeline_destroy(timeline);

    nodes[0] = tween(8,0,0,0,5);
    nodes[1] = tween(8,0,0,0,9);
    CHECK(nt_timeline_create(NULL,&root,NULL,&timeline) == NT_RESULT_OK);
    at(timeline,8,0,0,0,9,NT_FINISHED);
    nt_timeline_destroy(timeline);
    timeline = NULL;
    CHECK(nt_timeline_create(NULL,&root,&(NtTimelineOptions){0,NT_LOOP,false},&timeline) ==
          NT_RESULT_INVALID_ARGUMENT && !timeline);

    root = tween(8,0,UINT64_MAX,10,20);
    CHECK(nt_timeline_create(NULL,&root,&(NtTimelineOptions){0,NT_LOOP,true},&timeline) == NT_RESULT_OK);
    const NtTimelineSample wrapped = sample(timeline,8,UINT64_MAX);
    CHECK(wrapped.playhead_us == UINT64_MAX && wrapped.iteration == 1 && wrapped.direction == -1);
    CHECK(wrapped.value == 20 && wrapped.phase == NT_RUNNING);
    nt_timeline_destroy(timeline);
    timeline = NULL;
    CHECK(nt_timeline_create(NULL,&root,&(NtTimelineOptions){1,NT_ONCE,false},&timeline) ==
          NT_RESULT_OUT_OF_RANGE && !timeline);
    root.delay_us = 1;
    CHECK(nt_timeline_create(NULL,&root,NULL,&timeline) == NT_RESULT_OUT_OF_RANGE && !timeline);

    root = (NtTimelineNode){.kind=NT_TIMELINE_SEQUENCE,.data.group={&root,1}};
    CHECK(nt_timeline_create(NULL,&root,NULL,&timeline) == NT_RESULT_OUT_OF_RANGE && !timeline);
    puts("channel ownership, same-start boundaries, zero spans, reversed loops and overflow");
}

static void clock_modifiers(void)
{
    const NtTrackEdit edits[] = {{NT_TRACK_DWELL,6,4},{NT_TRACK_SKIP,6,3},{NT_TRACK_DWELL,12,2}};
    NtTrackTime timing = {{3,2},edits,3,2};
    const NtTimeUs mapped[] = {0,0,2,4,6,6,6,9,11,11,12,13,15};
    for (size_t i=0; i<sizeof(mapped)/sizeof(*mapped); ++i) {
        NtTimeUs out;
        CHECK(nt_track_time(&timing,i,&out) == NT_RESULT_OK && out == mapped[i]);
    }
    NtTrackRate rate = {1,1};
    CHECK(nt_track_rate_limit((NtTrackRate){1000,1001},(NtTrackRate){1,1},(NtTrackRate){3,2},&rate) ==
          NT_RESULT_OK && rate.numerator == 1 && rate.denominator == 1);
    CHECK(nt_track_rate_limit((NtTrackRate){UINT32_MAX,1},(NtTrackRate){1,1},(NtTrackRate){3,2},&rate) ==
          NT_RESULT_OK && rate.numerator == 3 && rate.denominator == 2);
    CHECK(nt_track_rate_limit((NtTrackRate){1,1},(NtTrackRate){2,1},(NtTrackRate){1,1},&rate) ==
          NT_RESULT_INVALID_ARGUMENT && rate.numerator == 3);

    NtTimeUs out = 100;
    timing = (NtTrackTime){.rate={2,1}};
    CHECK(nt_track_time(&timing,UINT64_MAX,&out) == NT_RESULT_OUT_OF_RANGE && out == 100);
    timing.rate = (NtTrackRate){UINT32_MAX,UINT32_MAX};
    CHECK(nt_track_time(&timing,UINT64_MAX,&out) == NT_RESULT_OK && out == UINT64_MAX);
    timing.rate = (NtTrackRate){0,1};
    CHECK(nt_track_time(&timing,UINT64_MAX,&out) == NT_RESULT_OK && out == 0);
    const NtTrackEdit wrong[] = {{NT_TRACK_SKIP,100,10},{NT_TRACK_DWELL,109,1}};
    timing = (NtTrackTime){.edits=wrong,.count=2};
    CHECK(nt_track_time(&timing,0,&out) == NT_RESULT_INVALID_ARGUMENT && out == 0);
    const NtTrackEdit overflow[] = {{NT_TRACK_SKIP,UINT64_MAX,1}};
    timing = (NtTrackTime){.edits=overflow,.count=1};
    CHECK(nt_track_time(&timing,0,&out) == NT_RESULT_OUT_OF_RANGE && out == 0);
    puts("bounded exact rates, quantization, ordered dwells and skips");
}

static void wide_step_end(void)
{
    const NtTimeUs span = (UINT64_C(1) << 53) + 1;
    const NtKeyframe keys[] = {{0,2,NT_STEP_END},{span,37,NT_LINEAR}};
    NtTimelineNode sources[] = {tween(3,0,span,2,37),{
        .kind=NT_TIMELINE_CLIP,.data.clip={.channel=5,.span_us=span,
            .kind=NT_TRACK_KEYFRAMES,.source.keyframes={keys,2,0,NT_ONCE}}}};
    sources[0].data.clip.source.tween.easing = NT_STEP_END;
    const NtTimelineNode root = {.kind=NT_TIMELINE_PARALLEL,.data.group={sources,2}};
    NtTimeline *timeline;
    CHECK(nt_timeline_create(NULL,&root,NULL,&timeline) == NT_RESULT_OK);
    at(timeline,3,span-1,span-1,span-1,2,NT_RUNNING);
    at(timeline,5,span-1,span-1,span-1,2,NT_RUNNING);
    at(timeline,3,span,span,span,37,NT_FINISHED);
    at(timeline,5,span,span,span,37,NT_FINISHED);
    NtAnimationSample direct;
    CHECK(nt_tween_sample(&sources[0].data.clip.source.tween,span-1,&direct) == NT_OK);
    CHECK(direct.progress < 1 && direct.phase == NT_RUNNING);
    nt_timeline_destroy(timeline);
    puts("wide clock step-end changes on the exact integer boundary");
}

static void schedule_and_noise(void)
{
    NtTrackSchedule schedule = {100,0,0,3,22};
    NtTrackSlot slot;
    CHECK(nt_track_schedule(&schedule,2,99,11,&slot) == NT_RESULT_OK && slot.delay_us == 0 && slot.span_us == 11);
    CHECK(nt_track_schedule(&schedule,3,60,11,&slot) == NT_RESULT_OK && slot.delay_us == 100);
    schedule.jitter_us = 13;
    schedule.duration_variation_us = 7;
    NtTrackSlot slots[128];
    bool differing = false;
    for (uint64_t i=0; i<128; ++i) {
        CHECK(nt_track_schedule(&schedule,i,4*i,15,&slots[i]) == NT_RESULT_OK);
        CHECK(slots[i].delay_us >= i/3*100 && slots[i].delay_us <= i/3*100 + 13);
        CHECK(slots[i].span_us >= 8 && slots[i].span_us <= 22);
        differing |= slots[i].span_us != 15;
    }
    CHECK(differing);
    for (uint64_t i=128; i>0; --i) {
        CHECK(nt_track_schedule(&schedule,i-1,4*(i-1),15,&slot) == NT_RESULT_OK);
        CHECK(slot.delay_us == slots[i-1].delay_us && slot.span_us == slots[i-1].span_us);
    }
    const NtTrackSlot saved = slot;
    CHECK(nt_track_schedule(&schedule,UINT64_MAX,0,7,&slot) == NT_RESULT_OUT_OF_RANGE);
    CHECK(slot.delay_us == saved.delay_us && slot.span_us == saved.span_us);

    NtRandomTrack track = {-2,6,8,18,44,false};
    NtRandomTrack smooth = track;
    smooth.smooth = true;
    double stepped[130], smoothed[129];
    for (uint64_t i=0; i<130; ++i) {
        CHECK(nt_random_track_sample(&track,8*i,&stepped[i]) == NT_RESULT_OK);
        CHECK(stepped[i] >= -2 && stepped[i] <= 6);
    }
    for (uint64_t i=0; i<129; ++i) {
        double at_start, at_end_of_step;
        CHECK(nt_random_track_sample(&track,8*i+7,&at_end_of_step) == NT_RESULT_OK && at_end_of_step == stepped[i]);
        CHECK(nt_random_track_sample(&smooth,8*i,&at_start) == NT_RESULT_OK && at_start == stepped[i]);
        CHECK(nt_random_track_sample(&smooth,8*i+4,&smoothed[i]) == NT_RESULT_OK);
        CHECK(fabs(smoothed[i]-(stepped[i]+stepped[i+1])/2) <= 1e-12);
    }
    for (uint64_t i=129; i>0; --i) {
        double value;
        CHECK(nt_random_track_sample(&smooth,8*(i-1)+4,&value) == NT_RESULT_OK && value == smoothed[i-1]);
    }
    double ceiling;
    smooth.interval_us = 1;
    smooth.from = -DBL_MAX;
    smooth.to = DBL_MAX;
    CHECK(nt_random_track_sample(&smooth,UINT64_MAX,&ceiling) == NT_RESULT_OK && isfinite(ceiling));

    NtTimelineNode noise = {.kind=NT_TIMELINE_CLIP,.delay_us=slot.delay_us,.data.clip={
        .channel=UINT64_MAX,.span_us=31,.kind=NT_TRACK_RANDOM,.source.random=track}};
    NtTimeline *timeline;
    CHECK(nt_timeline_create(NULL,&noise,NULL,&timeline) == NT_RESULT_OK);
    at(timeline,UINT64_MAX,slot.delay_us+17,slot.delay_us+17,17,stepped[2],NT_RUNNING);
    nt_timeline_destroy(timeline);
    puts("rank and batch stagger, deterministic jitter and duration, random and smooth random");
}

typedef struct AllocationLog { size_t calls, live, refuse; } AllocationLog;

static void *allocate(void *context, size_t bytes, size_t alignment)
{
    AllocationLog *log = context;
    CHECK(bytes && alignment);
    if (++log->calls == log->refuse) return NULL;
    void *value = malloc(bytes);
    CHECK(value);
    ++log->live;
    return value;
}

static void release(void *context, void *address, size_t bytes, size_t alignment)
{
    AllocationLog *log = context;
    CHECK(log->live && address && bytes && alignment);
    --log->live;
    free(address);
}

static void refusal_is_atomic(void)
{
    const NtTrackEdit hold = {NT_TRACK_DWELL,2,1};
    const NtKeyframe keys[] = {{0,1,NT_LINEAR},{10,2,NT_LINEAR}};
    const NtTimelineNode node = {.kind=NT_TIMELINE_CLIP,.data.clip={
        .span_us=11,.timing={.edits=&hold,.count=1},.kind=NT_TRACK_KEYFRAMES,
        .source.keyframes={keys,2,0,NT_ONCE}}};
    NtTimeline *prior;
    CHECK(nt_timeline_create(NULL,&node,NULL,&prior) == NT_RESULT_OK);
    size_t failures = 0;
    for (size_t refuse=1; ; ++refuse) {
        AllocationLog log = {.refuse=refuse};
        const NtAllocator allocator = {&log,allocate,release};
        NtTimeline *out = prior;
        const NtResult status = nt_timeline_create(&allocator,&node,NULL,&out);
        if (status == NT_RESULT_OK) {
            CHECK(out != prior && log.live);
            nt_timeline_destroy(out);
            CHECK(!log.live);
            break;
        }
        CHECK(status == NT_RESULT_ALLOCATION_FAILED && out == prior && log.live == 0);
        ++failures;
        CHECK(failures < 32);
    }
    CHECK(failures >= 3);
    at(prior,0,11,11,10,2,NT_FINISHED);
    nt_timeline_destroy(prior);
    puts("copied input and transactional allocation refusal");
}

int main(void)
{
    composed_playback();
    ownership_and_limits();
    clock_modifiers();
    wide_step_end();
    schedule_and_noise();
    refusal_is_atomic();
    return 0;
}
