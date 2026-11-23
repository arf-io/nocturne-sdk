import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { AnimationPhase, Easing, loadNocturne, PlaybackState, Repeat, RetargetPolicy } from "../dist/index.js";

const source = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));
const start = (1n << 53n) + 1n;
const max = (1n << 64n) - 1n;
const invalid = { nativeName: "NT_RESULT_INVALID_ARGUMENT" };

test("samples eased keys on exact uint64 boundaries in both directions", async () => {
  const sdk = await loadNocturne(source);
  assert.equal(sdk.ease(Easing.easeIn, .5), .25);
  assert.throws(() => sdk.ease(Easing.linear, Infinity), invalid);

  const track = {
    delayUs: start, repeat: Repeat.pingPong,
    keys: [{ atUs: 0n, value: 0 }, { atUs: 50n, value: 10, easing: Easing.easeIn }, { atUs: 100n, value: 20 }],
  };
  assert.deepEqual(sdk.sampleKeyframes(track, start - 1n),
    { value: 0, progress: 0, iteration: 0n, phase: AnimationPhase.delayed, direction: 1 });
  assert.deepEqual(sdk.sampleKeyframes(track, start + 50n),
    { value: 10, progress: .5, iteration: 0n, phase: AnimationPhase.running, direction: 1 });
  assert.deepEqual(sdk.sampleKeyframes(track, start + 100n),
    { value: 20, progress: 1, iteration: 1n, phase: AnimationPhase.running, direction: -1 });
  assert.deepEqual(sdk.sampleKeyframes(track, start + 125n),
    { value: 12.5, progress: .75, iteration: 1n, phase: AnimationPhase.running, direction: -1 });

  const exact = { keys: [{ atUs: 0n, value: 1, easing: Easing.stepEnd }, { atUs: max, value: 2 }] };
  const before = sdk.sampleKeyframes(exact, max - 1n);
  assert.equal(before.value, 1);
  assert.ok(before.progress < 1);
  assert.deepEqual(sdk.sampleKeyframes(exact, max),
    { value: 2, progress: 1, iteration: 0n, phase: AnimationPhase.finished, direction: 1 });
  for (const invalidTrack of [
    { keys: [] },
    { keys: [{ atUs: 0n, value: 1 }], repeat: Repeat.loop },
    { keys: [{ atUs: 1n, value: 1 }, { atUs: 2n, value: 2 }] },
    { keys: [{ atUs: 0n, value: 1 }, { atUs: 0n, value: 2 }] },
  ]) {
    assert.throws(() => sdk.sampleKeyframes(invalidTrack, 0n), invalid);
  }
  assert.throws(() => sdk.sampleKeyframes(exact, max + 1n), RangeError);
});

test("branches playback snapshots without changing the caller clock or prior state", async () => {
  const sdk = await loadNocturne(source);
  const playing = sdk.createPlayback(start);
  const paused = playing.pause(start + 20n);
  assert.equal(playing.state, PlaybackState.playing);
  assert.equal(playing.elapsed(start + 90n), 90n);
  assert.equal(paused.state, PlaybackState.paused);
  assert.equal(paused.elapsed(start + 90n), 20n);
  assert.throws(() => paused.elapsed(start + 19n), invalid);

  const resumed = paused.seek(start + 30n, 2n).resume(start + 90n);
  const cancelled = resumed.cancel(start + 100n);
  assert.equal(cancelled.state, PlaybackState.cancelled);
  assert.equal(cancelled.elapsed(max), 12n);
  assert.throws(() => cancelled.resume(start + 101n), invalid);
  assert.equal(cancelled.elapsed(start + 200n), 12n);

  const overflow = playing.seek(start, max);
  assert.throws(() => overflow.pause(start + 1n), { nativeName: "NT_RESULT_OUT_OF_RANGE" });
  assert.equal(overflow.elapsed(start), max);
  assert.throws(() => playing.seek(start, -1n), RangeError);
});

test("samples and retargets spring segments with the native reduced-motion policy", async () => {
  const sdk = await loadNocturne(source);
  const tuning = { responseUs: 100_000n, positionTolerance: .001, velocityTolerance: .001 };
  const spring = sdk.createSpring({ tuning, value: 0, target: 10, startUs: start });
  const at = start + 100_000n;
  assert.deepEqual(spring.sample(start - 1n), { value: 0, velocity: 0, phase: AnimationPhase.delayed });
  const initial = spring.sample(at);
  assert.equal(initial.phase, AnimationPhase.running);
  assert.ok(Math.abs(initial.value - (10 - 20 / Math.E)) < 1e-12);
  assert.ok(Math.abs(initial.velocity - 100 / Math.E) < 1e-12);

  assert.deepEqual(spring.retarget(at, 20, RetargetPolicy.keepVelocity).sample(at), initial);
  assert.deepEqual(spring.retarget(at, 20, RetargetPolicy.resetVelocity).sample(at), { ...initial, velocity: 0 });
  assert.deepEqual(spring.retarget(at, 20, RetargetPolicy.settle).sample(at),
    { value: 20, velocity: 0, phase: AnimationPhase.finished });
  assert.deepEqual(spring.sample(at), initial);
  assert.deepEqual(spring.sample(start - 1n, true), { value: 10, velocity: 0, phase: AnimationPhase.finished });
  assert.throws(() => spring.retarget(start - 1n, 20, RetargetPolicy.keepVelocity), invalid);
  assert.throws(() => sdk.createSpring({ tuning: { ...tuning, responseUs: 0n }, value: 0, target: 10, startUs: start }),
    invalid);
});
