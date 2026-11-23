import type { Vec3 } from "./index.js";
import type { RawAPI, RawArena } from "./raw.js";
import { double, checkedCount, field, pointer, readDouble, readUInt, readVector, uint64, vector,
  writeUInt, writeVector } from "./spatial.js";

export interface FlowStage {
  readonly transitUs: bigint;
  readonly residenceUs: bigint;
}

export interface FlowInput {
  readonly beginUs: bigint;
  readonly endUs: bigint;
  readonly rate: number;
}

export interface Flow {
  readonly stages: readonly FlowStage[];
  readonly inputs: readonly FlowInput[];
}

export interface FlowPort {
  readonly volume: number;
  readonly rate: number;
}

export interface FlowSample {
  /** Inlet followed by successive outlets. */
  readonly ports: readonly FlowPort[];
  /** Transit and reservoir volume for each stage. */
  readonly retained: readonly number[];
}

export interface SprayOptions {
  readonly origin: Vec3;
  readonly velocity: Vec3;
  readonly velocitySpread: Vec3;
  readonly acceleration: Vec3;
  readonly lifetimeUs: bigint;
  readonly dropletVolume: number;
  readonly seed: bigint;
  readonly stream: bigint;
}

export interface SprayBatch {
  readonly options: SprayOptions;
  readonly birthUs: bigint;
  readonly firstId: bigint;
  readonly count: number;
  readonly emittedVolume: number;
  readonly remainingVolume: number;
}

export interface SpraySample {
  readonly id: bigint;
  readonly position: Vec3;
  readonly velocity: Vec3;
  readonly volume: number;
  readonly progress: number;
}

export interface FlowSimulation {
  sampleFlow(flow: Flow, timeUs: bigint): FlowSample;
  flowSectionPoint(dry: Vec3, depthDirection: Vec3, depth: number, weight: number): Vec3;
  emitSpray(options: SprayOptions, availableVolume: number, capacity: number, firstId: bigint, birthUs: bigint): SprayBatch;
  /** Return undefined before birth and at or after expiry. */
  sampleSpray(batch: SprayBatch, index: number, nowUs: bigint): SpraySample | undefined;
}

type Failure = (code: number) => Error;

export function timeSlot(arena: RawArena, timeUs: bigint): number {
  const out = arena.value("NtTimeUs");
  arena.setUnsigned(out, arena.api.size("NtTimeUs"), uint64(timeUs));
  return out;
}

export function sizeValue(value: number, failure: Failure): bigint {
  if (!Number.isSafeInteger(value) || value < 0) throw failure(1);
  if (value > 0xffffffff) throw failure(2);
  return BigInt(value);
}

export function sizeSlot(arena: RawArena, value: number, failure: Failure): number {
  const out = arena.value("size_t");
  arena.setUnsigned(out, arena.api.size("size_t"), sizeValue(value, failure));
  return out;
}

function flowPointer(arena: RawArena, flow: Flow, failure: Failure): number {
  const type = "NtFlow", input = arena.value(type);
  const { stages, inputs } = flow;
  const stage = "NtFlowStage", segment = "NtFlowInput";
  const step = arena.api.size(stage), span = arena.api.size(segment);
  const count = checkedCount(stages.length, step, failure), intervals = checkedCount(inputs.length, span, failure);
  if (count > Number(arena.api.constant("NT_FLOW_MAX_STAGES")) ||
      intervals > Number(arena.api.constant("NT_FLOW_MAX_INPUTS"))) throw failure(2);
  const stageArray = count ? arena.allocate(count * step) : 0;
  const inputArray = intervals ? arena.allocate(intervals * span) : 0;
  for (let i = 0; i < count; i++) {
    const address = stageArray + i * step;
    const { transitUs, residenceUs } = stages[i];
    writeUInt(arena, address, stage, "transit_us", uint64(transitUs));
    writeUInt(arena, address, stage, "residence_us", uint64(residenceUs));
  }
  for (let i = 0; i < intervals; i++) {
    const address = inputArray + i * span;
    const { beginUs, endUs, rate } = inputs[i];
    writeUInt(arena, address, segment, "begin_us", uint64(beginUs));
    writeUInt(arena, address, segment, "end_us", uint64(endUs));
    arena.setFloat64(field(arena, address, segment, "rate"), rate);
  }
  arena.setPointer(field(arena, input, type, "stages"), stageArray);
  arena.setPointer(field(arena, input, type, "inputs"), inputArray);
  writeUInt(arena, input, type, "stage_count", BigInt(count));
  writeUInt(arena, input, type, "input_count", BigInt(intervals));
  return pointer(arena, input);
}

function readFlow(arena: RawArena, address: number): FlowSample {
  const type = "NtFlowSample", port = "NtFlowPort";
  const count = Number(readUInt(arena, address, type, "stage_count"));
  const ports = field(arena, address, type, "ports"), retained = field(arena, address, type, "retained");
  const portStride = arena.api.size(port), scalarStride = arena.api.size("double");
  return {
    ports: Array.from({ length: count + 1 }, (_, i) => {
      const value = ports + i * portStride;
      return { volume: readDouble(arena, value, port, "volume"), rate: readDouble(arena, value, port, "rate") };
    }),
    retained: Array.from({ length: count }, (_, i) => arena.float64(retained + i * scalarStride)),
  };
}

const optionsType = "NtSprayOptions";
const batchType = "NtSprayBatch";

function writeSprayOptions(arena: RawArena, address: number, options: SprayOptions): void {
  const { origin, velocity, velocitySpread, acceleration, lifetimeUs, dropletVolume, seed, stream } = options;
  writeVector(arena, field(arena, address, optionsType, "origin"), origin);
  writeVector(arena, field(arena, address, optionsType, "velocity"), velocity);
  writeVector(arena, field(arena, address, optionsType, "velocity_spread"), velocitySpread);
  writeVector(arena, field(arena, address, optionsType, "acceleration"), acceleration);
  writeUInt(arena, address, optionsType, "lifetime_us", uint64(lifetimeUs));
  writeUInt(arena, address, optionsType, "seed", uint64(seed));
  writeUInt(arena, address, optionsType, "stream", uint64(stream));
  arena.setFloat64(field(arena, address, optionsType, "droplet_volume"), dropletVolume);
}

function readSprayOptions(arena: RawArena, address: number): SprayOptions {
  const vec = (name: string) => readVector(arena, field(arena, address, optionsType, name));
  return { origin: vec("origin"), velocity: vec("velocity"), velocitySpread: vec("velocity_spread"),
    acceleration: vec("acceleration"), lifetimeUs: readUInt(arena, address, optionsType, "lifetime_us"),
    dropletVolume: readDouble(arena, address, optionsType, "droplet_volume"),
    seed: readUInt(arena, address, optionsType, "seed"), stream: readUInt(arena, address, optionsType, "stream") };
}

function readBatch(arena: RawArena, address: number): SprayBatch {
  return { options: readSprayOptions(arena, field(arena, address, batchType, "options")),
    birthUs: readUInt(arena, address, batchType, "birth_us"), firstId: readUInt(arena, address, batchType, "first_id"),
    count: Number(readUInt(arena, address, batchType, "count")),
    emittedVolume: readDouble(arena, address, batchType, "emitted_volume"),
    remainingVolume: readDouble(arena, address, batchType, "remaining_volume") };
}

function batchPointer(arena: RawArena, batch: SprayBatch, failure: Failure): number {
  const address = arena.value(batchType);
  const { options, birthUs, firstId, count, emittedVolume, remainingVolume } = batch;
  writeSprayOptions(arena, field(arena, address, batchType, "options"), options);
  writeUInt(arena, address, batchType, "birth_us", uint64(birthUs));
  writeUInt(arena, address, batchType, "first_id", uint64(firstId));
  writeUInt(arena, address, batchType, "count", sizeValue(count, failure));
  arena.setFloat64(field(arena, address, batchType, "emitted_volume"), emittedVolume);
  arena.setFloat64(field(arena, address, batchType, "remaining_volume"), remainingVolume);
  return pointer(arena, address);
}

export function flowSimulation(raw: RawAPI, failure: Failure): FlowSimulation {
  return {
    sampleFlow: (flow, timeUs) => raw.withArena(arena => {
      const out = arena.value("NtFlowSample");
      arena.result("nt_flow_sample", flowPointer(arena, flow, failure), timeSlot(arena, timeUs), pointer(arena, out));
      return readFlow(arena, out);
    }),
    flowSectionPoint: (dry, depthDirection, depth, weight) => raw.withArena(arena => {
      const out = arena.value("NtVec3");
      arena.result("nt_flow_section_point", vector(arena, dry), vector(arena, depthDirection),
        double(arena, depth), double(arena, weight), pointer(arena, out));
      return readVector(arena, out);
    }),
    emitSpray: (options, availableVolume, capacity, firstId, birthUs) => raw.withArena(arena => {
      const input = arena.value(optionsType), out = arena.value(batchType);
      const id = arena.value("uint64_t");
      arena.setUnsigned(id, arena.api.size("uint64_t"), uint64(firstId));
      writeSprayOptions(arena, input, options);
      arena.result("nt_spray_emit", pointer(arena, input), double(arena, availableVolume),
        sizeSlot(arena, capacity, failure), id, timeSlot(arena, birthUs), pointer(arena, out));
      return readBatch(arena, out);
    }),
    sampleSpray: (batch, index, nowUs) => raw.withArena(arena => {
      const type = "NtSpraySample", out = arena.value(type), active = arena.value("_Bool");
      arena.result("nt_spray_sample", batchPointer(arena, batch, failure), sizeSlot(arena, index, failure),
        timeSlot(arena, nowUs), pointer(arena, active), pointer(arena, out));
      if (arena.unsigned(active, arena.api.size("_Bool")) === 0n) return undefined;
      return { id: readUInt(arena, out, type, "id"), position: readVector(arena, field(arena, out, type, "position")),
        velocity: readVector(arena, field(arena, out, type, "velocity")), volume: readDouble(arena, out, type, "volume"),
        progress: readDouble(arena, out, type, "progress") };
    }),
  };
}
