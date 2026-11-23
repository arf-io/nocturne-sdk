//go:build nocturne_spatial

package nocturne

import (
	"math"
	"sync"
	"testing"
)

func spatialVector(t *testing.T, value Vec3, err error, expected Vec3) {
	t.Helper()
	mustOK(t, err)
	for i, component := range []float64{value.X - expected.X, value.Y - expected.Y, value.Z - expected.Z} {
		if math.IsNaN(component) || math.Abs(component) > 2e-13 {
			t.Fatalf("vector %v, want %v (component %d)", value, expected, i)
		}
	}
}

func spatialScalar(t *testing.T, value float64, err error, expected float64) {
	t.Helper()
	mustOK(t, err)
	if value != expected {
		t.Fatalf("scalar %v, want %v", value, expected)
	}
}

func TestSpatialTransformsAndCamera(t *testing.T) {
	a, b := Vec3{1, 2, 3}, Vec3{2, 4, 5}
	v, err := a.Add(b)
	spatialVector(t, v, err, Vec3{3, 6, 8})
	v, err = b.Subtract(a)
	spatialVector(t, v, err, Vec3{1, 2, 2})
	v, err = a.Scale(2)
	spatialVector(t, v, err, Vec3{2, 4, 6})
	scalar, err := a.Dot(b)
	spatialScalar(t, scalar, err, float64(25))
	scalar, err = (Vec3{3, 4, 0}).Length()
	spatialScalar(t, scalar, err, float64(5))
	v, err = a.Cross(b)
	spatialVector(t, v, err, Vec3{-2, 1, 0})
	v, err = (Vec3{3, 4, 0}).Normalize()
	spatialVector(t, v, err, Vec3{.6, .8, 0})
	if _, err = (Vec3{}).Normalize(); err != InvalidArgument {
		t.Fatalf("zero direction: %v", err)
	}

	options := DefaultSpatialTransformOptions()
	identity, err := NewSpatialTransform(options)
	mustOK(t, err)
	if identity != IdentitySpatialTransform() {
		t.Fatalf("default transform %v", identity)
	}
	options.Scale, options.Pivot, options.Translation = Vec3{2, 3, 4}, a, Vec3{10, -2, 1}
	transform, err := NewSpatialTransform(options)
	mustOK(t, err)
	v, err = transform.Point(b)
	spatialVector(t, v, err, Vec3{13, 6, 12})
	v, err = transform.Direction(b)
	spatialVector(t, v, err, Vec3{4, 12, 20})
	inverse, err := transform.Inverse()
	mustOK(t, err)
	twice, err := inverse.Compose(transform)
	mustOK(t, err)
	v, err = twice.Point(b)
	spatialVector(t, v, err, b)
	options.Scale.Z = 0
	singular, err := NewSpatialTransform(options)
	mustOK(t, err)
	if _, err = singular.Inverse(); err != InvalidArgument {
		t.Fatalf("singular inverse: %v", err)
	}

	camera, err := SpatialLookAt(Vec3{Z: 4}, Vec3{}, Vec3{Y: 1})
	mustOK(t, err)
	orbit, err := SpatialOrbit(Vec3{}, 4, 0, 0)
	mustOK(t, err)
	if camera != orbit {
		t.Fatalf("look-at %v, orbit %v", camera, orbit)
	}
	v, err = camera.ToView(Vec3{})
	spatialVector(t, v, err, Vec3{Z: -4})
	fly, err := camera.Fly(Vec3{1, 2, -1}, 0, 0, 0)
	mustOK(t, err)
	spatialVector(t, fly.Position, nil, Vec3{1, 2, 3})
	if _, err = (SpatialCamera{}).ToView(b); err != InvalidArgument {
		t.Fatalf("malformed camera: %v", err)
	}
}

func TestSpatialProjectionAndPicking(t *testing.T) {
	projection := SpatialProjection{Kind: SpatialPerspective, HalfWidth: 1, HalfHeight: .5, NearDepth: 1, FarDepth: 8}
	point := Vec3{2, 1, -4}
	projected, visible, err := projection.Project(point)
	if err != nil || !visible || projected != (SpatialProjected{Screen: SpatialScreen{.5, .5}, Depth: 4}) {
		t.Fatalf("projected = %v, visible = %v, error = %v", projected, visible, err)
	}
	roundtrip, err := projection.Unproject(projected.Screen, projected.Depth)
	spatialVector(t, roundtrip, err, point)
	if _, visible, err := projection.Project(Vec3{Z: 4}); visible || err != nil {
		t.Fatalf("projected miss = %v, %v", visible, err)
	}
	segment, visible, err := projection.ClipSegment(Vec3{-8, 0, -4}, Vec3{8, 0, -4})
	if err != nil || !visible || segment.First.Weights != (Vec3{.75, .25, 0}) ||
		segment.Last.Weights != (Vec3{.25, .75, 0}) ||
		segment.First.Projected.Screen.X != -1 || segment.Last.Projected.Screen.X != 1 {
		t.Fatalf("segment = %v, visible = %v, error = %v", segment, visible, err)
	}
	view := func(depth float64) [3]Vec3 { return [3]Vec3{{-1, -1, -depth}, {1, -1, -depth}, {0, 1, -depth}} }
	polygon, err := projection.ClipTriangle([3]Vec3{{-8, -1, -4}, {8, -1, -4}, {0, 1, -4}})
	mustOK(t, err)
	if len(polygon) != 5 {
		t.Fatalf("clipped polygon = %v", polygon)
	}
	empty, err := projection.ClipTriangle(view(-2))
	mustOK(t, err)
	if len(empty) != 0 {
		t.Fatalf("hidden polygon = %v", empty)
	}
	picks := []SpatialPickTriangle{
		{View: view(4), ObjectID: 1, PrimitiveID: 2},
		{View: view(2), ObjectID: math.MaxUint64, PrimitiveID: 1 << 63},
		{View: view(2), ObjectID: 3, PrimitiveID: 4},
	}
	hit, found, err := projection.Pick(SpatialScreen{}, picks)
	if err != nil || !found || hit != (SpatialHit{ObjectID: math.MaxUint64, PrimitiveID: 1 << 63,
		TriangleIndex: 1, View: Vec3{Z: -2}, Weights: Vec3{.25, .25, .5}, Depth: 2}) {
		t.Fatalf("hit = %v, found = %v, error = %v", hit, found, err)
	}
	if _, found, err := projection.Pick(SpatialScreen{X: 2}, picks); found || err != nil {
		t.Fatalf("pick miss = %v, %v", found, err)
	}
	if _, found, err := projection.Pick(SpatialScreen{}, nil); found || err != nil {
		t.Fatalf("empty pick = %v, %v", found, err)
	}
	picks[2].View[0].X = math.NaN()
	if _, _, err := projection.Pick(SpatialScreen{X: 2}, picks); err != InvalidArgument {
		t.Fatalf("nonfinite triangle on a miss = %v", err)
	}
	projection.Kind = SpatialOrthographic
	ortho, visible, err := projection.Project(Vec3{.5, .25, -4})
	if err != nil || !visible || ortho != projected {
		t.Fatalf("orthographic projection = %v, %v, %v", ortho, visible, err)
	}
}

func TestSpatialPathLifetimeAndTravel(t *testing.T) {
	points := []Vec3{{}, {X: 3}, {X: 3, Y: 4}}
	path, err := NewSpatialPath(SpatialPathOptions{Points: points, Boundary: PingPong})
	mustOK(t, err)
	defer path.Close()
	points[1] = Vec3{}
	length, err := path.Length()
	spatialScalar(t, length, err, float64(7))
	for _, tc := range []struct {
		input  float64
		sample SpatialPathSample
	}{
		{-1, SpatialPathSample{Vec3{X: 1}, Vec3{X: -1}, 1, 7}},
		{3, SpatialPathSample{Vec3{X: 3}, Vec3{Y: 1}, 3, 7}},
		{8, SpatialPathSample{Vec3{X: 3, Y: 3}, Vec3{Y: -1}, 6, 7}},
	} {
		got, err := path.Sample(tc.input)
		mustOK(t, err)
		if got != tc.sample {
			t.Fatalf("sample %v = %v, want %v", tc.input, got, tc.sample)
		}
	}
	if _, err = NewSpatialPath(SpatialPathOptions{Points: points}); err != InvalidArgument {
		t.Fatalf("duplicate path positions = %v", err)
	}

	alias := *path
	start := make(chan struct{})
	var reads sync.WaitGroup
	for range 4 {
		reads.Add(1)
		go func() {
			defer reads.Done()
			<-start
			for range 30 {
				got, err := alias.Sample(-1)
				if err != nil && err != StaleReference || err == nil && got.Distance != 1 {
					t.Errorf("sample during close = %v, %v", got, err)
				}
			}
		}()
	}
	close(start)
	path.Close()
	reads.Wait()
	alias.Close()
	if _, err := alias.Sample(0); err != StaleReference {
		t.Fatalf("closed path = %v", err)
	}
}
