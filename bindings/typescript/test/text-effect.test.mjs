import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { CallError, loadNocturne, TextGlyphEffectKind, TextVisibility } from "../dist/index.js";

const wasm = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));
const invalid = { nativeName: "NT_RESULT_INVALID_ARGUMENT" };
const stale = { nativeName: "NT_RESULT_STALE_REFERENCE" };
const wrong = { nativeName: "NT_RESULT_WRONG_PHASE" };
const point = (x = 0, y = 0) => ({ x, y });
const bounds = columns => ({ columns, rows: 1 });
const ink = { foreground: [240, 210, 140, 255], background: [0, 0, 0, 0] };
const delay = 1n << 60n, seed = (1n << 63n) + 61n;
const base = { color: ink, owner: seed + 1n };

test("uses an exact caller clock, width-matched glyphs and owned transform samples", async () => {
  const sdk = await loadNocturne(wasm);
  const layout = sdk.layoutText("e\u0301👩‍🚀");
  const frame = sdk.createOffscreenFrame(bounds(5));
  const surface = frame.surface();
  let calls = 0, saved;
  const effect = {
    delayUs: delay, durationUs: 100n, seed, visibility: TextVisibility.visible,
    glyphs: { kind: TextGlyphEffectKind.scramble, alphabet: ["*", "界"], intervalUs: 3n },
    trails: { count: 2, spacingUs: 1n, decay: .5 },
    transform(sample, pose) {
      calls++;
      saved = sample;
      assert.ok(sample.elapsedUs >= delay + 2n && sample.elapsedUs <= delay + 4n);
      assert.equal(sample.localUs, sample.elapsedUs - delay);
      assert.equal(sample.durationUs, 100n);
      assert.equal(sample.seed, seed);
      assert.equal(sample.identity, sample.cluster.id);
      assert.equal(sample.count, 2);
      assert.equal(sample.lineBegin, 0);
      assert.equal(sample.lineEnd, 3);
      assert.throws(() => frame.size, wrong);
      const glyph = sample.cluster.columns === 1 ? "+" : "層";
      assert.equal(pose.glyph, sample.cluster.columns === 1 ? "*" : "界");
      return sdk.textPoseMotion(sample, { ...pose, glyph: new TextEncoder().encode(glyph) }, {});
    },
  };
  try {
    surface.paintTextEffect(point(), layout, { base, elapsedUs: delay + 4n }, effect);
    assert.equal(calls, 6);
    const cells = Array.from({ length: 3 }, (_, x) => frame.cell(point(x)));
    assert.deepEqual(cells.map(cell => cell.glyph), ["+", "層", "層"]);
    assert.ok(cells.every(cell => cell.owner === base.owner));
    assert.equal(saved.cluster.glyph, "👩‍🚀");
    const pose = { glyph: saved.cluster.glyph, opacity: 1, offset: point(), hueDegrees: 0 };
    const noise = sdk.textPoseNoise(saved, pose, point(2, 2), 1n, true);
    assert.notDeepEqual(noise.offset, pose.offset);
    assert.deepEqual(sdk.textPoseNoise(saved, pose, point(2, 2), 1n, true), noise);
    surface.paintTextEffect(point(), layout, { base, motion: "reduced" }, effect);
    assert.equal(frame.cell(point(1)).glyph, "👩‍🚀");
    assert.equal(calls, 6);
  } finally {
    frame.close();
    layout.close();
  }
});

test("rolls back refused transforms and keeps admitted layouts alive through close", async () => {
  const sdk = await loadNocturne(wasm);
  const layout = sdk.layoutText("AB");
  const foreign = (await loadNocturne(wasm)).layoutText("AB");
  const frame = sdk.createOffscreenFrame(bounds(4));
  const surface = frame.surface();
  const options = { base, elapsedUs: 5n };
  const draw = transform => surface.paintTextEffect(point(), layout, options,
    { durationUs: 10n, visibility: TextVisibility.visible, transform });
  try {
    frame.setGlyph(point(), "X", 1, ink, 17n);
    const cells = () => Array.from({ length: 4 }, (_, x) => frame.cell(point(x)));
    const before = cells();
    assert.throws(() => draw(sample => sample.logicalIndex === 1 ? 2 : undefined), {
      nativeName: "NT_RESULT_OUT_OF_RANGE",
    });
    assert.throws(() => draw(sample => sample.logicalIndex === 1 ? -1 : undefined), { code: -1 });
    const cause = new Error("transform error");
    assert.throws(() => draw(sample => {
      if (sample.logicalIndex === 1) throw cause;
    }), error => error instanceof CallError && error.native?.nativeName === "NT_RESULT_CALLBACK_FAILED" &&
      error.callbacks[0].cause === cause);
    assert.throws(() => draw(async () => { throw cause; }), error => error instanceof CallError &&
      error.callbacks[0].cause instanceof TypeError);
    assert.throws(() => surface.paintTextEffect(point(), foreign, options, {}), invalid);
    assert.deepEqual(cells(), before);
    draw((sample, pose) => {
      if (sample.logicalIndex === 0) {
        layout.close();
        assert.throws(() => layout.size, stale);
        assert.throws(() => draw(() => undefined), wrong);
        assert.throws(() => frame.close(), wrong);
      } else {
        assert.equal(pose.glyph, "B");
      }
      return pose;
    });
    assert.equal(frame.cell(point(1)).glyph, "B");
    assert.throws(() => draw(() => undefined), stale);
  } finally {
    frame.close();
    foreign.close();
    layout.close();
  }
});

test("paints effects in mounted clips and rejects reentrant drawing through the context", async () => {
  const sdk = await loadNocturne(wasm);
  const layout = sdk.layoutText("AB");
  const tree = sdk.createTree();
  let escaped;
  try {
    tree.mountComponent({ paint(paint) {
      escaped = paint;
      paint.paintTextEffect(point(), layout, { base, elapsedUs: 5n }, {
        durationUs: 10n, visibility: TextVisibility.visible,
        transform(_sample, pose) {
          assert.throws(() => paint.glyph(point(), "X", 1, ink), wrong);
          assert.throws(() => tree.close(), wrong);
          return pose;
        },
      });
    } });
    tree.publish(bounds(1));
    const cell = tree.frame.cell(point());
    assert.equal(cell.glyph, "A");
    assert.notEqual(cell.owner, 0n);
    assert.notEqual(cell.owner, base.owner);
    assert.throws(() => escaped.paintTextEffect(point(), layout, { base }, {}), stale);
  } finally {
    tree.close();
    layout.close();
  }
});
