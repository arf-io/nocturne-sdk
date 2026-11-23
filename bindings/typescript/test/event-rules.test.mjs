import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import {
  CallError, EventKind, EventPhase, IntentDeliveryError, NamedKey, Property, loadNocturne,
} from "../dist/index.js";

const wasm = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));
const text = value => ({ kind: "text", value });
const unsigned = value => ({ kind: "unsigned", value });
const maximum = (1n << 64n) - 1n;
const size = { columns: 10, rows: 4 };
const key = { kind: EventKind.key, key: { code: NamedKey.enter } };
const refused = (call, code) => assert.throws(call, err => err.code === code);
const keyEvent = (tree, event, parent) => tree.mountComponent({
  measure: () => ({ columns: 1, rows: 1 }),
  event: input => { if (input.kind === EventKind.key) event(input); },
}, { parent, focusable: true });

test("stages typed rules atomically and delivers copied intents in registration order", async () => {
  const sdk = await loadNocturne(wasm);
  const tree = sdk.createTree();
  const independent = sdk.createTree();
  const deriveFailure = new Error("derive stopped");
  const receiverFailure = new Error("receiver stopped");
  try {
    const foreign = independent.mountLabel("foreign");
    const root = tree.mountBox("column");
    const label = tree.mountLabel("☾\0", { parent: root });
    const event = EventKind.key, phase = EventPhase.target;
    let escaped;
    let retained;
    let derives = 0;
    const emitted = [];
    const sender = keyEvent(tree, current => {
      escaped = current;
      current.emit(71, Uint8Array.of(255, 0));
      if (current.at === 4n) {
        refused(() => current.applyRules([{ event, phase, intentKind: 72,
          properties: [{ node: foreign, id: Property.text }] }]), 1);
        return;
      }
      const run = () => current.applyRules([
        { event, phase: EventPhase.bubble, intentKind: 72, properties: [{ node: foreign, id: Property.text }] },
        { event, phase, intentKind: 73, properties: [{ node: label, id: Property.text }],
          predicate(snapshot, values) {
            assert.equal(snapshot.at, current.at);
            assert.deepEqual(independent.property(foreign, Property.text), text("foreign"));
            assert.equal(current.phase, phase);
            values[0] = text("local edit");
            return snapshot.at !== 3n;
          },
          fields: [
            { id: 3, kind: "text", derive(snapshot, values) {
              derives++;
              retained = values;
              assert.equal(snapshot.input.kind, EventKind.key);
              assert.equal(snapshot.input.key.code, NamedKey.enter);
              refused(() => current.consume(), 6);
              refused(() => current.requestFocus(), 6);
              refused(() => current.emitFields(74, []), 6);
              if (snapshot.at === 2n) throw deriveFailure;
              return values[0];
            } },
            { id: 4, kind: "unsigned", derive(_snapshot, values) {
              assert.deepEqual(values[0], text("☾\0"));
              return unsigned(maximum);
            } },
          ],
        },
      ]);
      if (current.at === 2n) refused(run, 9);
      else emitted.push(run());
      if (current.at === 3n) current.emitFields(75, []);
    }, root);
    tree.publish(size);
    tree.setFocus(sender);
    tree.configureSchedule({ batches: 0, subscriptions: 4 });
    const deliveries = [];
    let retainedIntent;
    const receive = (owner, intent) => {
      refused(() => tree.deliverIntents(1), 6);
      retainedIntent = intent;
      const child = tree.sameNode(owner, sender);
      deliveries.push(child ? "child" : "root");
      if (child) throw receiverFailure;
    };
    const first = tree.subscribeIntent(sender, 73, receive);
    const second = tree.subscribeIntent(root, 73, receive);
    assert.ok(first < second);
    for (const at of [0n, 1n, 2n, 3n, 4n]) {
      if (at === 2n) {
        assert.throws(() => tree.dispatch(at, key), err => {
          assert.equal(err.delivery.admitted, true);
          assert.ok(err.cause instanceof CallError);
          assert.equal(err.cause.native.code, 9);
          assert.deepEqual(err.cause.callbacks, [{ phase: "eventDerive", cause: deriveFailure }]);
          return true;
        });
      } else if (at === 4n) {
        assert.throws(() => tree.dispatch(at, key), err => err.cause?.code === 1);
      } else {
        const report = tree.dispatch(at, key);
        assert.equal(report.admitted, true);
        assert.equal(report.consumed, false);
      }
    }
    assert.deepEqual(emitted, [1, 1, 0]);
    assert.equal(derives, 3);
    refused(() => escaped.emitFields(73, []), 5);
    const raw = tree.peekEventIntent();
    assert.equal(raw.payloadKind, "bytes");
    assert.deepEqual(raw.payload, Uint8Array.of(255, 0));
    assert.equal(tree.deliverIntents(0), 0);
    assert.deepEqual(deliveries, []);
    assert.throws(() => tree.deliverIntents(2), err => {
      assert.ok(err instanceof IntentDeliveryError);
      assert.equal(err.delivered, 2);
      assert.equal(err.native, null);
      assert.deepEqual(err.callbacks, [{ phase: "intentReceive", cause: receiverFailure }]);
      return true;
    });
    assert.deepEqual(deliveries, ["child", "root"]);
    tree.remove(sender);
    refused(() => tree.cancelSchedule(first), 5);
    tree.popIntent();
    const historical = tree.peekEventIntent();
    assert.ok(tree.sameNode(historical.source, sender));
    assert.equal(historical.kind, 73);
    assert.equal(tree.deliverIntents(1), 1);
    assert.deepEqual(deliveries, ["child", "root", "root"]);
    tree.popIntent();
    const empty = tree.peekEventIntent();
    assert.equal(empty.kind, 75);
    assert.equal(empty.payloadKind, "fields");
    assert.deepEqual(empty.fields, []);
    tree.popIntent();
    assert.equal(tree.peekEventIntent(), undefined);
    assert.deepEqual(retained, [text("☾\0")]);
    assert.deepEqual(retainedIntent.fields, [
      { id: 3, value: text("☾\0") }, { id: 4, value: unsigned(maximum) },
    ]);
    tree.cancelSchedule(second);
  } finally {
    tree.close();
    independent.close();
  }
});

test("suspends an ancestor event context during nested subscription delivery", async () => {
  const sdk = await loadNocturne(wasm);
  const outer = sdk.createTree();
  const inner = sdk.createTree();
  let escaped;
  try {
    const innerNode = keyEvent(inner, event => event.emit(7, "inner"));
    inner.publish(size);
    inner.setFocus(innerNode);
    inner.configureSchedule({ batches: 0, subscriptions: 1 });
    const received = [];
    inner.subscribeIntent(innerNode, 7, (_owner, item) => {
      if (received.length === 0) refused(() => escaped.emit(1, "off route"), 6);
      return received.push(item.kind);
    });
    inner.dispatch(0n, key);
    const outerNode = keyEvent(outer, event => {
      escaped = event;
      assert.equal(inner.deliverIntents(1), 1);
      event.emit(2, "restored");
    });
    outer.publish(size);
    outer.setFocus(outerNode);
    outer.dispatch(0n, key);
    assert.deepEqual(received, [7]);
    assert.equal(outer.peekEventIntent().kind, 2);

    const failure = new Error("unexpected asynchronous receiver");
    inner.configureSchedule({ batches: 0, subscriptions: 2 });
    inner.subscribeIntent(innerNode, 7, async () => { throw failure; });
    inner.dispatch(1n, key);
    assert.throws(() => inner.deliverIntents(1), error => {
      assert.ok(error instanceof IntentDeliveryError);
      assert.equal(error.delivered, 1);
      assert.equal(error.callbacks.length, 1);
      assert.ok(error.callbacks[0].cause instanceof TypeError);
      return true;
    });
    assert.deepEqual(received, [7, 7]);
  } finally {
    outer.close();
    inner.close();
  }
});
