import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { CallError, loadNocturne } from "../dist/index.js";

const wasm = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));
const typed = (kind, value) => ({ kind, value });
const text = value => typed("text", value);
const count = value => typed("unsigned", value);
const refusal = (call, code) => assert.throws(call, { code });

test("defines independent custom properties and preserves transaction order", async () => {
  const sdk = await loadNocturne(wasm);
  const tree = sdk.createTree();
  const node = tree.mountBox("column");
  const titleID = 100, countID = 101, readonlyID = 102;
  const titleRef = { node, id: titleID }, countRef = { node, id: countID };
  let title = "start", amount = 7n, order = [], snapshot;
  const unicode = "e\u0301\0界";
  const descriptors = [
    { id: titleID, kind: "text", invalidation: "layout", read: () => text(title),
      prepare(value) {
        order.push("title prepare");
        refusal(() => tree.property(node, titleID), 6);
        return { prepared: [value.value] };
      },
      finish(value, prepared, commit) {
        assert.equal(prepared[0], value.value);
        order.push(commit ? "title commit" : "title discard");
        if (commit) title = value.value;
      },
    },
    { id: countID, kind: "unsigned", invalidation: "paint", read: () => count(amount),
      prepare(value) {
        order.push("amount prepare");
        return value.value <= 100n ? { prepared: value.value } : 2;
      },
      finish(value, prepared, commit) {
        assert.equal(commit, true);
        assert.equal(prepared, value.value);
        amount = value.value;
        order.push("amount commit");
      },
    },
    { id: readonlyID, kind: "bool", read: () => typed("bool", true) },
  ];
  try {
    refusal(() => tree.defineProperties(node, [descriptors[0], descriptors[0]]), 1);
    tree.defineProperties(node, descriptors);
    descriptors[0].read = null;
    assert.deepEqual(tree.properties(node), [
      { id: titleID, kind: "text", writable: true, invalidation: "layout" },
      { id: countID, kind: "unsigned", writable: true, invalidation: "paint" },
      { id: readonlyID, kind: "bool", writable: false, invalidation: "none" },
    ]);
    const held = tree.property(node, titleID);
    assert.deepEqual(held, text("start"));
    const writes = [{ property: titleRef, value: text(unicode) }, { property: countRef, value: count(101n) }];
    refusal(() => tree.applyProperties(writes), 2);
    assert.deepEqual(order, ["title prepare", "amount prepare", "title discard"]);
    assert.equal(title, "start");
    assert.equal(amount, 7n);
    writes[1].value = count(10n);
    order = [];
    tree.applyProperties(writes);
    assert.deepEqual(order, ["title prepare", "amount prepare", "title commit", "amount commit"]);
    assert.equal(title, unicode);
    assert.equal(amount, 10n);
    assert.deepEqual(held, text("start"));
    refusal(() => tree.setProperty(node, readonlyID, typed("bool", false)), 7);
    const refs = [titleRef, countRef];
    snapshot = tree.captureState(refs);
    tree.setProperty(node, titleID, text("other"));
    tree.restoreState(refs, snapshot);
    assert.equal(title, unicode);
    assert.equal(amount, 10n);
    tree.remove(node);
    refusal(() => tree.properties(node), 5);
  } finally {
    snapshot?.close();
    tree.close();
  }
});

test("reports property callback failures on the invoking call", async () => {
  const sdk = await loadNocturne(wasm);
  const tree = sdk.createTree();
  try {
    const node = tree.mountBox("column");
    const id = 100;
    const cause = new Error("custom property failure");
    let mode, value = 9n;
    tree.defineProperties(node, [{ id, kind: "unsigned", read() {
      if (mode === "result") return -701;
      if (mode === "throw") throw cause;
      return count(value);
    }, prepare(next) {
      if (mode === "async") return Promise.reject(cause);
      return { prepared: next.value };
    }, finish(next, prepared) {
      assert.equal(next.value, prepared);
      value = next.value;
      if (mode === "finish") throw cause;
    } }]);
    mode = "result";
    refusal(() => tree.property(node, id), -701);
    mode = "throw";
    assert.throws(() => tree.property(node, id), error => error instanceof CallError &&
      error.native.code === 9 && error.callbacks[0].phase === "propertyRead" && error.callbacks[0].cause === cause);
    mode = "async";
    assert.throws(() => tree.setProperty(node, id, count(11n)), error => error instanceof CallError &&
      error.native.code === 9 && error.callbacks[0].phase === "propertyPrepare" && error.callbacks[0].cause instanceof TypeError);
    assert.equal(value, 9n);
    mode = "finish";
    assert.throws(() => tree.setProperty(node, id, count(12n)), error => error instanceof CallError &&
      error.native === null && error.callbacks[0].phase === "propertyFinish" && error.callbacks[0].cause === cause);
    assert.equal(value, 12n);
    assert.deepEqual(tree.property(node, id), count(12n));
  } finally {
    tree.close();
  }
});
