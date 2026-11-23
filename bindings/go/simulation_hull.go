//go:build nocturne_simulation && nocturne_spatial

package nocturne

/*
#include <nocturne/simulation.h>
*/
import "C"

import (
	"sync"
	"unsafe"
)

type HullTriangle [3]uint32

type HullGeometry struct {
	Vertices  []Vec3
	Triangles []HullTriangle
}

// Hull owns a closed, outward-wound convex boundary. Copies share its owner.
type Hull struct{ state *hullState }

type hullState struct {
	mu     sync.RWMutex
	native *C.NtHull
}

type WaterPlane struct {
	Point, Normal Vec3
}

type WaterTriangle [3]Vec3

type HullImmersion struct {
	FullVolume, DisplacedVolume float64
	Center, CenterOffset        Vec3
}

type Buoyancy struct {
	Lift, Weight, Force, Torque Vec3
}

func NewHull(geometry HullGeometry) (*Hull, error) {
	if err := ensureABI(); err != nil {
		return nil, err
	}
	if len(geometry.Vertices) > C.NT_HULL_MAX_VERTICES || len(geometry.Triangles) > C.NT_HULL_MAX_TRIANGLES {
		return nil, OutOfRange
	}
	var arena RawArena
	defer arena.close()
	vertices, err := nativeSlice[C.NtVec3](&arena, len(geometry.Vertices))
	if err != nil {
		return nil, err
	}
	triangles, err := nativeSlice[C.NtHullTriangle](&arena, len(geometry.Triangles))
	if err != nil {
		return nil, err
	}
	for i, vertex := range geometry.Vertices {
		vertices[i] = vertex.native()
	}
	for i, triangle := range geometry.Triangles {
		for j, index := range triangle {
			triangles[i].indices[j] = C.uint32_t(index)
		}
	}
	input := C.NtHullGeometry{vertices: unsafe.SliceData(vertices), vertex_count: C.size_t(len(vertices)),
		triangles: unsafe.SliceData(triangles), triangle_count: C.size_t(len(triangles))}
	state := &hullState{}
	if err := check(C.nt_hull_create(nil, &input, &state.native)); err != nil {
		return nil, err
	}
	return &Hull{state}, nil
}

func (hull *Hull) immersion(call func(*C.NtHull, *C.NtHullImmersion) C.NtResult) (HullImmersion, error) {
	if hull == nil || hull.state == nil {
		return HullImmersion{}, StaleReference
	}
	state := hull.state
	state.mu.RLock()
	defer state.mu.RUnlock()
	if state.native == nil {
		return HullImmersion{}, StaleReference
	}
	var out C.NtHullImmersion
	err := check(call(state.native, &out))
	return HullImmersion{FullVolume: float64(out.full_volume), DisplacedVolume: float64(out.displaced_volume),
		Center: vecFromNative(out.center), CenterOffset: vecFromNative(out.center_offset)}, err
}

// ImmersePlane clips the hull against the water below a world plane.
func (hull *Hull) ImmersePlane(body FloatingBody, plane WaterPlane) (HullImmersion, error) {
	placement := body.native()
	water := C.NtWaterPlane{point: plane.Point.native(), normal: plane.Normal.native()}
	return hull.immersion(func(native *C.NtHull, out *C.NtHullImmersion) C.NtResult {
		return C.nt_hull_immerse_plane(native, &placement, &water, out)
	})
}

// ImmerseWater queries the finite columns below the supplied water triangles.
func (hull *Hull) ImmerseWater(body FloatingBody, triangles []WaterTriangle) (HullImmersion, error) {
	if len(triangles) > C.NT_WATER_MAX_TRIANGLES {
		return HullImmersion{}, OutOfRange
	}
	var arena RawArena
	defer arena.close()
	input, err := nativeSlice[C.NtWaterTriangle](&arena, len(triangles))
	if err != nil {
		return HullImmersion{}, err
	}
	for i, triangle := range triangles {
		input[i].vertices = [3]C.NtVec3{triangle[0].native(), triangle[1].native(), triangle[2].native()}
	}
	placement := body.native()
	return hull.immersion(func(native *C.NtHull, out *C.NtHullImmersion) C.NtResult {
		return C.nt_hull_immerse_water(native, &placement, unsafe.SliceData(input), C.size_t(len(input)), out)
	})
}

// Buoyancy returns world forces and torque about the supplied body's center of mass.
func (immersion HullImmersion) Buoyancy(body FloatingBody, density, gravity float64) (Buoyancy, error) {
	input := C.NtHullImmersion{full_volume: C.double(immersion.FullVolume), displaced_volume: C.double(immersion.DisplacedVolume),
		center: immersion.Center.native(), center_offset: immersion.CenterOffset.native()}
	placement := body.native()
	var out C.NtBuoyancy
	err := check(C.nt_hull_buoyancy(&input, &placement, C.double(density), C.double(gravity), &out))
	return Buoyancy{Lift: vecFromNative(out.lift), Weight: vecFromNative(out.weight),
		Force: vecFromNative(out.force), Torque: vecFromNative(out.torque)}, err
}

func (hull *Hull) Close() {
	if hull == nil || hull.state == nil {
		return
	}
	state := hull.state
	state.mu.Lock()
	defer state.mu.Unlock()
	if state.native != nil {
		C.nt_hull_destroy(state.native)
		state.native = nil
	}
}
