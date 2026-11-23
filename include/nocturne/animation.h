#ifndef NOCTURNE_ANIMATION_H
#define NOCTURNE_ANIMATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* All functions leave outputs unchanged on error. Inputs are borrowed for the
 * call. No allocation, I/O, global state, callbacks, or implicit clock. */
typedef enum { NT_OK = 0, NT_INVALID_ARGUMENT, NT_OUT_OF_RANGE } NtStatus;
typedef enum { NT_ONCE = 0, NT_LOOP, NT_PING_PONG } NtRepeat;
typedef enum {
    NT_LINEAR = 0, NT_EASE_IN, NT_EASE_OUT, NT_EASE_IN_OUT,
    NT_SMOOTH, NT_SMOOTHER, NT_SINE, NT_STEP_START, NT_STEP_END
} NtEasing;
typedef enum { NT_DELAYED = 0, NT_RUNNING, NT_FINISHED } NtPhase;

typedef struct {
    double value;
    double progress;       /* Oriented, uneased progress through the whole track. */
    uint64_t iteration;    /* Zero-based leg; odd ping-pong legs travel backward. */
    NtPhase phase;
    int direction;        /* +1 forward, -1 backward; describes track time. */
} NtAnimationSample;

typedef struct {
    double from, to;
    uint64_t duration_us, delay_us;
    NtEasing easing;
    NtRepeat repeat;
} NtTween;

typedef struct {
    uint64_t at_us;
    double value;
    NtEasing easing;      /* Easing for the segment starting at this key. */
} NtKeyframe;

typedef struct {
    const NtKeyframe *keys;
    size_t count;
    uint64_t delay_us;
    NtRepeat repeat;
} NtKeyframeTrack;

/* progress must be finite and in [0,1]. */
NtStatus nt_ease(NtEasing easing, double progress, double *out);
/* elapsed_us is local playback time, not wall-clock time. Zero duration is
 * allowed only for NT_ONCE; it reaches the endpoint at delay_us. */
NtStatus nt_tween_sample(const NtTween *tween, uint64_t elapsed_us,
                         NtAnimationSample *out);
/* At least one key; first at zero, subsequent times strictly increasing.
 * A single key is a zero-duration, once-only track. No fixed key-count cap. */
NtStatus nt_keyframes_sample(const NtKeyframeTrack *track, uint64_t elapsed_us,
                             NtAnimationSample *out);

typedef enum { NT_PLAYING = 0, NT_PAUSED, NT_CANCELLED } NtPlaybackState;
typedef struct {
    uint64_t elapsed_us, anchor_us;
    NtPlaybackState state;
} NtPlayback;

/* Initialize before use. Mutations take host monotonic microseconds. Times may
 * not precede the last mutation. Queries are pure; overflow returns RANGE.
 * Pause/cancel freeze elapsed time. Cancel is terminal until reinitialization.
 * Seek changes local time without changing playing/paused state. */
NtStatus nt_playback_init(NtPlayback *playback, uint64_t now_us);
NtStatus nt_playback_elapsed(const NtPlayback *playback, uint64_t now_us,
                             uint64_t *out);
NtStatus nt_playback_pause(NtPlayback *playback, uint64_t now_us);
NtStatus nt_playback_resume(NtPlayback *playback, uint64_t now_us);
NtStatus nt_playback_seek(NtPlayback *playback, uint64_t now_us, uint64_t elapsed_us);
NtStatus nt_playback_cancel(NtPlayback *playback, uint64_t now_us);

typedef struct NtSpringTuning {
    uint64_t response_us;      /* Positive time constant. */
    double position_tolerance; /* Positive absolute settling tolerance. */
    double velocity_tolerance; /* Positive absolute tolerance in units per second. */
} NtSpringTuning;

typedef struct NtSpring {
    NtSpringTuning tuning;
    double initial_value, initial_velocity, target;
    uint64_t start_us;
    double settling_age;       /* Initialized by the API, in time constants. */
} NtSpring;

typedef struct NtSpringSample {
    double value, velocity; /* Velocity is in property units per second. */
    NtPhase phase;
} NtSpringSample;

typedef enum NtRetargetPolicy {
    NT_RETARGET_KEEP_VELOCITY = 0,
    NT_RETARGET_RESET_VELOCITY,
    NT_RETARGET_SETTLE
} NtRetargetPolicy;

/* Initialize a critically damped scalar spring on the caller's clock. */
NtStatus nt_spring_init(NtSpring *spring, NtSpringTuning tuning, double value,
                        double velocity, double target, uint64_t start_us);
/* Sampling is independent of call order. Before start_us, hold the initial
 * position. Reduced motion samples the target with zero velocity immediately. */
NtStatus nt_spring_sample(const NtSpring *spring, uint64_t now_us,
                          bool reduced_motion, NtSpringSample *out);
/* Replace the current segment at now_us, at or after its start. Keep or reset
 * the sampled velocity while preserving position, or settle at the new target. */
NtStatus nt_spring_retarget(NtSpring *spring, uint64_t now_us, double target,
                            NtRetargetPolicy policy);

#ifdef __cplusplus
}
#endif
#endif
