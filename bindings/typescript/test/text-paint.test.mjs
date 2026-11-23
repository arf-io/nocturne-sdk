import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { ColorSpace, loadNocturne, PaintAddress, PaintRotation, PaletteFilter } from "../dist/index.js";

const wasm = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));
const invalid = { nativeName: "NT_RESULT_INVALID_ARGUMENT" };
const outOfRange = { nativeName: "NT_RESULT_OUT_OF_RANGE" };
const stale = { nativeName: "NT_RESULT_STALE_REFERENCE" };
const wrongPhase = { nativeName: "NT_RESULT_WRONG_PHASE" };
const at = (x, y = 0) => ({ x, y });
const black = [0, 0, 0, 255];
const white = [255, 255, 255, 255];
const red = [255, 0, 0, 255];
const fg = [240, 220, 160, 255];
const bg = [60, 40, 20, 255];
const ink = { foreground: fg, background: [0, 0, 0, 0] };

test("samples typed colors, hard gradient stops, palettes and caller-timed fields", async () => {
  const sdk = await loadNocturne(wasm);
  const a = sdk.colorFromRGBA(black), b = sdk.colorFromRGBA(white), c = sdk.colorFromRGBA(red);
  assert.deepEqual(sdk.colorToRGBA(sdk.mixColor(a, b, .5, { space: ColorSpace.linearSrgb })), [188, 188, 188, 255]);
  assert.deepEqual(sdk.convertColor(a, ColorSpace.oklab), { space: ColorSpace.oklab, channels: [0, 0, 0, 0], alpha: 1 });
  const palette = [black, white, red, red];
  assert.deepEqual(sdk.lookupPalette(palette, 2), red);
  assert.equal(sdk.nearestPalette(palette, c), 2);
  assert.throws(() => sdk.lookupPalette(palette, 4), outOfRange);
  assert.equal(sdk.samplePalette({ palette: palette.slice(0, 2), address: PaintAddress.repeat }, .75).channels[0], .5);
  const gradient = { address: PaintAddress.reflect, stops: [
    { position: 0, color: a }, { position: .5, color: b }, { position: .5, color: c }, { position: 1, color: b },
  ] };
  assert.deepEqual(sdk.sampleGradient(gradient, 1.5), c);
  assert.equal(sdk.paintCoordinate({ scale: at(4, 4), rotation: PaintRotation.spin, rotationPeriodUs: 16n },
    { position: at(0, 4), elapsedUs: 4n }), 1);

  const descriptor = { kind: "gradient", gradient: { stops: [{ position: 0, color: a }, { position: 1, color: b }] },
    field: { scale: at(4, 1), phaseTrack: { from: 0, to: .5, durationUs: 8n } },
    opacity: .8, opacityTrack: { keys: [{ atUs: 0n, value: .5 }, { atUs: 8n, value: 1 }] } };
  const sample = { position: at(2, .5), elapsedUs: 4n };
  assert.equal(sdk.samplePaint(descriptor, sample).channels[0], .75);
  assert.ok(Math.abs(sdk.samplePaint(descriptor, sample).alpha - .6) < 1e-15);
  assert.deepEqual(sdk.samplePaintRGBA(descriptor, sample), [191, 191, 191, 153]);
  assert.deepEqual(sdk.samplePaintRGBA(descriptor, { ...sample, motion: "reduced" }), [128, 128, 128, 102]);
  assert.throws(() => sdk.samplePaint({ ...descriptor, field: { scale: at(0, 1) } }, sample), invalid);
  assert.throws(() => sdk.samplePaint(descriptor, { ...sample, motion: "constructor" }), invalid);
});

test("paints styled text spans, samples local colors, and rolls back late failures", async () => {
  const sdk = await loadNocturne(wasm);
  const layout = sdk.layoutText("e\u0301\t界 Q");
  const foreign = (await loadNocturne(wasm)).layoutText("a");
  const frame = sdk.createOffscreenFrame({ columns: 12, rows: 3 });
  const root = frame.surface();
  const base = { color: ink, owner: 7n };
  const owner = (1n << 63n) + 19n;
  const solid = { kind: "solid", color: sdk.colorFromRGBA(red), opacity: 1 };
  const backdrop = { kind: "palette", palette: { palette: [bg, red], filter: PaletteFilter.step },
    field: { scale: at(10, 1) }, opacity: 1 };
  const options = { base, spans: [
    { begin: 0, end: 3, style: base },
    { begin: 0, end: 1, style: { color: ink, owner, foreground: solid } },
    { begin: 1, end: 3, style: base },
    { begin: 3, end: 4, style: { color: { foreground: fg, background: bg }, owner: 21n } },
    { begin: 4, end: 7, style: { color: ink, owner: 22n, background: backdrop } },
  ] };
  try {
    root.paintText(at(0), layout, options);
    const first = frame.cell(at(0));
    assert.equal(first.glyph, "e\u0301");
    assert.equal(first.owner, owner);
    assert.deepEqual(first.style.foreground, red);
    for (let x = 1; x < 4; x++) {
      const cell = frame.cell(at(x));
      assert.equal(cell.glyph, " ");
      assert.equal(cell.owner, 21n);
      assert.deepEqual(cell.style.background, bg);
    }
    root.paintText(at(1, 1), layout, options);
    for (const point of [at(5), at(6, 1)]) {
      const cell = frame.cell(point);
      assert.equal(cell.glyph, "界");
      assert.deepEqual(cell.lead, at(point.x - 1, point.y));
      assert.equal(cell.owner, 22n);
      assert.deepEqual(cell.style.background, bg);
    }
    const child = root.child({ origin: at(5, 2), size: { columns: 1, rows: 1 } });
    child.paintText(at(-5), layout, options);
    assert.equal(frame.cell(at(5, 2)).glyph, "");
    assert.deepEqual(frame.background(at(5, 2)), { color: bg, owner: 22n });
    const cells = () => Array.from({ length: 12 }, (_, x) => frame.cell(at(x)));
    const before = cells();
    const late = { begin: 8, end: 9, style: { color: ink, foreground: { ...solid, opacity: 2 } } };
    assert.throws(() => root.paintText(at(1), layout, { ...options, spans: [...options.spans, late] }), invalid);
    assert.throws(() => root.paintText(at(0), layout, { ...options, spans: [{ ...late, end: 100 }] }), outOfRange);
    assert.throws(() => root.paintText(at(0), layout, { ...options, motion: "unsupported" }), invalid);
    assert.throws(() => root.paintText(at(0), foreign, options), invalid);
    assert.throws(() => root.paintText(at(0), Object.create(Object.getPrototypeOf(layout)), options), invalid);
    assert.throws(() => root.paintText(at(0), layout, { get base() { layout.close(); return base; } }), stale);
    assert.deepEqual(cells(), before);
  } finally {
    frame.close();
    layout.close();
    foreign.close();
  }
});

test("paint callbacks draw styled text with mounted ownership and a reusable candidate", async () => {
  const sdk = await loadNocturne(wasm);
  const layout = sdk.layoutText("AB");
  const tree = sdk.createTree();
  const external = (1n << 63n) + 25n;
  const base = { color: ink, owner: external };
  const bad = { kind: "solid", opacity: 2, color: sdk.colorFromRGBA(red) };
  let escaped;
  try {
    tree.mountComponent({ paint(paint) {
      escaped = paint;
      paint.glyph(at(0), "X", 1, ink);
      assert.throws(() => paint.paintText(at(0), layout, {
        base, spans: [{ begin: 1, end: 2, style: { color: ink, foreground: bad } }],
      }), invalid);
      paint.paintText(at(1), layout, { get base() {
        assert.throws(() => paint.glyph(at(0), "Y", 1, ink), wrongPhase);
        return base;
      } });
    } });
    tree.publish({ columns: 3, rows: 1 });
    const cells = [0, 1, 2].map(x => tree.frame.cell(at(x)));
    assert.deepEqual(cells.map(cell => cell.glyph), ["X", "A", "B"]);
    for (const cell of cells) {
      assert.notEqual(cell.owner, 0n);
      assert.notEqual(cell.owner, external);
    }
    assert.throws(() => escaped.paintText(at(0), layout, { base }), stale);
  } finally {
    tree.close();
    layout.close();
  }
});
