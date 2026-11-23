import type { Color, OffscreenFrame, PaintAddress, PlotOptions, Point, Size, Utf8, Vec3 } from "./index.js";
import type { RawAPI, RawArena } from "./raw.js";
import { cameraPointer, checkedCount, field, pointer, readDouble, readUInt, readVector, transformPointer,
  writeProjection, writeUInt, writeVector, type SpatialCamera, type SpatialProjection, type SpatialTransform } from "./spatial.js";

export interface SpatialVertex {
  readonly position: Vec3;
  readonly uv: Point;
}

export interface SpatialFace {
  readonly indices: readonly [number, number, number];
  readonly primitiveId: bigint;
}

export interface SpatialMeshData {
  readonly vertices: readonly SpatialVertex[];
  readonly faces: readonly SpatialFace[];
}

export interface SpatialMesh {
  /** Copy the indexed triangles and vertices. */
  data(): SpatialMeshData;
  close(): void;
}

export interface SpatialTexture {
  readonly pixels: readonly Color[];
  readonly size: Size;
  /** Row stride in pixels. Zero or omission means tightly packed. */
  readonly stride?: number;
  readonly filter?: "nearest" | "bilinear";
  readonly horizontal?: PaintAddress;
  readonly vertical?: PaintAddress;
}

export interface SpatialMeshInstance {
  readonly mesh: SpatialMesh;
  readonly transform?: SpatialTransform;
  readonly objectId: bigint;
  readonly owner: bigint;
  readonly tint: Color;
  readonly texture?: SpatialTexture;
}

export interface SpatialRasterOptions {
  readonly projection: SpatialProjection;
  /** Omit the camera when transformed positions are already in view space. */
  readonly camera?: SpatialCamera;
  readonly samples: Size;
  readonly background: Color;
  readonly sampleLimit?: number;
  readonly triangleLimit?: number;
}

export interface SpatialRasterSample {
  readonly color: Color;
  readonly owner: bigint;
  readonly covered: boolean;
  readonly objectId: bigint;
  readonly primitiveId: bigint;
  readonly depth: number;
  readonly uv: Point;
}

export interface SpatialCellHit {
  readonly samplePosition: Point;
  readonly sample: SpatialRasterSample;
}

export type SpatialSampling = NonNullable<PlotOptions["sampling"]>;

export interface SpatialRaster {
  readonly size: Size;
  readonly cellSize: Size;
  sample(position: Point): SpatialRasterSample;
  cellHit(cell: Point): SpatialCellHit | undefined;
  /** Produce a new frame whose cells remain valid after the raster is closed. */
  frame(mode: SpatialSampling): OffscreenFrame;
  close(): void;
}

export interface SpatialMeshes {
  createMesh(data: SpatialMeshData): SpatialMesh;
  decodeOBJ(source: Utf8): SpatialMesh;
  rasterize(options: SpatialRasterOptions, instances: readonly SpatialMeshInstance[]): SpatialRaster;
}

type Failure = (code: number) => Error;
type FrameFactory = (handle: number) => OffscreenFrame;
type NativeOwner = { active(): number; close(): void };

function uint32(value: number): bigint {
  if (!Number.isInteger(value) || value < 0 || value > 0xffffffff) throw new RangeError("Expected an unsigned 32-bit integer");
  return BigInt(value);
}

function uint64(value: bigint): bigint {
  if (typeof value !== "bigint" || BigInt.asUintN(64, value) !== value) throw new RangeError("Expected an unsigned 64-bit bigint");
  return value;
}

function writeSize(arena: RawArena, address: number, size: Size): void {
  writeUInt(arena, address, "NtCellSize", "columns", uint32(size.columns));
  writeUInt(arena, address, "NtCellSize", "rows", uint32(size.rows));
}

function readSize(arena: RawArena, address: number): Size {
  return { columns: Number(readUInt(arena, address, "NtCellSize", "columns")),
    rows: Number(readUInt(arena, address, "NtCellSize", "rows")) };
}

function writeUV(arena: RawArena, address: number, uv: Point): void {
  arena.setFloat64(field(arena, address, "NtPoint", "x"), uv.x);
  arena.setFloat64(field(arena, address, "NtPoint", "y"), uv.y);
}

function readUV(arena: RawArena, address: number): Point {
  return { x: readDouble(arena, address, "NtPoint", "x"), y: readDouble(arena, address, "NtPoint", "y") };
}

function cellSlot(arena: RawArena, point: Point): number {
  const type = "NtCellPoint", address = arena.value(type);
  for (const axis of ["x", "y"] as const) {
    const value = point[axis];
    if (!Number.isInteger(value) || value < -0x80000000 || value > 0x7fffffff) {
      throw new RangeError("Expected a signed 32-bit integer");
    }
    writeUInt(arena, address, type, axis, BigInt(value));
  }
  return address;
}

function readCellPoint(arena: RawArena, address: number): Point {
  const coordinate = (axis: "x" | "y") => Number(BigInt.asIntN(32, readUInt(arena, address, "NtCellPoint", axis)));
  return { x: coordinate("x"), y: coordinate("y") };
}

function writeColor(arena: RawArena, address: number, color: Color): void {
  const rgba = new Uint8Array(4);
  for (let i = 0; i < rgba.length; i++) {
    const channel = color[i];
    if (!Number.isInteger(channel) || channel < 0 || channel > 255) throw new RangeError("Expected a color channel from zero to 255");
    rgba[i] = channel;
  }
  arena.write(address, rgba);
}

function readColor(arena: RawArena, address: number): Color {
  const data = arena.read(address, arena.api.size("NtRgba"));
  return [data[0], data[1], data[2], data[3]];
}

function sizeSlot(arena: RawArena, value: number): number {
  const out = arena.value("size_t");
  arena.setUnsigned(out, arena.api.size("size_t"), uint32(value));
  return out;
}

function samplingSlot(arena: RawArena, mode: SpatialSampling, failure: Failure): number {
  const code = ["cell", "halfBlock", "braille", "hybrid"].indexOf(mode);
  if (code < 0) throw failure(1);
  const out = arena.value("NtSamplingMode");
  arena.setUnsigned(out, arena.api.size("NtSamplingMode"), BigInt(code));
  return out;
}

function nativeOwner(raw: RawAPI, address: number, destroy: string, failure: Failure): NativeOwner {
  return {
    active() {
      if (!address) throw failure(5);
      return address;
    },
    close() {
      if (!address) return;
      raw.withArena(arena => arena.call(destroy, pointer(arena, address)));
      address = 0;
    },
  };
}

const dataType = "NtSpatialMeshData";
const vertexType = "NtSpatialVertex";
const faceType = "NtSpatialFace";

function meshInput(arena: RawArena, data: SpatialMeshData, failure: Failure): number {
  const out = arena.value(dataType);
  const { vertices, faces } = data;
  const vStride = arena.api.size(vertexType), fStride = arena.api.size(faceType);
  const vCount = checkedCount(vertices.length, vStride, failure), fCount = checkedCount(faces.length, fStride, failure);
  const vBase = vCount ? arena.allocate(vCount * vStride) : 0;
  const fBase = fCount ? arena.allocate(fCount * fStride) : 0;
  for (let i = 0; i < vCount; i++) {
    const address = vBase + i * vStride;
    const { position, uv } = vertices[i];
    writeVector(arena, field(arena, address, vertexType, "position"), position);
    writeUV(arena, field(arena, address, vertexType, "uv"), uv);
  }
  const indexSize = arena.api.size("uint32_t");
  for (let i = 0; i < fCount; i++) {
    const address = fBase + i * fStride;
    const { indices, primitiveId } = faces[i];
    if (indices.length !== 3) throw failure(1);
    const start = field(arena, address, faceType, "indices");
    for (let j = 0; j < 3; j++) arena.setUnsigned(start + j * indexSize, indexSize, uint32(indices[j]));
    writeUInt(arena, address, faceType, "primitive_id", uint64(primitiveId));
  }
  arena.setPointer(field(arena, out, dataType, "vertices"), vBase);
  arena.setPointer(field(arena, out, dataType, "faces"), fBase);
  writeUInt(arena, out, dataType, "vertex_count", BigInt(vCount));
  writeUInt(arena, out, dataType, "face_count", BigInt(fCount));
  return pointer(arena, out);
}

function meshOutput(arena: RawArena, out: number): SpatialMeshData {
  const vBase = arena.pointer(field(arena, out, dataType, "vertices")), fBase = arena.pointer(field(arena, out, dataType, "faces"));
  const vStride = arena.api.size(vertexType), fStride = arena.api.size(faceType), indexSize = arena.api.size("uint32_t");
  const vertices = Array.from({ length: Number(readUInt(arena, out, dataType, "vertex_count")) }, (_, i) => {
    const address = vBase + i * vStride;
    return { position: readVector(arena, field(arena, address, vertexType, "position")),
      uv: readUV(arena, field(arena, address, vertexType, "uv")) };
  });
  const faces = Array.from({ length: Number(readUInt(arena, out, dataType, "face_count")) }, (_, i): SpatialFace => {
    const address = fBase + i * fStride, start = field(arena, address, faceType, "indices");
    const index = (j: number) => Number(arena.unsigned(start + j * indexSize, indexSize));
    return { indices: [index(0), index(1), index(2)], primitiveId: readUInt(arena, address, faceType, "primitive_id") };
  });
  return { vertices, faces };
}

function writeTexture(arena: RawArena, entry: number, texture: SpatialTexture, failure: Failure): void {
  const { pixels, size, stride = 0, filter = "nearest", horizontal = 0, vertical = 0 } = texture;
  const colorSize = arena.api.size("NtRgba"), count = checkedCount(pixels.length, colorSize, failure);
  const colors = count ? arena.allocate(count * colorSize) : 0;
  for (let i = 0; i < count; i++) writeColor(arena, colors + i * colorSize, pixels[i]);
  const type = "NtImageView", image = arena.value(type), instance = "NtSpatialMeshInstance";
  arena.setPointer(field(arena, image, type, "pixels"), colors);
  const { columns, rows } = size;
  writeSize(arena, field(arena, image, type, "size"), { columns, rows });
  writeUInt(arena, image, type, "stride", uint32(stride === 0 ? columns : stride));
  writeUInt(arena, image, type, "count", BigInt(count));
  if (filter !== "nearest" && filter !== "bilinear") throw failure(1);
  const sampling = field(arena, entry, instance, "sampling");
  writeUInt(arena, sampling, "NtImageSampling", "filter", filter === "nearest" ? 0n : 1n);
  writeUInt(arena, sampling, "NtImageSampling", "horizontal", uint32(horizontal));
  writeUInt(arena, sampling, "NtImageSampling", "vertical", uint32(vertical));
  arena.setPointer(field(arena, entry, instance, "texture"), image);
}

function rasterOptions(arena: RawArena, options: SpatialRasterOptions, failure: Failure): number {
  const type = "NtSpatialRasterOptions", input = arena.value(type);
  const { projection, camera, samples, background, sampleLimit = 0, triangleLimit = 0 } = options;
  writeProjection(arena, field(arena, input, type, "projection"), projection, failure);
  if (camera !== undefined) arena.setPointer(field(arena, input, type, "camera"), arena.pointer(cameraPointer(arena, camera)));
  writeSize(arena, field(arena, input, type, "samples"), samples);
  writeColor(arena, field(arena, input, type, "background"), background);
  writeUInt(arena, input, type, "sample_limit", uint32(sampleLimit));
  writeUInt(arena, input, type, "triangle_limit", uint32(triangleLimit));
  return pointer(arena, input);
}

function sampleOutput(arena: RawArena, address: number): SpatialRasterSample {
  const type = "NtSpatialRasterSample";
  return { color: readColor(arena, field(arena, address, type, "color")),
    owner: readUInt(arena, address, type, "owner"), covered: readUInt(arena, address, type, "covered") !== 0n,
    objectId: readUInt(arena, address, type, "object_id"), primitiveId: readUInt(arena, address, type, "primitive_id"),
    depth: readDouble(arena, address, type, "depth"), uv: readUV(arena, field(arena, address, type, "uv")) };
}

export function spatialMeshBindings(raw: RawAPI, failure: Failure, createFrame: FrameFactory) {
  const meshes = new WeakMap<SpatialMesh, NativeOwner>();
  const rasters = new WeakMap<SpatialRaster, NativeOwner>();
  const resolve = <T extends object>(map: WeakMap<T, NativeOwner>, value: T): number => {
    const owner = map.get(value);
    if (!owner) throw failure(1);
    return owner.active();
  };
  function createMesh(name: string, input: (arena: RawArena) => number): SpatialMesh {
    return raw.withArena(arena => {
      const out = arena.value("pointer");
      arena.result(name, pointer(arena, 0), input(arena), pointer(arena, out));
      const owner = nativeOwner(raw, arena.pointer(out), "nt_spatial_mesh_destroy", failure);
      const mesh = Object.freeze<SpatialMesh>({
        data() {
          return raw.withArena(arena => {
            const out = arena.value(dataType);
            arena.result("nt_spatial_mesh_data", pointer(arena, owner.active()), pointer(arena, out));
            return meshOutput(arena, out);
          });
        },
        close: owner.close,
      });
      meshes.set(mesh, owner);
      return mesh;
    });
  }
  function ownRaster(address: number): SpatialRaster {
    const owner = nativeOwner(raw, address, "nt_spatial_raster_destroy", failure);
    const size = (name: string) => raw.withArena(arena => readSize(arena, arena.call(name, pointer(arena, owner.active()))));
    const raster = Object.freeze<SpatialRaster>({
      get size() { return size("nt_spatial_raster_size"); },
      get cellSize() { return size("nt_spatial_raster_cell_size"); },
      sample(position) {
        return raw.withArena(arena => {
          const at = cellSlot(arena, position), out = arena.value("NtSpatialRasterSample");
          arena.result("nt_spatial_raster_sample", pointer(arena, owner.active()), at, pointer(arena, out));
          return sampleOutput(arena, out);
        });
      },
      cellHit(cell) {
        return raw.withArena(arena => {
          const type = "NtSpatialCellHit", at = cellSlot(arena, cell), out = arena.value(type), found = arena.value("_Bool");
          arena.result("nt_spatial_raster_cell_hit", pointer(arena, owner.active()), at, pointer(arena, found), pointer(arena, out));
          if (arena.unsigned(found, arena.api.size("_Bool")) === 0n) return undefined;
          return { samplePosition: readCellPoint(arena, field(arena, out, type, "sample_position")),
            sample: sampleOutput(arena, field(arena, out, type, "sample")) };
        });
      },
      frame(mode) {
        return raw.withArena(arena => {
          const out = arena.value("pointer"), settings = samplingSlot(arena, mode, failure);
          arena.result("nt_spatial_raster_frame", pointer(arena, owner.active()), settings, pointer(arena, 0), pointer(arena, out));
          return createFrame(arena.pointer(out));
        });
      },
      close: owner.close,
    });
    rasters.set(raster, owner);
    return raster;
  }
  const methods: SpatialMeshes = {
    createMesh: data => createMesh("nt_spatial_mesh_create", arena => meshInput(arena, data, failure)),
    decodeOBJ: source => createMesh("nt_spatial_mesh_decode_obj", arena => arena.byteView(source)),
    rasterize(options, instances) {
      return raw.withArena(arena => {
        const type = "NtSpatialMeshInstance", stride = arena.api.size(type), count = checkedCount(instances.length, stride, failure);
        const input = rasterOptions(arena, options, failure), entries = count ? arena.allocate(count * stride) : 0;
        const sources: SpatialMesh[] = [];
        for (let i = 0; i < count; i++) {
          const address = entries + i * stride;
          const { mesh, transform, objectId, owner, tint, texture } = instances[i];
          sources.push(mesh);
          if (transform !== undefined) arena.setPointer(field(arena, address, type, "transform"),
            arena.pointer(transformPointer(arena, transform, failure)));
          writeUInt(arena, address, type, "object_id", uint64(objectId));
          writeUInt(arena, address, type, "owner", uint64(owner));
          writeColor(arena, field(arena, address, type, "tint"), tint);
          if (texture !== undefined) writeTexture(arena, address, texture, failure);
        }
        // All caller fields have been read. Resolve the live owners before C reads any handles.
        for (let i = 0; i < count; i++) {
          arena.setPointer(field(arena, entries + i * stride, type, "mesh"), resolve(meshes, sources[i]));
        }
        const out = arena.value("pointer");
        arena.result("nt_spatial_rasterize", pointer(arena, 0), input, pointer(arena, entries), sizeSlot(arena, count), pointer(arena, out));
        return ownRaster(arena.pointer(out));
      });
    },
  };
  return {
    methods,
    paint(raster: SpatialRaster, context: number, origin: Point, mode: SpatialSampling, borrow: () => void) {
      raw.withArena(arena => {
        const at = cellSlot(arena, origin), settings = samplingSlot(arena, mode, failure);
        borrow();
        arena.result("nt_spatial_raster_paint", pointer(arena, resolve(rasters, raster)), pointer(arena, context), at, settings);
      });
    },
  };
}
