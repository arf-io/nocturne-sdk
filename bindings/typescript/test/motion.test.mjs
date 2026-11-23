import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { Easing, loadNocturne, Repeat } from "../dist/index.js";

const source = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));
const invalid = { nativeName: "NT_RESULT_INVALID_ARGUMENT" };
const range = { nativeName: "NT_RESULT_OUT_OF_RANGE" };

test("samples open and closed paths with direction-aware joins", async () => {
  const sdk = await loadNocturne(source);
  const points = [{ x: 0, y: 0 }, { x: 3, y: 0 }, { x: 3, y: 4 }];
  assert.equal(sdk.pathLength({ points }), 7);
  assert.deepEqual(sdk.samplePath({ points }, 3), {
    position: { x: 3, y: 0 }, tangent: { x: 0, y: 1 }, distance: 3, length: 7,
  });
  assert.deepEqual(sdk.samplePath({ points }, 99).position, { x: 3, y: 4 });
  for (const distance of [-3, 11]) {
    const reverse = sdk.samplePath({ points, boundary: Repeat.pingPong }, distance);
    assert.deepEqual(reverse, { position: { x: 3, y: 0 }, tangent: { x: -1, y: -0 }, distance: 3, length: 7 });
  }
  const wrapped = sdk.samplePath({ points, closed: true, boundary: Repeat.loop }, -2);
  assert.equal(wrapped.distance, 10);
  assert.equal(wrapped.length, 12);
  assert.ok(Math.abs(wrapped.position.x - 1.2) < 1e-15);
  assert.ok(Math.abs(wrapped.position.y - 1.6) < 1e-15);
  assert.deepEqual(wrapped.tangent, { x: -.6, y: -.8 });

  const shrinking = [{ get x() {
    shrinking.pop();
    return 0;
  }, y: 0 }, points[1]];
  assert.equal(sdk.pathLength({ points: shrinking }), 3);
  assert.equal(shrinking.length, 1);
  for (const path of [{ points: [] }, { points: [points[0], points[0]] },
    { points: points.slice(0, 2), closed: true }, { points: [...points, points[0]], closed: true },
    { points: [points[0], { x: NaN, y: 0 }] }]) assert.throws(() => sdk.pathLength(path), invalid);
  assert.throws(() => sdk.samplePath({ points }, Infinity), invalid);
  assert.throws(() => sdk.pathLength({ points: [points[0], { x: Number.MAX_VALUE, y: 0 }],
    boundary: Repeat.pingPong }), range);
});

test("projects signed panel scales and samples exact reduced-motion and delay endpoints", async () => {
  const sdk = await loadNocturne(source);
  const pose = { anchor: { x: 2, y: 3 }, offset: { x: -1, y: 4 }, scale: { x: -2, y: .5 },
    turns: .25, opacity: 1 };
  assert.deepEqual(sdk.projectPanelPoint(pose, { x: 4, y: 7 }), { x: -1, y: 3 });
  assert.deepEqual(sdk.projectPanelPoint(pose, pose.anchor), { x: 1, y: 7 });

  const delayUs = (1n << 53n) + 1n;
  const from = { anchor: { x: 0, y: 0 }, offset: { x: 0, y: 0 }, scale: { x: 1, y: 1 }, turns: 0, opacity: .25 };
  const to = { anchor: { x: 4, y: 8 }, offset: { x: 8, y: -4 }, scale: { x: 3, y: -1 }, turns: 1, opacity: .75 };
  const transition = { from, to, delayUs, durationUs: 100n, easing: Easing.easeIn };
  assert.deepEqual(sdk.samplePanelPose(transition, delayUs - 1n), from);
  assert.deepEqual(sdk.samplePanelPose(transition, delayUs + 50n), {
    anchor: { x: 1, y: 2 }, offset: { x: 2, y: -1 }, scale: { x: 1.5, y: .5 }, turns: .25, opacity: .375,
  });
  assert.deepEqual(sdk.samplePanelPose(transition, delayUs + 100n), to);
  assert.deepEqual(sdk.samplePanelPose(transition, 0n, "reduced"), to);
  assert.throws(() => sdk.samplePanelPose({ ...transition, delayUs: (1n << 64n) - 1n }, 0n, "reduced"), range);
  assert.throws(() => sdk.samplePanelPose(transition, 1n << 64n), RangeError);
  assert.throws(() => sdk.projectPanelPoint({ ...pose, opacity: 1.01 }, pose.anchor), invalid);
  assert.throws(() => sdk.projectPanelPoint({ ...from, scale: { x: Number.MAX_VALUE, y: 1 } }, { x: 2, y: 0 }), range);
});
