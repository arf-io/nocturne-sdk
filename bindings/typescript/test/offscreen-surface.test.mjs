import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { CallError, loadNocturne } from "../dist/index.js";

const source = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));
const at = x => ({ x, y: 0 });
const size = columns => ({ columns, rows: 1 });
const rect = (x, columns) => ({ origin: at(x), size: size(columns) });
const ink = { foreground: [240, 210, 140, 255], background: [0, 0, 0, 0] };
const blue = [0, 0, 255, 255];
const owner = (1n << 63n) + 17n;
const stale = { nativeName: "NT_RESULT_STALE_REFERENCE" };
const wrong = { nativeName: "NT_RESULT_WRONG_PHASE" };
const pose = { anchor: at(0), offset: at(0), scale: { x: 1, y: 1 }, turns: 0, opacity: 1 };

test("preserves captured clips, mask bytes and physical backgrounds over frame edits", async () => {
  const sdk = await loadNocturne(source);
  const frame = sdk.createOffscreenFrame(size(4));
  try {
    const root = frame.surface();
    root.fill(rect(0, 2), blue, 55n);
    root.glyph(at(0), "界", 2, ink, owner);
    const alpha = Uint8Array.of(255, 0, 128);
    const child = root.child(rect(1, 3), { kind: "alpha", bounds: rect(0, 3), stride: 3, alpha });
    alpha[2] = 0;
    assert.equal(child.coverage(at(2)), 128 / 255);
    assert.equal(child.coverage(at(1)), 0);
    child.eraseGlyph(at(0));
    assert.equal(frame.cell(at(1)).glyph, "界");
    assert.equal(frame.cell(at(1)).owner, owner);
    assert.deepEqual(frame.background(at(1)), { color: blue, owner: 55n });
    child.glyph(at(2), "P", 1, ink, 74n);
    assert.equal(frame.cell(at(3)).glyph, "P");
    assert.equal(frame.cell(at(3)).style.foreground[3], 128);
    assert.equal(frame.cell(at(3)).owner, 74n);

    assert.throws(() => frame.setGlyph(at(2), "x", 257, ink), { nativeName: "NT_RESULT_INVALID_ARGUMENT" });
    frame.resize(size(1));
    assert.equal(frame.cell(at(0)).glyph, "");
    assert.deepEqual(frame.background(at(0)), { color: blue, owner: 55n });
    frame.resize(size(8));
    assert.deepEqual(child.clip, rect(1, 3));
    assert.deepEqual(frame.surface().clip, rect(0, 8));
    frame.setGlyph(at(4), "界", 2, ink, owner);
    frame.clearGlyph(at(5));
    assert.equal(frame.cell(at(4)).glyph, "");
    frame.close();
    assert.throws(() => child.clip, stale);
    assert.throws(() => frame.size, stale);
  } finally {
    frame.close();
  }
});

test("samples aliases before the batch, maps owners and restores cells after callback failure", async () => {
  const sdk = await loadNocturne(source);
  const frame = sdk.createOffscreenFrame(size(6));
  const external = sdk.createOffscreenFrame(size(2));
  const target = sdk.createTree();
  const foreign = (await loadNocturne(source)).createOffscreenFrame(size(2));
  try {
    for (const [i, glyph] of ["A", "B", "C", "D"].entries()) frame.setGlyph(at(i), glyph, 1, ink, owner + BigInt(i));
    const root = frame.surface();
    root.compose([
      { source: frame, region: rect(0, 2), position: at(2) },
      { source: frame, region: rect(2, 2), position: at(4), owners: { map(token) {
        assert.throws(() => frame.size, wrong);
        return token + 100n;
      } } },
    ]);
    const cells = () => Array.from({ length: 6 }, (_, x) => frame.cell(at(x)));
    const before = cells();
    assert.deepEqual(before.map(cell => cell.glyph), ["A", "B", "A", "B", "C", "D"]);
    assert.equal(before[2].owner, 0n);
    assert.equal(before[4].owner, owner + 102n);
    assert.equal(before[5].owner, owner + 103n);

    external.setGlyph(at(0), "X", 1, ink, owner);
    external.setGlyph(at(1), "Y", 1, ink, owner + 1n);
    const fail = new Error("owner rejected");
    const layer = { source: external, region: rect(0, 2), position: at(0), owners: "shared" };
    assert.throws(() => root.compose([{ ...layer, position: at(2) }, { ...layer, owners: { map(token) {
      assert.throws(() => external.close(), wrong);
      if (token === owner + 1n) throw fail;
      return token;
    } } }]), error => error instanceof CallError &&
      error.native?.nativeName === "NT_RESULT_CALLBACK_FAILED" && error.callbacks[0].cause === fail);
    assert.deepEqual(cells(), before);
    assert.deepEqual(external.size, size(2));
    assert.throws(() => root.compose([{ ...layer, source: foreign }]), { nativeName: "NT_RESULT_INVALID_ARGUMENT" });
    assert.deepEqual(cells(), before);
    assert.throws(() => root.compose([{ ...layer, owners: { async map() { throw fail; } } }]), error =>
      error instanceof CallError && error.callbacks[0].cause instanceof TypeError);
    assert.deepEqual(cells(), before);

    root.projectPanels([{ source: external, region: rect(0, 2), viewport: rect(0, 2), pose,
      remap(sample) { return { source: { x: 2 - sample.source.x, y: sample.source.y }, opacity: 1 }; },
      owners: { map(token) { return token + 1000n; } },
    }]);
    assert.equal(frame.cell(at(0)).glyph, "Y");
    assert.equal(frame.cell(at(0)).owner, owner + 1001n);
    target.mountComponent({ paint(paint) { paint.projectPanels([
      { source: external, region: rect(0, 2), viewport: rect(0, 2), pose },
    ]); } });
    target.publish(size(2));
    assert.equal(target.frame.cell(at(0)).glyph, "X");
  } finally {
    target.close();
    foreign.close();
    external.close();
    frame.close();
  }
});
