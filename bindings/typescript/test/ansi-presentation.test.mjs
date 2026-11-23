import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { loadNocturne } from "../dist/index.js";

const wasm = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));

test("presents an owned frame through native ANSI capabilities and stops on sink failure", async () => {
  const sdk = await loadNocturne(wasm);
  const style = { foreground: [255, 255, 255, 255], background: [0, 0, 0, 0] };
  const frame = sdk.frameFromCells({ columns: 4, rows: 1 }, [
    { position: { x: 0, y: 0 }, glyph: "界", columns: 2, style },
    { position: { x: 2, y: 0 }, glyph: "A", columns: 1, style },
  ]);
  try {
    let chunks = [];
    sdk.writeAnsi(frame, (1n << 64n) - 1n, { capabilities: { color: "truecolor" } },
      bytes => { chunks.push(bytes); });
    assert.equal(Buffer.concat(chunks).toString(), "??A \n");

    const palette = sdk.xterm256Palette();
    assert.equal(palette.length, 256);
    assert.deepEqual(palette[231], [255, 255, 255, 255]);
    const options = { capabilities: { color: "indexed", interactive: true, utf8: true }, palette };
    chunks = [];
    sdk.writeAnsi(frame, 50n, options, bytes => { chunks.push(bytes); });
    const output = Buffer.concat(chunks).toString();
    assert.ok(output.startsWith("\x1b[0m\x1b[2J"));
    assert.ok(output.includes("界"));
    assert.ok(output.endsWith("\x1b[0m"));

    const cause = new Error("cannot write frame");
    let calls = 0;
    assert.throws(() => sdk.writeAnsi(frame, 50n, options, () => {
      calls++;
      assert.throws(() => frame.size, error => error.code === 6);
      throw cause;
    }), error => error === cause);
    assert.equal(calls, 1);
    assert.throws(() => sdk.writeAnsi(frame, 50n, options, async () => {}), TypeError);
  } finally {
    frame.close();
  }
});

test("borrows only a live frame from the same SDK instance", async () => {
  const sdk = await loadNocturne(wasm);
  const other = await loadNocturne(wasm);
  const tree = sdk.createTree();
  const size = { columns: 1, rows: 1 };
  const options = { capabilities: {} };
  try {
    const label = tree.mountLabel("A");
    tree.publish(size);
    const prior = tree.frame;
    tree.setLabel(label, "B");
    tree.publish(size);
    let calls = 0;
    assert.throws(() => sdk.writeAnsi(prior, 0n, options, () => { calls++; }), error => error.code === 5);
    assert.throws(() => other.writeAnsi(tree.frame, 0n, options, () => { calls++; }), error => error.code === 1);
    assert.equal(calls, 0);
    const output = [];
    sdk.writeAnsi(tree.frame, 0n, options, bytes => { output.push(bytes); });
    assert.equal(Buffer.concat(output).toString(), "B\n");
  } finally {
    tree.close();
  }
});
