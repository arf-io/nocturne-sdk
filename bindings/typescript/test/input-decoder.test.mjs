import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { EventKind, loadNocturne } from "../dist/index.js";

const wasm = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));
const encode = value => new TextEncoder().encode(value);

test("decodes exact timestamps, a fragmented paste and a caller-owned cursor report", async () => {
  const sdk = await loadNocturne(wasm);
  const decoder = sdk.createInputDecoder();
  const start = (1n << 63n) + 3n;
  try {
    assert.deepEqual(decoder.read(encode("\x1b"), start), { status: "pending", consumed: 1 });
    assert.equal(decoder.deadline(), start + 25000n);
    const time = start + 25000n;
    const first = decoder.read(encode("A"), time);
    assert.equal(first.status, "ready");
    assert.equal(first.consumed, 0);
    assert.equal(first.at, start);
    assert.equal(first.input.key.code, 0x110000);
    const key = decoder.read(encode("A"), time);
    assert.equal(key.consumed, 1);
    assert.deepEqual(key.input, { kind: EventKind.key, key: { code: 97, modifiers: 1, state: 0 }, text: "A" });
    assert.deepEqual(decoder.read(encode("\x1b[200~café\0"), time + 1n), { status: "pending", consumed: 12 });
    const paste = decoder.read(encode("\n\x1b[201~X"), time + 2n);
    assert.equal(paste.consumed, 7);
    assert.deepEqual(paste.input, { kind: EventKind.paste, text: "café\0\n" });
    decoder.expectCursorReport(true);
    assert.deepEqual(decoder.read(encode("\x1b[1;2R"), time + 3n), { status: "discarded", consumed: 6 });
    const cancel = decoder.cancel(time + 4n, 0);
    assert.deepEqual(cancel, { kind: EventKind.hostCancel, reason: 0 });
    assert.equal(paste.input.text, "café\0\n");
    assert.equal(key.input.text, "A");
  } finally {
    decoder.close();
  }
  assert.throws(() => decoder.read(new Uint8Array(), start + 25030n), error => error.code === 5);
});

test("selects the Kitty protocol and rejects time reversal without input loss", async () => {
  const sdk = await loadNocturne(wasm);
  const decoder = sdk.createInputDecoder({ keyboard: "kitty", pasteLimit: 0 });
  try {
    const release = decoder.read(encode("\x1b[97;1:3u"), 5n);
    assert.equal(release.status, "ready");
    assert.deepEqual(release.input, { kind: EventKind.key, key: { code: 97, modifiers: 0, state: 2 } });
    assert.deepEqual(decoder.read(encode("\x1b[200~X\x1b[201~"), 6n), { status: "discarded", consumed: 13 });
    assert.throws(() => decoder.read(encode("A"), 5n), error => error.code === 1);
    const words = new Uint16Array([0x161]);
    Object.defineProperty(words, Symbol.toStringTag, { value: "Uint8Array" });
    assert.throws(() => decoder.read(words, 8n), error => error.code === 1);
    assert.throws(() => decoder.read("A", 8n), error => error.code === 1);
    const key = decoder.read(Buffer.from("A"), 7n);
    assert.equal(key.status, "ready");
    assert.equal(key.consumed, 1);
    assert.equal(key.input.text, "A");
  } finally {
    decoder.close();
  }
});

test("decodes only the bytes inside the supplied typed array view", async () => {
  const sdk = await loadNocturne(wasm);
  const decoder = sdk.createInputDecoder();
  const bytes = Uint8Array.of(0x41, 0x1b, 0x42).subarray(1, 2);
  for (const name of ["length", "byteOffset", "buffer"]) {
    Object.defineProperty(bytes, name, { get() { throw new Error(name); } });
  }
  try {
    assert.deepEqual(decoder.read(bytes, 0n), { status: "pending", consumed: 1 });
    assert.equal(decoder.deadline(), 25000n);
  } finally {
    decoder.close();
  }
});

test("tracks pointer releases and completes a fragmented UTF-8 key", async () => {
  const sdk = await loadNocturne(wasm);
  const decoder = sdk.createInputDecoder();
  const at = { x: 2, y: 4 };
  try {
    assert.deepEqual(decoder.read(encode("\x1b[<0;3;5M"), 1n).input,
      { kind: EventKind.pointerDown, pointer: 0n, at, button: 1, pressed: 1, modifiers: 0 });
    assert.deepEqual(decoder.read(encode("\x1b[<0;3;5m"), 2n).input,
      { kind: EventKind.pointerUp, pointer: 0n, at, button: 1, pressed: 0, modifiers: 0 });
    assert.deepEqual(decoder.read(encode("\x1b[<65;3;5M"), 3n).input,
      { kind: EventKind.wheel, at, dx: 0, dy: 1, modifiers: 0 });
    assert.deepEqual(decoder.read(Uint8Array.of(0xc3), 4n), { status: "pending", consumed: 1 });
    const key = decoder.read(Uint8Array.of(0xa9), 5n);
    assert.equal(key.at, 5n);
    assert.deepEqual(key.input, { kind: EventKind.key, key: { code: 233, modifiers: 0, state: 0 }, text: "é" });
    decoder.close();
    assert.equal(key.input.text, "é");
  } finally {
    decoder.close();
  }
});
