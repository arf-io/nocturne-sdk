//go:build nocturne_graph

package nocturne

import "testing"

func TestGraphSnapshotAndPublishedSelection(t *testing.T) {
	const left, right, edge uint64 = 1<<63 + 1, 1<<63 + 2, ^uint64(0)
	graph := Graph{
		Nodes: []GraphNode{{ID: right, Pinned: true, X: .75, Y: .5}, {ID: left, Pinned: true, X: .25, Y: .5}},
		Edges: []GraphEdge{{ID: edge, Source: left, Target: right, Directed: true}},
	}
	snapshot, err := graph.LayoutSnapshot()
	mustOK(t, err)
	if len(snapshot.Nodes) != 2 || snapshot.Nodes[0] != graph.Nodes[1] ||
		len(snapshot.Edges) != 1 || snapshot.Edges[0] != graph.Edges[0] {
		t.Fatalf("layout snapshot = %+v", snapshot)
	}

	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	view := GraphViewOptions{Graph: graph, Labels: []GraphLabel{{Node: left, Text: "北"}}}
	node, err := tree.MountGraphView(Node{}, MountOptions{Enabled: true, Focusable: true, HitTestable: true}, view)
	mustOK(t, err)
	size := Size{Columns: 21, Rows: 5}
	mustOK(t, tree.Publish(size))
	checkHit := func(at Point, want GraphHit) {
		t.Helper()
		hit, err := tree.GraphHitAt(node, at)
		mustOK(t, err)
		if hit != want {
			t.Fatalf("hit %+v = %+v, want %+v", at, hit, want)
		}
	}
	for _, x := range []int32{7, 8} {
		checkHit(Point{X: x, Y: 2}, GraphHit{Kind: GraphHitNode, ID: left})
	}
	at := Point{X: 10, Y: 2}
	wantEdge := GraphHit{Kind: GraphHitEdge, ID: edge}
	checkHit(at, wantEdge)
	dispatch, err := tree.Dispatch(1, Input{Kind: PointerDownEvent, Pointer: PointerInput{
		ID: 1, Position: at, HasPosition: true, Button: PrimaryButton, Pressed: PrimaryButton,
	}}, DispatchPolicy{})
	mustOK(t, err)
	if !dispatch.Consumed {
		t.Fatal("pointer did not select the edge")
	}
	intent, found, err := tree.PeekEventIntent()
	mustOK(t, err)
	if !found {
		t.Fatal("no graph selection intent")
	}
	selected, err := DecodeGraphIntent(intent)
	mustOK(t, err)
	if selected != wantEdge {
		t.Fatalf("edge intent = %+v", selected)
	}
	if err := tree.SelectGraph(node, GraphHit{Kind: GraphHitNode, ID: 99}); err != InvalidArgument {
		t.Fatalf("unknown node selection = %v", err)
	}
	selected, err = tree.GraphSelection(node)
	mustOK(t, err)
	if selected != wantEdge {
		t.Fatalf("rejected selection replaced %+v", selected)
	}
	invalid := view
	invalid.Labels = []GraphLabel{{Node: left, Text: "\xff"}}
	if err := tree.SetGraphView(node, invalid); err != InvalidArgument {
		t.Fatalf("invalid label replacement = %v", err)
	}
	checkHit(at, wantEdge)
	view.Graph.Edges = nil
	mustOK(t, tree.SetGraphView(node, view))
	selected, err = tree.GraphSelection(node)
	mustOK(t, err)
	if selected != (GraphHit{}) {
		t.Fatalf("removed edge stays selected: %+v", selected)
	}
	checkHit(at, wantEdge)
	mustOK(t, tree.Publish(size))
	checkHit(at, GraphHit{})
}
