import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { CallError, loadNocturne, Property, ScheduleError } from "../dist/index.js";

const source = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));
const text = value => ({ kind: "text", value });
const check = (action, code) => assert.throws(action, error => error.code === code);

test("validates and atomically applies copied binding graphs with synchronous compute", async () => {
  const tree = (await loadNocturne(source)).createTree();
  const label = tree.mountLabel("draft");
  const property = { node: label, id: Property.text };
  const targets = [{ property, value: 0 }];
  const dependencies = [1, 2];
  let graph, retained, calls = 0, mode;
  const cause = new Error("binding refused");
  const initial = [
    { kind: "text", source: "computed", dependencies, compute(inputs) {
      calls++;
      retained = inputs;
      if (mode === "reject") return -701;
      if (mode === "throw") throw cause;
      if (mode === "async") return Promise.reject(cause);
      if (mode === "invalid") return { kind: "bool", value: false };
      check(() => graph.close(), 6);
      check(() => tree.property(label, Property.text), 6);
      return text(inputs.map(value => value.value).join("|"));
    } },
    { kind: "text", source: "property", property },
    { kind: "text", source: "input" },
  ];
  try {
    check(() => tree.createBindingGraph([{ ...initial[0], dependencies: [0] }], targets), 1);
    graph = tree.createBindingGraph(initial, targets);
    dependencies[0] = 0;
    targets[0].value = 2;
    const input = text("e\u0301\0☾");
    graph.apply([input]);
    assert.deepEqual(retained, [text("draft"), input]);
    assert.deepEqual(tree.property(label, Property.text), text("draft|" + input.value));
    mode = "reject";
    check(() => graph.apply([input]), -701);
    mode = "throw";
    assert.throws(() => graph.apply([input]), error =>
      error instanceof CallError && error.native.code === 9 &&
      error.callbacks.length === 1 && error.callbacks[0].phase === "bindingCompute" &&
      error.callbacks[0].cause === cause);
    mode = "async";
    assert.throws(() => graph.apply([input]), error =>
      error instanceof CallError && error.native.code === 9 &&
      error.callbacks.length === 1 && error.callbacks[0].cause instanceof TypeError);
    mode = "invalid";
    check(() => graph.apply([input]), 1);
    assert.deepEqual(tree.property(label, Property.text), text("draft|" + input.value));
    tree.remove(label);
    check(() => graph.apply([input]), 5);
    assert.equal(calls, 5);
    tree.close();
    graph.close();
    graph.close();
    check(() => graph.apply([input]), 5);
  } finally {
    graph?.close();
    tree.close();
  }
});

test("batches writes atomically and owns scheduled assignments at uint64 deadlines", async () => {
  const tree = (await loadNocturne(source)).createTree();
  try {
    const parent = tree.mountBox("column");
    const label = tree.mountLabel("initial", { parent });
    const checkbox = tree.mountCheckbox("Done", false, { parent });
    const name = { node: label, id: Property.text };
    const flag = { node: checkbox, id: Property.checked };
    const assignments = [
      { property: name, value: text("pending") },
      { property: flag, value: text("bad") },
    ];
    const read = ({ node, id }) => tree.property(node, id);
    check(() => tree.applyProperties(assignments), 1);
    assert.deepEqual(read(name), text("initial"));
    assignments[1].value = { kind: "bool", value: true };
    tree.applyProperties(assignments);
    assert.equal(read(flag).value, true);
    tree.configureSchedule({ batches: 3, subscriptions: 0 });
    const deadline = 2n ** 64n - 3n;
    const first = tree.scheduleBatch(parent, deadline, assignments.slice(0, 1));
    assignments[0].value = text("final");
    const last = tree.scheduleBatch(parent, deadline, assignments.slice(0, 1));
    const cancel = tree.scheduleBatch(checkbox, deadline - 1n, assignments.slice(0, 1));
    assert.ok(first < last && last < cancel);
    tree.remove(checkbox);
    check(() => tree.cancelSchedule(cancel), 5);
    const ink = { foreground: [255, 255, 255, 255], background: [0, 0, 0, 0] };
    const progress = tree.mountProgress({ value: 5, maximum: 12, filled: ink, remaining: ink }, { parent });
    const counter = { node: progress, id: Property.value };
    const rejected = tree.scheduleBatch(parent, deadline + 1n, [
      { property: name, value: text("should roll back") },
      { property: counter, value: { kind: "unsigned", value: 99n } },
    ]);
    assert.equal(tree.nextScheduled, deadline);
    assert.deepEqual(tree.runSchedule(deadline, 1), { attempted: 1, failed: 0n });
    assert.deepEqual(read(name), text("pending"));
    assert.deepEqual(tree.runSchedule(deadline, 1), { attempted: 1, failed: 0n });
    assert.deepEqual(read(name), text("final"));
    assert.throws(() => tree.runSchedule(deadline + 1n, 1), error => {
      assert.ok(error instanceof ScheduleError);
      assert.equal(error.code, 2);
      assert.deepEqual(error.report, { attempted: 1, failed: rejected });
      return true;
    });
    assert.deepEqual(read(name), text("final"));
    assert.deepEqual(read(counter), { kind: "unsigned", value: 5n });
    check(() => tree.cancelSchedule(rejected), 5);
    assert.throws(() => tree.runSchedule(deadline - 1n, 1),
      error => error.code === 2 && !(error instanceof ScheduleError));
    assert.equal(tree.nextScheduled, undefined);
  } finally {
    tree.close();
  }
});
