import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { loadNocturne } from "../dist/index.js";

const wasm = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));

test("particle schedules preserve full-width clocks and copied samples", async () => {
  const sdk = await loadNocturne(wasm);
  const now = (1n << 64n) - 1n;
  const options = { periodUs: 1n, capacity: 2, lifetimeMinUs: 2n, lifetimeMaxUs: 2n,
    seed: now, stream: (1n << 63n) + 5n };
  const emitter = sdk.createParticleEmitter(options);
  try {
    options.periodUs = 0n;
    const samples = emitter.sample(now);
    assert.deepEqual(samples.map(({ id, bornUs, ageUs }) => [id, bornUs, ageUs]),
      [[now - 1n, now - 1n, 1n], [now, now, 0n]]);
    assert.deepEqual(emitter.particle(now - 1n, now), samples[0]);
    assert.equal(emitter.particle(now - 2n, now), undefined);
    assert.deepEqual(emitter.trail(now - 1n, now, { spacingUs: 1n, samples: 4 })
      .map(({ id, ageUs }) => [id, ageUs]), [[now - 1n, 0n]]);
    assert.equal(emitter.deadline(now, "full"), undefined);
    emitter.close();
    assert.throws(() => emitter.sample(now), error => error.code === 5);
    assert.equal(samples[1].id, now);
  } finally { emitter.close(); }
});
