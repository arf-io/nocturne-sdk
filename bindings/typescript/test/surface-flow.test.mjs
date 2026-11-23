import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { loadNocturne } from "../dist/index.js";

const wasm = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));
const v = (x = 0, y = 0, z = 0) => ({ x, y, z });
const max = 2n ** 64n - 1n;
const second = 1_000_000n;

function close(actual, expected) {
  assert.ok(Math.abs(actual - expected) <= 3e-13, `${actual} instead of ${expected}`);
}

function closeVector(actual, expected) {
  for (const key of ["x", "y", "z"]) close(actual[key], expected[key]);
}

test("samples signed surface poses and instantaneous world velocity", async () => {
  const { spatial, availableComponents } = await loadNocturne(wasm);
  const motion = { pivot: v(2, 3, 4), linearVelocity: v(2, -1, .5), angularVelocity: v(0, 0, 1) };
  if (!availableComponents.includes("spatial")) {
    assert.throws(() => spatial.surfacePose(motion, 0), { code: 7 });
    return;
  }
  const q = Math.PI / 2, initial = v(4, 3, 4);
  for (const [seconds, position, velocity] of [
    [q, v(2 + 2*q, 5 - q, 4 + .5*q), v(0, -1, .5)],
    [-q, v(2 - 2*q, 1 + q, 4 - .5*q), v(4, -1, .5)],
    [0, initial, v(2, 1, .5)],
  ]) {
    const sample = spatial.sampleSurface(motion, seconds, initial);
    closeVector(sample.position, position);
    closeVector(sample.velocity, velocity);
    closeVector(spatial.transformPoint(spatial.surfacePose(motion, seconds), initial), position);
  }
  const limit = Number.MAX_VALUE;
  const wide = { pivot: v(-limit), linearVelocity: v(limit), angularVelocity: v() };
  const sample = spatial.sampleSurface(wide, 2, wide.pivot);
  assert.equal(sample.position.x, limit);
  assert.equal(sample.velocity.x, limit);
  assert.throws(() => spatial.surfacePose(wide, 2), { code: 2 });
});

test("sweeps translating triangles with exact ties and coplanar entry", async () => {
  const { spatial, availableComponents } = await loadNocturne(wasm);
  if (!availableComponents.includes("spatial")) {
    assert.throws(() => spatial.sweepPoint(v(), v(), []), { code: 7 });
    return;
  }
  const initial = [v(), v(2), v(0, 2)];
  const triangles = [
    { initial, displacement: v(), objectId: 11n, primitiveId: 101n },
    { initial, displacement: v(0, 0, 2), objectId: max, primitiveId: 1n << 63n },
    { initial: [initial[0], initial[2], initial[1]], displacement: v(0, 0, 2), objectId: 13n, primitiveId: 103n },
  ];
  const first = v(.5, .5, 3), last = v(.5, .5, -1);
  const hit = spatial.sweepPoint(first, last, triangles);
  assert.equal(hit.objectId, max);
  assert.equal(hit.primitiveId, 1n << 63n);
  assert.equal(hit.triangleIndex, 1);
  assert.equal(hit.fraction, .5);
  closeVector(hit.position, v(.5, .5, 1));
  closeVector(hit.weights, v(.5, .25, .25));
  closeVector(hit.normal, v(0, 0, 1));
  const coplanar = spatial.sweepPoint(v(-2, 1, 0), v(2, 1, 0), triangles.slice(0, 1));
  assert.equal(coplanar.fraction, .5);
  closeVector(coplanar.position, v(0, 1, 0));
  closeVector(coplanar.weights, v(.5, 0, .5));
  const stationary = spatial.sweepPoint(v(), v(), triangles.slice(2));
  assert.equal(stationary.fraction, 0);
  closeVector(stationary.normal, v(0, 0, -1));
  assert.equal(spatial.sweepPoint(first, last, []), undefined);
  assert.throws(() => spatial.sweepPoint(first, last, [{ ...triangles[0], objectId: max + 1n }]), RangeError);
  triangles[2].displacement.z = NaN;
  assert.throws(() => spatial.sweepPoint(first, last, triangles), { code: 1 });
});

test("samples every flow port and retained volume at unsigned caller time", async () => {
  const sdk = await loadNocturne(wasm);
  if (!sdk.availableComponents.includes("simulation")) {
    assert.throws(() => sdk.sampleFlow({ stages: [], inputs: [] }, 0n), { code: 7 });
    return;
  }
  const channel = {
    stages: [{ transitUs: 2n * second, residenceUs: 0n }, { transitUs: second, residenceUs: second },
      { transitUs: 0n, residenceUs: second }],
    inputs: [{ beginUs: second, endUs: 4n * second, rate: 6 }, { beginUs: 5n * second, endUs: 6n * second, rate: 2 }],
  };
  const at = sdk.sampleFlow(channel, 3n * second);
  assert.deepEqual(at.ports.slice(0, 3), [{ volume: 12, rate: 6 }, { volume: 0, rate: 6 }, { volume: 0, rate: 0 }]);
  assert.deepEqual(at.retained, [12, 0, 0]);
  const later = sdk.sampleFlow(channel, 6n * second);
  const e = Math.exp(-2);
  close(later.ports[3].volume, 24 * e);
  close(later.ports[3].rate, 6 * (1 - 3 * e));
  for (const [i, stored] of later.retained.entries()) {
    assert.ok(stored >= 0);
    close(later.ports[i + 1].volume + stored, later.ports[i].volume);
  }
  const drained = sdk.sampleFlow(channel, max);
  close(drained.ports[3].volume, 20);
  assert.equal(drained.ports[3].rate, 0);
  assert.throws(() => sdk.sampleFlow(channel, -1n), RangeError);
  assert.throws(() => sdk.sampleFlow({ stages: Array(9).fill(channel.stages[0]), inputs: [] }, 0n), { code: 2 });
  assert.throws(() => sdk.sampleFlow({ stages: [], inputs: [{ beginUs: 0n, endUs: 2n, rate: 1 },
    { beginUs: 1n, endUs: 3n, rate: 1 }] }, 0n), { code: 1 });
});

test("displaces a section point and uses copied spray batches at lifetime and ID boundaries", async () => {
  const sdk = await loadNocturne(wasm);
  const base = { velocity: v(3, 1, 0), velocitySpread: v(), acceleration: v(0, -2, 0),
    lifetimeUs: 2n * second, dropletVolume: .125, seed: max, stream: 1n << 63n };
  if (!sdk.availableComponents.includes("simulation")) {
    assert.throws(() => sdk.emitSpray({ ...base, origin: v() }, 2, 5, 0n, 0n), { code: 7 });
    return;
  }
  const section = sdk.sampleFlowSection({ width: 4, speed: 2, maximumDepth: .5 }, 6);
  const origin = sdk.flowSectionPoint(v(2, 3, 4), v(0, 1, 0), section.depth, .5);
  assert.deepEqual(origin, v(2, 3.25, 4));
  assert.throws(() => sdk.flowSectionPoint(v(), v(0, 2, 0), 1, .5), { code: 1 });
  const birth = max - 2n * second;
  const options = { ...base, origin };
  const batch = sdk.emitSpray(options, section.excessRate, 5, max - 4n, birth);
  assert.deepEqual(batch, { options, birthUs: birth, firstId: max - 4n, count: 5, emittedVolume: .625, remainingVolume: 1.375 });
  options.origin.x = 0;
  assert.deepEqual(sdk.sampleSpray(batch, 4, birth + second), { id: max, position: v(5, 3.25, 4),
    velocity: v(3, -1, 0), volume: .125, progress: .5 });
  assert.equal(sdk.sampleSpray(batch, 0, birth - 1n), undefined);
  assert.equal(sdk.sampleSpray(batch, 0, max), undefined);
  assert.throws(() => sdk.emitSpray(options, 1, 2, max, 0n), { code: 2 });
  assert.throws(() => sdk.sampleSpray(batch, 5, birth), { code: 2 });
});
