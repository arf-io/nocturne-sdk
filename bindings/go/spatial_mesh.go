//go:build nocturne_spatial

package nocturne

/*
#include <nocturne/spatial_mesh.h>
*/
import "C"

import (
	"runtime"
	"sync"
	"unsafe"
)

type SpatialVertex struct {
	Position Vec3
	UV       ContinuousPoint
}

type SpatialFace struct {
	Indices     [3]uint32
	PrimitiveID uint64
}

type SpatialMeshData struct {
	Vertices []SpatialVertex
	Faces    []SpatialFace
}

// SpatialMesh owns immutable positions, texture coordinates and triangles.
// Copies share one owner. Close releases the native mesh.
type SpatialMesh struct{ state *spatialMeshState }

type spatialMeshState struct {
	mu     sync.Mutex
	native *C.NtSpatialMesh
	uses   int
	closed bool
}

func NewSpatialMesh(data SpatialMeshData) (*SpatialMesh, error) {
	if err := ensureABI(); err != nil {
		return nil, err
	}
	var arena RawArena
	defer arena.close()
	vertices, err := nativeSlice[C.NtSpatialVertex](&arena, len(data.Vertices))
	if err != nil {
		return nil, err
	}
	faces, err := nativeSlice[C.NtSpatialFace](&arena, len(data.Faces))
	if err != nil {
		return nil, err
	}
	for i, vertex := range data.Vertices {
		vertices[i] = C.NtSpatialVertex{position: vertex.Position.native(), uv: vertex.UV.native()}
	}
	for i, face := range data.Faces {
		out := &faces[i]
		for j, index := range face.Indices {
			out.indices[j] = C.uint32_t(index)
		}
		out.primitive_id = C.uint64_t(face.PrimitiveID)
	}
	input := C.NtSpatialMeshData{vertices: unsafe.SliceData(vertices), vertex_count: C.size_t(len(vertices)),
		faces: unsafe.SliceData(faces), face_count: C.size_t(len(faces))}
	state := &spatialMeshState{}
	if err := check(C.nt_spatial_mesh_create(nil, &input, &state.native)); err != nil {
		return nil, err
	}
	return &SpatialMesh{state}, nil
}

// DecodeSpatialOBJ reads a counted polygonal Wavefront OBJ source.
func DecodeSpatialOBJ(source string) (*SpatialMesh, error) {
	if err := ensureABI(); err != nil {
		return nil, err
	}
	state := &spatialMeshState{}
	err := check(C.nt_spatial_mesh_decode_obj(nil, nativeBytes(source), &state.native))
	runtime.KeepAlive(source)
	if err != nil {
		return nil, err
	}
	return &SpatialMesh{state}, nil
}

func (state *spatialMeshState) borrow() (*C.NtSpatialMesh, error) {
	if state == nil {
		return nil, StaleReference
	}
	state.mu.Lock()
	defer state.mu.Unlock()
	if state.closed {
		return nil, StaleReference
	}
	state.uses++
	return state.native, nil
}

func (state *spatialMeshState) release() {
	state.mu.Lock()
	defer state.mu.Unlock()
	state.uses--
	state.destroyIfUnused()
}

func (state *spatialMeshState) destroyIfUnused() {
	if state.closed && state.uses == 0 && state.native != nil {
		C.nt_spatial_mesh_destroy(state.native)
		state.native = nil
	}
}

// Data returns copies that remain valid after the mesh is closed.
func (mesh *SpatialMesh) Data() (SpatialMeshData, error) {
	if mesh == nil {
		return SpatialMeshData{}, StaleReference
	}
	native, err := mesh.state.borrow()
	if err != nil {
		return SpatialMeshData{}, err
	}
	defer mesh.state.release()
	var out C.NtSpatialMeshData
	if err := check(C.nt_spatial_mesh_data(native, &out)); err != nil {
		return SpatialMeshData{}, err
	}
	data := SpatialMeshData{Vertices: make([]SpatialVertex, int(out.vertex_count)),
		Faces: make([]SpatialFace, int(out.face_count))}
	for i, vertex := range unsafe.Slice(out.vertices, len(data.Vertices)) {
		data.Vertices[i] = SpatialVertex{Position: vecFromNative(vertex.position), UV: plotPointFromNative(vertex.uv)}
	}
	for i, face := range unsafe.Slice(out.faces, len(data.Faces)) {
		result := &data.Faces[i]
		for j, index := range face.indices {
			result.Indices[j] = uint32(index)
		}
		result.PrimitiveID = uint64(face.primitive_id)
	}
	return data, nil
}

// Close invalidates the mesh and releases it once admitted calls finish.
func (mesh *SpatialMesh) Close() {
	if mesh == nil || mesh.state == nil {
		return
	}
	state := mesh.state
	state.mu.Lock()
	defer state.mu.Unlock()
	state.closed = true
	state.destroyIfUnused()
}
