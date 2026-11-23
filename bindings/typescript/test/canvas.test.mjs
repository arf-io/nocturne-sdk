import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { CompositeOperator, loadNocturne } from "../dist/index.js";

const wasm = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));

test("owned canvas reduces clipped subcell ink into an independent frame", async () => {
  const sdk = await loadNocturne(wasm);
  const mode = sdk.parseSamplingMode("braille");
  const ink = { color: [250, 80, 0, 255], owner: (1n << 63n) + 11n };
  const background = [0, 0, 60, 255];
  const canvas = sdk.createCanvas({ columns: 3, rows: 5 });
  try {
    canvas.clear(background);
    canvas.line({ x: -1, y: 0 }, { x: 1, y: 0 }, ink);
    const first = canvas.pixel({ x: 0, y: 0 });
    assert.deepEqual(first, { covered: true, color: ink.color, owner: ink.owner });
    const blank = canvas.pixel({ x: 2, y: 0 });
    assert.deepEqual(blank, { covered: false, color: background, owner: 0n });
    assert.throws(() => canvas.pixel({ x: -1, y: 0 }), error => error.code === 2);
    const reduced = sdk.reduceCellSamples([first, first, blank, blank, blank, blank, blank, blank],
      Array(8).fill(background), mode);
    assert.equal(reduced.glyph, "⠉");
    assert.equal(reduced.owner, ink.owner);
    const frame = canvas.render(mode);
    try {
      canvas.clear([0, 0, 0, 0]);
      canvas.close();
      assert.deepEqual(frame.size, { columns: 2, rows: 2 });
      const cell = frame.cell({ x: 0, y: 0 });
      assert.equal(cell.glyph, reduced.glyph);
      assert.equal(cell.owner, ink.owner);
      assert.deepEqual(cell.style, reduced.style);
    } finally {
      frame.close();
    }
    assert.throws(() => canvas.pixel({ x: 0, y: 0 }), error => error.code === 5);
  } finally {
    canvas.close();
  }
});

test("canvas draws each primitive and keeps its state after rejected ink", async () => {
  const sdk = await loadNocturne(wasm);
  const size = { columns: 4, rows: 4 };
  const canvas = sdk.createCanvas(size);
  const origin = { x: 0, y: 0 };
  const center = { x: 1, y: 1 };
  const inside = { x: 2, y: 2 };
  const bounds = { origin, size };
  const ink = { color: [120, 0, 220, 255], owner: (1n << 64n) - 1n };
  const owner = (at, expected) => {
    const pixel = canvas.pixel(at);
    assert.equal(pixel.owner, expected);
    assert.equal(pixel.covered, expected !== 0n);
  };
  try {
    assert.deepEqual(canvas.size, size);
    assert.equal(sdk.parseSamplingMode("half-block"), "halfBlock");
    assert.throws(() => sdk.parseSamplingMode("cell\0braille"), error => error.code === 1);
    canvas.border(bounds, ink);
    owner({ x: 3, y: 0 }, ink.owner);
    owner(center, 0n);
    canvas.triangle(origin, { x: 2, y: 0 }, { x: 0, y: 2 }, ink);
    owner(center, ink.owner);
    owner(inside, 0n);
    canvas.fill({ origin: inside, size: { columns: 1, rows: 1 } }, ink);
    owner(inside, ink.owner);
    assert.throws(() => canvas.fill(bounds, { ...ink, owner: 1n, composite: { operation: 999 } }),
      error => error.code === 1);
    owner(origin, ink.owner);
    owner({ x: 1, y: 2 }, 0n);

    const reentrantInk = { get color() { canvas.close(); return ink.color; } };
    assert.throws(() => canvas.point(center, reentrantInk), error => error.code === 6);
    owner(center, ink.owner);
    canvas.point(center, { ...ink, composite: { operation: CompositeOperator.clear } });
    owner(center, 0n);
    for (const mode of ["cell", "halfBlock", "braille", "hybrid"]) canvas.render(mode).close();
    assert.throws(() => canvas.render("unknown"), error => error.code === 1);
    assert.throws(() => sdk.reduceCellSamples([], [], "braille"), error => error.code === 1);
  } finally {
    canvas.close();
  }
});
