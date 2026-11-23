//go:build nocturne_spatial

package nocturne

/*
#include <nocturne/spatial.h>
*/
import "C"

import "unsafe"

type SpatialProjectionKind uint32

const (
	SpatialPerspective  SpatialProjectionKind = C.NT_SPATIAL_PERSPECTIVE
	SpatialOrthographic SpatialProjectionKind = C.NT_SPATIAL_ORTHOGRAPHIC
)

// SpatialProjection uses depth -view.Z. Perspective half extents are slopes
// at depth one. Orthographic half extents are distances in view units.
type SpatialProjection struct {
	Kind                  SpatialProjectionKind
	HalfWidth, HalfHeight float64
	NearDepth, FarDepth   float64
}

// SpatialScreen is normalized to [-1,1] with X rightward and Y upward.
type SpatialScreen struct{ X, Y float64 }

type SpatialProjected struct {
	Screen SpatialScreen
	Depth  float64
}

type SpatialClipVertex struct {
	View, Weights Vec3
	Projected     SpatialProjected
}

type SpatialClippedSegment struct{ First, Last SpatialClipVertex }

type SpatialPickTriangle struct {
	View                  [3]Vec3
	ObjectID, PrimitiveID uint64
}

type SpatialHit struct {
	ObjectID, PrimitiveID uint64
	TriangleIndex         uint
	View, Weights         Vec3
	Depth                 float64
}

func (projection SpatialProjection) native() C.NtSpatialProjection {
	return C.NtSpatialProjection{kind: C.NtSpatialProjectionKind(projection.Kind),
		half_width: C.double(projection.HalfWidth), half_height: C.double(projection.HalfHeight),
		near_depth: C.double(projection.NearDepth), far_depth: C.double(projection.FarDepth)}
}

func (screen SpatialScreen) native() C.NtSpatialScreen {
	return C.NtSpatialScreen{x: C.double(screen.X), y: C.double(screen.Y)}
}

func spatialProjectedFromNative(projected C.NtSpatialProjected) SpatialProjected {
	return SpatialProjected{Screen: SpatialScreen{X: float64(projected.screen.x), Y: float64(projected.screen.y)},
		Depth: float64(projected.depth)}
}

func spatialClipVertexFromNative(vertex C.NtSpatialClipVertex) SpatialClipVertex {
	return SpatialClipVertex{View: vecFromNative(vertex.view), Weights: vecFromNative(vertex.weights),
		Projected: spatialProjectedFromNative(vertex.projected)}
}

// Project reports whether the point lies inside the closed frustum.
func (projection SpatialProjection) Project(view Vec3) (SpatialProjected, bool, error) {
	native := projection.native()
	var out C.NtSpatialProjected
	var visible C.bool
	err := check(C.nt_spatial_project(&native, view.native(), &visible, &out))
	return spatialProjectedFromNative(out), bool(visible), err
}

func (projection SpatialProjection) Unproject(screen SpatialScreen, depth float64) (Vec3, error) {
	native := projection.native()
	var out C.NtVec3
	err := check(C.nt_spatial_unproject(&native, screen.native(), C.double(depth), &out))
	return vecFromNative(out), err
}

// ClipSegment preserves the endpoint order and their weights.
func (projection SpatialProjection) ClipSegment(first, last Vec3) (SpatialClippedSegment, bool, error) {
	native := projection.native()
	var out C.NtSpatialClippedSegment
	var visible C.bool
	err := check(C.nt_spatial_clip_segment(&native, first.native(), last.native(), &visible, &out))
	return SpatialClippedSegment{First: spatialClipVertexFromNative(out.first),
		Last: spatialClipVertexFromNative(out.last)}, bool(visible), err
}

// ClipTriangle returns the ordered clipped polygon, including the original
// barycentric weights and projection of each vertex.
func (projection SpatialProjection) ClipTriangle(view [3]Vec3) ([]SpatialClipVertex, error) {
	native := projection.native()
	points := [3]C.NtVec3{view[0].native(), view[1].native(), view[2].native()}
	var out C.NtSpatialClippedPolygon
	if err := check(C.nt_spatial_clip_triangle(&native, &points[0], &out)); err != nil {
		return nil, err
	}
	vertices := make([]SpatialClipVertex, int(out.count))
	for i := range vertices {
		vertices[i] = spatialClipVertexFromNative(out.vertices[i])
	}
	return vertices, nil
}

// Pick returns the nearest hit and preserves input order on exact depth ties.
func (projection SpatialProjection) Pick(screen SpatialScreen, triangles []SpatialPickTriangle) (SpatialHit, bool, error) {
	var arena RawArena
	defer arena.close()
	input, err := nativeSlice[C.NtSpatialPickTriangle](&arena, len(triangles))
	if err != nil {
		return SpatialHit{}, false, err
	}
	for i, triangle := range triangles {
		input[i] = C.NtSpatialPickTriangle{view: [3]C.NtVec3{triangle.View[0].native(),
			triangle.View[1].native(), triangle.View[2].native()},
			object_id: C.uint64_t(triangle.ObjectID), primitive_id: C.uint64_t(triangle.PrimitiveID)}
	}
	native := projection.native()
	var out C.NtSpatialHit
	var hit C.bool
	err = check(C.nt_spatial_pick(&native, screen.native(), unsafe.SliceData(input), C.size_t(len(input)), &hit, &out))
	return SpatialHit{ObjectID: uint64(out.object_id), PrimitiveID: uint64(out.primitive_id),
		TriangleIndex: uint(out.triangle_index), View: vecFromNative(out.view),
		Weights: vecFromNative(out.weights), Depth: float64(out.depth)}, bool(hit), err
}
