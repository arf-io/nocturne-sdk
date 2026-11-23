package nocturne

import (
	"math"
	"testing"
)

func TestPathDirectionsAndBoundary(t *testing.T) {
	path := Path{Points: []ContinuousPoint{{0, 0}, {3, 0}, {3, 4}}}
	length, err := path.Length()
	if must(t, length, err) != 7 {
		t.Fatalf("length = %g", length)
	}
	corner, err := path.Sample(3)
	if got := must(t, corner, err); got != (PathSample{ContinuousPoint{3, 0}, ContinuousPoint{0, 1}, 3, 7}) {
		t.Fatalf("forward corner = %+v", got)
	}
	end, err := path.Sample(99)
	if got := must(t, end, err); got.Position != (ContinuousPoint{3, 4}) || got.Distance != 7 {
		t.Fatalf("clamped endpoint = %+v", got)
	}
	path.Boundary = PingPong
	for _, distance := range []float64{-3, 11} {
		reverse, err := path.Sample(distance)
		if got := must(t, reverse, err); got != (PathSample{ContinuousPoint{3, 0}, ContinuousPoint{-1, 0}, 3, 7}) {
			t.Fatalf("reverse corner at %g = %+v", distance, got)
		}
	}
	path.Boundary, path.Closed = Loop, true
	wrapped, err := path.Sample(-2)
	if got := must(t, wrapped, err); got.Length != 12 || got.Distance != 10 ||
		math.Abs(got.Position.X-1.2) > 1e-15 || math.Abs(got.Position.Y-1.6) > 1e-15 ||
		got.Tangent != (ContinuousPoint{-.6, -.8}) {
		t.Fatalf("wrapped closing segment = %+v", got)
	}
	for _, invalid := range []Path{
		{}, {Points: []ContinuousPoint{{0, 0}, {0, 0}}},
		{Points: []ContinuousPoint{{0, 0}, {1, 0}}, Closed: true},
		{Points: []ContinuousPoint{{0, 0}, {math.NaN(), 0}}},
		{Points: []ContinuousPoint{{0, 0}, {1, 0}, {0, 0}}, Closed: true},
	} {
		if _, err := invalid.Length(); err != InvalidArgument {
			t.Fatalf("invalid path %+v = %v", invalid, err)
		}
	}
	path = Path{Points: []ContinuousPoint{{0, 0}, {math.MaxFloat64, 0}}, Boundary: PingPong}
	if _, err := path.Length(); err != OutOfRange {
		t.Fatalf("unrepresentable period = %v", err)
	}
}

func TestPanelPoseProjectionAndMotionPolicy(t *testing.T) {
	pose := PanelPose{Anchor: ContinuousPoint{2, 3}, Offset: ContinuousPoint{-1, 4},
		Scale: ContinuousPoint{-2, .5}, Turns: .25, Opacity: 1}
	point, err := pose.Project(ContinuousPoint{4, 7})
	if must(t, point, err) != (ContinuousPoint{-1, 3}) {
		t.Fatalf("projected point = %+v", point)
	}
	anchor, err := pose.Project(pose.Anchor)
	if must(t, anchor, err) != (ContinuousPoint{1, 7}) {
		t.Fatalf("projected anchor = %+v", anchor)
	}

	const delay = 1<<53 + 1
	transition := PanelTransition{DelayUs: delay, DurationUs: 100, Easing: EaseIn,
		From: PanelPose{Scale: ContinuousPoint{1, 1}, Opacity: .25},
		To: PanelPose{Anchor: ContinuousPoint{4, 8}, Offset: ContinuousPoint{8, -4},
			Scale: ContinuousPoint{3, -1}, Turns: 1, Opacity: .75}}
	for _, sample := range []struct {
		at     uint64
		motion Motion
		want   PanelPose
	}{
		{delay - 1, MotionFull, transition.From},
		{delay + 50, MotionFull, PanelPose{Anchor: ContinuousPoint{1, 2}, Offset: ContinuousPoint{2, -1},
			Scale: ContinuousPoint{1.5, .5}, Turns: .25, Opacity: .375}},
		{delay + 100, MotionFull, transition.To},
		{0, MotionReduced, transition.To},
	} {
		got, err := transition.Sample(sample.at, sample.motion)
		if must(t, got, err) != sample.want {
			t.Fatalf("pose at %d = %+v, want %+v", sample.at, got, sample.want)
		}
	}
	transition.DelayUs = math.MaxUint64
	if _, err := transition.Sample(0, MotionReduced); err != OutOfRange {
		t.Fatalf("overflowing endpoint = %v", err)
	}
	pose.Opacity = 1.01
	if _, err := pose.Project(ContinuousPoint{}); err != InvalidArgument {
		t.Fatalf("out-of-domain opacity = %v", err)
	}
	pose = PanelPose{Scale: ContinuousPoint{math.MaxFloat64, 1}, Opacity: 1}
	if _, err := pose.Project(ContinuousPoint{2, 0}); err != OutOfRange {
		t.Fatalf("unrepresentable projection = %v", err)
	}
}
