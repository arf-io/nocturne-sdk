//go:build nocturne_spatial

package nocturne

/*
#include <nocturne/spatial.h>
*/
import "C"

func init() { linked.spatial = true }

type Vec3 struct{ X, Y, Z float64 }

func (value Vec3) native() C.NtVec3 {
	return C.NtVec3{x: C.double(value.X), y: C.double(value.Y), z: C.double(value.Z)}
}

func vecFromNative(value C.NtVec3) Vec3 {
	return Vec3{X: float64(value.x), Y: float64(value.y), Z: float64(value.z)}
}

func (left Vec3) Add(right Vec3) (Vec3, error) {
	var out C.NtVec3
	err := check(C.nt_spatial_vec_add(left.native(), right.native(), &out))
	return vecFromNative(out), err
}

func (left Vec3) Subtract(right Vec3) (Vec3, error) {
	var out C.NtVec3
	err := check(C.nt_spatial_vec_subtract(left.native(), right.native(), &out))
	return vecFromNative(out), err
}

func (value Vec3) Scale(factor float64) (Vec3, error) {
	var out C.NtVec3
	err := check(C.nt_spatial_vec_scale(value.native(), C.double(factor), &out))
	return vecFromNative(out), err
}

func (left Vec3) Dot(right Vec3) (float64, error) {
	var out C.double
	err := check(C.nt_spatial_vec_dot(left.native(), right.native(), &out))
	return float64(out), err
}

// Cross calls the native spatial cross product.
func (left Vec3) Cross(right Vec3) (Vec3, error) {
	var out C.NtVec3
	err := check(C.nt_spatial_vec_cross(left.native(), right.native(), &out))
	return vecFromNative(out), err
}

// Normalize returns the native unit vector or the native result on invalid input.
func (value Vec3) Normalize() (Vec3, error) {
	var out C.NtVec3
	err := check(C.nt_spatial_vec_normalize(value.native(), &out))
	return vecFromNative(out), err
}

func (value Vec3) Length() (float64, error) {
	var out C.double
	err := check(C.nt_spatial_vec_length(value.native(), &out))
	return float64(out), err
}

// SpatialTransform applies its row-major linear matrix to column vectors.
type SpatialTransform struct {
	Linear      [3][3]float64
	Translation Vec3
}

type SpatialTransformOptions struct {
	Translation, Pivot, Scale, RotationAxis Vec3
	RotationRadians                         float64
}

func (transform SpatialTransform) native() C.NtSpatialTransform {
	out := C.NtSpatialTransform{translation: transform.Translation.native()}
	for row := range transform.Linear {
		for column, value := range transform.Linear[row] {
			out.linear[row][column] = C.double(value)
		}
	}
	return out
}

func spatialTransformFromNative(transform C.NtSpatialTransform) SpatialTransform {
	out := SpatialTransform{Translation: vecFromNative(transform.translation)}
	for row := range transform.linear {
		for column, value := range transform.linear[row] {
			out.Linear[row][column] = float64(value)
		}
	}
	return out
}

func IdentitySpatialTransform() SpatialTransform {
	return spatialTransformFromNative(C.nt_spatial_transform_identity())
}

func DefaultSpatialTransformOptions() SpatialTransformOptions {
	options := C.nt_spatial_transform_options_default()
	return SpatialTransformOptions{
		Translation: vecFromNative(options.translation), Pivot: vecFromNative(options.pivot),
		Scale: vecFromNative(options.scale), RotationAxis: vecFromNative(options.rotation_axis),
		RotationRadians: float64(options.rotation_radians),
	}
}

// NewSpatialTransform scales, rotates about the pivot, and then translates.
func NewSpatialTransform(options SpatialTransformOptions) (SpatialTransform, error) {
	native := C.NtSpatialTransformOptions{translation: options.Translation.native(), pivot: options.Pivot.native(),
		scale: options.Scale.native(), rotation_axis: options.RotationAxis.native(),
		rotation_radians: C.double(options.RotationRadians)}
	var out C.NtSpatialTransform
	err := check(C.nt_spatial_transform_create(&native, &out))
	return spatialTransformFromNative(out), err
}

// Compose applies inner first and outer second.
func (outer SpatialTransform) Compose(inner SpatialTransform) (SpatialTransform, error) {
	a, b := outer.native(), inner.native()
	var out C.NtSpatialTransform
	err := check(C.nt_spatial_transform_compose(&a, &b, &out))
	return spatialTransformFromNative(out), err
}

func (transform SpatialTransform) Inverse() (SpatialTransform, error) {
	native := transform.native()
	var out C.NtSpatialTransform
	err := check(C.nt_spatial_transform_inverse(&native, &out))
	return spatialTransformFromNative(out), err
}

func (transform SpatialTransform) Point(point Vec3) (Vec3, error) {
	native := transform.native()
	var out C.NtVec3
	err := check(C.nt_spatial_transform_point(&native, point.native(), &out))
	return vecFromNative(out), err
}

// Direction applies the linear part of the transform.
func (transform SpatialTransform) Direction(direction Vec3) (Vec3, error) {
	native := transform.native()
	var out C.NtVec3
	err := check(C.nt_spatial_transform_direction(&native, direction.native(), &out))
	return vecFromNative(out), err
}

// SpatialCamera is a right-handed camera pose in world coordinates.
type SpatialCamera struct{ Position, Right, Up, Backward Vec3 }

func (camera SpatialCamera) native() C.NtSpatialCamera {
	return C.NtSpatialCamera{position: camera.Position.native(), right: camera.Right.native(),
		up: camera.Up.native(), backward: camera.Backward.native()}
}

func spatialCameraFromNative(camera C.NtSpatialCamera) SpatialCamera {
	return SpatialCamera{Position: vecFromNative(camera.position), Right: vecFromNative(camera.right),
		Up: vecFromNative(camera.up), Backward: vecFromNative(camera.backward)}
}

func SpatialLookAt(position, target, upHint Vec3) (SpatialCamera, error) {
	var out C.NtSpatialCamera
	err := check(C.nt_spatial_camera_look_at(position.native(), target.native(), upHint.native(), &out))
	return spatialCameraFromNative(out), err
}

func SpatialOrbit(target Vec3, radius, azimuth, elevation float64) (SpatialCamera, error) {
	var out C.NtSpatialCamera
	err := check(C.nt_spatial_camera_orbit(target.native(), C.double(radius), C.double(azimuth), C.double(elevation), &out))
	return spatialCameraFromNative(out), err
}

func (camera SpatialCamera) Fly(localTranslation Vec3, yaw, pitch, roll float64) (SpatialCamera, error) {
	native := camera.native()
	var out C.NtSpatialCamera
	err := check(C.nt_spatial_camera_fly(&native, localTranslation.native(), C.double(yaw), C.double(pitch), C.double(roll), &out))
	return spatialCameraFromNative(out), err
}

func (camera SpatialCamera) ToView(worldPoint Vec3) (Vec3, error) {
	native := camera.native()
	var out C.NtVec3
	err := check(C.nt_spatial_camera_to_view(&native, worldPoint.native(), &out))
	return vecFromNative(out), err
}
