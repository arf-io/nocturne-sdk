#include "animation_internal.h"
#include "seed_internal.h"
#include "timeline_internal.h"

#include <math.h>

static NtTrackRate rate_default(NtTrackRate rate)
{
    return !rate.numerator && !rate.denominator ? (NtTrackRate){1,1} : rate;
}

static bool rate_less(NtTrackRate a, NtTrackRate b)
{
    return (uint64_t)a.numerator * b.denominator < (uint64_t)b.numerator * a.denominator;
}

NtResult nt_track_rate_limit(NtTrackRate requested, NtTrackRate minimum,
                             NtTrackRate maximum, NtTrackRate *out)
{
    requested = rate_default(requested);
    minimum = rate_default(minimum);
    maximum = rate_default(maximum);
    if (!out || !requested.denominator || !minimum.denominator || !maximum.denominator ||
        rate_less(maximum,minimum)) return NT_RESULT_INVALID_ARGUMENT;
    if (rate_less(requested,minimum)) requested = minimum;
    else if (rate_less(maximum,requested)) requested = maximum;
    *out = requested;
    return NT_RESULT_OK;
}

static NtResult scale_time(NtTimeUs time, NtTrackRate rate, NtTimeUs *out)
{
    rate = rate_default(rate);
    if (!rate.numerator) {
        *out = 0;
        return NT_RESULT_OK;
    }
    const NtTimeUs whole = time / rate.denominator;
    const NtTimeUs tail = (time % rate.denominator) * rate.numerator / rate.denominator;
    if (whole > (UINT64_MAX - tail) / rate.numerator) return NT_RESULT_OUT_OF_RANGE;
    *out = whole * rate.numerator + tail;
    return NT_RESULT_OK;
}

NtResult nt_track_time_validated(const NtTrackTime *timing, NtTimeUs elapsed, NtTimeUs *out)
{
    NtTimeUs budget;
    const NtResult result = scale_time(elapsed,timing->rate,&budget);
    if (result != NT_RESULT_OK) return result;
    if (timing->quantum_us) budget -= budget % timing->quantum_us;
    NtTimeUs source = 0;
    for (size_t i = 0; i < timing->count; ++i) {
        const NtTrackEdit edit = timing->edits[i];
        const NtTimeUs distance = edit.at_us - source;
        if (budget < distance) break;
        budget -= distance;
        source = edit.at_us;
        if (edit.kind == NT_TRACK_SKIP) {
            source += edit.length_us;
        } else if (budget < edit.length_us) {
            budget = 0;
            break;
        } else {
            budget -= edit.length_us;
        }
    }
    if (source > UINT64_MAX - budget) return NT_RESULT_OUT_OF_RANGE;
    *out = source + budget;
    return NT_RESULT_OK;
}

NtResult nt_track_time(const NtTrackTime *timing, NtTimeUs elapsed_us, NtTimeUs *out)
{
    if (!out) return NT_RESULT_INVALID_ARGUMENT;
    if (!timing) {
        *out = elapsed_us;
        return NT_RESULT_OK;
    }
    if (!rate_default(timing->rate).denominator || (timing->count && !timing->edits))
        return NT_RESULT_INVALID_ARGUMENT;
    if (timing->count > SIZE_MAX / sizeof(NtTrackEdit)) return NT_RESULT_OUT_OF_RANGE;
    NtTimeUs cursor = 0;
    for (size_t i = 0; i < timing->count; ++i) {
        const NtTrackEdit edit = timing->edits[i];
        if (edit.kind < NT_TRACK_DWELL || edit.kind > NT_TRACK_SKIP ||
            !edit.length_us || edit.at_us < cursor) return NT_RESULT_INVALID_ARGUMENT;
        cursor = edit.at_us;
        if (edit.kind == NT_TRACK_SKIP) {
            if (cursor > UINT64_MAX - edit.length_us) return NT_RESULT_OUT_OF_RANGE;
            cursor += edit.length_us;
        }
    }
    return nt_track_time_validated(timing,elapsed_us,out);
}

NtResult nt_random_track_sample(const NtRandomTrack *track, NtTimeUs at_us, double *out)
{
    if (!out || !track || !track->interval_us || !isfinite(track->from) || !isfinite(track->to))
        return NT_RESULT_INVALID_ARGUMENT;
    const uint64_t index = at_us / track->interval_us;
    const NtTimeUs extra = at_us % track->interval_us;
    double unit = nt_seed_unit(track->seed,track->stream,index);
    if (track->smooth && extra) {
        const double ratio = (double)extra / (double)track->interval_us;
        const double weight = ratio * ratio * (3 - 2 * ratio);
        unit = nt_animation_mix(unit,nt_seed_unit(track->seed,track->stream,index+1),weight);
    }
    const double value = nt_animation_mix(track->from,track->to,unit);
    if (!isfinite(value)) return NT_RESULT_OUT_OF_RANGE;
    *out = value;
    return NT_RESULT_OK;
}

static uint64_t bounded(uint64_t sample, uint64_t maximum)
{
    return maximum == UINT64_MAX ? sample : sample % (maximum + 1);
}

NtResult nt_track_schedule(const NtTrackSchedule *schedule, uint64_t rank, uint64_t item,
                           NtTimeUs span_us, NtTrackSlot *out)
{
    if (!schedule || !out) return NT_RESULT_INVALID_ARGUMENT;
    const uint64_t batch = rank / (schedule->batch_size ? schedule->batch_size : 1);
    const uint64_t jitter = bounded(nt_seed_bits(schedule->seed,0,item),schedule->jitter_us);
    if (schedule->stagger_us && batch > (UINT64_MAX - jitter) / schedule->stagger_us)
        return NT_RESULT_OUT_OF_RANGE;
    const uint64_t variation = bounded(nt_seed_bits(schedule->seed,1,item),schedule->duration_variation_us);
    if (nt_seed_bits(schedule->seed,2,item) & 1) {
        if (span_us > UINT64_MAX - variation) return NT_RESULT_OUT_OF_RANGE;
        span_us += variation;
    } else {
        span_us = variation > span_us ? 0 : span_us - variation;
    }
    *out = (NtTrackSlot){batch * schedule->stagger_us + jitter,span_us};
    return NT_RESULT_OK;
}
