import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { Easing, OpticalFalloff, PaletteFilter, loadNocturne } from "../dist/index.js";

const wasm = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));
const at = (x = 0, y = 0) => ({ x, y });
const max = (1n << 64n) - 1n;

test("samples optical ripple color and schedules exact clock boundaries", async () => {
  const sdk = await loadNocturne(wasm);
  assert.equal(sdk.opticalFalloff(OpticalFalloff.inverseQuadratic, 2 ** 530), 2 ** -1060);
  assert.throws(() => sdk.opticalFalloff(OpticalFalloff.linear, -Number.MIN_VALUE), { code: 1 });
  const ripple = { ...sdk.defaultRippleField(), origin: at(10, 10), aspect: at(2, 1),
    startUs: 100n, lifetimeUs: 100n, spacingUs: 25n, rings: 3, lastRadius: 8,
    falloff: OpticalFalloff.linear, fade: Easing.linear };
  const query = { position: at(12, 10), now: 150n };
  assert.deepEqual(sdk.sampleField(ripple, query), { value: .5, coverage: .5 });
  assert.deepEqual(sdk.fieldColor(ripple, { color: [200, 0, 0, 255], opacity: 1 }, query), [200, 0, 0, 128]);
  assert.equal(sdk.fieldDeadline(ripple, 150n, 19n), 169n);
  assert.equal(sdk.fieldDeadline(ripple, 150n, 19n, "reduced"), 200n);
  assert.equal(sdk.fieldDeadline(ripple, 250n, 19n), undefined);
  assert.equal(sdk.fieldDeadline({ ...ripple, startUs: max - 10n, spacingUs: 10n }, max - 1n, 1n, "reduced"), max);
  assert.throws(() => sdk.sampleField({ ...ripple, rings: 33 }, query), { code: 2 });
  assert.throws(() => sdk.sampleField(ripple, { ...query, now: -1n }), RangeError);
  assert.throws(() => sdk.sampleField(ripple, { ...query, motion: "constructor" }), { code: 1 });
});

test("paints a wave palette through mounted clip and checks paint scope", async () => {
  const sdk = await loadNocturne(wasm);
  const wave = { ...sdk.defaultWaveField(), periodUs: 0n, cyclesPerUnit: at(), phaseTurns: .25 };
  const ink = { color: [0, 0, 0, 255], opacity: 1,
    palette: { palette: [[0, 0, 0, 255], [200, 0, 0, 255]], filter: PaletteFilter.step } };
  const tree = sdk.createTree();
  let saved;
  const bounds = { origin: at(1), size: { columns: 8, rows: 1 } };
  try {
    tree.mountComponent({ paint(paint) {
      saved = paint;
      paint.drawField(bounds, wave, { ...ink, get opacity() {
        assert.throws(() => paint.fill(bounds, ink.color), { code: 6 });
        return 1;
      } }, 0n);
    } });
    tree.publish({ columns: 3, rows: 1 });
    assert.equal(tree.frame.cell(at(0)).owner, 0n);
    for (const x of [1, 2]) {
      const cell = tree.frame.cell(at(x));
      assert.deepEqual(cell.style.background, [200, 0, 0, 255]);
      assert.notEqual(cell.owner, 0n);
    }
    assert.throws(() => saved.drawField(bounds, wave, ink, 0n), { code: 5 });
  } finally {
    tree.close();
  }
});
