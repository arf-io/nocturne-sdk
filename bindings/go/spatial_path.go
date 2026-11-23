//go:build nocturne_spatial

package nocturne

/*
#include <nocturne/spatial.h>
*/
import "C"

import (
	"sync"
	"unsafe"
)

type SpatialPathOptions struct {
	Points   []Vec3
	Closed   bool
	Boundary Repeat
}

type SpatialPathSample struct {
	Position, Tangent Vec3
	Distance, Length  float64
}

// SpatialPath owns an immutable native polyline. Copies share one owner.
type SpatialPath struct{ state *spatialPathState }

type spatialPathState struct {
	mu     sync.RWMutex
	native *C.NtSpatialPath
}

// NewSpatialPath copies the points and resolves lengths in world units.
func NewSpatialPath(options SpatialPathOptions) (*SpatialPath, error) {
	var arena RawArena
	defer arena.close()
	points, err := nativeSlice[C.NtVec3](&arena, len(options.Points))
	if err != nil {
		return nil, err
	}
	for i, point := range options.Points {
		points[i] = point.native()
	}
	native := C.NtSpatialPathOptions{points: unsafe.SliceData(points), count: C.size_t(len(points)),
		closed: C.bool(options.Closed), boundary: C.NtRepeat(options.Boundary)}
	state := &spatialPathState{}
	if err := check(C.nt_spatial_path_create(nil, &native, &state.native)); err != nil {
		return nil, err
	}
	return &SpatialPath{state: state}, nil
}

func (path *SpatialPath) borrow() (*spatialPathState, error) {
	if path == nil || path.state == nil {
		return nil, StaleReference
	}
	state := path.state
	state.mu.RLock()
	if state.native == nil {
		state.mu.RUnlock()
		return nil, StaleReference
	}
	return state, nil
}

func (path *SpatialPath) Length() (float64, error) {
	state, err := path.borrow()
	if err != nil {
		return 0, err
	}
	defer state.mu.RUnlock()
	var out C.double
	err = check(C.nt_spatial_path_length(state.native, &out))
	return float64(out), err
}

func (path *SpatialPath) Sample(distance float64) (SpatialPathSample, error) {
	state, err := path.borrow()
	if err != nil {
		return SpatialPathSample{}, err
	}
	defer state.mu.RUnlock()
	var out C.NtSpatialPathSample
	err = check(C.nt_spatial_path_sample(state.native, C.double(distance), &out))
	return SpatialPathSample{Position: vecFromNative(out.position), Tangent: vecFromNative(out.tangent),
		Distance: float64(out.distance), Length: float64(out.length)}, err
}

func (path *SpatialPath) Close() {
	if path == nil || path.state == nil {
		return
	}
	state := path.state
	state.mu.Lock()
	defer state.mu.Unlock()
	if state.native != nil {
		C.nt_spatial_path_destroy(state.native)
		state.native = nil
	}
}
