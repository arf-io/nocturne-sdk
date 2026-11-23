//go:build nocturne_simulation && nocturne_spatial

package nocturne

import (
	"math"
	"reflect"
	"sync"
	"testing"
)

func simulationNear(t *testing.T, got, want float64) {
	t.Helper()
	if math.IsNaN(got) || math.Abs(got-want) > 5e-12*math.Max(1, math.Abs(want)) {
		t.Fatalf("%.17g instead of %.17g", got, want)
	}
}

func TestSurfaceTransportOwnedStep(t *testing.T) {
	options := TransportOptions{
		Cells: []TransportCellOptions{
			{WaterCapacity: 8, FoamCapacity: 4, SubstrateHeatCapacity: 2, FoamGeneration: 2},
			{WaterCapacity: 3, FoamCapacity: 1, SubstrateHeatCapacity: 2, InletRate: 2, InletTemperature: 300, FoamDecay: math.Ln2},
			{WaterCapacity: 8, FoamCapacity: 8, SubstrateHeatCapacity: 2},
		},
		Edges: []TransportEdge{
			{Source: 0, Destination: 1, WaterRate: 6, FoamRate: 1},
			{Source: 0, Destination: TransportDrain, WaterRate: 2, FoamRate: 1},
			{Source: 1, Destination: 2, WaterRate: 9, FoamRate: 1},
		}, WaterHeatCapacity: 2,
	}
	initial := []TransportCell{{4, 4, 2000}, {0, 0, 200}, {3, 1, 3200}}
	original := append([]TransportCell(nil), initial...)
	result, err := options.Step(initial, 1_000_000)
	mustOK(t, err)
	if !reflect.DeepEqual(initial, original) || result.Cells[0] != (TransportCell{0, 2, 400}) ||
		result.Cells[1].Water != 3 || result.Cells[1].Foam != 1 || result.Cells[2] != original[2] {
		t.Fatalf("step = %+v, input = %+v", result, initial)
	}
	simulationNear(t, result.Cells[1].Energy, 5200.0/3)
	b := result.Budget
	if b.WaterInlet != 2 || b.WaterDrained != 1 || b.WaterSpilled != 2 || b.FoamGenerated != 2 || b.FoamDrained != 2 ||
		b.HeatInlet != 1200 || b.HeatDrained != 400 {
		t.Fatalf("external budget = %+v", b)
	}
	simulationNear(t, b.FoamDecayed, 1)
	simulationNear(t, b.HeatSpilled, 2600.0/3)
	zero, err := options.Step(initial, 0)
	mustOK(t, err)
	if !reflect.DeepEqual(zero.Cells, initial) || zero.Budget != (TransportBudget{}) {
		t.Fatalf("zero step = %+v", zero)
	}
	result.Cells[2] = TransportCell{}
	if !reflect.DeepEqual(initial, original) {
		t.Fatal("returned cells share input storage")
	}
	if _, err := options.Step(initial[:2], 0); err != InvalidArgument {
		t.Fatalf("count mismatch = %v", err)
	}
	options.Cells[2].Power = -1e18
	rejected, err := options.Step(initial, 1_000_000)
	if err != OutOfRange || rejected.Cells != nil || !reflect.DeepEqual(initial, original) {
		t.Fatalf("rejected step = %+v %v, source %+v", rejected, err, initial)
	}
}

func simulationCube() HullGeometry {
	return HullGeometry{
		Vertices: []Vec3{{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1}, {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}},
		Triangles: []HullTriangle{{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4},
			{3, 7, 6}, {3, 6, 2}, {0, 4, 7}, {0, 7, 3}, {1, 2, 6}, {1, 6, 5}},
	}
}

func TestHullImmersionAndAdvance(t *testing.T) {
	geometry := simulationCube()
	hull, err := NewHull(geometry)
	mustOK(t, err)
	defer hull.Close()
	geometry.Vertices[0].Y = 7
	body := FloatingBody{Mass: 3, PrincipalInertia: Vec3{2, 2, 2}}
	plane := WaterPlane{Normal: Vec3{Y: 1}}
	half, err := hull.ImmersePlane(body, plane)
	mustOK(t, err)
	if half != (HullImmersion{FullVolume: 8, DisplacedVolume: 4, Center: Vec3{Y: -.5}, CenterOffset: Vec3{Y: -.5}}) {
		t.Fatalf("half immersion = %+v", half)
	}
	triangles := []WaterTriangle{{{-1, 0, -1}, {1, 0, -1}, {1, 0, 1}}, {{-1, 0, -1}, {1, 0, 1}, {-1, 0, 1}}}
	patch, err := hull.ImmerseWater(body, triangles)
	mustOK(t, err)
	if patch != half {
		t.Fatalf("finite patch = %+v", patch)
	}
	dry, err := hull.ImmerseWater(body, nil)
	mustOK(t, err)
	if dry != (HullImmersion{FullVolume: 8}) {
		t.Fatalf("empty patch = %+v", dry)
	}
	if _, err := hull.ImmerseWater(body, []WaterTriangle{triangles[0], triangles[0]}); err != InvalidArgument {
		t.Fatalf("overlap = %v", err)
	}
	buoyancy, err := patch.Buoyancy(body, 1, 10)
	mustOK(t, err)
	if buoyancy != (Buoyancy{Lift: Vec3{Y: 40}, Weight: Vec3{Y: -30}, Force: Vec3{Y: 10}}) {
		t.Fatalf("buoyancy = %+v", buoyancy)
	}
	next, err := body.Step(BodyLoad{buoyancy.Force, buoyancy.Torque}, 500_000)
	mustOK(t, err)
	simulationNear(t, next.Position.Y, 5.0/12)
	simulationNear(t, next.LinearVelocity.Y, 5.0/3)
	pose, err := next.Transform()
	mustOK(t, err)
	origin, err := pose.Point(Vec3{})
	spatialVector(t, origin, err, next.Position)
	if body.Position != (Vec3{}) || body.LinearVelocity != (Vec3{}) {
		t.Fatalf("body source changed: %+v", body)
	}

	alias := *hull
	start := make(chan struct{})
	var reads sync.WaitGroup
	for range 3 {
		reads.Add(1)
		go func() {
			defer reads.Done()
			<-start
			for range 20 {
				got, err := alias.ImmersePlane(body, plane)
				if err != nil && err != StaleReference || err == nil && got != half {
					t.Errorf("immersion during close = %+v, %v", got, err)
				}
			}
		}()
	}
	close(start)
	hull.Close()
	reads.Wait()
	alias.Close()
	if _, err := alias.ImmersePlane(body, plane); err != StaleReference {
		t.Fatalf("closed hull = %v", err)
	}
}

func TestSimulationDragAndRotation(t *testing.T) {
	const second = 1_000_000
	linear := LinearPair{LinearStore{1, Vec3{4, -1, 0}}, LinearStore{1, Vec3{-2, -1, 0}}}
	l, err := ExchangeLinearDrag(linear, math.Ln2/2, second)
	mustOK(t, err)
	spatialVector(t, l.FirstVelocity, nil, Vec3{2.5, -1, 0})
	spatialVector(t, l.SecondVelocity, nil, Vec3{-.5, -1, 0})
	spatialVector(t, l.ImpulseOnSecond, nil, Vec3{X: 1.5})
	simulationNear(t, l.DissipatedEnergy, 6.75)
	angular := AngularPair{AngularStore{Vec3{1, 2, 4}, Vec3{4, 8, -3}}, AngularStore{Vec3{1, 2, 4}, Vec3{-2, 0, 1}}}
	a, err := ExchangeAngularDrag(angular, Vec3{math.Ln2 / 2, math.Ln2, 0}, second)
	mustOK(t, err)
	spatialVector(t, a.FirstVelocity, nil, Vec3{2.5, 6, -3})
	spatialVector(t, a.SecondVelocity, nil, Vec3{-.5, 2, 1})
	spatialVector(t, a.ImpulseOnSecond, nil, Vec3{1.5, 4, 0})
	simulationNear(t, a.DissipatedEnergy, 30.75)
	if _, err := ExchangeLinearDrag(linear, 1, second+1); err != OutOfRange {
		t.Fatalf("long drag step = %v", err)
	}
	tiny := math.SmallestNonzeroFloat64
	body := FloatingBody{Mass: 1, PrincipalInertia: Vec3{1, 1, 1}, AngularVelocity: Vec3{tiny, tiny, tiny}}
	next, err := body.Step(BodyLoad{}, second)
	mustOK(t, err)
	if next.RotationRadians != 2*tiny {
		t.Fatalf("subnormal rotation = %+v", next)
	}
	axis := 1 / math.Sqrt(3)
	spatialVector(t, next.RotationAxis, nil, Vec3{axis, axis, axis})
	body.AngularVelocity = Vec3{X: .5}
	if _, err := body.Step(BodyLoad{}, second); err != OutOfRange {
		t.Fatalf("too-large rotation = %v", err)
	}
}
