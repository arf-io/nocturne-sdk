import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { CallError, EventKind, loadNocturne } from "../dist/index.js";

const wasm = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));

test("replaces window chrome and transfers focus within the published window", async () => {
  const tree = (await loadNocturne(wasm)).createTree();
  const size = { columns: 30, rows: 12 };
  try {
    const workspace = tree.mountWindowWorkspace(size);
    const options = { parent: workspace, focusable: true, hitTestable: true };
    const floating = { origin: { x: 2, y: 1 }, size: { columns: 16, rows: 6 } };
    const window = tree.mountWindow({ placement: { floating }, panel: { framed: true } }, options);
    const body = tree.mountLabel("Body", { parent: window, focusable: true });
    const other = tree.mountWindow({ placement: { floating }, panel: { framed: true } }, options);
    tree.publish(size);
    assert.deepEqual(tree.hit(floating.origin), other);
    tree.focusWindow(window, body);
    assert.deepEqual(tree.focus, body);
    assert.throws(() => tree.focusWindow(window, other), error => error.code === 1);
    assert.deepEqual(tree.focus, body);
    tree.publish(size);
    assert.deepEqual(tree.hit(floating.origin), window);
    const before = tree.geometry(body);
    tree.setWindowPanel(window, { framed: true, padding: { left: 3 }, title: "Updated" });
    tree.publish(size);
    const after = tree.geometry(body);
    assert.deepEqual(after.parent, window);
    assert.equal(after.bounds.origin.x, before.bounds.origin.x + 3);
    tree.focusWindow(window);
    assert.deepEqual(tree.focus, window);
  } finally {
    tree.close();
  }
});

test("panel content owns provider lifetime and preserves a frame after paint failure", async () => {
  const tree = (await loadNocturne(wasm)).createTree();
  const size = { columns: 12, rows: 6 }, origin = { x: 0, y: 0 };
  const style = { foreground: [255, 255, 255, 255], background: [0, 0, 0, 0] };
  const cause = new Error("cannot paint provider");
  let retained, updates = 0, focused = 0, disposed = 0, fail = false;
  const provider = {
    preferred: { columns: 3, rows: 2 },
    paint(context) {
      retained = context;
      if (fail) throw cause;
      context.glyph(origin, "P", 1, style);
    },
    update() { updates++; return { invalidate: "paint" }; },
    event(event) { if (event.kind === EventKind.focusGain) focused++; },
    dispose() { disposed++; },
  };
  try {
    const panel = tree.mountPanel({});
    const body = tree.mountPanelContent(provider, { parent: panel, focusable: true });
    assert.throws(() => tree.mountPanelContent(provider), error => error.code === 1);
    assert.equal(disposed, 0);
    tree.tick(1n);
    tree.publish(size);
    tree.setFocus(body);
    assert.equal(updates, 1);
    assert.equal(focused, 1);
    const cell = tree.frame.cell(origin);
    assert.equal(cell.glyph, "P");
    assert.notEqual(cell.owner, 0n);
    assert.throws(() => retained.glyph(origin, "!", 1, style), error => error.code === 5);
    fail = true;
    tree.invalidate(body, "paint");
    assert.throws(() => tree.publish(size), error => error instanceof CallError &&
      error.native?.code === 9 && error.callbacks[0]?.cause === cause);
    assert.deepEqual(tree.frame.cell(origin), cell);
    tree.remove(body);
    assert.equal(disposed, 1);
  } finally {
    tree.close();
  }
  assert.equal(disposed, 1);
});
