#include "animation_internal.h"
#include <math.h>

static int repeat_valid(NtRepeat repeat) {
    return repeat >= NT_ONCE && repeat <= NT_PING_PONG;
}

static int easing_valid(NtEasing easing) {
    return easing >= NT_LINEAR && easing <= NT_STEP_END;
}

NtStatus nt_ease(NtEasing easing, double progress, double *out) {
    if (!out || !easing_valid(easing) || !isfinite(progress) ||
        progress < 0 || progress > 1) return NT_INVALID_ARGUMENT;
    double t = progress;
    switch (easing) {
        case NT_LINEAR: break;
        case NT_EASE_IN: t *= t; break;
        case NT_EASE_OUT: t = 1 - (1 - t) * (1 - t); break;
        case NT_EASE_IN_OUT:
            t = t < .5 ? 2 * t * t : 1 - 2 * (1 - t) * (1 - t); break;
        case NT_SMOOTH: t = t * t * (3 - 2 * t); break;
        case NT_SMOOTHER: t = t * t * t * (t * (6 * t - 15) + 10); break;
        case NT_SINE: t = (1 - cos(3.14159265358979323846 * t)) / 2; break;
        case NT_STEP_START: t = t == 0 ? 0 : 1; break;
        case NT_STEP_END: t = t == 1 ? 1 : 0; break;
    }
    /* Polynomial roundoff near the endpoint must not escape a property's
     * normalized domain (for example opacity or progress). */
    *out = fmax(0, fmin(1, t));
    return NT_OK;
}

double nt_animation_progress(uint64_t at, uint64_t duration)
{
    const double progress = (double)at / (double)duration;
    /* Preserve a distinct endpoint for discontinuous easing even when a
     * final interior integer tick rounds to one in double precision. */
    return at < duration && progress >= 1.0 ? nextafter(1.0, 0.0) : progress;
}

uint64_t nt_animation_timing(uint64_t elapsed, uint64_t delay, uint64_t duration,
                       NtRepeat repeat, NtAnimationSample *sample) {
    sample->iteration = 0;
    sample->direction = 1;
    if (elapsed < delay) {
        sample->phase = NT_DELAYED;
        sample->progress = 0;
        return 0;
    }
    uint64_t age = elapsed - delay;
    if (repeat == NT_ONCE && age >= duration) {
        sample->phase = NT_FINISHED;
        sample->progress = 1;
        return duration;
    }
    sample->phase = NT_RUNNING;
    uint64_t at = age;
    if (repeat != NT_ONCE) {
        /* Division by one leg avoids duration*2 overflow for ping-pong. */
        sample->iteration = age / duration;
        at = age % duration;
        if (repeat == NT_PING_PONG && (sample->iteration & 1)) {
            at = duration - at;
            sample->direction = -1;
        }
    }
    sample->progress = nt_animation_progress(at,duration);
    return at;
}

NtStatus nt_tween_sample(const NtTween *tween, uint64_t elapsed_us,
                         NtAnimationSample *out) {
    if (!tween || !out || !isfinite(tween->from) || !isfinite(tween->to) ||
        !easing_valid(tween->easing) || !repeat_valid(tween->repeat) ||
        (!tween->duration_us && tween->repeat != NT_ONCE)) return NT_INVALID_ARGUMENT;
    NtAnimationSample sample = {0};
    nt_animation_timing(elapsed_us, tween->delay_us, tween->duration_us, tween->repeat, &sample);
    double eased;
    NtStatus status = nt_ease(tween->easing, sample.progress, &eased);
    if (status != NT_OK) return status;
    sample.value = nt_animation_mix(tween->from, tween->to, eased);
    if (!isfinite(sample.value)) return NT_OUT_OF_RANGE;
    *out = sample;
    return NT_OK;
}

NtStatus nt_keyframes_sample(const NtKeyframeTrack *track, uint64_t elapsed_us,
                             NtAnimationSample *out) {
    if (!track || !out || !track->keys || !track->count ||
        !repeat_valid(track->repeat) || track->keys[0].at_us != 0 ||
        (track->count == 1 && track->repeat != NT_ONCE)) return NT_INVALID_ARGUMENT;
    for (size_t i = 0; i < track->count; ++i) {
        if (!isfinite(track->keys[i].value) || !easing_valid(track->keys[i].easing) ||
            (i && track->keys[i].at_us <= track->keys[i - 1].at_us)) return NT_INVALID_ARGUMENT;
    }
    NtAnimationSample sample = {0};
    uint64_t duration = track->keys[track->count - 1].at_us;
    uint64_t at = nt_animation_timing(elapsed_us, track->delay_us, duration, track->repeat, &sample);
    if (!at) sample.value = track->keys[0].value;
    else if (at == duration) sample.value = track->keys[track->count - 1].value;
    else {
        size_t i = 0;
        while (i + 1 < track->count && at >= track->keys[i + 1].at_us) ++i;
        const NtKeyframe *a = &track->keys[i], *b = &track->keys[i + 1];
        double progress = nt_animation_progress(at - a->at_us,b->at_us - a->at_us);
        double eased;
        NtStatus status = nt_ease(a->easing, progress, &eased);
        if (status != NT_OK) return status;
        sample.value = nt_animation_mix(a->value, b->value, eased);
    }
    if (!isfinite(sample.value)) return NT_OUT_OF_RANGE;
    *out = sample;
    return NT_OK;
}

static int playback_valid(const NtPlayback *playback, uint64_t now_us) {
    return playback && playback->state >= NT_PLAYING && playback->state <= NT_CANCELLED &&
        now_us >= playback->anchor_us;
}

NtStatus nt_playback_init(NtPlayback *playback, uint64_t now_us) {
    if (!playback) return NT_INVALID_ARGUMENT;
    NtPlayback result = {0, now_us, NT_PLAYING};
    *playback = result;
    return NT_OK;
}

NtStatus nt_playback_elapsed(const NtPlayback *playback, uint64_t now_us,
                             uint64_t *out) {
    if (!out || !playback_valid(playback, now_us)) return NT_INVALID_ARGUMENT;
    uint64_t delta = playback->state == NT_PLAYING ? now_us - playback->anchor_us : 0;
    if (delta > UINT64_MAX - playback->elapsed_us) return NT_OUT_OF_RANGE;
    *out = playback->elapsed_us + delta;
    return NT_OK;
}

static NtStatus change_state(NtPlayback *playback, uint64_t now_us, NtPlaybackState state) {
    uint64_t elapsed;
    NtStatus status = nt_playback_elapsed(playback, now_us, &elapsed);
    if (status != NT_OK) return status;
    if (playback->state == NT_CANCELLED && state != NT_CANCELLED) return NT_INVALID_ARGUMENT;
    NtPlayback result = {elapsed, now_us, state};
    *playback = result;
    return NT_OK;
}

NtStatus nt_playback_pause(NtPlayback *playback, uint64_t now_us) {
    return change_state(playback, now_us, NT_PAUSED);
}

NtStatus nt_playback_resume(NtPlayback *playback, uint64_t now_us) {
    return change_state(playback, now_us, NT_PLAYING);
}

NtStatus nt_playback_seek(NtPlayback *playback, uint64_t now_us, uint64_t elapsed_us) {
    if (!playback_valid(playback, now_us) || playback->state == NT_CANCELLED) return NT_INVALID_ARGUMENT;
    NtPlayback result = {elapsed_us, now_us, playback->state};
    *playback = result;
    return NT_OK;
}

NtStatus nt_playback_cancel(NtPlayback *playback, uint64_t now_us) {
    return change_state(playback, now_us, NT_CANCELLED);
}
