import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { CallError, loadNocturne } from "../dist/index.js";

const source = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));
const size = { columns: 4, rows: 1 };
const at = (x, y = 0) => ({ x, y });
const rect = (x, columns) => ({ origin: at(x), size: { columns, rows: 1 } });
const region = rect(0, 4);
const ink = { foreground: [240, 210, 140, 255], background: [0, 0, 0, 0] };
const identity = { anchor: at(0), offset: at(0), scale: at(1, 1), turns: 0, opacity: 1 };
const wrongPhase = { nativeName: "NT_RESULT_WRONG_PHASE" };
const stale = { nativeName: "NT_RESULT_STALE_REFERENCE" };

function publishSource(sdk) {
  const tree = sdk.createTree();
  tree.mountComponent({ paint(paint) {
    paint.glyph(at(0), "界", 2, ink);
    paint.glyph(at(2), "B", 1, ink);
    paint.glyph(at(3), "C", 1, ink);
  } });
  tree.publish(size);
  return tree;
}

test("projects masked and remapped frame layers with borrowed source lifetimes", async () => {
  const sdk = await loadNocturne(source);
  const upstream = publishSource(sdk);
  const target = sdk.createTree();
  const frame = upstream.frame;
  const otherHandle = upstream.frame;
  const clock = (1n << 63n) + 9n;
  const samples = [];
  let escaped;
  try {
    target.mountComponent({ paint(paint) {
      escaped = paint;
      paint.projectPanels([
        { source: frame, region, viewport: region, pose: identity,
          mask: { kind: "alpha", bounds: region, alpha: Uint8Array.of(255, 255, 0, 255), stride: 4 } },
        { source: otherHandle, region, viewport: rect(4, 4), pose: identity, elapsedUs: clock, remap(sample) {
          samples.push(sample);
          assert.throws(() => paint.fill(region, ink.background), wrongPhase);
          assert.throws(() => paint.projectPanels([]), wrongPhase);
          assert.throws(() => upstream.close(), wrongPhase);
          return { source: at(4 - sample.source.x, sample.source.y), opacity: 1 };
        } },
      ]);
      assert.deepEqual(frame.size, size);
    } });
    target.publish({ columns: 8, rows: 1 });
    assert.equal(samples.length, 4);
    for (const [i, sample] of samples.entries()) {
      assert.deepEqual(sample, { destination: at(i + .5, .5), source: at(i + .5, .5),
        viewportSize: size, regionSize: size, elapsedUs: clock });
    }
    const output = target.frame;
    for (const [x, glyph] of [[0, "界"], [2, ""], [3, "C"], [4, "C"], [5, "B"], [6, "界"]]) {
      assert.equal(output.cell(at(x)).glyph, glyph);
    }
    for (const x of [1, 7]) {
      assert.deepEqual(output.cell(at(x)).lead, at(x - 1));
      assert.notEqual(output.cell(at(x)).owner, 0n);
    }
    assert.throws(() => escaped.projectPanels([]), stale);
  } finally {
    target.close();
    upstream.close();
  }
});

test("projects the prior same-tree frame and rolls back refused remaps", async () => {
  const sdk = await loadNocturne(source);
  const upstream = publishSource(sdk);
  const unrelated = publishSource(await loadNocturne(source));
  const target = sdk.createTree();
  const frame = upstream.frame;
  const cause = new Error("remap refused");
  const layer = { source: frame, region, viewport: region, pose: identity };
  const invalid = { nativeName: "NT_RESULT_INVALID_ARGUMENT" };
  let phase = 0;
  let prior;
  try {
    const node = target.mountComponent({ paint(paint) {
      paint.glyph(at(0), "Q", 1, ink);
      if (phase === 1) {
        paint.projectPanels([{ ...layer, source: prior, region: rect(0, 1), viewport: rect(1, 1) }]);
      } else if (phase !== 0) {
        const remap = phase === 2 ? sample => {
          if (sample.destination.x >= 2) throw cause;
          return { source: sample.source, opacity: 1 };
        } : async () => { throw cause; };
        assert.throws(() => paint.projectPanels([layer, { ...layer, remap }]), error =>
          error instanceof CallError && error.native?.nativeName === "NT_RESULT_CALLBACK_FAILED" &&
          (phase === 2 ? error.callbacks[0].cause === cause : error.callbacks[0].cause instanceof TypeError));
        assert.deepEqual(frame.size, size);
        assert.throws(() => paint.projectPanels([{ ...layer, source: prior }]), stale);
        assert.throws(() => paint.projectPanels([{ ...layer, source: unrelated.frame }]), invalid);
        assert.throws(() => paint.projectPanels([{ ...layer, source: { size, cell() {} } }]), invalid);
        paint.glyph(at(3), "Z", 1, ink);
      }
    } });
    target.publish(size);
    prior = target.frame;
    for (phase = 1; phase <= 3; ++phase) {
      target.invalidate(node, "paint");
      target.publish(size);
      const output = target.frame;
      assert.equal(output.cell(at(0)).glyph, "Q");
      assert.equal(output.cell(at(1)).glyph, phase === 1 ? "Q" : "");
      if (phase !== 1) assert.equal(output.cell(at(3)).glyph, "Z");
    }
  } finally {
    target.close();
    unrelated.close();
    upstream.close();
  }
});
