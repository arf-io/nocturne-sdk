//go:build nocturne_spatial

package nocturne

import (
	"math"
	"testing"
)

func TestSpatialSurfaceMotion(t *testing.T) {
	motion := SpatialSurfaceMotion{Pivot: Vec3{2, 3, 4}, LinearVelocity: Vec3{2, -1, .5},
		AngularVelocity: Vec3{Z: 1}}
	initial := Vec3{4, 3, 4}
	quarter := math.Pi / 2
	for _, tc := range []struct {
		seconds            float64
		position, velocity Vec3
	}{
		{quarter, Vec3{2 + 2*quarter, 5 - quarter, 4 + .5*quarter}, Vec3{0, -1, .5}},
		{-quarter, Vec3{2 - 2*quarter, 1 + quarter, 4 - .5*quarter}, Vec3{4, -1, .5}},
		{0, initial, Vec3{2, 1, .5}},
	} {
		sample, err := motion.Sample(tc.seconds, initial)
		mustOK(t, err)
		spatialVector(t, sample.Position, nil, tc.position)
		spatialVector(t, sample.Velocity, nil, tc.velocity)
		pose, err := motion.Pose(tc.seconds)
		mustOK(t, err)
		position, err := pose.Point(initial)
		spatialVector(t, position, err, tc.position)
	}
	wide := SpatialSurfaceMotion{Pivot: Vec3{X: -math.MaxFloat64}, LinearVelocity: Vec3{X: math.MaxFloat64}}
	sample, err := wide.Sample(2, wide.Pivot)
	mustOK(t, err)
	if sample.Position.X != math.MaxFloat64 || sample.Velocity.X != math.MaxFloat64 {
		t.Fatalf("wide sample = %+v", sample)
	}
	if _, err := wide.Pose(2); err != OutOfRange {
		t.Fatalf("wide pose = %v", err)
	}
}

func TestSpatialSweepPointContact(t *testing.T) {
	initial := [3]Vec3{{}, {X: 2}, {Y: 2}}
	triangles := []SpatialSweepTriangle{
		{Initial: initial, ObjectID: 11, PrimitiveID: 101},
		{Initial: initial, Displacement: Vec3{Z: 2}, ObjectID: math.MaxUint64, PrimitiveID: 1 << 63},
		{Initial: [3]Vec3{initial[0], initial[2], initial[1]}, Displacement: Vec3{Z: 2}, ObjectID: 13, PrimitiveID: 103},
	}
	first, last := Vec3{.5, .5, 3}, Vec3{.5, .5, -1}
	hit, found, err := SweepSpatialPoint(first, last, triangles)
	want := SpatialContact{ObjectID: math.MaxUint64, PrimitiveID: 1 << 63, TriangleIndex: 1, Fraction: .5,
		Position: Vec3{.5, .5, 1}, Weights: Vec3{.5, .25, .25}, Normal: Vec3{Z: 1}}
	if err != nil || !found || hit != want {
		t.Fatalf("crossing = %+v %v %v, want %+v", hit, found, err, want)
	}
	hit, found, err = SweepSpatialPoint(Vec3{-2, 1, 0}, Vec3{2, 1, 0}, triangles[:1])
	if err != nil || !found || hit.Fraction != .5 || hit.Position != (Vec3{Y: 1}) || hit.Weights != (Vec3{.5, 0, .5}) {
		t.Fatalf("coplanar entry = %+v %v %v", hit, found, err)
	}
	hit, found, err = SweepSpatialPoint(Vec3{}, Vec3{}, triangles[2:])
	if err != nil || !found || hit.Fraction != 0 || hit.Normal != (Vec3{Z: -1}) {
		t.Fatalf("stationary contact = %+v %v %v", hit, found, err)
	}
	if _, found, err := SweepSpatialPoint(first, last, nil); found || err != nil {
		t.Fatalf("empty sweep = %v %v", found, err)
	}
	triangles[2].Displacement.Z = math.NaN()
	if _, _, err := SweepSpatialPoint(first, last, triangles); err != InvalidArgument {
		t.Fatalf("nonfinite later triangle = %v", err)
	}
}
