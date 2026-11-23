//go:build nocturne_simulation && nocturne_spatial

package nocturne

/*
#include <nocturne/simulation.h>
*/
import "C"

import "unsafe"

// TransportDrain routes an edge out of the network.
const TransportDrain uint = C.NT_TRANSPORT_DRAIN

type TransportCell struct {
	Water, Foam, Energy float64
}

type TransportCellOptions struct {
	WaterCapacity, FoamCapacity, SubstrateHeatCapacity float64
	InletRate, InletTemperature                        float64
	FoamGeneration, FoamDecay                          float64
	Evaporation, LatentHeat                            float64
	AmbientTemperature, AmbientConductance, Power      float64
}

type TransportEdge struct {
	Source, Destination              uint
	WaterRate, FoamRate, Conductance float64
}

type TransportOptions struct {
	Cells             []TransportCellOptions
	Edges             []TransportEdge
	WaterHeatCapacity float64
}

type TransportBudget struct {
	WaterInlet, WaterDrained, WaterSpilled, WaterEvaporated float64
	FoamGenerated, FoamDrained, FoamSpilled, FoamDecayed    float64
	HeatInlet, HeatDrained, HeatSpilled, HeatEvaporated     float64
	HeatLatent, HeatApplied, HeatAmbient                    float64
}

type TransportStep struct {
	Cells  []TransportCell
	Budget TransportBudget
}

func (options TransportCellOptions) native() C.NtTransportCellOptions {
	return C.NtTransportCellOptions{
		water_capacity: C.double(options.WaterCapacity), foam_capacity: C.double(options.FoamCapacity),
		substrate_heat_capacity: C.double(options.SubstrateHeatCapacity), inlet_rate: C.double(options.InletRate),
		inlet_temperature: C.double(options.InletTemperature), foam_generation: C.double(options.FoamGeneration),
		foam_decay: C.double(options.FoamDecay), evaporation: C.double(options.Evaporation),
		latent_heat: C.double(options.LatentHeat), ambient_temperature: C.double(options.AmbientTemperature),
		ambient_conductance: C.double(options.AmbientConductance), power: C.double(options.Power),
	}
}

func transportBudgetFromNative(out C.NtTransportBudget) TransportBudget {
	return TransportBudget{
		WaterInlet: float64(out.water_inlet), WaterDrained: float64(out.water_drained), WaterSpilled: float64(out.water_spilled),
		WaterEvaporated: float64(out.water_evaporated), FoamGenerated: float64(out.foam_generated),
		FoamDrained: float64(out.foam_drained), FoamSpilled: float64(out.foam_spilled), FoamDecayed: float64(out.foam_decayed),
		HeatInlet: float64(out.heat_inlet), HeatDrained: float64(out.heat_drained), HeatSpilled: float64(out.heat_spilled),
		HeatEvaporated: float64(out.heat_evaporated), HeatLatent: float64(out.heat_latent),
		HeatApplied: float64(out.heat_applied), HeatAmbient: float64(out.heat_ambient),
	}
}

// Step returns owned cells and the external budget for one explicit interval.
// The caller's cells remain valid snapshots even after the step succeeds.
func (options TransportOptions) Step(cells []TransportCell, elapsedUs uint64) (TransportStep, error) {
	if len(cells) != len(options.Cells) {
		return TransportStep{}, InvalidArgument
	}
	if len(cells) > C.NT_TRANSPORT_MAX_CELLS || len(options.Edges) > C.NT_TRANSPORT_MAX_EDGES {
		return TransportStep{}, OutOfRange
	}
	var arena RawArena
	defer arena.close()
	descriptors, err := nativeSlice[C.NtTransportCellOptions](&arena, len(cells))
	if err != nil {
		return TransportStep{}, err
	}
	edges, err := nativeSlice[C.NtTransportEdge](&arena, len(options.Edges))
	if err != nil {
		return TransportStep{}, err
	}
	values, err := nativeSlice[C.NtTransportCell](&arena, len(cells))
	if err != nil {
		return TransportStep{}, err
	}
	for i, cell := range cells {
		descriptors[i] = options.Cells[i].native()
		values[i] = C.NtTransportCell{water: C.double(cell.Water), foam: C.double(cell.Foam), energy: C.double(cell.Energy)}
	}
	for i, edge := range options.Edges {
		edges[i] = C.NtTransportEdge{source: C.size_t(edge.Source), destination: C.size_t(edge.Destination),
			water_rate: C.double(edge.WaterRate), foam_rate: C.double(edge.FoamRate), conductance: C.double(edge.Conductance)}
	}
	input := C.NtTransportOptions{cells: unsafe.SliceData(descriptors), cell_count: C.size_t(len(values)),
		edges: unsafe.SliceData(edges), edge_count: C.size_t(len(edges)), water_heat_capacity: C.double(options.WaterHeatCapacity)}
	var budget C.NtTransportBudget
	if err := check(C.nt_surface_transport_step(&input, unsafe.SliceData(values), C.NtTimeUs(elapsedUs), &budget)); err != nil {
		return TransportStep{}, err
	}
	out := TransportStep{Cells: make([]TransportCell, len(values)), Budget: transportBudgetFromNative(budget)}
	for i, cell := range values {
		out.Cells[i] = TransportCell{Water: float64(cell.water), Foam: float64(cell.foam), Energy: float64(cell.energy)}
	}
	return out, nil
}
