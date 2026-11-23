import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { loadNocturne } from "../dist/index.js";

const wasm = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));

test("updates native box and rule configuration while retaining child mounts", async () => {
  const sdk = await loadNocturne(wasm);
  const tree = sdk.createTree();
  const items = [{ columns: { min: 2, max: 2 } }];
  const layout = { kind: "row", padding: { left: 1 }, gap: 1, items };
  const size = { columns: 8, rows: 5 };
  try {
    const box = tree.mountBoxLayout(layout);
    const label = tree.mountLabel("A", { parent: box });
    const rule = tree.mountRule({ ascii: true }, { parent: box });
    items[0].columns.max = 1;

    tree.publish(size);
    assert.deepEqual(tree.geometry(label).bounds, {
      origin: { x: 1, y: 0 }, size: { columns: 2, rows: 1 },
    });
    assert.throws(() => tree.setBoxLayout(box, layout), error => error.code === 1);
    tree.setBoxLayout(box, { kind: "column", padding: { left: 2, top: 1 } });
    tree.setRule(rule, { vertical: true, ascii: true });
    tree.publish(size);
    assert.deepEqual(tree.geometry(label).bounds.origin, { x: 2, y: 1 });
    assert.equal(tree.frame.cell({ x: 2, y: 2 }).glyph, "|");
  } finally {
    tree.close();
  }
});

test("restricts custom placement to a parent-local clip and exposes local paint bounds", async () => {
  const sdk = await loadNocturne(wasm);
  const tree = sdk.createTree();
  const bounds = { origin: { x: 1, y: 0 }, size: { columns: 4, rows: 2 } };
  const clip = { origin: { x: 2, y: 0 }, size: { columns: 1, rows: 1 } };
  const style = { foreground: [255, 255, 255, 255], background: [0, 0, 0, 0] };
  let retainedLayout, paintClip;
  try {
    const root = tree.mountComponent({
      measure(measure, limits) {
        return measure.child(0, limits);
      },
      layout(layout) {
        retainedLayout = layout;
        layout.placeClipped(0, bounds, clip);
      },
    });
    const node = tree.mountComponent({
      paint(paint) {
        paintClip = paint.clip;
        paint.glyph({ x: 1, y: 0 }, "X", 1, style);
        paint.eraseGlyph({ x: 1, y: 0 });
      },
    }, { parent: root, hitTestable: true });
    tree.publish({ columns: 6, rows: 2 });
    assert.deepEqual(paintClip, { origin: { x: 1, y: 0 }, size: clip.size });
    assert.deepEqual(tree.geometry(node).bounds, bounds);
    assert.deepEqual(tree.geometry(node).clip, clip);
    assert.deepEqual(tree.hit({ x: 2, y: 0 }), node);
    assert.notDeepEqual(tree.hit({ x: 1, y: 0 }), node);
    assert.throws(() => retainedLayout.placeClipped(0, bounds, clip), error => error.code === 5);
    assert.notEqual(tree.frame.cell({ x: 2, y: 0 }).glyph, "X");
  } finally {
    tree.close();
  }
});

test("maps the editor caret through a matching layout and resolves viewport geometry", async () => {
  const sdk = await loadNocturne(wasm);
  const editor = sdk.createEditor("A界B");
  const layout = sdk.layoutText("A界B");
  const cell = { x: 2, y: 0 };
  try {
    const caret = layout.hit(cell);
    editor.selectAt(layout, cell);
    assert.deepEqual(editor.selection, { anchor: caret, active: caret });
    assert.deepEqual(editor.caretPosition(layout), layout.caretPosition(caret));
    editor.replaceSelection("x");
    assert.throws(() => editor.selectAt(layout, cell), error => error.code === 5);

    const viewport = { columns: 3, rows: 3 }, child = { columns: 8, rows: 7 };
    assert.deepEqual(sdk.resolveScroll({
      scrollColumns: true, requested: { columns: 10, rows: 10 },
    }, viewport, child), { columns: 5, rows: 0 });
    assert.deepEqual(sdk.anchorRect(viewport, child, "center", "end", { x: 1, y: -1 }),
      { origin: { x: -2, y: -5 }, size: child });
  } finally {
    editor.close();
    layout.close();
  }
});
