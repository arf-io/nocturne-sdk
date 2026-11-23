import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { loadNocturne } from "../dist/index.js";

const wasm = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));

test("particle views and mounted draws use the caller clock, motion and owner", async () => {
  const sdk = await loadNocturne(wasm);
  const tree = sdk.createTree();
  const emitterOptions = { periodUs: 0n, lifetimeMinUs: 1000000n, lifetimeMaxUs: 1000000n,
    origin: { min: { x: 1, y: 1 }, max: { x: 1, y: 1 } },
    velocity: { min: { x: 4, y: 0 }, max: { x: 4, y: 0 } } };
  const options = { emitter: emitterOptions, paint: { glyph: "+", trail: { spacingUs: 250000n, samples: 3 } } };
  const size = { columns: 10, rows: 3 };
  const emitter = sdk.createParticleEmitter(emitterOptions);
  try {
    const node = tree.mountParticleView(options);
    tree.tick(500000n, "full");
    tree.publish(size);
    const cell = x => tree.frame.cell({ x, y: 1 });
    const owner = cell(3).owner;
    assert.equal(cell(3).glyph, "+");
    assert.notEqual(owner, 0n);
    assert.equal(cell(2).owner, owner);
    assert.throws(() => tree.setParticleView(node, { ...options, paint: { glyph: "wide" } }), error => error.code === 1);
    tree.tick(600000n, "reduced");
    tree.publish(size);
    assert.equal(cell(1).glyph, "+");
    assert.equal(cell(2).owner, 0n);
    tree.remove(node);
    let retained;
    tree.mountComponent({ paint(paint) {
      retained = paint;
      paint.drawParticles(emitter, 500000n, options.paint, "reduced");
    } });
    tree.publish(size);
    assert.equal(cell(1).glyph, "+");
    assert.notEqual(cell(1).owner, 0n);
    assert.notEqual(cell(1).owner, owner);
    assert.throws(() => retained.drawParticles(emitter, 500000n), error => error.code === 5);
  } finally { emitter.close(); tree.close(); }
});
