//go:build nocturne_simulation && nocturne_spatial

package nocturne

/*
#include <nocturne/simulation.h>
*/
import "C"

import "unsafe"

type FlowStage struct {
	TransitUs, ResidenceUs uint64
}

type FlowInput struct {
	BeginUs, EndUs uint64
	Rate           float64
}

// Flow describes serial transit and reservoirs fed by half-open inlet intervals.
type Flow struct {
	Stages []FlowStage
	Inputs []FlowInput
}

type FlowPort struct {
	Volume, Rate float64
}

type FlowSample struct {
	Ports    []FlowPort // Inlet, then each successive outlet.
	Retained []float64  // Transit plus reservoir volume per stage.
}

// Sample evaluates a fresh snapshot at the caller's microsecond timestamp.
func (flow Flow) Sample(timeUs uint64) (FlowSample, error) {
	if len(flow.Stages) > C.NT_FLOW_MAX_STAGES || len(flow.Inputs) > C.NT_FLOW_MAX_INPUTS {
		return FlowSample{}, OutOfRange
	}
	var arena RawArena
	defer arena.close()
	stages, err := nativeSlice[C.NtFlowStage](&arena, len(flow.Stages))
	if err != nil {
		return FlowSample{}, err
	}
	for i, stage := range flow.Stages {
		stages[i] = C.NtFlowStage{transit_us: C.NtTimeUs(stage.TransitUs), residence_us: C.NtTimeUs(stage.ResidenceUs)}
	}
	inputs, err := nativeSlice[C.NtFlowInput](&arena, len(flow.Inputs))
	if err != nil {
		return FlowSample{}, err
	}
	for i, input := range flow.Inputs {
		inputs[i] = C.NtFlowInput{begin_us: C.NtTimeUs(input.BeginUs), end_us: C.NtTimeUs(input.EndUs),
			rate: C.double(input.Rate)}
	}
	native := C.NtFlow{stages: unsafe.SliceData(stages), stage_count: C.size_t(len(stages)),
		inputs: unsafe.SliceData(inputs), input_count: C.size_t(len(inputs))}
	var out C.NtFlowSample
	if err := check(C.nt_flow_sample(&native, C.NtTimeUs(timeUs), &out)); err != nil {
		return FlowSample{}, err
	}
	count := int(out.stage_count)
	sample := FlowSample{Ports: make([]FlowPort, count+1), Retained: make([]float64, count)}
	for i := range sample.Ports {
		sample.Ports[i] = FlowPort{Volume: float64(out.ports[i].volume), Rate: float64(out.ports[i].rate)}
	}
	for i := range sample.Retained {
		sample.Retained[i] = float64(out.retained[i])
	}
	return sample, nil
}

// FlowSectionPoint displaces a dry point along a unit depth direction.
func FlowSectionPoint(dry, depthDirection Vec3, depth, weight float64) (Vec3, error) {
	var out C.NtVec3
	err := check(C.nt_flow_section_point(dry.native(), depthDirection.native(), C.double(depth), C.double(weight), &out))
	return vecFromNative(out), err
}

type SprayOptions struct {
	Origin, Velocity, VelocitySpread, Acceleration Vec3
	LifetimeUs                                     uint64
	DropletVolume                                  float64
	Seed, Stream                                   uint64
}

type SprayBatch struct {
	Options                        SprayOptions
	BirthUs, FirstID               uint64
	Count                          uint
	EmittedVolume, RemainingVolume float64
}

type SpraySample struct {
	ID                 uint64
	Position, Velocity Vec3
	Volume, Progress   float64
}

func (options SprayOptions) native() C.NtSprayOptions {
	return C.NtSprayOptions{origin: options.Origin.native(), velocity: options.Velocity.native(),
		velocity_spread: options.VelocitySpread.native(), acceleration: options.Acceleration.native(),
		lifetime_us: C.NtTimeUs(options.LifetimeUs), droplet_volume: C.double(options.DropletVolume),
		seed: C.uint64_t(options.Seed), stream: C.uint64_t(options.Stream)}
}

// EmitSpray allocates whole drops from the offered volume into a copyable batch.
func EmitSpray(options SprayOptions, availableVolume float64, capacity uint, firstID, birthUs uint64) (SprayBatch, error) {
	native := options.native()
	var out C.NtSprayBatch
	if err := check(C.nt_spray_emit(&native, C.double(availableVolume), C.size_t(capacity),
		C.uint64_t(firstID), C.NtTimeUs(birthUs), &out)); err != nil {
		return SprayBatch{}, err
	}
	return SprayBatch{Options: options, BirthUs: uint64(out.birth_us), FirstID: uint64(out.first_id),
		Count: uint(out.count), EmittedVolume: float64(out.emitted_volume), RemainingVolume: float64(out.remaining_volume)}, nil
}

// Sample reports whether the selected drop is active at the caller's time.
func (batch SprayBatch) Sample(index uint, nowUs uint64) (SpraySample, bool, error) {
	native := C.NtSprayBatch{options: batch.Options.native(), birth_us: C.NtTimeUs(batch.BirthUs),
		first_id: C.uint64_t(batch.FirstID), count: C.size_t(batch.Count),
		emitted_volume: C.double(batch.EmittedVolume), remaining_volume: C.double(batch.RemainingVolume)}
	var out C.NtSpraySample
	var active C.bool
	err := check(C.nt_spray_sample(&native, C.size_t(index), C.NtTimeUs(nowUs), &active, &out))
	return SpraySample{ID: uint64(out.id), Position: vecFromNative(out.position), Velocity: vecFromNative(out.velocity),
		Volume: float64(out.volume), Progress: float64(out.progress)}, bool(active), err
}
