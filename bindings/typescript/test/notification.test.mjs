import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import {
  EventKind, NamedKey, NotificationEventKind, NotificationIntentKind, NotificationStatus, loadNocturne,
} from "../dist/index.js";

const wasm = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));

test("notification settings and owned intents preserve absolute clocks and full IDs", async () => {
  const sdk = await loadNocturne(wasm);
  const tree = sdk.createTree();
  const id = (1n << 64n) - 1n;
  const action = (1n << 63n) + 17n;
  const expiry = (1n << 53n) + 7n;
  const options = {
    id, title: "Café planning", expiresAt: expiry,
    actions: [{ id: 2n, label: "Unavailable", disabled: true }, { id: action, label: "Review" }],
  };
  try {
    const card = tree.mountNotification(options, { focusable: true, hitTestable: true });
    options.actions[1].id = 99n;
    tree.publish({ columns: 32, rows: 4 });
    tree.setFocus(card);
    const key = (at, code) => {
      assert.equal(tree.dispatch(at, { kind: EventKind.key, key: { code } }).consumed, true);
    };
    const readIntent = () => {
      const intent = tree.peekEventIntent();
      assert.ok(tree.sameNode(card, intent.source));
      assert.equal(intent.kind, NotificationIntentKind);
      tree.popIntent();
      return intent;
    };
    key(1n, NamedKey.down);
    assert.deepEqual(tree.notification(card), { id, selectedAction: action, expiresAt: expiry,
      status: NotificationStatus.active });
    assert.throws(() => tree.setNotification(card, { ...options, id: 8n,
      actions: [options.actions[0], options.actions[0]] }), error => error.code === 1);
    key(2n, NamedKey.enter);
    const retained = readIntent();
    const performed = { kind: NotificationEventKind.performed, notification: id, action };
    assert.deepEqual(sdk.decodeNotificationIntent(retained), performed);
    key(3n, NamedKey.escape);
    assert.deepEqual(sdk.decodeNotificationIntent(readIntent()),
      { kind: NotificationEventKind.dismissed, notification: id, action: 0n });
    assert.equal(tree.notification(card).status, NotificationStatus.dismissed);

    tree.setNotification(card, { id: 8n, title: "Waiting", expiresAt: expiry, dismissible: false });
    tree.tick(4n, "reduced");
    assert.equal(tree.nextUpdate, expiry);
    tree.tick(expiry, "reduced");
    assert.deepEqual(tree.notification(card), { id: 8n, selectedAction: 0n, expiresAt: expiry,
      status: NotificationStatus.expired });
    tree.setNotification(card, { id: 8n, title: "Waiting", dismissible: false });
    tree.dismissNotification(card);
    assert.deepEqual(tree.notification(card), { id: 8n, selectedAction: 0n, expiresAt: 0n,
      status: NotificationStatus.dismissed });
    assert.equal(tree.peekEventIntent(), undefined);
    tree.remove(card);
    assert.throws(() => tree.notification(card), error => error.code === 5);
    assert.deepEqual(sdk.decodeNotificationIntent(retained), performed);
    assert.throws(() => sdk.decodeNotificationIntent({ ...retained, payloadKind: "bytes" }), error => error.code === 1);
    assert.throws(() => sdk.decodeNotificationIntent({ ...retained, fields: retained.fields.map(field => field.id === 3
      ? { ...field, value: { kind: "unsigned", value: 0n } } : field) }), error => error.code === 1);
  } finally {
    tree.close();
  }
});
