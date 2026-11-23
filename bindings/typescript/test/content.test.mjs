import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { ImageFilter, loadNocturne } from "../dist/index.js";

const wasm = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));

test("authored cell content and stride-aware images produce owned frames", async () => {
  const sdk = await loadNocturne(wasm);
  const owner = (1n << 64n) - 1n;
  const color = [255, 10, 20, 255], background = [3, 4, 5, 255], blank = [0, 0, 0, 0];
  const origin = { x: 0, y: 0 }, second = { x: 1, y: 0 };
  const frame = sdk.frameFromCells({ columns: 2, rows: 1 }, [
    { position: origin, glyph: "界", columns: 2, style: { foreground: color, background: blank }, owner },
    { position: second, columns: 0, style: { foreground: blank, background }, owner: 12n },
  ]);
  try {
    const cell = frame.cell(second);
    assert.equal(cell.glyph, "界");
    assert.equal(cell.owner, owner);
    assert.deepEqual(cell.lead, origin);
    assert.deepEqual(frame.background(second), { color: background, owner: 12n });
  } finally { frame.close(); }
  assert.throws(() => sdk.frameFromCells({ columns: 1, rows: 1 }, [
    { position: origin, columns: 0, glyph: "X", style: { foreground: blank, background } },
  ]), error => error.code === 1);
  const image = { pixels: [color, blank, [255, 255, 0, 255], color, blank],
    size: { columns: 2, rows: 2 }, stride: 3 };
  assert.deepEqual(sdk.sampleImage(image, .5, .5, { filter: ImageFilter.bilinear }), [255, 10, 20, 128]);
  assert.throws(() => sdk.sampleImage({ ...image, pixels: image.pixels.slice(0, 4) }, .5, .5), error => error.code === 4);
  const raster = sdk.rasterizeImage({ pixels: [color], size: { columns: 1, rows: 1 }, stride: 1 },
    { columns: 3, rows: 5 }, { owner });
  try {
    assert.deepEqual(raster.size, { columns: 2, rows: 2 });
    assert.deepEqual(raster.background(origin), { color, owner });
  } finally { raster.close(); }
});
