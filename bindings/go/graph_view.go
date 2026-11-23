//go:build nocturne_graph

package nocturne

/*
#include <nocturne/graph.h>
*/
import "C"

import "unsafe"

// GraphSnapshot contains copied layout placements and edges in ID order.
type GraphSnapshot struct {
	Nodes []GraphNode
	Edges []GraphEdge
}

func (graph Graph) LayoutSnapshot() (GraphSnapshot, error) {
	if err := ensureABI(); err != nil {
		return GraphSnapshot{}, err
	}
	var arena RawArena
	defer arena.close()
	data, layout, err := graph.native(&arena)
	if err != nil {
		return GraphSnapshot{}, err
	}
	var native *C.NtGraphLayout
	if err := check(C.nt_graph_layout_create(nil, data, layout, &native)); err != nil {
		return GraphSnapshot{}, err
	}
	defer C.nt_graph_layout_destroy(native)

	var count C.size_t
	placements := C.nt_graph_layout_nodes(native, &count)
	nodes := make([]GraphNode, int(count))
	for index, point := range unsafe.Slice(placements, len(nodes)) {
		nodes[index] = GraphNode{ID: uint64(point.id), Pinned: bool(point.pinned),
			X: float64(point.position.x), Y: float64(point.position.y)}
	}
	records := C.nt_graph_layout_edges(native, &count)
	edges := make([]GraphEdge, int(count))
	for index, edge := range unsafe.Slice(records, len(edges)) {
		edges[index] = GraphEdge{ID: uint64(edge.id), Source: uint64(edge.source),
			Target: uint64(edge.target), Directed: bool(edge.directed)}
	}
	return GraphSnapshot{Nodes: nodes, Edges: edges}, nil
}

type GraphLabel struct {
	Node uint64
	Text string
}

type GraphViewColors struct {
	Node, Pinned, Edge, Selected, Background Color
}

// GraphViewOptions configures graph data, labels and paint. Omit Width and
// Colors to use the native view defaults.
type GraphViewOptions struct {
	Graph  Graph
	Labels []GraphLabel
	Width  *WidthPolicy
	Colors *GraphViewColors
}

func (options GraphViewOptions) native(arena *RawArena) (C.NtGraphViewOptions, error) {
	data, layout, err := options.Graph.native(arena)
	if err != nil {
		return C.NtGraphViewOptions{}, err
	}
	if len(options.Labels) > C.NT_GRAPH_MAX_NODES {
		return C.NtGraphViewOptions{}, OutOfRange
	}
	labels, err := nativeSlice[C.NtGraphLabel](arena, len(options.Labels))
	if err != nil {
		return C.NtGraphViewOptions{}, err
	}
	for index, label := range options.Labels {
		if len(label.Text) > C.NT_GRAPH_MAX_LABEL_BYTES {
			return C.NtGraphViewOptions{}, OutOfRange
		}
		text, err := arenaByteView(arena, label.Text)
		if err != nil {
			return C.NtGraphViewOptions{}, err
		}
		labels[index] = C.NtGraphLabel{node: C.NtGraphId(label.Node), text: text}
	}
	view := C.nt_graph_view_options_default()
	view.data, view.layout = data, layout
	view.labels, view.label_count = unsafe.SliceData(labels), C.size_t(len(labels))
	if options.Graph.Preferred != (Size{}) {
		view.preferred = options.Graph.Preferred.native()
	}
	if options.Width != nil {
		view.width = options.Width.native()
	}
	if colors := options.Colors; colors != nil {
		view.node, view.pinned, view.edge = colors.Node.native(), colors.Pinned.native(), colors.Edge.native()
		view.selected, view.background = colors.Selected.native(), colors.Background.native()
	}
	return view, nil
}

func (tree *Tree) MountGraphView(parent Node, mount MountOptions, options GraphViewOptions) (Node, error) {
	var arena RawArena
	defer arena.close()
	view, err := options.native(&arena)
	if err != nil {
		return Node{}, err
	}
	return tree.mountNative(parent, func(tree *C.NtTree, parent C.NtNodeId, out *C.NtNodeId) C.NtResult {
		return C.nt_graph_view_mount(tree, parent, mount.native(), &view, out)
	})
}

func (tree *Tree) SetGraphView(node Node, options GraphViewOptions) error {
	var arena RawArena
	defer arena.close()
	view, err := options.native(&arena)
	if err != nil {
		return err
	}
	return tree.callNode(node, func(tree *C.NtTree, id C.NtNodeId) C.NtResult {
		return C.nt_graph_view_set(tree, id, &view)
	})
}

type GraphHitKind uint32

const (
	GraphHitNone GraphHitKind = C.NT_GRAPH_HIT_NONE
	GraphHitNode GraphHitKind = C.NT_GRAPH_HIT_NODE
	GraphHitEdge GraphHitKind = C.NT_GRAPH_HIT_EDGE
)

const GraphSelectionIntent = C.NT_INTENT_GRAPH_SELECTION

type GraphHit struct {
	Kind GraphHitKind
	ID   uint64
}

func graphHitFromNative(hit C.NtGraphHit) GraphHit {
	return GraphHit{Kind: GraphHitKind(hit.kind), ID: uint64(hit.id)}
}

// GraphHitAt reads the last published hit map in frame coordinates.
func (tree *Tree) GraphHitAt(node Node, at Point) (GraphHit, error) {
	var out C.NtGraphHit
	err := tree.callNode(node, func(tree *C.NtTree, id C.NtNodeId) C.NtResult {
		return C.nt_graph_view_hit(tree, id, at.native(), &out)
	})
	return graphHitFromNative(out), err
}

func (tree *Tree) GraphSelection(node Node) (GraphHit, error) {
	var out C.NtGraphHit
	err := tree.callNode(node, func(tree *C.NtTree, id C.NtNodeId) C.NtResult {
		return C.nt_graph_view_selection(tree, id, &out)
	})
	return graphHitFromNative(out), err
}

// SelectGraph changes the selection without emitting an intent.
func (tree *Tree) SelectGraph(node Node, selection GraphHit) error {
	return tree.callNode(node, func(tree *C.NtTree, id C.NtNodeId) C.NtResult {
		hit := C.NtGraphHit{kind: C.NtGraphHitKind(selection.Kind), id: C.NtGraphId(selection.ID)}
		return C.nt_graph_view_select(tree, id, hit)
	})
}

func DecodeGraphIntent(intent EventIntent) (GraphHit, error) {
	if err := ensureABI(); err != nil {
		return GraphHit{}, err
	}
	if intent.Kind != GraphSelectionIntent || intent.PayloadKind != IntentFields || len(intent.Fields) != 2 {
		return GraphHit{}, InvalidArgument
	}
	var arena RawArena
	defer arena.close()
	fields, err := nativeSlice[C.NtIntentField](&arena, 2)
	if err != nil {
		return GraphHit{}, err
	}
	for index, field := range intent.Fields {
		if field.Value.Kind != UnsignedValue {
			return GraphHit{}, InvalidArgument
		}
		fields[index].id = C.uint32_t(field.ID)
		if err := field.Value.writeNative(&arena, &fields[index].value); err != nil {
			return GraphHit{}, err
		}
	}
	native := C.NtEventIntentView{intent: C.NtIntentView{kind: C.uint32_t(intent.Kind)},
		payload_kind: C.NT_INTENT_FIELDS, fields: unsafe.SliceData(fields), field_count: 2}
	var out C.NtGraphHit
	err = check(C.nt_graph_event_decode(native, &out))
	return graphHitFromNative(out), err
}
