//go:build nocturne_simulation && nocturne_spatial

package nocturne

/*
#include <nocturne/simulation.h>
*/
import "C"

import "unsafe"

func init() { linked.simulation = true }

type WaterWave struct {
	Amplitude              float64
	CyclesPerX, CyclesPerZ float64
	CyclesPerSecond, Phase float64
}

type WaterSample struct {
	Height                      float64
	GradientX, GradientZ        float64
	Normal                      Vec3
	VerticalVelocity, Laplacian float64
}

type FlowSection struct {
	Width, Speed, MaximumDepth float64
}

type FlowSectionSample struct {
	Depth, CarriedRate, ExcessRate float64
}

func (section FlowSection) Sample(rate float64) (FlowSectionSample, error) {
	spec := C.NtFlowSection{width: C.double(section.Width), speed: C.double(section.Speed),
		maximum_depth: C.double(section.MaximumDepth)}
	var out C.NtFlowSectionSample
	err := check(C.nt_flow_section_sample(&spec, C.double(rate), &out))
	return FlowSectionSample{Depth: float64(out.depth), CarriedRate: float64(out.carried_rate),
		ExcessRate: float64(out.excess_rate)}, err
}

type ThermalStore struct {
	HeatCapacity, Energy float64
}

type ThermalPair struct{ First, Second ThermalStore }

func (store ThermalStore) native() C.NtThermalStore {
	return C.NtThermalStore{heat_capacity: C.double(store.HeatCapacity), energy: C.double(store.Energy)}
}

func thermalStoreFromNative(store C.NtThermalStore) ThermalStore {
	return ThermalStore{HeatCapacity: float64(store.heat_capacity), Energy: float64(store.energy)}
}

// ExchangeHeat applies conductive contact for the caller-supplied duration.
func ExchangeHeat(stores ThermalPair, conductance float64, elapsedUs uint64) (ThermalPair, error) {
	in := C.NtThermalPair{first: stores.First.native(), second: stores.Second.native()}
	var out C.NtThermalPair
	err := check(C.nt_thermal_exchange(&in, C.double(conductance), C.NtTimeUs(elapsedUs), &out))
	return ThermalPair{First: thermalStoreFromNative(out.first), Second: thermalStoreFromNative(out.second)}, err
}

// SampleWater evaluates the selected native wave modes at caller time.
func SampleWater(waves []WaterWave, x, z, seconds float64) (WaterSample, error) {
	if len(waves) > C.NT_WATER_MAX_WAVES {
		return WaterSample{}, OutOfRange
	}
	native := make([]C.NtWaterWave, len(waves))
	for index, wave := range waves {
		native[index] = C.NtWaterWave{amplitude: C.double(wave.Amplitude),
			cycles_per_unit:   C.NtPoint{x: C.double(wave.CyclesPerX), y: C.double(wave.CyclesPerZ)},
			cycles_per_second: C.double(wave.CyclesPerSecond), phase_turns: C.double(wave.Phase)}
	}
	var out C.NtWaterWaveSample
	position := C.NtPoint{x: C.double(x), y: C.double(z)}
	err := check(C.nt_water_wave_sample(unsafe.SliceData(native), C.size_t(len(native)), position, C.double(seconds), &out))
	return WaterSample{Height: float64(out.height), GradientX: float64(out.gradient.x), GradientZ: float64(out.gradient.y),
		Normal: vecFromNative(out.normal), VerticalVelocity: float64(out.vertical_velocity), Laplacian: float64(out.laplacian)}, err
}
