package nocturne

import (
	"math"
	"testing"
)

func TestEaseAndKeyframes(t *testing.T) {
	if got, err := Ease(EaseIn, .5); err != nil || got != .25 {
		t.Fatalf("Ease(EaseIn, .5) = %g, %v", got, err)
	}
	if _, err := Ease(Linear, math.Inf(1)); err != InvalidArgument {
		t.Fatalf("nonfinite easing progress: %v", err)
	}
	const start = 1<<53 + 1
	track := KeyframeTrack{DelayUs: start, Repeat: PingPong, Keys: []Keyframe{
		{AtUs: 0, Value: 0}, {AtUs: 50, Value: 10, Easing: EaseIn}, {AtUs: 100, Value: 20},
	}}
	for _, want := range []struct {
		elapsed uint64
		sample  AnimationSample
	}{
		{start - 1, AnimationSample{Phase: Delayed, Direction: 1}},
		{start + 50, AnimationSample{Value: 10, Progress: .5, Phase: Running, Direction: 1}},
		{start + 100, AnimationSample{Value: 20, Progress: 1, Iteration: 1, Phase: Running, Direction: -1}},
		{start + 125, AnimationSample{Value: 12.5, Progress: .75, Iteration: 1, Phase: Running, Direction: -1}},
	} {
		if got, err := SampleKeyframes(track, want.elapsed); err != nil || got != want.sample {
			t.Fatalf("sample at %d = %+v, %v, want %+v", want.elapsed, got, err, want.sample)
		}
	}
	exact := KeyframeTrack{Keys: []Keyframe{
		{Value: 1, Easing: StepEnd}, {AtUs: math.MaxUint64, Value: 2},
	}}
	before, err := SampleKeyframes(exact, math.MaxUint64-1)
	if err != nil || before.Value != 1 || before.Progress >= 1 || before.Phase != Running {
		t.Fatalf("last interior tick: %+v, %v", before, err)
	}
	end, err := SampleKeyframes(exact, math.MaxUint64)
	if err != nil || end.Value != 2 || end.Progress != 1 || end.Phase != Finished {
		t.Fatalf("endpoint: %+v, %v", end, err)
	}
	for _, invalid := range []KeyframeTrack{
		{}, {Repeat: Loop, Keys: []Keyframe{{Value: 1}}},
		{Keys: []Keyframe{{AtUs: 1}, {AtUs: 2}}},
		{Keys: []Keyframe{{AtUs: 0}, {AtUs: 0}}},
	} {
		if _, err := SampleKeyframes(invalid, 0); err != InvalidArgument {
			t.Fatalf("invalid track %+v: %v", invalid, err)
		}
	}
}

func TestPlaybackSnapshots(t *testing.T) {
	must := func(value Playback, err error) Playback {
		t.Helper()
		if err != nil {
			t.Fatal(err)
		}
		return value
	}
	elapsed := func(value Playback, now, want uint64) {
		t.Helper()
		if got, err := value.Elapsed(now); err != nil || got != want {
			t.Fatalf("elapsed at %d = %d, %v, want %d", now, got, err, want)
		}
	}
	const start = 1<<53 + 1
	playing := must(NewPlayback(start))
	paused := must(playing.Pause(start + 20))
	elapsed(playing, start+90, 90)
	elapsed(paused, start+90, 20)
	if state, err := paused.State(); err != nil || state != PlaybackPaused {
		t.Fatalf("paused state: %d, %v", state, err)
	}
	if _, err := paused.Elapsed(start + 19); err != InvalidArgument {
		t.Fatalf("backward query: %v", err)
	}
	seeked := must(paused.Seek(start+30, 2))
	resumed := must(seeked.Resume(start + 90))
	cancelled := must(resumed.Cancel(start + 100))
	elapsed(cancelled, math.MaxUint64, 12)
	unchanged, err := cancelled.Resume(start + 101)
	if err != InvalidArgument {
		t.Fatalf("resuming cancelled playback: %v", err)
	}
	elapsed(unchanged, start+200, 12)
	overflow := must(playing.Seek(start, math.MaxUint64))
	if _, err := overflow.Pause(start + 1); err != OutOfRange {
		t.Fatalf("overflowing transition: %v", err)
	}
	elapsed(overflow, start, math.MaxUint64)
	if _, err := (Playback{}).Elapsed(0); err != InvalidArgument {
		t.Fatalf("uninitialized playback: %v", err)
	}
}

func TestSpringSnapshots(t *testing.T) {
	const start = 1<<53 + 1
	const at = start + 100_000
	tuning := SpringTuning{ResponseUs: 100_000, PositionTolerance: .001, VelocityTolerance: .001}
	spring, err := NewSpring(tuning, 0, 0, 10, start)
	if err != nil {
		t.Fatal(err)
	}
	if got, err := spring.Sample(start-1, false); err != nil || got != (SpringSample{Phase: Delayed}) {
		t.Fatalf("delayed spring: %+v, %v", got, err)
	}
	initial, err := spring.Sample(at, false)
	if err != nil || initial.Phase != Running || math.Abs(initial.Value-(10-20/math.E)) > 1e-12 ||
		math.Abs(initial.Velocity-100/math.E) > 1e-12 {
		t.Fatalf("analytic spring at one time constant: %+v, %v", initial, err)
	}
	for _, policy := range []RetargetPolicy{RetargetKeepVelocity, RetargetResetVelocity, RetargetSettle} {
		next, err := spring.Retarget(at, 20, policy)
		if err != nil {
			t.Fatal(err)
		}
		want := initial
		switch policy {
		case RetargetResetVelocity:
			want.Velocity = 0
		case RetargetSettle:
			want = SpringSample{Value: 20, Phase: Finished}
		}
		if got, err := next.Sample(at, false); err != nil || got != want {
			t.Fatalf("retarget policy %d: %+v, %v, want %+v", policy, got, err, want)
		}
	}
	if got, err := spring.Sample(at, false); err != nil || got != initial {
		t.Fatalf("original spring after retargets: %+v, %v", got, err)
	}
	if got, err := spring.Sample(start-1, true); err != nil || got != (SpringSample{Value: 10, Phase: Finished}) {
		t.Fatalf("reduced motion: %+v, %v", got, err)
	}
	if _, err := spring.Retarget(start-1, 20, RetargetKeepVelocity); err != InvalidArgument {
		t.Fatalf("backward retarget: %v", err)
	}
	if _, err := NewSpring(SpringTuning{}, 0, 0, 10, start); err != InvalidArgument {
		t.Fatalf("invalid tuning: %v", err)
	}
}
