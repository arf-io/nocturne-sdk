import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { CallError, loadNocturne } from "../dist/index.js";

const wasm = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));
const at = (x = 0, y = 0) => ({ x, y });
const v = (x, y, z) => ({ x, y, z });
const white = [255, 255, 255, 255];
const id = 2n ** 64n - 1n, owner = 2n ** 63n;
const obj = "v -4 -4 -2\nv 4 -4 -2\nv 0 6 -2\nvt 1 0\nvt 0 1\nf -3/1 -2/2 -1\n";
const triangle = () => ({
  vertices: [
    { position: v(-2, -2, -2), uv: at() }, { position: v(2, -2, -2), uv: at(1) },
    { position: v(0, 2, -4), uv: at(1, 1) },
  ],
  faces: [{ indices: [0, 1, 2], primitiveId: id }],
});
const fixture = {
  projection: { kind: "perspective", halfWidth: 1, halfHeight: 1, nearDepth: 1, farDepth: 8 },
  samples: { columns: 3, rows: 3 },
  background: [4, 8, 16, 255],
};

test("owns indexed meshes, preserves textured depth ties, and returns an independent frame", async () => {
  const { spatial, availableComponents } = await loadNocturne(wasm);
  const data = triangle();
  if (!availableComponents.includes("spatial")) {
    assert.throws(() => spatial.createMesh(data), { code: 7 });
    assert.throws(() => spatial.rasterize(fixture, []), { code: 7 });
    return;
  }
  const mesh = spatial.createMesh(data);
  let raster, frame;
  try {
    data.vertices[1].position.x = 999;
    const copied = mesh.data();
    assert.equal(copied.vertices[1].position.x, 2);
    assert.equal(copied.faces[0].primitiveId, id);
    assert.throws(() => spatial.createMesh({ ...data, faces: [{ indices: [0, 1, 3], primitiveId: 1n }] }), { code: 1 });
    assert.throws(() => spatial.createMesh({ ...data, faces: [{ indices: [0, 1, 2], primitiveId: -1n }] }), RangeError);
    const pixels = [[0, 255, 0, 255], [1, 2, 3, 255], [0, 0, 0, 0], [100, 100, 100, 255], [240, 120, 60, 255]];
    const texture = { size: { columns: 2, rows: 2 }, stride: 3, pixels };
    const instance = { mesh, objectId: id, owner, tint: white, texture };
    const draws = [instance, { mesh, objectId: 72n, owner: 82n, tint: white }];
    assert.throws(() => spatial.rasterize({ ...fixture, sampleLimit: 8 }, draws), { code: 2 });
    assert.throws(() => spatial.rasterize({ ...fixture, triangleLimit: 1 }, draws), { code: 2 });
    raster = spatial.rasterize(fixture, draws);
    pixels[4] = [0, 0, 0, 0];
    mesh.close();
    assert.throws(() => mesh.data(), { code: 5 });
    assert.equal(copied.vertices[1].position.x, 2);
    assert.throws(() => spatial.rasterize(fixture, draws), { code: 5 });
    assert.deepEqual(raster.size, fixture.samples);
    assert.deepEqual(raster.sample(at(1, 1)), { color: [240, 120, 60, 255], covered: true, owner,
      objectId: id, primitiveId: id, depth: 3, uv: at(.75, .5) });
    assert.deepEqual(raster.sample(at(1, 0)), { color: fixture.background, covered: false, owner: 0n,
      objectId: 0n, primitiveId: 0n, depth: 0, uv: at() });
    frame = raster.frame("braille");
    raster.close();
    assert.deepEqual(frame.size, { columns: 2, rows: 1 });
    assert.throws(() => raster.sample(at()), { code: 5 });
  } finally {
    frame?.close();
    raster?.close();
    mesh.close();
  }
});

test("decodes counted OBJ and paints mounted rasters with cell hit identity", async () => {
  const sdk = await loadNocturne(wasm);
  const { spatial } = sdk;
  if (!sdk.availableComponents.includes("spatial")) {
    assert.throws(() => spatial.decodeOBJ(obj), { code: 7 });
    return;
  }
  const mesh = spatial.decodeOBJ(new TextEncoder().encode(obj));
  const tree = sdk.createTree();
  let raster, clear, direct, escaped;
  try {
    assert.deepEqual(mesh.data().faces, [{ indices: [0, 1, 2], primitiveId: 0n }]);
    assert.deepEqual(mesh.data().vertices[0].uv, at(1, 0));
    assert.throws(() => spatial.decodeOBJ(obj + "\0"), { code: 1 });
    const camera = spatial.lookAt(v(0, 0, 1), v(0, 0, 0), v(0, 1, 0));
    const transform = spatial.createTransform({ ...spatial.defaultTransformOptions(), translation: v(0, 0, 1) });
    const grid = {
      projection: { ...fixture.projection, kind: "orthographic", farDepth: 3 },
      samples: { columns: 5, rows: 7 }, background: [14, 17, 19, 255], camera,
    };
    raster = spatial.rasterize(grid, [{ mesh, transform, objectId: 50n, owner: 81n, tint: [30, 80, 160, 255] }]);
    assert.deepEqual(raster.cellSize, { columns: 3, rows: 2 });
    const hit = raster.cellHit(at(2, 1));
    assert.deepEqual(hit.samplePosition, at(4, 5));
    assert.equal(hit.sample.objectId, 50n);
    assert.equal(hit.sample.owner, 81n);
    assert.equal(hit.sample.depth, 2);
    clear = spatial.rasterize(grid, []);
    assert.equal(clear.cellHit(at()), undefined);
    assert.throws(() => clear.cellHit(at(-1)), { code: 2 });

    direct = raster.frame("braille");
    let mode = "braille";
    const node = tree.mountComponent({ paint(paint) {
      escaped = paint;
      paint.paintRaster(raster, { get x() {
        assert.throws(() => paint.fill({ origin: at(), size: { columns: 1, rows: 1 } }, white), { code: 6 });
        return -1;
      }, y: 0 }, mode);
    } });
    const size = { columns: 2, rows: 2 };
    tree.publish(size);
    const prior = tree.frame;
    const cell = prior.cell(at()), reference = direct.cell(at(1));
    assert.equal(cell.glyph, reference.glyph);
    assert.deepEqual(cell.style, reference.style);
    assert.notEqual(cell.owner, reference.owner);
    assert.notEqual(cell.owner, 0n);
    assert.throws(() => escaped.paintRaster(raster, at(), "braille"), { code: 5 });
    mode = "invalid";
    tree.invalidate(node, "paint");
    assert.throws(() => tree.publish(size), error => error instanceof CallError && error.callbacks[0].cause.code === 1);
    assert.deepEqual(prior.cell(at()), cell);
  } finally {
    tree.close();
    direct?.close();
    clear?.close();
    raster?.close();
    mesh.close();
  }
});

test("checks instance ownership and liveness after reading caller fields", async () => {
  const sdk = await loadNocturne(wasm);
  if (!sdk.availableComponents.includes("spatial")) {
    assert.throws(() => sdk.spatial.decodeOBJ(obj), { code: 7 });
    return;
  }
  const second = await loadNocturne(wasm);
  const mesh = sdk.spatial.createMesh(triangle()), otherMesh = second.spatial.createMesh(triangle());
  const draw = { mesh, objectId: 1n, owner: 2n, tint: white };
  let raster;
  try {
    assert.throws(() => sdk.spatial.rasterize(fixture, [{ ...draw, mesh: otherMesh }]), { code: 1 });
    assert.throws(() => sdk.spatial.rasterize(fixture, [draw, { ...draw, get tint() {
      mesh.close();
      return white;
    } }]), { code: 5 });
    raster = sdk.spatial.rasterize(fixture, []);
    assert.throws(() => raster.sample({ get x() { raster.close(); return 0; }, y: 0 }), { code: 5 });
  } finally {
    raster?.close();
    mesh.close();
    otherMesh.close();
  }
});
