import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { runInNewContext } from "node:vm";
import { Property, StateSchemaVersion, loadNocturne } from "../dist/index.js";

const wasm = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));
const paint = { foreground: [255, 255, 255, 255], background: [0, 0, 0, 0] };
const check = (f, code) => assert.throws(f, { code });
const original = "e\u0301\0界";

function panel(sdk, title, value, maximum) {
  const tree = sdk.createTree();
  const root = tree.mountBox("column");
  const label = tree.mountLabel(title, { parent: root });
  const progress = tree.mountProgress({ value, maximum, filled: paint, remaining: paint }, { parent: root });
  return { tree, root, label, progress, properties: [{ node: label, id: Property.text },
    { node: progress, id: Property.value }] };
}

test("captures and imports owned binary state with atomic ordered restoration", async () => {
  const sdk = await loadNocturne(wasm);
  const source = panel(sdk, original, 9, 10);
  const target = panel(sdk, "before", 4, 5);
  const otherSdk = await loadNocturne(wasm);
  const captured = source.tree.captureState(source.properties);
  let imported, recaptured, empty, foreign;
  try {
    assert.equal(BigInt(StateSchemaVersion), sdk.raw.constant("NT_STATE_SCHEMA_VERSION"));
    check(() => source.tree.captureState([source.properties[0], source.properties[0]]), 1);
    source.tree.close();
    const bytes = captured.bytes;
    assert.equal(new TextDecoder().decode(bytes.subarray(0, 4)), "NTST");
    assert.equal(new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength).getUint32(4, true), StateSchemaVersion);
    const want = bytes.slice();
    bytes[0] = 0;
    assert.deepEqual(captured.bytes, want);
    captured.close();
    check(() => captured.bytes, 5);
    const version = want.slice();
    version[4] = StateSchemaVersion + 1;
    check(() => sdk.importStateSnapshot(version), 7);
    for (let i = 0; i < want.length; i++) check(() => sdk.importStateSnapshot(want.subarray(0, i)), 1);
    const extended = new Uint8Array(want.length + 1);
    extended.set(want);
    check(() => sdk.importStateSnapshot(extended), 1);
    check(() => sdk.importStateSnapshot("NTST"), 1);

    const input = runInNewContext("Uint8Array.from(bytes)", { bytes: [...want] });
    imported = sdk.importStateSnapshot(input);
    input.fill(0);
    assert.deepEqual(imported.bytes, want);
    foreign = otherSdk.importStateSnapshot(want);
    check(() => target.tree.restoreState(target.properties, foreign), 1);
    check(() => target.tree.restoreState(target.properties, Object.create(Object.getPrototypeOf(imported))), 1);

    target.tree.publish({ columns: 20, rows: 6 });
    const retained = target.tree.frame;
    const point = { x: 0, y: 0 };
    const cell = retained.cell(point);
    check(() => target.tree.restoreState(target.properties, imported), 2);
    assert.deepEqual(retained.cell(point), cell);
    assert.deepEqual(target.tree.property(target.label, Property.text), { kind: "text", value: "before" });
    assert.deepEqual(target.tree.property(target.progress, Property.value), { kind: "unsigned", value: 4n });
    target.tree.remove(target.progress);
    check(() => target.tree.restoreState(target.properties, imported), 5);
    target.properties[1].node = target.tree.mountProgress({
      value: 0, maximum: 10, filled: paint, remaining: paint,
    }, { parent: target.root });
    check(() => target.tree.restoreState([...target.properties].reverse(), imported), 1);
    const refs = [{
      get node() {
        check(() => target.tree.close(), 6);
        return target.label;
      },
      id: Property.text,
    }, target.properties[1]];
    target.tree.restoreState(refs, imported);
    recaptured = target.tree.captureState(target.properties);
    assert.deepEqual(recaptured.bytes, want);
    target.tree.close();
    assert.deepEqual(recaptured.bytes, want);

    imported.close();
    check(() => imported.bytes, 5);
    const newTarget = sdk.createTree();
    try {
      empty = newTarget.captureState([]);
      newTarget.restoreState([], empty);
      check(() => newTarget.restoreState([], imported), 5);
    } finally {
      newTarget.close();
    }
  } finally {
    source.tree.close();
    target.tree.close();
    for (const state of [captured, imported, recaptured, empty, foreign]) state?.close();
  }
});
