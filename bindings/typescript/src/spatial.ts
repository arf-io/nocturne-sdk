import type { Repeat, Vec3 } from "./index.js";
import type { RawAPI, RawArena } from "./raw.js";

export type SpatialTriangle = readonly [Vec3, Vec3, Vec3];
type LinearRow = readonly [number, number, number];

export interface SpatialTransform {
  /** Row-major linear matrix applied to column vectors. */
  readonly linear: readonly [LinearRow, LinearRow, LinearRow];
  readonly translation: Vec3;
}

export interface SpatialTransformOptions {
  readonly translation: Vec3;
  readonly pivot: Vec3;
  readonly scale: Vec3;
  readonly rotationAxis: Vec3;
  readonly rotationRadians: number;
}

export interface SpatialPathOptions {
  readonly points: readonly Vec3[];
  readonly closed?: boolean;
  readonly boundary?: typeof Repeat[keyof typeof Repeat];
}

export interface SpatialPathSample {
  readonly position: Vec3;
  readonly tangent: Vec3;
  readonly distance: number;
  readonly length: number;
}

/** An owned immutable path. It copies the input points. */
export interface SpatialPath {
  length(): number;
  sample(distance: number): SpatialPathSample;
  close(): void;
}

export interface SpatialCamera {
  readonly position: Vec3;
  readonly right: Vec3;
  readonly up: Vec3;
  readonly backward: Vec3;
}

export interface SpatialProjection {
  readonly kind: "perspective" | "orthographic";
  /** Slopes at depth one in perspective, distances in orthographic view. */
  readonly halfWidth: number;
  readonly halfHeight: number;
  readonly nearDepth: number;
  readonly farDepth: number;
}

/** Coordinates increase right and up on the normalized rectangle [-1, 1]. */
export interface SpatialScreen {
  readonly x: number;
  readonly y: number;
}

export interface SpatialProjected {
  readonly screen: SpatialScreen;
  readonly depth: number;
}

export interface SpatialClipVertex {
  readonly view: Vec3;
  readonly weights: Vec3;
  readonly projected: SpatialProjected;
}

export interface SpatialClippedSegment {
  readonly first: SpatialClipVertex;
  readonly last: SpatialClipVertex;
}

export interface SpatialPickTriangle {
  readonly view: SpatialTriangle;
  readonly objectId: bigint;
  readonly primitiveId: bigint;
}

export interface SpatialHit {
  readonly objectId: bigint;
  readonly primitiveId: bigint;
  readonly triangleIndex: number;
  readonly view: Vec3;
  readonly weights: Vec3;
  readonly depth: number;
}

export interface SpatialSurfaceMotion {
  readonly pivot: Vec3;
  readonly linearVelocity: Vec3;
  readonly angularVelocity: Vec3;
}

export interface SpatialSurfaceSample {
  readonly position: Vec3;
  readonly velocity: Vec3;
}

export interface SpatialSweepTriangle {
  readonly initial: SpatialTriangle;
  readonly displacement: Vec3;
  readonly objectId: bigint;
  readonly primitiveId: bigint;
}

export interface SpatialContact {
  readonly objectId: bigint;
  readonly primitiveId: bigint;
  readonly triangleIndex: number;
  readonly fraction: number;
  readonly position: Vec3;
  readonly weights: Vec3;
  readonly normal: Vec3;
}

export interface SpatialGeometry {
  add(left: Vec3, right: Vec3): Vec3;
  subtract(left: Vec3, right: Vec3): Vec3;
  scale(vector: Vec3, factor: number): Vec3;
  dot(left: Vec3, right: Vec3): number;
  cross(left: Vec3, right: Vec3): Vec3;
  length(vector: Vec3): number;
  normalize(vector: Vec3): Vec3;
  identityTransform(): SpatialTransform;
  defaultTransformOptions(): SpatialTransformOptions;
  /** Apply scale, rotate about the pivot, then translate. */
  createTransform(options: SpatialTransformOptions): SpatialTransform;
  /** Apply inner first and outer second. */
  compose(outer: SpatialTransform, inner: SpatialTransform): SpatialTransform;
  inverse(transform: SpatialTransform): SpatialTransform;
  transformPoint(transform: SpatialTransform, point: Vec3): Vec3;
  transformDirection(transform: SpatialTransform, direction: Vec3): Vec3;
  createPath(options: SpatialPathOptions): SpatialPath;
  lookAt(position: Vec3, target: Vec3, upHint: Vec3): SpatialCamera;
  orbit(target: Vec3, radius: number, azimuth: number, elevation: number): SpatialCamera;
  fly(camera: SpatialCamera, localTranslation: Vec3, yaw: number, pitch: number, roll: number): SpatialCamera;
  toView(camera: SpatialCamera, worldPoint: Vec3): Vec3;
  project(projection: SpatialProjection, view: Vec3): SpatialProjected | undefined;
  unproject(projection: SpatialProjection, screen: SpatialScreen, depth: number): Vec3;
  clipSegment(projection: SpatialProjection, first: Vec3, last: Vec3): SpatialClippedSegment | undefined;
  clipTriangle(projection: SpatialProjection, view: SpatialTriangle): readonly SpatialClipVertex[];
  pick(projection: SpatialProjection, screen: SpatialScreen, triangles: readonly SpatialPickTriangle[]): SpatialHit | undefined;
  /** Map world placement at time zero to a signed elapsed time in seconds. */
  surfacePose(motion: SpatialSurfaceMotion, seconds: number): SpatialTransform;
  sampleSurface(motion: SpatialSurfaceMotion, seconds: number, initial: Vec3): SpatialSurfaceSample;
  /** Find the first contact in the closed interval, with input order on exact ties. */
  sweepPoint(first: Vec3, last: Vec3, triangles: readonly SpatialSweepTriangle[]): SpatialContact | undefined;
}

type Failure = (code: number) => Error;

export function field(arena: RawArena, address: number, type: string, name: string): number {
  return address + arena.api.field(type, name).offset;
}

export function readUInt(arena: RawArena, address: number, type: string, name: string): bigint {
  const { offset, size } = arena.api.field(type, name);
  return arena.unsigned(address + offset, size);
}

export function writeUInt(arena: RawArena, address: number, type: string, name: string, value: bigint): void {
  const { offset, size } = arena.api.field(type, name);
  arena.setUnsigned(address + offset, size, value);
}

export function pointer(arena: RawArena, target: number): number {
  const slot = arena.value("pointer");
  arena.setPointer(slot, target);
  return slot;
}

export function readDouble(arena: RawArena, address: number, type: string, name: string): number {
  return arena.float64(field(arena, address, type, name));
}

export function double(arena: RawArena, value: number): number {
  const slot = arena.value("double");
  arena.setFloat64(slot, value);
  return slot;
}

export function writeVector(arena: RawArena, address: number, vector: Vec3): void {
  for (const axis of ["x", "y", "z"] as const) {
    arena.setFloat64(field(arena, address, "NtVec3", axis), vector[axis]);
  }
}

export function readVector(arena: RawArena, address: number): Vec3 {
  return { x: readDouble(arena, address, "NtVec3", "x"),
    y: readDouble(arena, address, "NtVec3", "y"), z: readDouble(arena, address, "NtVec3", "z") };
}

export function vector(arena: RawArena, value: Vec3): number {
  const slot = arena.value("NtVec3");
  writeVector(arena, slot, value);
  return slot;
}

function writeVectors(arena: RawArena, address: number, data: readonly Vec3[], count: number): void {
  const stride = arena.api.size("NtVec3");
  for (let i = 0; i < count; i++) writeVector(arena, address + i * stride, data[i]);
}

export function checkedCount(count: number, stride: number, failure: Failure): number {
  if (!Number.isSafeInteger(count) || count < 0) throw failure(1);
  if (count > Math.floor(0xffffffff / stride)) throw failure(2);
  return count;
}

export function readTransform(arena: RawArena, address: number): SpatialTransform {
  const type = "NtSpatialTransform";
  const matrix = field(arena, address, type, "linear"), stride = arena.api.size("double");
  const row = (i: number): LinearRow => {
    const start = matrix + 3 * i * stride;
    return [arena.float64(start), arena.float64(start + stride), arena.float64(start + 2 * stride)];
  };
  return { linear: [row(0), row(1), row(2)], translation: readVector(arena, field(arena, address, type, "translation")) };
}

export function transformPointer(arena: RawArena, transform: SpatialTransform, failure: Failure): number {
  const type = "NtSpatialTransform", address = arena.value(type);
  const matrix = field(arena, address, type, "linear"), stride = arena.api.size("double");
  const linear = transform.linear;
  if (linear.length !== 3) throw failure(1);
  for (let i = 0; i < 3; i++) {
    const row = linear[i];
    if (row.length !== 3) throw failure(1);
    for (let j = 0; j < 3; j++) arena.setFloat64(matrix + (3 * i + j) * stride, row[j]);
  }
  writeVector(arena, field(arena, address, type, "translation"), transform.translation);
  return pointer(arena, address);
}

const transformOptionsType = "NtSpatialTransformOptions";
const transformOptionVectors = {
  translation: "translation", pivot: "pivot", scale: "scale", rotationAxis: "rotation_axis",
} as const;

function transformOptions(arena: RawArena, options: SpatialTransformOptions): number {
  const address = arena.value(transformOptionsType);
  for (const key of Object.keys(transformOptionVectors) as (keyof typeof transformOptionVectors)[]) {
    writeVector(arena, field(arena, address, transformOptionsType, transformOptionVectors[key]), options[key]);
  }
  arena.setFloat64(field(arena, address, transformOptionsType, "rotation_radians"), options.rotationRadians);
  return pointer(arena, address);
}

const cameraType = "NtSpatialCamera";

function readCamera(arena: RawArena, address: number): SpatialCamera {
  return { position: readVector(arena, field(arena, address, cameraType, "position")),
    right: readVector(arena, field(arena, address, cameraType, "right")),
    up: readVector(arena, field(arena, address, cameraType, "up")),
    backward: readVector(arena, field(arena, address, cameraType, "backward")) };
}

export function cameraPointer(arena: RawArena, camera: SpatialCamera): number {
  const address = arena.value(cameraType);
  for (const key of ["position", "right", "up", "backward"] as const) {
    writeVector(arena, field(arena, address, cameraType, key), camera[key]);
  }
  return pointer(arena, address);
}

export function writeProjection(arena: RawArena, address: number, projection: SpatialProjection, failure: Failure): void {
  const type = "NtSpatialProjection";
  const { kind, halfWidth, halfHeight, nearDepth, farDepth } = projection;
  if (kind !== "perspective" && kind !== "orthographic") throw failure(1);
  writeUInt(arena, address, type, "kind", kind === "perspective" ? 0n : 1n);
  arena.setFloat64(field(arena, address, type, "half_width"), halfWidth);
  arena.setFloat64(field(arena, address, type, "half_height"), halfHeight);
  arena.setFloat64(field(arena, address, type, "near_depth"), nearDepth);
  arena.setFloat64(field(arena, address, type, "far_depth"), farDepth);
}

function projectionPointer(arena: RawArena, projection: SpatialProjection, failure: Failure): number {
  const address = arena.value("NtSpatialProjection");
  writeProjection(arena, address, projection, failure);
  return pointer(arena, address);
}

function screenSlot(arena: RawArena, screen: SpatialScreen): number {
  const type = "NtSpatialScreen", slot = arena.value(type);
  arena.setFloat64(field(arena, slot, type, "x"), screen.x);
  arena.setFloat64(field(arena, slot, type, "y"), screen.y);
  return slot;
}

function readProjected(arena: RawArena, address: number): SpatialProjected {
  const type = "NtSpatialProjected";
  const screenType = "NtSpatialScreen", screen = field(arena, address, type, "screen");
  return { screen: { x: readDouble(arena, screen, screenType, "x"), y: readDouble(arena, screen, screenType, "y") },
    depth: readDouble(arena, address, type, "depth") };
}

function readClipVertex(arena: RawArena, address: number): SpatialClipVertex {
  const type = "NtSpatialClipVertex";
  return { view: readVector(arena, field(arena, address, type, "view")),
    weights: readVector(arena, field(arena, address, type, "weights")),
    projected: readProjected(arena, field(arena, address, type, "projected")) };
}

function readHit(arena: RawArena, address: number): SpatialHit {
  const type = "NtSpatialHit";
  return { objectId: readUInt(arena, address, type, "object_id"),
    primitiveId: readUInt(arena, address, type, "primitive_id"),
    triangleIndex: Number(readUInt(arena, address, type, "triangle_index")),
    view: readVector(arena, field(arena, address, type, "view")),
    weights: readVector(arena, field(arena, address, type, "weights")),
    depth: readDouble(arena, address, type, "depth") };
}

export function writeTriangle(arena: RawArena, address: number, points: SpatialTriangle, failure: Failure): void {
  if (points.length !== 3) throw failure(1);
  writeVectors(arena, address, points, 3);
}

export function uint64(value: bigint): bigint {
  if (typeof value !== "bigint" || BigInt.asUintN(64, value) !== value) {
    throw new RangeError("Expected an unsigned 64-bit bigint");
  }
  return value;
}

function writeTriangleIDs(arena: RawArena, address: number, type: string, objectId: bigint, primitiveId: bigint): void {
  writeUInt(arena, address, type, "object_id", uint64(objectId));
  writeUInt(arena, address, type, "primitive_id", uint64(primitiveId));
}

function pickArguments(arena: RawArena, triangles: readonly SpatialPickTriangle[], failure: Failure): readonly number[] {
  const type = "NtSpatialPickTriangle", stride = arena.api.size(type);
  const count = checkedCount(triangles.length, stride, failure);
  const data = count === 0 ? 0 : arena.allocate(count * stride);
  const countSlot = arena.value("size_t");
  arena.setUnsigned(countSlot, arena.api.size("size_t"), BigInt(count));
  for (let i = 0; i < count; i++) {
    const { view, objectId, primitiveId } = triangles[i];
    const address = data + i * stride;
    writeTriangleIDs(arena, address, type, objectId, primitiveId);
    writeTriangle(arena, field(arena, address, type, "view"), view, failure);
  }
  return [pointer(arena, data), countSlot];
}

function motionPointer(arena: RawArena, motion: SpatialSurfaceMotion): number {
  const type = "NtSpatialSurfaceMotion", address = arena.value(type);
  const { pivot, linearVelocity, angularVelocity } = motion;
  writeVector(arena, field(arena, address, type, "pivot"), pivot);
  writeVector(arena, field(arena, address, type, "linear_velocity"), linearVelocity);
  writeVector(arena, field(arena, address, type, "angular_velocity"), angularVelocity);
  return pointer(arena, address);
}

function readSurfaceSample(arena: RawArena, address: number): SpatialSurfaceSample {
  const type = "NtSpatialSurfaceSample";
  return { position: readVector(arena, field(arena, address, type, "position")),
    velocity: readVector(arena, field(arena, address, type, "velocity")) };
}

function sweepArguments(arena: RawArena, triangles: readonly SpatialSweepTriangle[], failure: Failure): readonly number[] {
  const type = "NtSpatialSweepTriangle", stride = arena.api.size(type);
  const count = checkedCount(triangles.length, stride, failure);
  const data = count === 0 ? 0 : arena.allocate(count * stride);
  const countSlot = arena.value("size_t");
  arena.setUnsigned(countSlot, arena.api.size("size_t"), BigInt(count));
  for (let i = 0; i < count; i++) {
    const { initial, displacement, objectId, primitiveId } = triangles[i];
    const address = data + i * stride;
    writeTriangleIDs(arena, address, type, objectId, primitiveId);
    writeTriangle(arena, field(arena, address, type, "initial"), initial, failure);
    writeVector(arena, field(arena, address, type, "displacement"), displacement);
  }
  return [pointer(arena, data), countSlot];
}

function readContact(arena: RawArena, address: number): SpatialContact {
  const type = "NtSpatialContact";
  return { objectId: readUInt(arena, address, type, "object_id"),
    primitiveId: readUInt(arena, address, type, "primitive_id"),
    triangleIndex: Number(readUInt(arena, address, type, "triangle_index")),
    fraction: readDouble(arena, address, type, "fraction"),
    position: readVector(arena, field(arena, address, type, "position")),
    weights: readVector(arena, field(arena, address, type, "weights")),
    normal: readVector(arena, field(arena, address, type, "normal")) };
}

class OwnedSpatialPath implements SpatialPath {
  readonly #raw: RawAPI;
  readonly #failure: Failure;
  #handle: number;

  constructor(raw: RawAPI, handle: number, failure: Failure) {
    this.#raw = raw;
    this.#failure = failure;
    this.#handle = handle;
  }

  #active(arena: RawArena): number {
    if (this.#handle === 0) throw this.#failure(5);
    return pointer(arena, this.#handle);
  }

  length(): number {
    return this.#raw.withArena(arena => {
      const out = arena.value("double");
      arena.result("nt_spatial_path_length", this.#active(arena), pointer(arena, out));
      return arena.float64(out);
    });
  }

  sample(distance: number): SpatialPathSample {
    return this.#raw.withArena(arena => {
      const type = "NtSpatialPathSample", out = arena.value(type);
      const input = double(arena, distance);
      arena.result("nt_spatial_path_sample", this.#active(arena), input, pointer(arena, out));
      return { position: readVector(arena, field(arena, out, type, "position")),
        tangent: readVector(arena, field(arena, out, type, "tangent")),
        distance: readDouble(arena, out, type, "distance"), length: readDouble(arena, out, type, "length") };
    });
  }

  close(): void {
    if (this.#handle === 0) return;
    this.#raw.withArena(arena => arena.call("nt_spatial_path_destroy", pointer(arena, this.#handle)));
    this.#handle = 0;
  }
}

function createPath(raw: RawAPI, options: SpatialPathOptions, failure: Failure): SpatialPath {
  return raw.withArena(arena => {
    const type = "NtSpatialPathOptions", input = arena.value(type);
    const stride = arena.api.size("NtVec3");
    const { points, closed = false, boundary = 0 } = options;
    if (boundary !== 0 && boundary !== 1 && boundary !== 2) throw failure(1);
    const count = checkedCount(points.length, stride, failure);
    const data = count === 0 ? 0 : arena.allocate(count * stride);
    writeVectors(arena, data, points, count);
    arena.setPointer(field(arena, input, type, "points"), data);
    writeUInt(arena, input, type, "count", BigInt(count));
    writeUInt(arena, input, type, "closed", closed ? 1n : 0n);
    writeUInt(arena, input, type, "boundary", BigInt(boundary));
    const out = arena.value("pointer");
    arena.result("nt_spatial_path_create", pointer(arena, 0), pointer(arena, input), pointer(arena, out));
    return new OwnedSpatialPath(raw, arena.pointer(out), failure);
  });
}

export function spatialGeometry(raw: RawAPI, failure: Failure): SpatialGeometry {
  type Inputs = (arena: RawArena) => readonly number[];

  function result<T>(name: string, type: string, input: Inputs, read: (arena: RawArena, out: number) => T): T {
    return raw.withArena(arena => {
      const out = arena.value(type);
      arena.result(name, ...input(arena), pointer(arena, out));
      return read(arena, out);
    });
  }

  function hitResult<T>(name: string, type: string, input: Inputs, read: (arena: RawArena, out: number) => T): T | undefined {
    return raw.withArena(arena => {
      const out = arena.value(type), hit = arena.value("_Bool");
      arena.result(name, ...input(arena), pointer(arena, hit), pointer(arena, out));
      return arena.unsigned(hit, arena.api.size("_Bool")) === 0n ? undefined : read(arena, out);
    });
  }

  const twoVectors = (left: Vec3, right: Vec3): Inputs => arena => [vector(arena, left), vector(arena, right)];
  const oneVector = (value: Vec3): Inputs => arena => [vector(arena, value)];
  const scalar = (arena: RawArena, out: number) => arena.float64(out);
  const transform = "NtSpatialTransform";
  const vec = "NtVec3";
  return Object.freeze<SpatialGeometry>({
    add: (left, right) => result("nt_spatial_vec_add", vec, twoVectors(left, right), readVector),
    subtract: (left, right) => result("nt_spatial_vec_subtract", vec, twoVectors(left, right), readVector),
    scale: (value, factor) => result("nt_spatial_vec_scale", vec, arena => [vector(arena, value), double(arena, factor)], readVector),
    dot: (left, right) => result("nt_spatial_vec_dot", "double", twoVectors(left, right), scalar),
    cross: (left, right) => result("nt_spatial_vec_cross", vec, twoVectors(left, right), readVector),
    length: value => result("nt_spatial_vec_length", "double", oneVector(value), scalar),
    normalize: value => result("nt_spatial_vec_normalize", vec, oneVector(value), readVector),
    identityTransform: () => raw.withArena(arena => readTransform(arena, arena.call("nt_spatial_transform_identity"))),
    defaultTransformOptions: () => raw.withArena(arena => {
      const out = arena.call("nt_spatial_transform_options_default");
      const at = (key: keyof typeof transformOptionVectors) => field(arena, out, transformOptionsType, transformOptionVectors[key]);
      return { translation: readVector(arena, at("translation")), pivot: readVector(arena, at("pivot")),
        scale: readVector(arena, at("scale")), rotationAxis: readVector(arena, at("rotationAxis")),
        rotationRadians: readDouble(arena, out, transformOptionsType, "rotation_radians") };
    }),
    createTransform: options => result("nt_spatial_transform_create", transform, arena => [transformOptions(arena, options)], readTransform),
    compose: (outer, inner) => result("nt_spatial_transform_compose", transform,
      arena => [transformPointer(arena, outer, failure), transformPointer(arena, inner, failure)], readTransform),
    inverse: value => result("nt_spatial_transform_inverse", transform, arena => [transformPointer(arena, value, failure)], readTransform),
    transformPoint: (value, point) => result("nt_spatial_transform_point", vec,
      arena => [transformPointer(arena, value, failure), vector(arena, point)], readVector),
    transformDirection: (value, direction) => result("nt_spatial_transform_direction", vec,
      arena => [transformPointer(arena, value, failure), vector(arena, direction)], readVector),
    createPath: options => createPath(raw, options, failure),
    lookAt: (position, target, upHint) => result("nt_spatial_camera_look_at", cameraType,
      arena => [vector(arena, position), vector(arena, target), vector(arena, upHint)], readCamera),
    orbit: (target, radius, azimuth, elevation) => result("nt_spatial_camera_orbit", cameraType,
      arena => [vector(arena, target), double(arena, radius), double(arena, azimuth), double(arena, elevation)], readCamera),
    fly: (camera, translation, yaw, pitch, roll) => result("nt_spatial_camera_fly", cameraType,
      arena => [cameraPointer(arena, camera), vector(arena, translation), double(arena, yaw), double(arena, pitch),
        double(arena, roll)], readCamera),
    toView: (camera, point) => result("nt_spatial_camera_to_view", vec,
      arena => [cameraPointer(arena, camera), vector(arena, point)], readVector),
    project: (projection, view) => hitResult("nt_spatial_project", "NtSpatialProjected",
      arena => [projectionPointer(arena, projection, failure), vector(arena, view)], readProjected),
    unproject: (projection, screen, depth) => result("nt_spatial_unproject", vec,
      arena => [projectionPointer(arena, projection, failure), screenSlot(arena, screen), double(arena, depth)], readVector),
    clipSegment: (projection, first, last) => hitResult("nt_spatial_clip_segment", "NtSpatialClippedSegment",
      arena => [projectionPointer(arena, projection, failure), vector(arena, first), vector(arena, last)], (arena, out) => ({
        first: readClipVertex(arena, field(arena, out, "NtSpatialClippedSegment", "first")),
        last: readClipVertex(arena, field(arena, out, "NtSpatialClippedSegment", "last")),
      })),
    clipTriangle: (projection, view) => result("nt_spatial_clip_triangle", "NtSpatialClippedPolygon", arena => {
      const data = arena.allocate(3 * arena.api.size(vec));
      writeTriangle(arena, data, view, failure);
      return [projectionPointer(arena, projection, failure), pointer(arena, data)];
    }, (arena, out) => {
      const type = "NtSpatialClippedPolygon";
      const count = Number(readUInt(arena, out, type, "count"));
      const start = field(arena, out, type, "vertices"), stride = arena.api.size("NtSpatialClipVertex");
      return Array.from({ length: count }, (_, i) => readClipVertex(arena, start + i * stride));
    }),
    pick: (projection, screen, triangles) => hitResult("nt_spatial_pick", "NtSpatialHit", arena => [
      projectionPointer(arena, projection, failure), screenSlot(arena, screen), ...pickArguments(arena, triangles, failure),
    ], readHit),
    surfacePose: (motion, seconds) => result("nt_spatial_surface_pose", transform,
      arena => [motionPointer(arena, motion), double(arena, seconds)], readTransform),
    sampleSurface: (motion, seconds, initial) => result("nt_spatial_surface_sample", "NtSpatialSurfaceSample",
      arena => [motionPointer(arena, motion), double(arena, seconds), vector(arena, initial)], readSurfaceSample),
    sweepPoint: (first, last, triangles) => hitResult("nt_spatial_sweep_point", "NtSpatialContact", arena => [
      vector(arena, first), vector(arena, last), ...sweepArguments(arena, triangles, failure),
    ], readContact),
  });
}
