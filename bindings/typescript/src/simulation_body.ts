import type { Vec3 } from "./index.js";
import type { RawAPI, RawArena } from "./raw.js";
import { sizeSlot, sizeValue, timeSlot } from "./simulation_flow.js";
import { checkedCount, double, field, pointer, readDouble, readTransform, readVector, vector, writeTriangle,
  writeUInt, writeVector, type SpatialTransform, type SpatialTriangle } from "./spatial.js";

export interface FloatingBody {
  readonly position: Vec3;
  readonly rotationAxis: Vec3;
  readonly rotationRadians: number;
  readonly mass: number;
  readonly principalInertia: Vec3;
  readonly linearVelocity: Vec3;
  readonly angularVelocity: Vec3;
}

export interface BodyLoad {
  readonly force: Vec3;
  readonly torque: Vec3;
}

export interface LinearStore {
  readonly mass: number;
  readonly velocity: Vec3;
}

export interface AngularStore {
  readonly inertia: Vec3;
  readonly velocity: Vec3;
}

export interface LinearPair { readonly first: LinearStore; readonly second: LinearStore }
export interface AngularPair { readonly first: AngularStore; readonly second: AngularStore }

export interface DragResult {
  readonly firstVelocity: Vec3;
  readonly secondVelocity: Vec3;
  readonly impulseOnSecond: Vec3;
  readonly dissipatedEnergy: number;
}

export type HullTriangle = readonly [number, number, number];

export interface HullGeometry {
  readonly vertices: readonly Vec3[];
  readonly triangles: readonly HullTriangle[];
}

export interface WaterPlane {
  readonly point: Vec3;
  readonly normal: Vec3;
}

export type WaterTriangle = SpatialTriangle;

export interface HullImmersion {
  readonly fullVolume: number;
  readonly displacedVolume: number;
  readonly center: Vec3;
  readonly centerOffset: Vec3;
}

export interface Buoyancy {
  readonly lift: Vec3;
  readonly weight: Vec3;
  readonly force: Vec3;
  readonly torque: Vec3;
}

/** An owned convex boundary in body space. */
export interface Hull {
  immersePlane(body: FloatingBody, plane: WaterPlane): HullImmersion;
  immerseWater(body: FloatingBody, triangles: readonly WaterTriangle[]): HullImmersion;
  close(): void;
}

export interface BodySimulation {
  createHull(geometry: HullGeometry): Hull;
  floatingBodyTransform(body: FloatingBody): SpatialTransform;
  stepFloatingBody(body: FloatingBody, load: BodyLoad, elapsedUs: bigint): FloatingBody;
  hullBuoyancy(immersion: HullImmersion, body: FloatingBody, density: number, gravity: number): Buoyancy;
  exchangeLinearDrag(stores: LinearPair, conductance: number, elapsedUs: bigint): DragResult;
  exchangeAngularDrag(stores: AngularPair, conductance: Vec3, elapsedUs: bigint): DragResult;
}

type Failure = (code: number) => Error;
const bodyType = "NtFloatingBody";
const immersionType = "NtHullImmersion";
const bodyVectors = {
  position: "position", rotationAxis: "rotation_axis", principalInertia: "principal_inertia",
  linearVelocity: "linear_velocity", angularVelocity: "angular_velocity",
} as const;

function bodyPointer(arena: RawArena, body: FloatingBody): number {
  const input = arena.value(bodyType);
  for (const key of Object.keys(bodyVectors) as (keyof typeof bodyVectors)[]) {
    writeVector(arena, field(arena, input, bodyType, bodyVectors[key]), body[key]);
  }
  arena.setFloat64(field(arena, input, bodyType, "rotation_radians"), body.rotationRadians);
  arena.setFloat64(field(arena, input, bodyType, "mass"), body.mass);
  return pointer(arena, input);
}

function readBody(arena: RawArena, address: number): FloatingBody {
  const vec = (key: keyof typeof bodyVectors) => readVector(arena, field(arena, address, bodyType, bodyVectors[key]));
  return { position: vec("position"), rotationAxis: vec("rotationAxis"), principalInertia: vec("principalInertia"),
    linearVelocity: vec("linearVelocity"), angularVelocity: vec("angularVelocity"),
    rotationRadians: readDouble(arena, address, bodyType, "rotation_radians"), mass: readDouble(arena, address, bodyType, "mass") };
}

function immersionPointer(arena: RawArena, immersion: HullImmersion): number {
  const type = immersionType, address = arena.value(type);
  arena.setFloat64(field(arena, address, type, "full_volume"), immersion.fullVolume);
  arena.setFloat64(field(arena, address, type, "displaced_volume"), immersion.displacedVolume);
  writeVector(arena, field(arena, address, type, "center"), immersion.center);
  writeVector(arena, field(arena, address, type, "center_offset"), immersion.centerOffset);
  return pointer(arena, address);
}

function readImmersion(arena: RawArena, address: number): HullImmersion {
  const type = immersionType;
  return { fullVolume: readDouble(arena, address, type, "full_volume"),
    displacedVolume: readDouble(arena, address, type, "displaced_volume"),
    center: readVector(arena, field(arena, address, type, "center")),
    centerOffset: readVector(arena, field(arena, address, type, "center_offset")) };
}

function readDrag(arena: RawArena, address: number): DragResult {
  const type = "NtDragResult";
  return { firstVelocity: readVector(arena, field(arena, address, type, "first_velocity")),
    secondVelocity: readVector(arena, field(arena, address, type, "second_velocity")),
    impulseOnSecond: readVector(arena, field(arena, address, type, "impulse_on_second")),
    dissipatedEnergy: readDouble(arena, address, type, "dissipated_energy") };
}

function hullPointer(arena: RawArena, geometry: HullGeometry, failure: Failure): number {
  const type = "NtHullGeometry", input = arena.value(type);
  const { vertices, triangles } = geometry;
  const vertexStride = arena.api.size("NtVec3"), triangleType = "NtHullTriangle", triangleStride = arena.api.size(triangleType);
  const vertexCount = checkedCount(vertices.length, vertexStride, failure);
  const triangleCount = checkedCount(triangles.length, triangleStride, failure);
  if (vertexCount > Number(arena.api.constant("NT_HULL_MAX_VERTICES")) ||
      triangleCount > Number(arena.api.constant("NT_HULL_MAX_TRIANGLES"))) throw failure(2);
  const points = vertexCount ? arena.allocate(vertexCount * vertexStride) : 0;
  const faces = triangleCount ? arena.allocate(triangleCount * triangleStride) : 0;
  for (let i = 0; i < vertexCount; i++) writeVector(arena, points + i * vertexStride, vertices[i]);
  const indexSize = arena.api.size("uint32_t");
  for (let i = 0; i < triangleCount; i++) {
    const triangle = triangles[i], start = field(arena, faces + i * triangleStride, triangleType, "indices");
    if (triangle.length !== 3) throw failure(1);
    for (let j = 0; j < 3; j++) arena.setUnsigned(start + j * indexSize, indexSize, sizeValue(triangle[j], failure));
  }
  arena.setPointer(field(arena, input, type, "vertices"), points);
  arena.setPointer(field(arena, input, type, "triangles"), faces);
  writeUInt(arena, input, type, "vertex_count", BigInt(vertexCount));
  writeUInt(arena, input, type, "triangle_count", BigInt(triangleCount));
  return pointer(arena, input);
}

function waterArguments(arena: RawArena, triangles: readonly WaterTriangle[], failure: Failure): readonly number[] {
  const type = "NtWaterTriangle", stride = arena.api.size(type);
  const count = checkedCount(triangles.length, stride, failure);
  if (count > Number(arena.api.constant("NT_WATER_MAX_TRIANGLES"))) throw failure(2);
  const input = count ? arena.allocate(count * stride) : 0;
  for (let i = 0; i < count; i++) {
    writeTriangle(arena, field(arena, input + i * stride, type, "vertices"), triangles[i], failure);
  }
  return [pointer(arena, input), sizeSlot(arena, count, failure)];
}

function ownHull(raw: RawAPI, failure: Failure, initial: number): Hull {
  let handle = initial;
  function immersion(name: string, body: FloatingBody, input: (arena: RawArena) => readonly number[]): HullImmersion {
    if (!handle) throw failure(5);
    return raw.withArena(arena => {
      const placement = bodyPointer(arena, body), water = input(arena), out = arena.value(immersionType);
      // A caller accessor may have closed the hull while the inputs were copied.
      if (!handle) throw failure(5);
      arena.result(name, pointer(arena, handle), placement, ...water, pointer(arena, out));
      return readImmersion(arena, out);
    });
  }
  return Object.freeze<Hull>({
    immersePlane: (body, plane) => immersion("nt_hull_immerse_plane", body, arena => {
      const type = "NtWaterPlane", input = arena.value(type);
      writeVector(arena, field(arena, input, type, "point"), plane.point);
      writeVector(arena, field(arena, input, type, "normal"), plane.normal);
      return [pointer(arena, input)];
    }),
    immerseWater: (body, triangles) => immersion("nt_hull_immerse_water", body, arena => waterArguments(arena, triangles, failure)),
    close() {
      if (!handle) return;
      raw.withArena(arena => arena.call("nt_hull_destroy", pointer(arena, handle)));
      handle = 0;
    },
  });
}

export function bodySimulation(raw: RawAPI, failure: Failure): BodySimulation {
  return {
    createHull: geometry => raw.withArena(arena => {
      const out = arena.value("pointer");
      arena.result("nt_hull_create", pointer(arena, 0), hullPointer(arena, geometry, failure), pointer(arena, out));
      return ownHull(raw, failure, arena.pointer(out));
    }),
    floatingBodyTransform: body => raw.withArena(arena => {
      const out = arena.value("NtSpatialTransform");
      arena.result("nt_floating_body_transform", bodyPointer(arena, body), pointer(arena, out));
      return readTransform(arena, out);
    }),
    stepFloatingBody: (body, load, elapsedUs) => raw.withArena(arena => {
      const type = "NtBodyLoad", input = arena.value(type), out = arena.value(bodyType);
      writeVector(arena, field(arena, input, type, "force"), load.force);
      writeVector(arena, field(arena, input, type, "torque"), load.torque);
      arena.result("nt_floating_body_step", bodyPointer(arena, body), pointer(arena, input), timeSlot(arena, elapsedUs), pointer(arena, out));
      return readBody(arena, out);
    }),
    hullBuoyancy: (immersion, body, density, gravity) => raw.withArena(arena => {
      const type = "NtBuoyancy", out = arena.value(type);
      arena.result("nt_hull_buoyancy", immersionPointer(arena, immersion), bodyPointer(arena, body),
        double(arena, density), double(arena, gravity), pointer(arena, out));
      const vec = (name: string) => readVector(arena, field(arena, out, type, name));
      return { lift: vec("lift"), weight: vec("weight"), force: vec("force"), torque: vec("torque") };
    }),
    exchangeLinearDrag: (stores, conductance, elapsedUs) => raw.withArena(arena => {
      const type = "NtLinearPair", storeType = "NtLinearStore", input = arena.value(type), out = arena.value("NtDragResult");
      for (const key of ["first", "second"] as const) {
        const at = field(arena, input, type, key), store = stores[key];
        arena.setFloat64(field(arena, at, storeType, "mass"), store.mass);
        writeVector(arena, field(arena, at, storeType, "velocity"), store.velocity);
      }
      arena.result("nt_linear_drag_exchange", pointer(arena, input), double(arena, conductance),
        timeSlot(arena, elapsedUs), pointer(arena, out));
      return readDrag(arena, out);
    }),
    exchangeAngularDrag: (stores, conductance, elapsedUs) => raw.withArena(arena => {
      const type = "NtAngularPair", storeType = "NtAngularStore", input = arena.value(type), out = arena.value("NtDragResult");
      for (const key of ["first", "second"] as const) {
        const at = field(arena, input, type, key), store = stores[key];
        writeVector(arena, field(arena, at, storeType, "inertia"), store.inertia);
        writeVector(arena, field(arena, at, storeType, "velocity"), store.velocity);
      }
      arena.result("nt_angular_drag_exchange", pointer(arena, input), vector(arena, conductance),
        timeSlot(arena, elapsedUs), pointer(arena, out));
      return readDrag(arena, out);
    }),
  };
}
