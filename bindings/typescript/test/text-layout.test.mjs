import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { GlyphPresentation, loadNocturne } from "../dist/index.js";

const wasm = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));
const invalid = { nativeName: "NT_RESULT_INVALID_ARGUMENT" };
const outOfRange = { nativeName: "NT_RESULT_OUT_OF_RANGE" };
const stale = { nativeName: "NT_RESULT_STALE_REFERENCE" };

test("measures a complete grapheme with explicit ambiguous and emoji widths", async () => {
  const sdk = await loadNocturne(wasm);
  for (const [glyph, width, presentation, columns] of [
    ["👩‍💻", {}, GlyphPresentation.emoji, 2],
    ["👩‍💻", { emoji: 0 }, GlyphPresentation.emoji, 2],
    ["👩‍💻", { emoji: 1 }, GlyphPresentation.emoji, 1],
    ["·", { ambiguous: 0 }, GlyphPresentation.text, 1],
    ["·", { ambiguous: 2 }, GlyphPresentation.text, 2],
    ["\u0301", {}, GlyphPresentation.isolatedMark, 1],
    ["\t", {}, GlyphPresentation.tab, 0],
    ["\r\n", {}, GlyphPresentation.lineBreak, 0],
    ["\0", {}, GlyphPresentation.replacement, 1],
  ]) {
    assert.deepEqual(sdk.measureGlyph(glyph, width), { presentation, columns });
  }
  for (const value of ["", "ab", Uint8Array.of(0xff), "\uD800"]) {
    assert.throws(() => sdk.measureGlyph(value), invalid);
  }
  assert.throws(() => sdk.measureGlyph("a", { emoji: 256 }), invalid);
});

test("snapshots text IDs, source, transformed glyphs, and visual caret edges", async () => {
  const sdk = await loadNocturne(wasm);
  const text = "e\u0301中👩‍💻\tQ\r\nאב";
  const source = sdk.createText("q" + text);
  const ids = [1, 4, 7].map(index => source.cluster(index).id);
  source.replace(0, 1, "");
  const layout = sdk.createTextLayout(source, { wrap: "word" });
  try {
    source.replace(0, 25, "changed");
    source.close();
    assert.throws(() => sdk.createTextLayout(source), stale);
    assert.equal(layout.source, text);
    assert.equal(Object.hasOwn(layout, "runtime"), false);
    assert.deepEqual(layout.size, { columns: 9, rows: 2 });
    assert.equal(layout.count, 7);
    assert.deepEqual(layout.cluster(0), { id: ids[0], begin: 0, end: 3, glyph: "e\u0301",
      position: { x: 0, y: 0 }, columns: 1, bidiLevel: 0 });
    assert.deepEqual(layout.cluster(3), { id: ids[1], begin: 17, end: 18, glyph: " ",
      position: { x: 5, y: 0 }, columns: 3, bidiLevel: 0 });
    assert.deepEqual(layout.cluster(6), { id: ids[2], begin: 21, end: 23, glyph: "א",
      position: { x: 8, y: 1 }, columns: 1, bidiLevel: 1 });
    assert.deepEqual(layout.hit({ x: 2, y: 0 }), { offset: 6, affinity: "trailing" });
    assert.deepEqual(layout.caretPosition({ offset: 21, affinity: "trailing" }), { x: 9, y: 1 });
    assert.throws(() => layout.caretPosition({ offset: 1, affinity: "leading" }), outOfRange);
    assert.throws(() => layout.hit({ x: -1, y: 0 }), outOfRange);
    assert.throws(() => layout.cluster(7), outOfRange);
  } finally {
    source.close();
    layout.close();
  }
  layout.close();
  assert.throws(() => layout.cluster(0), stale);
});

test("uses byte carets at wrap and ellipsis edges and checks layout ownership", async () => {
  const sdk = await loadNocturne(wasm);
  const other = await loadNocturne(wasm);
  const wrapped = sdk.layoutText("ab cd", { wrap: "word", bounds: { columns: { min: 0, max: 4 }, rows: { min: 0 } } });
  const clipped = sdk.layoutText("אבאב", { baseDirection: "rtl", ellipsis: true,
    bounds: { columns: { min: 3, max: 3 }, rows: { min: 0 } } });
  const zero = sdk.layoutText("a", { bounds: { columns: { min: 0, max: 0 }, rows: { min: 0 } } });
  const foreign = other.createText("abc");
  const guarded = sdk.createText("a");
  const prefix = new TextEncoder().encode("\uFEFF\0");
  const counted = sdk.layoutText(prefix);
  prefix.fill(0xff);
  try {
    assert.deepEqual(wrapped.caretPosition({ offset: 3, affinity: "leading" }), { x: 0, y: 1 });
    assert.deepEqual(wrapped.caretPosition({ offset: 3, affinity: "trailing" }), { x: 3, y: 0 });
    assert.deepEqual(clipped.cluster(0), { id: 0n, begin: 4, end: 4, glyph: "…",
      position: { x: 0, y: 0 }, columns: 1, bidiLevel: 1 });
    assert.deepEqual(clipped.hit({ x: 0, y: 0 }), { offset: 4, affinity: "leading" });
    assert.throws(() => zero.hit({ x: 0, y: 0 }), outOfRange);
    assert.throws(() => sdk.layoutText("a", { bounds: { columns: { min: 4, max: 2 }, rows: { min: 0 } } }), invalid);
    assert.throws(() => sdk.layoutText("a", { wrap: "zigzag" }), invalid);
    assert.throws(() => sdk.layoutText("a", { align: "constructor" }), invalid);
    assert.throws(() => sdk.layoutText("a", { baseDirection: "sideways" }), invalid);
    assert.throws(() => sdk.createTextLayout(foreign), invalid);
    assert.throws(() => sdk.createTextLayout(Object.create(Object.getPrototypeOf(foreign))), invalid);
    assert.throws(() => sdk.createTextLayout(guarded, { get tabStop() { guarded.close(); return 4; } }), stale);
    assert.equal(counted.source, "\uFEFF\0");
    assert.throws(() => counted.hit({ get x() { counted.close(); return 0; }, y: 0 }), stale);
  } finally {
    for (const resource of [wrapped, clipped, zero, foreign, guarded, counted]) resource.close();
  }
});
