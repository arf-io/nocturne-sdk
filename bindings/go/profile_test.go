//go:build nocturne_graph && nocturne_posix_host && nocturne_spatial && nocturne_simulation

package nocturne

import (
	"math"
	"os"
	"slices"
	"testing"
)

func TestNativeGraph(t *testing.T) {
	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	graph := Graph{
		Nodes: []GraphNode{{ID: 1, Pinned: true, X: 0.2, Y: 0.5}, {ID: 2, Pinned: true, X: 0.8, Y: 0.5}},
		Edges: []GraphEdge{{ID: 1, Source: 1, Target: 2}},
	}
	options := MountOptions{Enabled: true, HitTestable: true}
	node, err := tree.MountGraph(Node{}, options, graph)
	mustOK(t, err)
	size := Size{Columns: 10, Rows: 4}
	mustOK(t, tree.Publish(size))
	geometry, err := tree.Geometry(node)
	if got := must(t, geometry, err); !got.Placed {
		t.Fatalf("graph geometry = %+v", got)
	}
	graph.Layout = GraphRadial
	mustOK(t, tree.SetGraph(node, graph))
	mustOK(t, tree.Publish(size))
	mustOK(t, tree.Remove(node))
}

func TestNativeNumericProfiles(t *testing.T) {
	x, y := Vec3{X: 1}, Vec3{Y: 1}
	normal, err := x.Cross(y)
	if got := must(t, normal, err); got != (Vec3{Z: 1}) {
		t.Fatalf("cross product = %+v", got)
	}
	unit, err := (Vec3{Y: 4}).Normalize()
	if got := must(t, unit, err); got != y {
		t.Fatalf("normalized vector = %+v", got)
	}
	wave, err := SampleWater([]WaterWave{{Amplitude: 2, CyclesPerX: 1}}, 0, 0, 0)
	if got := must(t, wave, err); got.Height != 2 || got.Normal != (Vec3{Y: 1}) {
		t.Fatalf("water sample = %+v", got)
	}
	section := FlowSection{Width: 1e-6, Speed: 1e-6, MaximumDepth: math.Ldexp(1, -1040)}
	sample, err := section.Sample(math.SmallestNonzeroFloat64)
	if got := must(t, sample, err); got.Depth != section.MaximumDepth {
		t.Fatalf("flow section depth = %.17g", got.Depth)
	}
	store := ThermalStore{HeatCapacity: 1e-6, Energy: 1e24}
	pair := ThermalPair{First: store, Second: store}
	result, err := ExchangeHeat(pair, 1e-6, 1000000)
	if got := must(t, result, err); got != pair {
		t.Fatalf("thermal equilibrium changed: %+v", got)
	}
}

func TestNativeTerminalProbeAndDiscovery(t *testing.T) {
	reader, writer, err := os.Pipe()
	mustOK(t, err)
	defer reader.Close()
	defer writer.Close()
	report, err := ProbeTerminal(-1, int32(writer.Fd()))
	if got := must(t, report, err); got.InputTTY || got.OutputTTY || got.HostAvailable {
		t.Fatalf("pipe capabilities = %+v", got)
	}
	expected := []string{"nocturne", "posix_host", "graph", "spatial", "simulation"}
	if actual := AvailableComponents(); !slices.Equal(actual, expected) {
		t.Fatalf("compiled components = %v", actual)
	}
}
