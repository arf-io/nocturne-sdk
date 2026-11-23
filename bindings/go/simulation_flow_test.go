//go:build nocturne_simulation && nocturne_spatial

package nocturne

import (
	"math"
	"testing"
)

func TestFlowPortsAndIntervals(t *testing.T) {
	const second = 1_000_000
	channel := Flow{
		Stages: []FlowStage{{2 * second, 0}, {second, second}, {0, second}},
		Inputs: []FlowInput{{second, 4 * second, 6}, {5 * second, 6 * second, 2}},
	}
	at, err := channel.Sample(3 * second)
	mustOK(t, err)
	if len(at.Ports) != 4 || len(at.Retained) != 3 || at.Ports[0] != (FlowPort{12, 6}) ||
		at.Ports[1] != (FlowPort{0, 6}) || at.Ports[2] != (FlowPort{}) || at.Retained[0] != 12 {
		t.Fatalf("port boundary = %+v", at)
	}
	later, err := channel.Sample(6 * second)
	mustOK(t, err)
	if e := math.Exp(-2); math.Abs(later.Ports[3].Volume-24*e) > 3e-13 ||
		math.Abs(later.Ports[3].Rate-6*(1-3*e)) > 3e-13 {
		t.Fatalf("filtered output = %+v", later)
	}
	for i, stored := range later.Retained {
		if stored < 0 || math.Abs(later.Ports[i+1].Volume+stored-later.Ports[i].Volume) > 3e-13 {
			t.Fatalf("retained stage %d = %+v", i, later)
		}
	}
	drained, err := channel.Sample(math.MaxUint64)
	mustOK(t, err)
	if math.Abs(drained.Ports[3].Volume-20) > 3e-13 || drained.Ports[3].Rate != 0 {
		t.Fatalf("drained schedule = %+v", drained)
	}
	if _, err := (Flow{Stages: make([]FlowStage, 9)}).Sample(0); err != OutOfRange {
		t.Fatalf("stage count = %v", err)
	}
	if _, err := (Flow{Inputs: []FlowInput{{0, 2, 1}, {1, 3, 1}}}).Sample(0); err != InvalidArgument {
		t.Fatalf("overlap = %v", err)
	}
}

func TestSectionPointAndSprayClock(t *testing.T) {
	section, err := (FlowSection{Width: 4, Speed: 2, MaximumDepth: .5}).Sample(6)
	mustOK(t, err)
	point, err := FlowSectionPoint(Vec3{2, 3, 4}, Vec3{Y: 1}, section.Depth, .5)
	spatialVector(t, point, err, Vec3{2, 3.25, 4})
	if _, err := FlowSectionPoint(Vec3{}, Vec3{Y: 2}, 1, .5); err != InvalidArgument {
		t.Fatalf("nonunit depth direction = %v", err)
	}
	const birth = math.MaxUint64 - 2_000_000
	options := SprayOptions{Origin: point, Velocity: Vec3{3, 1, 0}, Acceleration: Vec3{Y: -2},
		LifetimeUs: 2_000_000, DropletVolume: .125, Seed: math.MaxUint64, Stream: 1 << 63}
	batch, err := EmitSpray(options, section.ExcessRate, 5, math.MaxUint64-4, birth)
	mustOK(t, err)
	if batch.Count != 5 || batch.EmittedVolume != .625 || batch.RemainingVolume != 1.375 || batch.Options != options {
		t.Fatalf("batch = %+v", batch)
	}
	drop, active, err := batch.Sample(4, birth+1_000_000)
	if err != nil || !active || drop != (SpraySample{ID: math.MaxUint64, Position: Vec3{5, 3.25, 4},
		Velocity: Vec3{3, -1, 0}, Volume: .125, Progress: .5}) {
		t.Fatalf("active drop = %+v %v %v", drop, active, err)
	}
	for _, now := range []uint64{birth - 1, math.MaxUint64} {
		if _, active, err := batch.Sample(0, now); active || err != nil {
			t.Fatalf("inactive at %v = %v %v", now, active, err)
		}
	}
	if _, err := EmitSpray(options, 1, 2, math.MaxUint64, 0); err != OutOfRange {
		t.Fatalf("overflowing IDs = %v", err)
	}
	if _, _, err := batch.Sample(5, birth); err != OutOfRange {
		t.Fatalf("invalid index = %v", err)
	}
}
