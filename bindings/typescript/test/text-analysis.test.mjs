import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { BidiRemovedLevel, loadNocturne } from "../dist/index.js";

const source = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));
const invalid = { nativeName: "NT_RESULT_INVALID_ARGUMENT" };

test("reports Unicode line breaks and grapheme-safe scripts by byte offsets", async () => {
  const sdk = await loadNocturne(source);
  assert.equal(sdk.unicodeVersion, "17.0.0");
  assert.deepEqual(sdk.lineBreaks("A B\r\nC\u2028D"), [
    { offset: 2, mandatory: false }, { offset: 5, mandatory: true },
    { offset: 9, mandatory: true }, { offset: 10, mandatory: true },
  ]);
  assert.deepEqual(sdk.lineBreaks(" \u0301"), [{ offset: 1, mandatory: false }, { offset: 3, mandatory: true }]);
  assert.deepEqual(sdk.lineBreaks("\0"), [{ offset: 1, mandatory: true }]);
  assert.deepEqual(sdk.lineBreaks(""), [{ offset: 0, mandatory: true }]);
  assert.deepEqual(sdk.scriptRuns(".a\u0301 あーカ"), [
    { begin: 0, end: 5, script: "Latn" }, { begin: 5, end: 11, script: "Hira" }, { begin: 11, end: 14, script: "Kana" },
  ]);
  assert.deepEqual(sdk.scriptRuns(""), []);
  for (const malformed of [Uint8Array.of(0xff), "\uD800"]) {
    assert.throws(() => sdk.lineBreaks(malformed), invalid);
    assert.throws(() => sdk.scriptRuns(malformed), invalid);
    assert.throws(() => sdk.createBidi(malformed), invalid);
  }
});

test("reuses owned bidi paragraphs, preserves offsets, and applies mirroring", async () => {
  const sdk = await loadNocturne(source);
  const input = new TextEncoder().encode("(אב)\u202b\u202c\nb");
  const bidi = sdk.createBidi(input);
  input.fill(0xff);
  try {
    assert.equal(bidi.count, 8);
    assert.deepEqual(bidi.scalar(4), { begin: 6, end: 9, scalar: 0x202b, level: BidiRemovedLevel,
      paragraphLevel: 1, paragraphSeparator: false });
    assert.equal(bidi.scalar(6).paragraphSeparator, true);
    assert.equal(bidi.scalar(6).paragraphLevel, 1);
    assert.deepEqual(bidi.reorderLine(0, 12), [
      { logicalIndex: 3, begin: 5, end: 6, displayScalar: 0x28, level: 1 },
      { logicalIndex: 2, begin: 3, end: 5, displayScalar: 0x5d1, level: 1 },
      { logicalIndex: 1, begin: 1, end: 3, displayScalar: 0x5d0, level: 1 },
      { logicalIndex: 0, begin: 0, end: 1, displayScalar: 0x29, level: 1 },
    ]);
    assert.deepEqual(bidi.reorderLine(6, 12), []);
    assert.throws(() => bidi.reorderLine(2, 6), invalid);
    assert.throws(() => bidi.reorderLine(0, 14), invalid);
    assert.throws(() => bidi.reorderLine(6, 0), { nativeName: "NT_RESULT_OUT_OF_RANGE" });
    assert.throws(() => bidi.reorderLine(0, 15), { nativeName: "NT_RESULT_OUT_OF_RANGE" });
    assert.throws(() => bidi.reorderLine(2 ** 32, 2 ** 32), RangeError);
    assert.throws(() => sdk.createBidi("", "sideways"), invalid);
  } finally {
    bidi.close();
  }
  bidi.close();
  assert.throws(() => bidi.scalar(0), { nativeName: "NT_RESULT_STALE_REFERENCE" });
});
