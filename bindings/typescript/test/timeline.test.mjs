import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { AnimationPhase, loadNocturne, Repeat } from "../dist/index.js";

const source = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));
const wideChannel = (1n << 63n) + 17n;
const max = (1n << 64n) - 1n;
const invalid = { nativeName: "NT_RESULT_INVALID_ARGUMENT" };
const range = { nativeName: "NT_RESULT_OUT_OF_RANGE" };

test("compiles nested tracks with owned keys, edits and sorted channels", async () => {
  const sdk = await loadNocturne(source);
  const keys = [{ atUs: 0n, value: 0 }, { atUs: 200n, value: 40 }];
  const edits = [{ kind: "dwell", atUs: 50n, lengthUs: 20n }, { kind: "skip", atUs: 90n, lengthUs: 30n }];
  const random = { from: 0, to: 1, intervalUs: 8n, seed: max, stream: wideChannel, smooth: true };
  const randomValue = sdk.sampleRandomTrack(random, 16n);
  const parallel = { kind: "parallel", delayUs: 10n, children: [
    { kind: "clip", channel: wideChannel, spanUs: 100n, timing: { rate: { numerator: 2, denominator: 1 },
      quantumUs: 10n, edits }, source: { kind: "keyframes", keyframes: { keys } } },
    { kind: "clip", delayUs: 5n, channel: 7n, spanUs: 65n, source: { kind: "random", random } },
  ] };
  const timeline = sdk.createTimelineTree({ kind: "sequence", delayUs: 5n, children: [
    parallel,
    { kind: "clip", delayUs: 3n, channel: wideChannel, spanUs: 40n,
      source: { kind: "tween", tween: { from: 40, to: 80, durationUs: 40n } } },
  ] }, { delayUs: 9n, repeat: Repeat.pingPong });
  try {
    keys[1].value = -1;
    edits[0].lengthUs = 1n;
    random.intervalUs = 0n;
    parallel.children.length = 0;
    assert.equal(timeline.durationUs, 158n);
    assert.equal(timeline.channelCount, 2);
    assert.equal(timeline.channelAt(0), 7n);
    assert.equal(timeline.channelAt(1), wideChannel);
    assert.throws(() => timeline.channelAt(2), range);
    assert.deepEqual(timeline.sample(wideChannel, 84n), { value: 26, playheadUs: 75n, sourceUs: 130n, iteration: 0n,
      phase: AnimationPhase.running, clipPhase: AnimationPhase.running, direction: 1 });
    assert.equal(timeline.sample(7n, 45n).value, randomValue);
    assert.deepEqual(timeline.sample(wideChannel, 167n), { value: 80, playheadUs: 158n, sourceUs: 40n, iteration: 1n,
      phase: AnimationPhase.running, clipPhase: AnimationPhase.finished, direction: -1 });
  } finally {
    timeline.close();
  }
  assert.throws(() => timeline.channelCount, { nativeName: "NT_RESULT_STALE_REFERENCE" });
});

test("maps track time, schedules batches and bounds timeline nesting", async () => {
  const sdk = await loadNocturne(source);
  const upper = { numerator: 3, denominator: 2 };
  assert.deepEqual(sdk.limitTrackRate({ numerator: 5, denominator: 3 }, { numerator: 0, denominator: 0 }, upper), upper);
  const edits = [{ kind: "dwell", atUs: 10n, lengthUs: 8n }, { kind: "skip", atUs: 20n, lengthUs: 5n }];
  const timing = { rate: upper, quantumUs: 5n, edits };
  for (const [elapsed, want] of [[0n, 0n], [10n, 10n], [15n, 12n], [20n, 27n]]) {
    assert.equal(sdk.mapTrackTime(timing, elapsed), want);
  }
  assert.throws(() => sdk.mapTrackTime(timing, max), range);
  const schedule = { staggerUs: 10n, batchSize: 3n, seed: max };
  assert.deepEqual(sdk.scheduleTrack(schedule, 8n, max, 17n), { delayUs: 20n, spanUs: 17n });
  assert.throws(() => sdk.scheduleTrack(schedule, max, 0n, 0n), range);

  const clip = { kind: "clip", channel: 1n, spanUs: 1n,
    source: { kind: "tween", tween: { from: 0, to: 1, durationUs: 1n } } };
  assert.throws(() => sdk.createTimelineTree({ kind: "parallel", children: [clip, clip] }), invalid);
  assert.throws(() => sdk.createTimelineTree({ kind: "sequence", children: [] }), invalid);
  let root = clip;
  for (let i = 0; i < 127; ++i) root = { kind: "sequence", children: [root] };
  sdk.createTimelineTree(root).close();
  assert.throws(() => sdk.createTimelineTree({ kind: "sequence", children: [root] }), range);
  const cycle = { kind: "parallel", children: [] };
  cycle.children.push(cycle);
  assert.throws(() => sdk.createTimelineTree(cycle), range);

  const edited = [{ kind: "dwell", atUs: 10n, get lengthUs() {
    edited.pop();
    return 8n;
  } }, { kind: "skip", atUs: 20n, lengthUs: 5n }];
  assert.equal(sdk.mapTrackTime({ ...timing, edits: edited }, 20n), 27n);
  assert.equal(edited.length, 1);
});
