import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { loadNocturne, Repeat } from "../dist/index.js";

const wasm = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));
const v = (x = 0, y = 0, z = 0) => ({ x, y, z });
const assertVector = (value, expected) => {
  for (const key of ["x", "y", "z"]) {
    assert.ok(Math.abs(value[key] - expected[key]) <= 2e-13, `${key}: ${value[key]} instead of ${expected[key]}`);
  }
};
const projection = { kind: "perspective", halfWidth: 1, halfHeight: .5, nearDepth: 1, farDepth: 8 };

test("uses the typed spatial vector, affine and camera contracts", async () => {
  const sdk = await loadNocturne(wasm);
  const spatial = sdk.spatial;
  if (!sdk.availableComponents.includes("spatial")) {
    assert.throws(() => spatial.add(v(), v()), { code: 7 });
    assert.throws(() => spatial.identityTransform(), { code: 7 });
    return;
  }
  const a = v(1, 2, 3), b = v(2, 4, 5);
  assertVector(spatial.add(a, b), v(3, 6, 8));
  assertVector(spatial.subtract(b, a), v(1, 2, 2));
  assertVector(spatial.scale(a, 2), v(2, 4, 6));
  assert.equal(spatial.dot(a, b), 25);
  assertVector(spatial.cross(a, b), v(-2, 1));
  assert.equal(spatial.length(v(3, 4)), 5);
  assertVector(spatial.normalize(v(3, 4)), v(.6, .8));
  assert.throws(() => spatial.normalize(v()), { code: 1 });

  const options = spatial.defaultTransformOptions();
  assert.deepEqual(spatial.createTransform(options), spatial.identityTransform());
  const affine = spatial.createTransform({ ...options, scale: v(2, 3, 4), pivot: a, translation: v(10, -2, 1) });
  assertVector(spatial.transformPoint(affine, b), v(13, 6, 12));
  assertVector(spatial.transformDirection(affine, b), v(4, 12, 20));
  const roundtrip = spatial.compose(spatial.inverse(affine), affine);
  assertVector(spatial.transformPoint(roundtrip, b), b);
  const singular = spatial.createTransform({ ...options, scale: v(1, 1, 0) });
  assert.throws(() => spatial.inverse(singular), { code: 1 });
  assert.throws(() => spatial.inverse({ ...affine, linear: [[1, 0, 0]] }), { code: 1 });

  const camera = spatial.lookAt(v(0, 0, 4), v(), v(0, 1)), orbit = spatial.orbit(v(), 4, 0, 0);
  for (const key of ["position", "right", "up", "backward"]) assertVector(camera[key], orbit[key]);
  assertVector(spatial.toView(camera, v()), v(0, 0, -4));
  assertVector(spatial.fly(camera, v(1, 2, -1), 0, 0, 0).position, v(1, 2, 3));
  assert.throws(() => spatial.toView({ ...camera, up: v() }, b), { code: 1 });
});

test("retains clip weights, misses, IDs and exact picking order", async () => {
  const sdk = await loadNocturne(wasm);
  const spatial = sdk.spatial;
  const center = { x: 0, y: 0 };
  if (!sdk.availableComponents.includes("spatial")) {
    assert.throws(() => spatial.pick(projection, center, []), { code: 7 });
    return;
  }

  const point = v(2, 1, -4);
  const expected = { screen: { x: .5, y: .5 }, depth: 4 };
  assert.deepEqual(spatial.project(projection, point), expected);
  assertVector(spatial.unproject(projection, expected.screen, 4), point);
  assert.equal(spatial.project(projection, v(0, 0, 4)), undefined);
  assert.deepEqual(spatial.project({ ...projection, kind: "orthographic" }, v(.5, .25, -4)), expected);
  const segment = spatial.clipSegment(projection, v(-8, 0, -4), v(8, 0, -4));
  assertVector(segment.first.weights, v(.75, .25));
  assertVector(segment.last.weights, v(.25, .75));
  assert.equal(segment.first.projected.screen.x, -1);
  assert.equal(segment.last.projected.screen.x, 1);
  const tri = depth => [v(-1, -1, -depth), v(1, -1, -depth), v(0, 1, -depth)];
  const polygon = spatial.clipTriangle(projection, [v(-8, -1, -4), v(8, -1, -4), v(0, 1, -4)]);
  assert.equal(polygon.length, 5);
  assert.deepEqual(spatial.clipTriangle(projection, tri(-2)), []);
  assert.throws(() => spatial.clipTriangle(projection, [...tri(2), v()]), { code: 1 });

  const ids = { objectId: 2n ** 64n - 1n, primitiveId: 2n ** 63n };
  const picks = [
    { view: tri(4), objectId: 1n, primitiveId: 2n },
    { view: tri(2), ...ids },
    { view: tri(2), objectId: 3n, primitiveId: 4n },
  ];
  const hit = spatial.pick(projection, center, picks);
  assert.deepEqual({ objectId: hit.objectId, primitiveId: hit.primitiveId }, ids);
  assert.equal(hit.triangleIndex, 1);
  assert.equal(hit.depth, 2);
  assertVector(hit.weights, v(.25, .25, .5));
  assertVector(hit.view, v(0, 0, -2));
  assert.equal(spatial.pick(projection, { x: 2, y: 0 }, picks), undefined);
  assert.equal(spatial.pick(projection, center, []), undefined);
  assert.throws(() => spatial.pick(projection, center, [{ ...picks[1], objectId: 2n ** 64n }]), RangeError);
  picks[2].view[0].x = NaN;
  assert.throws(() => spatial.pick(projection, { x: 2, y: 0 }, picks), { code: 1 });
});

test("owns path points and gives signed travel and reliable close semantics", async () => {
  const sdk = await loadNocturne(wasm);
  const spatial = sdk.spatial;
  const points = [v(), v(3), v(3, 4)];
  if (!sdk.availableComponents.includes("spatial")) {
    assert.throws(() => spatial.createPath({ points }), { code: 7 });
    return;
  }
  assert.throws(() => spatial.createPath({ points, boundary: 2 ** 32 }), { code: 1 });
  const path = spatial.createPath({ points, boundary: Repeat.pingPong });
  try {
    assert.equal(Object.hasOwn(path, "raw"), false);
    assert.equal(Object.hasOwn(path, "failure"), false);
    points[1].x = 0;
    assert.equal(path.length(), 7);
    assert.throws(() => spatial.createPath({ points }), { code: 1 });
    for (const [input, distance, position, tangent] of [
      [-1, 1, v(1), v(-1)], [3, 3, v(3), v(0, 1)], [8, 6, v(3, 3), v(0, -1)],
    ]) {
      const sample = path.sample(input);
      assert.equal(sample.distance, distance);
      assert.equal(sample.length, 7);
      assertVector(sample.position, position);
      assertVector(sample.tangent, tangent);
    }
    assert.throws(() => path.sample({ valueOf() { path.close(); return 1; } }), { code: 5 });
    assert.throws(() => path.length(), { code: 5 });
  } finally {
    path.close();
  }
});
