import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { loadNocturne } from "../dist/index.js";

const wasm = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));

test("particle deposits aggregate, enforce capacity and release at the clock limit", async () => {
  const sdk = await loadNocturne(wasm);
  const store = sdk.createParticleDeposits({ capacity: 2, holdUs: 10n,
    release: { count: 1, lifetimeMinUs: 20n, lifetimeMaxUs: 20n } });
  const at = { x: 2, y: 3 }, end = (1n << 64n) - 1n;
  const red = [255, 0, 0, 255], blue = [0, 0, 255, 255];
  try {
    assert.equal(store.add({ cell: at, at: end - 10n, mass: 1, color: red }), 0n);
    assert.equal(store.add({ cell: at, at: end - 9n, mass: 3, color: blue }), 1n);
    assert.throws(() => store.add({ cell: at, at: end - 8n, mass: 1, color: red }), error => error.code === 2);
    const piles = store.piles(end - 9n);
    assert.deepEqual(piles, [{ cell: at, contacts: 2, mass: 4n, color: [64, 0, 191, 255] }]);
    assert.equal(store.deadline(end - 1n, "reduced"), end);
    const releases = store.releases(end);
    assert.equal(releases.length, 1);
    assert.equal(releases[0].depositId, 0n);
    assert.equal(releases[0].particle.bornUs, end);
    assert.deepEqual(releases[0].particle.position, { x: 2.5, y: 3.5 });
    store.advance(end);
    assert.throws(() => store.piles(end - 1n), error => error.code === 1);
    store.close();
    assert.throws(() => store.piles(end), error => error.code === 5);
    assert.equal(piles[0].mass, 4n);
    assert.equal(releases[0].particle.bornUs, end);
  } finally { store.close(); }
});

test("mounted deposits paint held piles and releases through the component", async () => {
  const sdk = await loadNocturne(wasm);
  const store = sdk.createParticleDeposits({ holdUs: 10n, release: { count: 1 } });
  const tree = sdk.createTree();
  const cell = { x: 1, y: 1 };
  let now, retained;
  const style = {
    get pileGlyph() {
      assert.throws(() => store.close(), error => error.code === 6);
      return "+";
    },
    release: { glyph: "*" },
  };
  try {
    store.add({ cell, at: 0n, mass: 2, color: [255, 0, 0, 255] });
    const node = tree.mountComponent({ paint(paint) {
      retained = paint;
      paint.drawParticleDeposits(store, now, style, "reduced");
    } });
    for (const [at, glyph] of [[9n, "+"], [10n, "*"]]) {
      now = at;
      tree.invalidate(node, "paint");
      tree.publish({ columns: 4, rows: 3 });
      assert.equal(tree.frame.cell(cell).glyph, glyph);
      assert.notEqual(tree.frame.cell(cell).owner, 0n);
    }
    assert.throws(() => retained.drawParticleDeposits(store, now), error => error.code === 5);
  } finally {
    tree.close();
    store.close();
  }
});
