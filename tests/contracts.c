#include <nocturne/animation.h>
#include <nocturne/motion.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Checks stay active in Release builds too. */
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define NEAR(a,b) CHECK(fabs((a) - (b)) < 1e-10)

static void easing_contract(void) {
    double value = 17;
    for (int e = NT_LINEAR; e <= NT_STEP_END; ++e) {
        CHECK(nt_ease((NtEasing)e, 0, &value) == NT_OK); NEAR(value, 0);
        CHECK(nt_ease((NtEasing)e, 1, &value) == NT_OK); NEAR(value, 1);
        double previous = 0;
        for (int i = 0; i <= 100; ++i) {
            CHECK(nt_ease((NtEasing)e, i / 100.0, &value) == NT_OK);
            CHECK(value >= previous && value <= 1); previous = value;
        }
        for (int exponent = 1; exponent <= 16; ++exponent) {
            CHECK(nt_ease((NtEasing)e, 1 - pow(10, -exponent), &value) == NT_OK);
            CHECK(value >= 0 && value <= 1);
        }
    }
    value = 17;
    CHECK(nt_ease((NtEasing)99, .5, &value) == NT_INVALID_ARGUMENT);
    CHECK(nt_ease(NT_LINEAR, NAN, &value) == NT_INVALID_ARGUMENT);
    CHECK(nt_ease(NT_LINEAR, -.01, &value) == NT_INVALID_ARGUMENT);
    NEAR(value, 17);
}

static void tween_contract(void) {
    NtTween tween = { 10, 30, 100, 50, NT_LINEAR, NT_ONCE };
    NtAnimationSample sample;
    CHECK(nt_tween_sample(&tween, 49, &sample) == NT_OK);
    CHECK(sample.phase == NT_DELAYED); NEAR(sample.value, 10);
    CHECK(nt_tween_sample(&tween, 50, &sample) == NT_OK);
    CHECK(sample.phase == NT_RUNNING); NEAR(sample.value, 10);
    CHECK(nt_tween_sample(&tween, 100, &sample) == NT_OK); NEAR(sample.value, 20);
    CHECK(nt_tween_sample(&tween, 150, &sample) == NT_OK);
    CHECK(sample.phase == NT_FINISHED); NEAR(sample.value, 30);
    CHECK(nt_tween_sample(&tween, UINT64_MAX, &sample) == NT_OK); NEAR(sample.value, 30);
    tween.repeat = NT_LOOP;
    CHECK(nt_tween_sample(&tween, 150, &sample) == NT_OK);
    NEAR(sample.value, 10); CHECK(sample.iteration == 1);
    tween.repeat = NT_PING_PONG;
    CHECK(nt_tween_sample(&tween, 150, &sample) == NT_OK);
    NEAR(sample.value, 30); CHECK(sample.direction == -1);
    CHECK(nt_tween_sample(&tween, 175, &sample) == NT_OK); NEAR(sample.value, 25);
    CHECK(nt_tween_sample(&tween, 250, &sample) == NT_OK); NEAR(sample.value, 10);
    tween.duration_us = 0;
    NtAnimationSample before = sample;
    CHECK(nt_tween_sample(&tween, 60, &sample) == NT_INVALID_ARGUMENT);
    CHECK(memcmp(&sample, &before, sizeof sample) == 0);
    tween.repeat = NT_ONCE;
    CHECK(nt_tween_sample(&tween, 49, &sample) == NT_OK); NEAR(sample.value, 10);
    CHECK(nt_tween_sample(&tween, 50, &sample) == NT_OK); NEAR(sample.value, 30);
    tween = (NtTween){-DBL_MAX, DBL_MAX, 100, 0, NT_LINEAR, NT_ONCE};
    CHECK(nt_tween_sample(&tween, 50, &sample) == NT_OK); NEAR(sample.value, 0);
    tween = (NtTween){0, 1, UINT64_MAX, 0, NT_LINEAR, NT_PING_PONG};
    CHECK(nt_tween_sample(&tween, UINT64_MAX, &sample) == NT_OK);
    NEAR(sample.value, 1); CHECK(sample.direction == -1);
    before = sample; tween.from = INFINITY;
    CHECK(nt_tween_sample(&tween, 0, &sample) == NT_INVALID_ARGUMENT);
    CHECK(memcmp(&sample, &before, sizeof sample) == 0);
}

static void keyframe_contract(void) {
    NtKeyframe keys[] = {{0, 0, NT_EASE_IN}, {100, 10, NT_LINEAR}, {300, 30, NT_LINEAR}};
    NtKeyframeTrack track = {keys, 3, 20, NT_ONCE};
    NtAnimationSample sample;
    CHECK(nt_keyframes_sample(&track, 70, &sample) == NT_OK); NEAR(sample.value, 2.5);
    CHECK(nt_keyframes_sample(&track, 120, &sample) == NT_OK); NEAR(sample.value, 10);
    CHECK(nt_keyframes_sample(&track, 220, &sample) == NT_OK); NEAR(sample.value, 20);
    CHECK(nt_keyframes_sample(&track, 320, &sample) == NT_OK); NEAR(sample.value, 30);
    track.repeat = NT_PING_PONG;
    CHECK(nt_keyframes_sample(&track, 570, &sample) == NT_OK); NEAR(sample.value, 2.5);
    NtAnimationSample before = sample; keys[2].at_us = 100;
    CHECK(nt_keyframes_sample(&track, 0, &sample) == NT_INVALID_ARGUMENT);
    CHECK(memcmp(&sample, &before, sizeof sample) == 0);
    track.count = 1; track.repeat = NT_ONCE;
    CHECK(nt_keyframes_sample(&track, 20, &sample) == NT_OK);
    NEAR(sample.value, 0); CHECK(sample.phase == NT_FINISHED);
    NtKeyframe long_keys[] = {{0, 10, NT_LINEAR}, {UINT64_MAX - 1, 20, NT_LINEAR},
                              {UINT64_MAX, 30, NT_LINEAR}};
    track = (NtKeyframeTrack){long_keys, 3, 0, NT_ONCE};
    CHECK(nt_keyframes_sample(&track, UINT64_MAX - 1, &sample) == NT_OK); NEAR(sample.value, 20);
    CHECK(nt_keyframes_sample(&track, UINT64_MAX, &sample) == NT_OK); NEAR(sample.value, 30);
}

static void playback_contract(void) {
    NtPlayback playback; uint64_t elapsed;
    CHECK(nt_playback_init(&playback, 100) == NT_OK);
    CHECK(nt_playback_elapsed(&playback, 150, &elapsed) == NT_OK); CHECK(elapsed == 50);
    CHECK(nt_playback_pause(&playback, 180) == NT_OK);
    CHECK(nt_playback_elapsed(&playback, 900, &elapsed) == NT_OK); CHECK(elapsed == 80);
    CHECK(nt_playback_seek(&playback, 900, 80) == NT_OK);
    CHECK(playback.state == NT_PAUSED);
    CHECK(nt_playback_pause(&playback, 950) == NT_OK);
    CHECK(nt_playback_resume(&playback, 1000) == NT_OK);
    CHECK(nt_playback_elapsed(&playback, 1020, &elapsed) == NT_OK); CHECK(elapsed == 100);
    CHECK(nt_playback_seek(&playback, 1020, 500) == NT_OK);
    CHECK(nt_playback_elapsed(&playback, 1030, &elapsed) == NT_OK); CHECK(elapsed == 510);
    NtPlayback before = playback;
    CHECK(nt_playback_pause(&playback, 1019) == NT_INVALID_ARGUMENT);
    CHECK(memcmp(&playback, &before, sizeof playback) == 0);
    CHECK(nt_playback_cancel(&playback, 1030) == NT_OK);
    CHECK(nt_playback_elapsed(&playback, 2000, &elapsed) == NT_OK); CHECK(elapsed == 510);
    CHECK(nt_playback_resume(&playback, 2000) == NT_INVALID_ARGUMENT);
    CHECK(nt_playback_seek(&playback, 2000, 0) == NT_INVALID_ARGUMENT);
    CHECK(nt_playback_init(&playback, 0) == NT_OK);
    CHECK(nt_playback_seek(&playback, 0, UINT64_MAX) == NT_OK);
    before = playback;
    CHECK(nt_playback_pause(&playback, 1) == NT_OUT_OF_RANGE);
    CHECK(memcmp(&playback, &before, sizeof playback) == 0);
}

static void path_contract(void) {
    const NtPoint points[] = {{0, 0}, {3, 0}, {3, 4}};
    NtPath path = {points, 3, 0, NT_ONCE}; NtPathSample sample;
    CHECK(nt_path_sample(&path, 5, &sample) == NT_OK);
    NEAR(sample.length, 7); NEAR(sample.position.x, 3); NEAR(sample.position.y, 2);
    NEAR(sample.tangent.x, 0); NEAR(sample.tangent.y, 1);
    CHECK(nt_path_sample(&path, 3, &sample) == NT_OK); NEAR(sample.tangent.y, 1);
    CHECK(nt_path_sample(&path, -2, &sample) == NT_OK); NEAR(sample.position.x, 0);
    CHECK(nt_path_sample(&path, 100, &sample) == NT_OK); NEAR(sample.position.y, 4);
    path.boundary = NT_LOOP;
    CHECK(nt_path_sample(&path, -1, &sample) == NT_OK); NEAR(sample.position.y, 3);
    CHECK(nt_path_sample(&path, 7, &sample) == NT_OK); NEAR(sample.position.x, 0);
    path.boundary = NT_PING_PONG;
    CHECK(nt_path_sample(&path, 8, &sample) == NT_OK); NEAR(sample.position.y, 3); NEAR(sample.tangent.y, -1);
    CHECK(nt_path_sample(&path, -1, &sample) == NT_OK); NEAR(sample.position.x, 1); NEAR(sample.tangent.x, -1);
    CHECK(nt_path_sample(&path, 7, &sample) == NT_OK); NEAR(sample.tangent.y, -1);
    CHECK(nt_path_sample(&path, 11, &sample) == NT_OK); NEAR(sample.tangent.x, -1);
    NEAR(sample.tangent.y, 0);
    path.closed = 1; path.boundary = NT_ONCE;
    CHECK(nt_path_sample(&path, 12, &sample) == NT_OK); NEAR(sample.position.x, 0); NEAR(sample.position.y, 0);
    NtPathSample before = sample;
    CHECK(nt_path_sample(&path, NAN, &sample) == NT_INVALID_ARGUMENT);
    CHECK(memcmp(&sample, &before, sizeof sample) == 0);
    NtPoint degenerate[] = {{0, 0}, {0, 0}};
    path = (NtPath){degenerate, 2, 0, NT_ONCE};
    CHECK(nt_path_sample(&path, 1, &sample) == NT_INVALID_ARGUMENT);
    CHECK(memcmp(&sample, &before, sizeof sample) == 0);
    const NtPoint huge[] = {{-DBL_MAX, 0}, {DBL_MAX, 0}};
    path.points = huge;
    CHECK(nt_path_sample(&path, 1, &sample) == NT_OUT_OF_RANGE);
    CHECK(memcmp(&sample, &before, sizeof sample) == 0);
    const NtPoint unequal_magnitudes[] = {{-1e16, 0}, {1, 0}};
    path.points = unequal_magnitudes;
    CHECK(nt_path_sample(&path, DBL_MAX, &sample) == NT_OK); NEAR(sample.position.x, 1);
}

static void composition_contract(void) {
    /* Two independent properties and a custom consumer-owned object. Sampling
     * unrelated times must not change the result at a given time. */
    const NtPoint points[] = {{0, 0}, {20, 0}, {20, 10}};
    NtPath path = {points, 3, 0, NT_ONCE};
    NtTween distance = {0, 30, 1000, 0, NT_LINEAR, NT_ONCE};
    NtTween opacity = {0, 1, 400, 0, NT_SMOOTH, NT_ONCE};
    NtAnimationSample travel, alpha; NtPathSample position;
    CHECK(nt_tween_sample(&distance, 500, &travel) == NT_OK);
    CHECK(nt_tween_sample(&opacity, 500, &alpha) == NT_OK);
    CHECK(nt_path_sample(&path, travel.value, &position) == NT_OK);
    NEAR(position.position.x, 15); NEAR(position.position.y, 0); NEAR(alpha.value, 1);
    for (uint64_t i = 1000; i > 0; --i) CHECK(nt_tween_sample(&distance, i, &travel) == NT_OK);
    CHECK(nt_tween_sample(&distance, 500, &travel) == NT_OK); NEAR(travel.value, 15);

    /* Equivalent one-segment descriptions must agree through delay, both
     * repeat boundaries, and the reverse leg for every supported easing. */
    for (int e = NT_LINEAR; e <= NT_STEP_END; ++e) {
        NtTween tween = {-5, 7, 100, 20, (NtEasing)e, NT_PING_PONG};
        NtKeyframe keys[] = {{0, -5, (NtEasing)e}, {100, 7, NT_LINEAR}};
        NtKeyframeTrack track = {keys, 2, 20, NT_PING_PONG};
        for (uint64_t time = 0; time < 450; ++time) {
            NtAnimationSample a, b;
            CHECK(nt_tween_sample(&tween, time, &a) == NT_OK);
            CHECK(nt_keyframes_sample(&track, time, &b) == NT_OK);
            NEAR(a.value, b.value);
            CHECK(a.phase == b.phase && a.direction == b.direction && a.iteration == b.iteration);
        }
    }
}

int main(void) {
    easing_contract(); tween_contract(); keyframe_contract(); playback_contract();
    path_contract(); composition_contract();
    puts("PASS: easing, tween, keyframes, playback, path, composition");
    return 0;
}
