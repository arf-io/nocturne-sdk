//go:build nocturne_simulation && nocturne_spatial

package nocturne

/*
#include <nocturne/simulation.h>
*/
import "C"

// FloatingBody is a body-to-world pose and caller-owned state at its center of mass.
type FloatingBody struct {
	Position, RotationAxis          Vec3
	RotationRadians, Mass           float64
	PrincipalInertia                Vec3
	LinearVelocity, AngularVelocity Vec3
}

type BodyLoad struct {
	Force, Torque Vec3
}

func (body FloatingBody) native() C.NtFloatingBody {
	return C.NtFloatingBody{position: body.Position.native(), rotation_axis: body.RotationAxis.native(),
		rotation_radians: C.double(body.RotationRadians), mass: C.double(body.Mass), principal_inertia: body.PrincipalInertia.native(),
		linear_velocity: body.LinearVelocity.native(), angular_velocity: body.AngularVelocity.native()}
}

func floatingBodyFromNative(body C.NtFloatingBody) FloatingBody {
	return FloatingBody{Position: vecFromNative(body.position), RotationAxis: vecFromNative(body.rotation_axis),
		RotationRadians: float64(body.rotation_radians), Mass: float64(body.mass), PrincipalInertia: vecFromNative(body.principal_inertia),
		LinearVelocity: vecFromNative(body.linear_velocity), AngularVelocity: vecFromNative(body.angular_velocity)}
}

func (body FloatingBody) Transform() (SpatialTransform, error) {
	input := body.native()
	var out C.NtSpatialTransform
	err := check(C.nt_floating_body_transform(&input, &out))
	return spatialTransformFromNative(out), err
}

// Step applies a constant world-frame load for the caller's elapsed time.
func (body FloatingBody) Step(load BodyLoad, elapsedUs uint64) (FloatingBody, error) {
	input := body.native()
	forcing := C.NtBodyLoad{force: load.Force.native(), torque: load.Torque.native()}
	var out C.NtFloatingBody
	err := check(C.nt_floating_body_step(&input, &forcing, C.NtTimeUs(elapsedUs), &out))
	return floatingBodyFromNative(out), err
}

type LinearStore struct {
	Mass     float64
	Velocity Vec3
}

type LinearPair struct{ First, Second LinearStore }

type AngularStore struct {
	Inertia, Velocity Vec3
}

type AngularPair struct{ First, Second AngularStore }

type DragResult struct {
	FirstVelocity, SecondVelocity, ImpulseOnSecond Vec3
	DissipatedEnergy                               float64
}

func dragResultFromNative(out C.NtDragResult) DragResult {
	return DragResult{FirstVelocity: vecFromNative(out.first_velocity), SecondVelocity: vecFromNative(out.second_velocity),
		ImpulseOnSecond: vecFromNative(out.impulse_on_second), DissipatedEnergy: float64(out.dissipated_energy)}
}

func (store LinearStore) native() C.NtLinearStore {
	return C.NtLinearStore{mass: C.double(store.Mass), velocity: store.Velocity.native()}
}

// ExchangeLinearDrag advances two isolated masses in one shared frame.
func ExchangeLinearDrag(stores LinearPair, conductance float64, elapsedUs uint64) (DragResult, error) {
	input := C.NtLinearPair{first: stores.First.native(), second: stores.Second.native()}
	var out C.NtDragResult
	err := check(C.nt_linear_drag_exchange(&input, C.double(conductance), C.NtTimeUs(elapsedUs), &out))
	return dragResultFromNative(out), err
}

func (store AngularStore) native() C.NtAngularStore {
	return C.NtAngularStore{inertia: store.Inertia.native(), velocity: store.Velocity.native()}
}

// ExchangeAngularDrag uses diagonal inertias and conductances in one principal frame.
func ExchangeAngularDrag(stores AngularPair, conductance Vec3, elapsedUs uint64) (DragResult, error) {
	input := C.NtAngularPair{first: stores.First.native(), second: stores.Second.native()}
	var out C.NtDragResult
	err := check(C.nt_angular_drag_exchange(&input, conductance.native(), C.NtTimeUs(elapsedUs), &out))
	return dragResultFromNative(out), err
}
