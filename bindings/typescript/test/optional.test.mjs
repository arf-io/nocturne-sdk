import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { loadNocturne } from "../dist/index.js";

const source = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));
const absent = { nativeName: "NT_RESULT_UNSUPPORTED_CAPABILITY" };
const invalid = { nativeName: "NT_RESULT_INVALID_ARGUMENT" };
const first = { id: (1n << 63n) + 1n, pinned: true, position: { x: 0, y: 0 } };
const second = { id: first.id + 1n, pinned: true, position: { x: 1, y: 1 } };
const graph = {
  nodes: [first, second],
  edges: [{ id: 1n, source: first.id, target: second.id, directed: true }],
  preferred: { columns: 8, rows: 4 },
};

test("discovers and composes an optional graph from an installed WASM module", async () => {
  const sdk = await loadNocturne(source);
  const tree = sdk.createTree();
  const other = sdk.createTree();
  try {
    assert.equal(sdk.availableComponents[0], "nocturne");
    assert.ok(Object.isFrozen(sdk.availableComponents));
    const parent = tree.mountBox("column");
    const label = tree.mountLabel("Map", { parent });
    if (!sdk.availableComponents.includes("graph")) {
      assert.throws(() => tree.mountGraph(graph, { parent }), absent);
      assert.throws(() => tree.setGraph(label, graph), absent);
      tree.publish({ columns: 8, rows: 1 });
      assert.equal(tree.frame.cell({ x: 0, y: 0 }).glyph, "M");
      return;
    }
    const node = tree.mountGraph(graph, { parent, focusable: true });
    const size = { columns: 8, rows: 5 };
    tree.publish(size);
    assert.equal(tree.frame.cell({ x: 0, y: 0 }).glyph, "M");
    assert.equal(tree.frame.cell({ x: 0, y: 1 }).glyph, "@");
    assert.equal(tree.frame.cell({ x: 7, y: 4 }).glyph, "@");
    assert.throws(() => other.setGraph(node, graph), invalid);
    assert.throws(() => tree.setGraph(node, { ...graph, nodes: [first, first] }), invalid);
    assert.throws(() => tree.mountGraph({ ...graph, nodes: Array(513).fill(first) }),
      { nativeName: "NT_RESULT_OUT_OF_RANGE" });
    assert.equal(tree.frame.cell({ x: 0, y: 1 }).glyph, "@");
    const updated = { ...first, position: { x: 1, y: 0 } };
    tree.setGraph(node, { ...graph, nodes: [updated], edges: [], layout: "radial" });
    tree.publish(size);
    assert.notEqual(tree.frame.cell({ x: 0, y: 1 }).glyph, "@");
    assert.equal(tree.frame.cell({ x: 7, y: 1 }).glyph, "@");
    tree.remove(node);
    assert.throws(() => tree.setGraph(node, graph), { nativeName: "NT_RESULT_STALE_REFERENCE" });
    tree.publish(size);
    assert.notEqual(tree.frame.cell({ x: 7, y: 1 }).glyph, "@");
  } finally {
    other.close();
    tree.close();
  }
});

test("samples the optional native vectors and water waves", async () => {
  const sdk = await loadNocturne(source);
  const i = { x: 1, y: 0, z: 0 }, j = { x: 0, y: 1, z: 0 };
  const wave = { amplitude: 2, cyclesPerX: 1, cyclesPerZ: 0, cyclesPerSecond: 1, phase: 0 };
  const flow = { width: 1e-6, speed: 1e-6, maximumDepth: 2 ** -1040 };
  const store = { heatCapacity: 1e-6, energy: 1e24 };
  const thermal = { first: store, second: store };
  if (sdk.availableComponents.includes("spatial")) {
    assert.deepEqual(sdk.crossVector(i, j), { x: 0, y: 0, z: 1 });
    const unit = sdk.normalizeVector({ x: 3, y: 4, z: 0 });
    assert.ok(Math.abs(unit.x - 0.6) < 1e-15 && Math.abs(unit.y - 0.8) < 1e-15);
    assert.throws(() => sdk.crossVector(i, { ...j, x: Infinity }), invalid);
  } else {
    assert.throws(() => sdk.crossVector(i, j), absent);
    assert.throws(() => sdk.normalizeVector(i), absent);
  }
  if (sdk.availableComponents.includes("simulation")) {
    assert.ok(sdk.availableComponents.includes("spatial"));
    const crest = sdk.sampleWater([wave], 0, 0, 0);
    assert.equal(crest.height, 2);
    assert.ok(Math.abs(crest.laplacian + 8 * Math.PI ** 2) < 1e-13);
    const slope = sdk.sampleWater([wave], 0.25, 0, 0);
    assert.ok(Math.abs(slope.height) < 1e-13);
    assert.ok(Math.abs(slope.gradientX + 4 * Math.PI) < 1e-13);
    assert.ok(Math.abs(slope.verticalVelocity - 4 * Math.PI) < 1e-13);
    assert.throws(() => sdk.sampleWater(Array(33).fill(wave), 0, 0, 0),
      { nativeName: "NT_RESULT_OUT_OF_RANGE" });
    assert.equal(sdk.sampleFlowSection(flow, Number.MIN_VALUE).depth, flow.maximumDepth);
    assert.deepEqual(sdk.exchangeHeat(thermal, 1e-6, 1000000n), thermal);
  } else {
    assert.throws(() => sdk.sampleWater([wave], 0, 0, 0), absent);
    assert.throws(() => sdk.sampleFlowSection(flow, Number.MIN_VALUE), absent);
    assert.throws(() => sdk.exchangeHeat(thermal, 1e-6, 1000000n), absent);
  }
});
