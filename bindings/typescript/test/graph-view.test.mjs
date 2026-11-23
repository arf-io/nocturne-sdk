import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { EventKind, loadNocturne } from "../dist/index.js";

const wasm = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));

test("graph snapshots and mounted selection preserve stable IDs and publication", async () => {
  const sdk = await loadNocturne(wasm);
  const left = (1n << 63n) + 1n, right = left + 1n, edge = (1n << 64n) - 1n;
  const graph = {
    nodes: [
      { id: right, pinned: true, position: { x: 0.75, y: 0.5 } },
      { id: left, pinned: true, position: { x: 0.25, y: 0.5 } },
    ],
    edges: [{ id: edge, source: left, target: right, directed: true }],
  };
  if (!sdk.availableComponents.includes("graph")) {
    assert.throws(() => sdk.layoutGraph(graph), error => error.code === 7);
    const tree = sdk.createTree();
    try {
      assert.throws(() => tree.mountGraph(graph), error => error.code === 7);
      const node = tree.mountLabel("minimal");
      assert.throws(() => tree.graphSelection(node), error => error.code === 7);
    } finally {
      tree.close();
    }
    return;
  }
  const snapshot = sdk.layoutGraph(graph);
  assert.deepEqual(snapshot.nodes, [...graph.nodes].reverse());
  assert.deepEqual(snapshot.edges, graph.edges);

  const tree = sdk.createTree();
  try {
    const view = { ...graph, labels: [{ node: left, text: "北" }] };
    const label = new TextEncoder().encode("北");
    const node = tree.mountGraph({ ...view, labels: [{ node: left, text: label }] },
      { focusable: true, hitTestable: true });
    label.fill(0xff);
    const size = { columns: 21, rows: 5 };
    tree.publish(size);
    for (const x of [7, 8]) assert.deepEqual(tree.graphHitAt(node, { x, y: 2 }), { kind: "node", id: left });
    const at = { x: 10, y: 2 }, wantEdge = { kind: "edge", id: edge };
    assert.deepEqual(tree.graphHitAt(node, at), wantEdge);
    assert.equal(tree.dispatch(1n, { kind: EventKind.pointerDown, pointer: 1n, at, button: 1, pressed: 1 }).consumed, true);
    assert.deepEqual(sdk.decodeGraphIntent(tree.peekEventIntent()), wantEdge);
    assert.throws(() => tree.selectGraph(node, { kind: "node", id: 99n }), error => error.code === 1);
    assert.deepEqual(tree.graphSelection(node), wantEdge);
    assert.throws(() => tree.setGraph(node, { ...view, labels: [{ node: left, text: label }] }),
      error => error.code === 1);
    assert.deepEqual(tree.graphHitAt(node, at), wantEdge);
    tree.setGraph(node, { ...view, edges: [] });
    assert.deepEqual(tree.graphSelection(node), { kind: "none", id: 0n });
    assert.deepEqual(tree.graphHitAt(node, at), wantEdge);
    tree.publish(size);
    assert.deepEqual(tree.graphHitAt(node, at), { kind: "none", id: 0n });
  } finally {
    tree.close();
  }
});
