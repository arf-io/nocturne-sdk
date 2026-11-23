import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { loadNocturne } from "../dist/index.js";

const wasm = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));
const second = 1_000_000n;
const v = (x = 0, y = 0, z = 0) => ({ x, y, z });
const bodyAtRest = () => ({
  position: v(), rotationAxis: v(), rotationRadians: 0, mass: 3,
  principalInertia: v(2, 2, 2), linearVelocity: v(), angularVelocity: v(),
});
const plane = { point: v(), normal: v(0, 1) };
const zeroLoad = { force: v(), torque: v() };

function near(actual, expected) {
  assert.ok(Math.abs(actual - expected) <= 5e-12 * Math.max(1, Math.abs(expected)), `${actual} instead of ${expected}`);
}

function nearVector(actual, expected) {
  for (const key of ["x", "y", "z"]) near(actual[key], expected[key]);
}

function cube() {
  return {
    vertices: [v(-1,-1,-1), v(1,-1,-1), v(1,1,-1), v(-1,1,-1), v(-1,-1,1), v(1,-1,1), v(1,1,1), v(-1,1,1)],
    triangles: [[0,2,1], [0,3,2], [4,5,6], [4,6,7], [0,1,5], [0,5,4],
      [3,7,6], [3,6,2], [0,4,7], [0,7,3], [1,2,6], [1,6,5]],
  };
}

test("returns owned transport cells and the complete external budget", async () => {
  const sdk = await loadNocturne(wasm);
  const options = {
    cells: [
      { waterCapacity: 8, foamCapacity: 4, substrateHeatCapacity: 2, foamGeneration: 2 },
      { waterCapacity: 3, foamCapacity: 1, substrateHeatCapacity: 2, inletRate: 2, inletTemperature: 300, foamDecay: Math.LN2 },
      { waterCapacity: 8, foamCapacity: 8, substrateHeatCapacity: 2 },
    ],
    edges: [
      { source: 0, destination: 1, waterRate: 6, foamRate: 1 },
      { source: 0, destination: "drain", waterRate: 2, foamRate: 1 },
      { source: 1, destination: 2, waterRate: 9, foamRate: 1 },
    ],
    waterHeatCapacity: 2,
  };
  const cells = [{ water: 4, foam: 4, energy: 2000 }, { water: 0, foam: 0, energy: 200 }, { water: 3, foam: 1, energy: 3200 }];
  if (!sdk.availableComponents.includes("simulation")) {
    assert.throws(() => sdk.stepSurfaceTransport(options, cells, second), { code: 7 });
    return;
  }
  const before = structuredClone(cells);
  const step = sdk.stepSurfaceTransport(options, cells, second);
  assert.deepEqual(step.cells[0], { water: 0, foam: 2, energy: 400 });
  assert.deepEqual(step.cells[2], before[2]);
  assert.equal(step.cells[1].water, 3);
  assert.equal(step.cells[1].foam, 1);
  near(step.cells[1].energy, 5200 / 3);
  const expected = { waterInlet: 2, waterDrained: 1, waterSpilled: 2, waterEvaporated: 0,
    foamGenerated: 2, foamDrained: 2, foamSpilled: 0, foamDecayed: 1,
    heatInlet: 1200, heatDrained: 400, heatSpilled: 2600 / 3, heatEvaporated: 0, heatLatent: 0, heatApplied: 0, heatAmbient: 0 };
  assert.deepEqual(Object.keys(step.budget).sort(), Object.keys(expected).sort());
  for (const [key, value] of Object.entries(expected)) near(step.budget[key], value);
  const zero = sdk.stepSurfaceTransport(options, cells, 0n);
  assert.deepEqual(zero.cells, before);
  assert.ok(Object.values(zero.budget).every(value => value === 0));
  step.cells[2].energy = 0;
  assert.throws(() => sdk.stepSurfaceTransport(options, cells.slice(1), second), { code: 1 });
  assert.throws(() => sdk.stepSurfaceTransport(options, cells, -1n), RangeError);
  assert.throws(() => sdk.stepSurfaceTransport(options, cells, 1n << 64n), RangeError);
  options.cells[2].power = -1e18;
  assert.throws(() => sdk.stepSurfaceTransport(options, cells, second), { code: 2 });
  assert.deepEqual(cells, before);
});

test("owns hull geometry and composes plane and patch immersion with body advance", async () => {
  const sdk = await loadNocturne(wasm);
  const geometry = cube();
  if (!sdk.availableComponents.includes("simulation")) {
    assert.throws(() => sdk.createHull(geometry), { code: 7 });
    return;
  }
  const hull = sdk.createHull(geometry);
  const body = bodyAtRest();
  const patch = [[v(-1, 0, -1), v(1, 0, -1), v(1, 0, 1)], [v(-1, 0, -1), v(1, 0, 1), v(-1, 0, 1)]];
  try {
    geometry.vertices[0].y = 7;
    const half = hull.immersePlane(body, plane);
    assert.equal(half.fullVolume, 8);
    assert.equal(half.displacedVolume, 4);
    nearVector(half.center, v(0, -.5));
    nearVector(half.centerOffset, v(0, -.5));
    assert.deepEqual(hull.immerseWater(body, patch), half);
    const dry = hull.immerseWater(body, []);
    assert.equal(dry.fullVolume, 8);
    assert.equal(dry.displacedVolume, 0);
    assert.throws(() => hull.immerseWater(body, [patch[0], patch[0]]), { code: 1 });
    const buoyancy = sdk.hullBuoyancy(half, body, 1, 10);
    nearVector(buoyancy.lift, v(0, 40));
    nearVector(buoyancy.weight, v(0, -30));
    nearVector(buoyancy.force, v(0, 10));
    nearVector(buoyancy.torque, v());
    const next = sdk.stepFloatingBody(body, buoyancy, second / 2n);
    nearVector(next.position, v(0, 5 / 12));
    nearVector(next.linearVelocity, v(0, 5 / 3));
    nearVector(sdk.spatial.transformPoint(sdk.floatingBodyTransform(next), v()), next.position);
    assert.deepEqual(body, bodyAtRest());
    assert.throws(() => hull.immersePlane(body, {
      get point() { hull.close(); return plane.point; },
      normal: plane.normal,
    }), { code: 5 });
  } finally {
    hull.close();
  }
  assert.throws(() => hull.immerseWater(body, patch), { code: 5 });
});

test("exchanges linear and angular drag and preserves tiny rotation", async () => {
  const sdk = await loadNocturne(wasm);
  const linear = { first: { mass: 1, velocity: v(4, -1) }, second: { mass: 1, velocity: v(-2, -1) } };
  if (!sdk.availableComponents.includes("simulation")) {
    assert.throws(() => sdk.exchangeLinearDrag(linear, 1, second), { code: 7 });
    assert.throws(() => sdk.stepFloatingBody(bodyAtRest(), zeroLoad, second), { code: 7 });
    return;
  }
  const l = sdk.exchangeLinearDrag(linear, Math.LN2 / 2, second);
  nearVector(l.firstVelocity, v(2.5, -1));
  nearVector(l.secondVelocity, v(-.5, -1));
  nearVector(l.impulseOnSecond, v(1.5));
  near(l.dissipatedEnergy, 6.75);
  const angular = {
    first: { inertia: v(1, 2, 4), velocity: v(4, 8, -3) },
    second: { inertia: v(1, 2, 4), velocity: v(-2, 0, 1) },
  };
  const a = sdk.exchangeAngularDrag(angular, v(Math.LN2 / 2, Math.LN2), second);
  nearVector(a.firstVelocity, v(2.5, 6, -3));
  nearVector(a.secondVelocity, v(-.5, 2, 1));
  nearVector(a.impulseOnSecond, v(1.5, 4));
  near(a.dissipatedEnergy, 30.75);
  assert.throws(() => sdk.exchangeLinearDrag(linear, 1, second + 1n), { code: 2 });
  const tiny = Number.MIN_VALUE;
  const body = { ...bodyAtRest(), mass: 1, principalInertia: v(1, 1, 1), angularVelocity: v(tiny, tiny, tiny) };
  const next = sdk.stepFloatingBody(body, zeroLoad, second);
  assert.equal(next.rotationRadians, 2 * tiny);
  const axis = 1 / Math.sqrt(3);
  nearVector(next.rotationAxis, v(axis, axis, axis));
  assert.throws(() => sdk.stepFloatingBody({ ...body, angularVelocity: v(.5) }, zeroLoad, second), { code: 2 });
});
