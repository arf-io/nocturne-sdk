package nocturne

/*
#include <nocturne/animation.h>

static NtStatus go_keyframes_sample(const NtKeyframe *keys, size_t count,
                                    uint64_t delay, NtRepeat repeat, uint64_t elapsed,
                                    NtAnimationSample *out)
{
    const NtKeyframeTrack track = {keys, count, delay, repeat};
    return nt_keyframes_sample(&track, elapsed, out);
}
*/
import "C"

import "unsafe"

type Easing uint32

const (
	Linear    Easing = C.NT_LINEAR
	EaseIn    Easing = C.NT_EASE_IN
	EaseOut   Easing = C.NT_EASE_OUT
	EaseInOut Easing = C.NT_EASE_IN_OUT
	Smooth    Easing = C.NT_SMOOTH
	Smoother  Easing = C.NT_SMOOTHER
	Sine      Easing = C.NT_SINE
	StepStart Easing = C.NT_STEP_START
	StepEnd   Easing = C.NT_STEP_END
)

type Repeat uint32

const (
	Once     Repeat = C.NT_ONCE
	Loop     Repeat = C.NT_LOOP
	PingPong Repeat = C.NT_PING_PONG
)

type AnimationPhase uint32

const (
	Delayed  AnimationPhase = C.NT_DELAYED
	Running  AnimationPhase = C.NT_RUNNING
	Finished AnimationPhase = C.NT_FINISHED
)

type Tween struct {
	From, To            float64
	DurationUs, DelayUs uint64
	Easing              Easing
	Repeat              Repeat
}

type AnimationSample struct {
	Value, Progress float64
	Iteration       uint64
	Phase           AnimationPhase
	Direction       int32
}

// Ease evaluates the curve at a finite progress in [0, 1].
func Ease(easing Easing, progress float64) (float64, error) {
	if err := ensureABI(); err != nil {
		return 0, err
	}
	var out C.double
	status := C.nt_ease(C.NtEasing(easing), C.double(progress), &out)
	return float64(out), animationError(status)
}

// SampleTween evaluates a scalar tween at caller-owned local time in microseconds.
func SampleTween(tween Tween, elapsedUs uint64) (AnimationSample, error) {
	if err := ensureABI(); err != nil {
		return AnimationSample{}, err
	}
	native := tween.native()
	var sample C.NtAnimationSample
	status := C.nt_tween_sample(&native, C.uint64_t(elapsedUs), &sample)
	return animationSample(sample, status)
}

// Keyframe describes a value and the easing of the segment starting at AtUs.
type Keyframe struct {
	AtUs   uint64
	Value  float64
	Easing Easing
}

func (key Keyframe) native() C.NtKeyframe {
	return C.NtKeyframe{at_us: C.uint64_t(key.AtUs), value: C.double(key.Value), easing: C.NtEasing(key.Easing)}
}

type KeyframeTrack struct {
	Keys    []Keyframe
	DelayUs uint64
	Repeat  Repeat
}

// SampleKeyframes samples a caller-timed track whose first key is at zero.
// Later keys must have strictly increasing times. A single key plays once.
func SampleKeyframes(track KeyframeTrack, elapsedUs uint64) (AnimationSample, error) {
	if err := ensureABI(); err != nil {
		return AnimationSample{}, err
	}
	keys := make([]C.NtKeyframe, len(track.Keys))
	for i, key := range track.Keys {
		keys[i] = key.native()
	}
	var sample C.NtAnimationSample
	status := C.go_keyframes_sample(unsafe.SliceData(keys), C.size_t(len(keys)),
		C.uint64_t(track.DelayUs), C.NtRepeat(track.Repeat), C.uint64_t(elapsedUs), &sample)
	return animationSample(sample, status)
}

func animationSample(sample C.NtAnimationSample, status C.NtStatus) (AnimationSample, error) {
	if err := animationError(status); err != nil {
		return AnimationSample{}, err
	}
	return AnimationSample{
		Value: float64(sample.value), Progress: float64(sample.progress),
		Iteration: uint64(sample.iteration), Phase: AnimationPhase(sample.phase),
		Direction: int32(sample.direction),
	}, nil
}

func animationError(status C.NtStatus) error {
	switch status {
	case C.NT_OK:
		return nil
	case C.NT_INVALID_ARGUMENT:
		return InvalidArgument
	case C.NT_OUT_OF_RANGE:
		return OutOfRange
	default:
		return Result(status)
	}
}

func (tween Tween) native() C.NtTween {
	return C.NtTween{
		from: C.double(tween.From), to: C.double(tween.To),
		duration_us: C.uint64_t(tween.DurationUs), delay_us: C.uint64_t(tween.DelayUs),
		easing: C.NtEasing(tween.Easing), repeat: C.NtRepeat(tween.Repeat),
	}
}

type PlaybackState uint32

const (
	PlaybackPlaying   PlaybackState = C.NT_PLAYING
	PlaybackPaused    PlaybackState = C.NT_PAUSED
	PlaybackCancelled PlaybackState = C.NT_CANCELLED
)

// Playback maps host time to local microseconds. Transitions return a new value.
// Construct it with NewPlayback. Failed transitions return the original value.
type Playback struct {
	native  C.NtPlayback
	started bool
}

func NewPlayback(nowUs uint64) (Playback, error) {
	if err := ensureABI(); err != nil {
		return Playback{}, err
	}
	var playback Playback
	err := animationError(C.nt_playback_init(&playback.native, C.uint64_t(nowUs)))
	playback.started = err == nil
	return playback, err
}

func (playback Playback) State() (PlaybackState, error) {
	if !playback.started {
		return 0, InvalidArgument
	}
	return PlaybackState(playback.native.state), nil
}

func (playback Playback) Elapsed(nowUs uint64) (uint64, error) {
	if !playback.started {
		return 0, InvalidArgument
	}
	var elapsed C.uint64_t
	err := animationError(C.nt_playback_elapsed(&playback.native, C.uint64_t(nowUs), &elapsed))
	return uint64(elapsed), err
}

func (playback Playback) transition(call func(*C.NtPlayback) C.NtStatus) (Playback, error) {
	if !playback.started {
		return playback, InvalidArgument
	}
	err := animationError(call(&playback.native))
	return playback, err
}

func (playback Playback) Pause(nowUs uint64) (Playback, error) {
	return playback.transition(func(native *C.NtPlayback) C.NtStatus {
		return C.nt_playback_pause(native, C.uint64_t(nowUs))
	})
}

func (playback Playback) Resume(nowUs uint64) (Playback, error) {
	return playback.transition(func(native *C.NtPlayback) C.NtStatus {
		return C.nt_playback_resume(native, C.uint64_t(nowUs))
	})
}

func (playback Playback) Seek(nowUs, elapsedUs uint64) (Playback, error) {
	return playback.transition(func(native *C.NtPlayback) C.NtStatus {
		return C.nt_playback_seek(native, C.uint64_t(nowUs), C.uint64_t(elapsedUs))
	})
}

func (playback Playback) Cancel(nowUs uint64) (Playback, error) {
	return playback.transition(func(native *C.NtPlayback) C.NtStatus {
		return C.nt_playback_cancel(native, C.uint64_t(nowUs))
	})
}

type SpringTuning struct {
	ResponseUs                           uint64
	PositionTolerance, VelocityTolerance float64
}

type SpringSample struct {
	Value, Velocity float64
	Phase           AnimationPhase
}

type RetargetPolicy uint32

const (
	RetargetKeepVelocity  RetargetPolicy = C.NT_RETARGET_KEEP_VELOCITY
	RetargetResetVelocity RetargetPolicy = C.NT_RETARGET_RESET_VELOCITY
	RetargetSettle        RetargetPolicy = C.NT_RETARGET_SETTLE
)

// Spring is a critically damped scalar segment. Retarget returns a new segment.
type Spring struct{ native C.NtSpring }

func NewSpring(tuning SpringTuning, value, velocity, target float64, startUs uint64) (Spring, error) {
	if err := ensureABI(); err != nil {
		return Spring{}, err
	}
	settings := C.NtSpringTuning{response_us: C.uint64_t(tuning.ResponseUs),
		position_tolerance: C.double(tuning.PositionTolerance), velocity_tolerance: C.double(tuning.VelocityTolerance)}
	var spring Spring
	status := C.nt_spring_init(&spring.native, settings, C.double(value), C.double(velocity),
		C.double(target), C.uint64_t(startUs))
	return spring, animationError(status)
}

func (spring Spring) Sample(nowUs uint64, reducedMotion bool) (SpringSample, error) {
	var out C.NtSpringSample
	status := C.nt_spring_sample(&spring.native, C.uint64_t(nowUs), C.bool(reducedMotion), &out)
	return SpringSample{Value: float64(out.value), Velocity: float64(out.velocity), Phase: AnimationPhase(out.phase)},
		animationError(status)
}

func (spring Spring) Retarget(nowUs uint64, target float64, policy RetargetPolicy) (Spring, error) {
	status := C.nt_spring_retarget(&spring.native, C.uint64_t(nowUs), C.double(target), C.NtRetargetPolicy(policy))
	return spring, animationError(status)
}
