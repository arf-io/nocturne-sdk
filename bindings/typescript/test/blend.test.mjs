import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { BlendMode, ColorSpace, CompositeOperator, loadNocturne } from "../dist/index.js";

const wasm = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));

test("parses canonical blend and composite modes and preserves alpha semantics", async () => {
  const sdk = await loadNocturne(wasm);
  assert.equal(sdk.parseBlendMode("color-dodge"), BlendMode.colorDodge);
  assert.equal(sdk.parseBlendMode("none"), BlendMode.normal);
  assert.equal(sdk.parseCompositeOperator("destination-atop"), CompositeOperator.destinationAtop);
  assert.throws(() => sdk.parseBlendMode("multiply\0other"), { code: 1 });
  assert.throws(() => sdk.parseCompositeOperator("source-under"), { code: 7 });
  const black = [0, 0, 0, 255], white = [255, 255, 255, 128];
  assert.deepEqual(sdk.compositeRGBA(black, white), [188, 188, 188, 255]);
  assert.deepEqual(sdk.compositeRGBA(black, white, { space: ColorSpace.srgb }), [128, 128, 128, 255]);
  assert.deepEqual(sdk.compositeRGBA(black, white, { space: ColorSpace.oklab }), [100, 100, 100, 255]);
  const backdrop = { space: ColorSpace.srgb, channels: [0, 0, 1, 0], alpha: .4 };
  const source = { space: ColorSpace.srgb, channels: [1, 0, 0, 0], alpha: .6 };
  const blend = sdk.compositeColor(backdrop, source, { operation: CompositeOperator.destinationAtop, space: ColorSpace.srgb });
  assert.ok(Math.abs(blend.alpha - .6) < 2e-15);
  assert.ok(Math.abs(blend.channels[0] - .6) < 2e-15);
  assert.ok(Math.abs(blend.channels[2] - .4) < 2e-15);
  const dissolve = { blend: BlendMode.dissolve, seed: (1n << 64n) - 1n, sampleIndex: (1n << 63n) + 17n };
  const selected = sdk.compositeRGBA(black, white, dissolve);
  assert.ok(selected[0] === 0 || selected[0] === 255);
  assert.deepEqual(sdk.compositeRGBA(black, white, dissolve), selected);
  assert.throws(() => sdk.compositeRGBA(black, white, { ...dissolve, sampleIndex: 1n << 64n }), RangeError);
});
