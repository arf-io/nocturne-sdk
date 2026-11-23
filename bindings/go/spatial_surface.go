//go:build nocturne_spatial

package nocturne

/*
#include <nocturne/spatial_surface.h>
*/
import "C"

import "unsafe"

// SpatialSurfaceMotion describes a time-zero world pivot and constant world
// linear and angular velocity. Angular velocity is in radians per second.
type SpatialSurfaceMotion struct {
	Pivot, LinearVelocity, AngularVelocity Vec3
}

type SpatialSurfaceSample struct {
	Position, Velocity Vec3
}

func (motion SpatialSurfaceMotion) native() C.NtSpatialSurfaceMotion {
	return C.NtSpatialSurfaceMotion{pivot: motion.Pivot.native(), linear_velocity: motion.LinearVelocity.native(),
		angular_velocity: motion.AngularVelocity.native()}
}

// Pose maps time-zero world coordinates to the requested signed elapsed time.
func (motion SpatialSurfaceMotion) Pose(seconds float64) (SpatialTransform, error) {
	native := motion.native()
	var out C.NtSpatialTransform
	err := check(C.nt_spatial_surface_pose(&native, C.double(seconds), &out))
	return spatialTransformFromNative(out), err
}

// Sample returns the point's position and instantaneous world velocity.
func (motion SpatialSurfaceMotion) Sample(seconds float64, initial Vec3) (SpatialSurfaceSample, error) {
	native := motion.native()
	var out C.NtSpatialSurfaceSample
	err := check(C.nt_spatial_surface_sample(&native, C.double(seconds), initial.native(), &out))
	return SpatialSurfaceSample{Position: vecFromNative(out.position), Velocity: vecFromNative(out.velocity)}, err
}

type SpatialSweepTriangle struct {
	Initial               [3]Vec3
	Displacement          Vec3
	ObjectID, PrimitiveID uint64
}

type SpatialContact struct {
	ObjectID, PrimitiveID     uint64
	TriangleIndex             uint
	Fraction                  float64
	Position, Weights, Normal Vec3
}

// SweepSpatialPoint finds the first contact with translating triangles.
// The closed interval includes coplanar entry and exact ties keep input order.
func SweepSpatialPoint(first, last Vec3, triangles []SpatialSweepTriangle) (SpatialContact, bool, error) {
	var arena RawArena
	defer arena.close()
	input, err := nativeSlice[C.NtSpatialSweepTriangle](&arena, len(triangles))
	if err != nil {
		return SpatialContact{}, false, err
	}
	for i, triangle := range triangles {
		input[i] = C.NtSpatialSweepTriangle{
			initial:      [3]C.NtVec3{triangle.Initial[0].native(), triangle.Initial[1].native(), triangle.Initial[2].native()},
			displacement: triangle.Displacement.native(),
			object_id:    C.uint64_t(triangle.ObjectID), primitive_id: C.uint64_t(triangle.PrimitiveID),
		}
	}
	var out C.NtSpatialContact
	var found C.bool
	err = check(C.nt_spatial_sweep_point(first.native(), last.native(), unsafe.SliceData(input),
		C.size_t(len(input)), &found, &out))
	return SpatialContact{ObjectID: uint64(out.object_id), PrimitiveID: uint64(out.primitive_id),
		TriangleIndex: uint(out.triangle_index), Fraction: float64(out.fraction),
		Position: vecFromNative(out.position), Weights: vecFromNative(out.weights),
		Normal: vecFromNative(out.normal)}, bool(found), err
}
