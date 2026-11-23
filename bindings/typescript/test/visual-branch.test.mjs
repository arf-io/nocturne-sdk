import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { loadNocturne, VisualBranchNoParent } from "../dist/index.js";

const wasm = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));

test("visual branches preserve segment parents, explicit time and paint clipping", async () => {
  const sdk = await loadNocturne(wasm);
  const options = { startUs: 100n, lifetimeUs: 120n, revealUs: 5n, flickerUs: 0n };
  const segments = sdk.sampleVisualBranch(options, 160n, "full");
  assert.equal(segments.length, 13);
  assert.equal(segments[0].color[3], 128);
  assert.equal(segments[0].parentId, VisualBranchNoParent);
  for (const [i, segment] of segments.entries()) {
    if (i === 0) continue;
    assert.equal(segment.id, i);
    assert.ok(segment.parentId < i);
    assert.deepEqual(segment.start, segments[segment.parentId].end);
  }
  const reduced = sdk.sampleVisualBranch(options, 100n, "reduced");
  assert.equal(reduced.length, 19);
  assert.equal(reduced[0].color[3], 255);
  assert.equal(sdk.visualBranchDeadline(options, 103n, "full", 60n), 105n);
  assert.equal(sdk.visualBranchDeadline(options, 103n, "reduced", 60n), 220n);
  assert.throws(() => sdk.sampleVisualBranch({ ...options, trunkSegments: 256 }, 160n), e => e.code === 2);

  const tree = sdk.createTree();
  const extreme = { start: { x: -(2 ** 31), y: 1 }, end: { x: 2 ** 31 - 1, y: 1 },
    trunkSegments: 2, branchCount: 0, jitterCells: 0 };
  try {
    let retained;
    tree.mountComponent({ paint(paint) {
      retained = paint;
      paint.drawVisualBranch(extreme, 0n, "#", undefined, "reduced");
    } });
    tree.publish({ columns: 6, rows: 3 });
    for (let x = 0; x < 6; x++) {
      const cell = tree.frame.cell({ x, y: 1 });
      assert.equal(cell.glyph, "#");
      assert.notEqual(cell.owner, 0n);
    }
    assert.throws(() => retained.drawVisualBranch(extreme, 0n, "#"), e => e.code === 5);
  } finally { tree.close(); }
});
