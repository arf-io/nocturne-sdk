import type { RawAPI, RawArena } from "./raw.js";
import { sizeValue, timeSlot } from "./simulation_flow.js";
import { checkedCount, field, pointer, readDouble, writeUInt } from "./spatial.js";

export interface TransportCell {
  readonly water: number;
  readonly foam: number;
  readonly energy: number;
}

export interface TransportCellOptions {
  readonly waterCapacity: number;
  readonly foamCapacity: number;
  readonly substrateHeatCapacity: number;
  readonly inletRate?: number;
  readonly inletTemperature?: number;
  readonly foamGeneration?: number;
  readonly foamDecay?: number;
  readonly evaporation?: number;
  readonly latentHeat?: number;
  readonly ambientTemperature?: number;
  readonly ambientConductance?: number;
  readonly power?: number;
}

export interface TransportEdge {
  readonly source: number;
  readonly destination: number | "drain";
  readonly waterRate?: number;
  readonly foamRate?: number;
  readonly conductance?: number;
}

export interface TransportOptions {
  readonly cells: readonly TransportCellOptions[];
  readonly edges: readonly TransportEdge[];
  readonly waterHeatCapacity: number;
}

export interface TransportBudget {
  readonly waterInlet: number;
  readonly waterDrained: number;
  readonly waterSpilled: number;
  readonly waterEvaporated: number;
  readonly foamGenerated: number;
  readonly foamDrained: number;
  readonly foamSpilled: number;
  readonly foamDecayed: number;
  readonly heatInlet: number;
  readonly heatDrained: number;
  readonly heatSpilled: number;
  readonly heatEvaporated: number;
  readonly heatLatent: number;
  readonly heatApplied: number;
  readonly heatAmbient: number;
}

export interface TransportStep {
  readonly cells: readonly TransportCell[];
  readonly budget: TransportBudget;
}

export interface SurfaceTransportSimulation {
  /** Return owned cells and the external budget for a single explicit interval. */
  stepSurfaceTransport(options: TransportOptions, cells: readonly TransportCell[], elapsedUs: bigint): TransportStep;
}

type Failure = (code: number) => Error;

const descriptors = {
  inletRate: "inlet_rate", inletTemperature: "inlet_temperature",
  foamGeneration: "foam_generation", foamDecay: "foam_decay",
  evaporation: "evaporation", latentHeat: "latent_heat",
  ambientTemperature: "ambient_temperature", ambientConductance: "ambient_conductance", power: "power",
} as const;

const budgetFields = {
  waterInlet: "water_inlet", waterDrained: "water_drained", waterSpilled: "water_spilled", waterEvaporated: "water_evaporated",
  foamGenerated: "foam_generated", foamDrained: "foam_drained", foamSpilled: "foam_spilled", foamDecayed: "foam_decayed",
  heatInlet: "heat_inlet", heatDrained: "heat_drained", heatSpilled: "heat_spilled", heatEvaporated: "heat_evaporated",
  heatLatent: "heat_latent", heatApplied: "heat_applied", heatAmbient: "heat_ambient",
} as const satisfies Record<keyof TransportBudget, string>;

const cellFields = ["water", "foam", "energy"] as const;

function writeDescriptor(arena: RawArena, address: number, value: TransportCellOptions): void {
  const type = "NtTransportCellOptions";
  arena.setFloat64(field(arena, address, type, "water_capacity"), value.waterCapacity);
  arena.setFloat64(field(arena, address, type, "foam_capacity"), value.foamCapacity);
  arena.setFloat64(field(arena, address, type, "substrate_heat_capacity"), value.substrateHeatCapacity);
  for (const key of Object.keys(descriptors) as (keyof typeof descriptors)[]) {
    arena.setFloat64(field(arena, address, type, descriptors[key]), value[key] ?? 0);
  }
}

function writeEdge(arena: RawArena, address: number, value: TransportEdge, failure: Failure): void {
  const type = "NtTransportEdge";
  const { source, destination, waterRate = 0, foamRate = 0, conductance = 0 } = value;
  const drain = arena.api.constant("NT_TRANSPORT_DRAIN")!;
  writeUInt(arena, address, type, "source", sizeValue(source, failure));
  writeUInt(arena, address, type, "destination", destination === "drain" ? drain : sizeValue(destination, failure));
  arena.setFloat64(field(arena, address, type, "water_rate"), waterRate);
  arena.setFloat64(field(arena, address, type, "foam_rate"), foamRate);
  arena.setFloat64(field(arena, address, type, "conductance"), conductance);
}

export function surfaceTransportSimulation(raw: RawAPI, failure: Failure): SurfaceTransportSimulation {
  return {
    stepSurfaceTransport(options, cells, elapsedUs) {
      return raw.withArena(arena => {
        const type = "NtTransportOptions", input = arena.value(type);
        const cellType = "NtTransportCell", edgeType = "NtTransportEdge", descriptorType = "NtTransportCellOptions";
        const { cells: settings, edges, waterHeatCapacity } = options;
        const stride = arena.api.size(cellType), edgeStride = arena.api.size(edgeType), descriptorStride = arena.api.size(descriptorType);
        const count = checkedCount(cells.length, stride, failure), edgeCount = checkedCount(edges.length, edgeStride, failure);
        if (count !== settings.length) throw failure(1);
        if (count > Number(arena.api.constant("NT_TRANSPORT_MAX_CELLS")) ||
            edgeCount > Number(arena.api.constant("NT_TRANSPORT_MAX_EDGES"))) throw failure(2);
        const descriptors = count ? arena.allocate(count * descriptorStride) : 0;
        const values = count ? arena.allocate(count * stride) : 0;
        const routes = edgeCount ? arena.allocate(edgeCount * edgeStride) : 0;
        for (let i = 0; i < count; i++) {
          writeDescriptor(arena, descriptors + i * descriptorStride, settings[i]);
          const cell = cells[i], address = values + i * stride;
          for (const key of cellFields) arena.setFloat64(field(arena, address, cellType, key), cell[key]);
        }
        for (let i = 0; i < edgeCount; i++) writeEdge(arena, routes + i * edgeStride, edges[i], failure);
        arena.setPointer(field(arena, input, type, "cells"), descriptors);
        arena.setPointer(field(arena, input, type, "edges"), routes);
        writeUInt(arena, input, type, "cell_count", BigInt(count));
        writeUInt(arena, input, type, "edge_count", BigInt(edgeCount));
        arena.setFloat64(field(arena, input, type, "water_heat_capacity"), waterHeatCapacity);
        const budgetType = "NtTransportBudget", budget = arena.value(budgetType);
        arena.result("nt_surface_transport_step", pointer(arena, input), pointer(arena, values), timeSlot(arena, elapsedUs),
          pointer(arena, budget));
        return {
          cells: Array.from({ length: count }, (_, i) => {
            const address = values + i * stride;
            return { water: readDouble(arena, address, cellType, "water"), foam: readDouble(arena, address, cellType, "foam"),
              energy: readDouble(arena, address, cellType, "energy") };
          }),
          budget: Object.fromEntries(Object.entries(budgetFields).map(([key, name]) =>
            [key, readDouble(arena, budget, budgetType, name)])) as unknown as TransportBudget,
        };
      });
    },
  };
}
