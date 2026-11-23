//go:build nocturne_graph

package nocturne

/*
#include <nocturne/graph.h>

*/
import "C"

import "unsafe"

func init() { linked.graph = true }

type GraphLayout uint32

const (
	GraphLayered GraphLayout = C.NT_GRAPH_LAYERED
	GraphRadial  GraphLayout = C.NT_GRAPH_RADIAL
	GraphForce   GraphLayout = C.NT_GRAPH_FORCE
)

type GraphNode struct {
	ID     uint64
	Pinned bool
	X, Y   float64
}

type GraphEdge struct {
	ID, Source, Target uint64
	Directed           bool
}

type Graph struct {
	Nodes           []GraphNode
	Edges           []GraphEdge
	Layout          GraphLayout
	ForceIterations uint32
	Preferred       Size
}

func (graph Graph) native(arena *RawArena) (C.NtGraphData, C.NtGraphLayoutOptions, error) {
	if len(graph.Nodes) > C.NT_GRAPH_MAX_NODES || len(graph.Edges) > C.NT_GRAPH_MAX_EDGES {
		return C.NtGraphData{}, C.NtGraphLayoutOptions{}, OutOfRange
	}
	nodes, err := nativeSlice[C.NtGraphNode](arena, len(graph.Nodes))
	if err != nil {
		return C.NtGraphData{}, C.NtGraphLayoutOptions{}, err
	}
	for index, node := range graph.Nodes {
		nodes[index] = C.NtGraphNode{id: C.NtGraphId(node.ID), pinned: C.bool(node.Pinned),
			position: C.NtPoint{x: C.double(node.X), y: C.double(node.Y)}}
	}
	edges, err := nativeSlice[C.NtGraphEdge](arena, len(graph.Edges))
	if err != nil {
		return C.NtGraphData{}, C.NtGraphLayoutOptions{}, err
	}
	for index, edge := range graph.Edges {
		edges[index] = C.NtGraphEdge{id: C.NtGraphId(edge.ID), source: C.NtGraphId(edge.Source),
			target: C.NtGraphId(edge.Target), directed: C.bool(edge.Directed)}
	}
	data := C.NtGraphData{nodes: unsafe.SliceData(nodes), node_count: C.size_t(len(nodes)),
		edges: unsafe.SliceData(edges), edge_count: C.size_t(len(edges))}
	layout := C.NtGraphLayoutOptions{kind: C.NtGraphLayoutKind(graph.Layout),
		force_iterations: C.uint32_t(graph.ForceIterations)}
	return data, layout, nil
}

// MountGraph mounts an optional graph leaf with default view styling.
func (tree *Tree) MountGraph(parent Node, mount MountOptions, graph Graph) (Node, error) {
	return tree.MountGraphView(parent, mount, GraphViewOptions{Graph: graph})
}

// SetGraph replaces the graph data and layout with default view styling.
func (tree *Tree) SetGraph(node Node, graph Graph) error {
	return tree.SetGraphView(node, GraphViewOptions{Graph: graph})
}
