import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { loadNocturne } from "../dist/index.js";

const wasm = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));

test("particle recipe defaults can be edited for owned and mounted effects", async () => {
  const sdk = await loadNocturne(wasm);
  const recipe = sdk.particleEffectRecipe("sparks");
  assert.equal(recipe.motion.radial, true);
  assert.equal(recipe.motion.directionTurns, 0.625);
  assert.equal(recipe.view.paint.trail.samples, 3);
  const snow = sdk.particleEffectRecipe("snow");
  assert.deepEqual(snow.motion.swayAmplitude, { x: 1.3, y: 0 });
  assert.equal(snow.motion.swayPeriodUs, 1600000n);
  assert.equal(snow.view.paint.glyph, "*");
  assert.throws(() => sdk.particleEffectRecipe("unknown"), RangeError);

  const startUs = (1n << 63n) + 17n;
  const now = startUs + 250000n;
  const origin = { x: 2, y: 2 };
  const effect = {
    view: { ...recipe.view, paint: { ...recipe.view.paint, glyph: "#", trail: { samples: 0, spacingUs: 0n } },
      emitter: { ...recipe.view.emitter, startUs, seed: (1n << 64n) - 1n, stream: (1n << 54n) + 3n,
        birthsPerTick: 1, origin: { min: origin, max: origin }, acceleration: { x: 0, y: 0 },
        lifetimeMinUs: 1000000n, lifetimeMaxUs: 1000000n } },
    motion: { ...recipe.motion, directionTurns: 0.25, sweepTurns: 0, speedMin: 4, speedMax: 4,
      glitterPeriodUs: 50000n, glitterMinAlpha: 120 },
  };
  const emitter = sdk.createParticleEffectEmitter(effect);
  const tree = sdk.createTree();
  try {
    const samples = emitter.sample(now);
    assert.equal(samples.length, 1);
    assert.equal(samples[0].bornUs, startUs);
    assert.deepEqual(samples[0].position, { x: 2, y: 3 });
    assert.equal(emitter.deadline(now, "full", 200000n), now + 50000n);

    const node = tree.mountParticleEffect(effect);
    const assertGlyph = (y, glyph) => {
      tree.publish({ columns: 8, rows: 6 });
      const cell = tree.frame.cell({ x: 2, y });
      assert.equal(cell.glyph, glyph);
      assert.notEqual(cell.owner, 0n);
    };
    tree.tick(now, "full");
    assertGlyph(3, "#");
    const badMotion = { ...effect, motion: { ...effect.motion, sweepTurns: 2 } };
    assert.throws(() => tree.setParticleEffect(node, badMotion), error => error.code === 1);
    const badGlyph = { ...effect, view: { ...effect.view, paint: { glyph: "wide" } } };
    sdk.createParticleEffectEmitter(badGlyph).close();
    assert.throws(() => tree.setParticleEffect(node, badGlyph), error => error.code === 1);
    tree.tick(now + 50000n, "reduced");
    assertGlyph(2, "#");
    tree.setParticleEffect(node, { ...effect, view: { ...effect.view, paint: { glyph: "+" } } });
    assertGlyph(2, "+");
  } finally {
    emitter.close();
    tree.close();
  }
});
