import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import {
  CallError, ControlEventKind, EditorAction, EventKind, KeyModifier, KeyState, loadNocturne, NamedKey,
} from "../dist/index.js";

const source = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));

test("mounts editable input and publishes clamped read-only reader offsets", async () => {
  const sdk = await loadNocturne(source);
  const tree = sdk.createTree();
  const editor = sdk.createEditor("Draft", { singleLine: true });
  const bank = sdk.createEditorRegisters(1);
  const document = "one\ntwo\nthree\nfour";
  const reader = sdk.createEditor(document, { readOnly: true });
  const mountOptions = { focusable: true, hitTestable: true };
  try {
    editor.move("documentEnd");
    bank.write(0, "☾");
    const chord = { code: 118, modifiers: KeyModifier.control };
    const bindings = [{ chord, action: EditorAction.registerPaste }];
    const view = tree.mountEditorView({ editor, keys: { bindings, registers: bank },
      followCaret: true, showCaret: true, submitOnEnter: true }, mountOptions);
    bindings[0].action = EditorAction.deletePrevious;
    assert.throws(() => tree.editorViewOffset(view), { nativeName: "NT_RESULT_WRONG_PHASE" });
    tree.publish({ columns: 12, rows: 2 });
    tree.setFocus(view);
    const expectIntent = kind => {
      const intent = tree.peekIntent();
      assert.equal(tree.sameNode(intent.source, view), true);
      assert.deepEqual(sdk.decodeControlIntent(intent), { kind, value: 0n });
      tree.popIntent();
    };
    tree.dispatch(1n, { kind: EventKind.text, text: "水" });
    expectIntent(ControlEventKind.editorChanged);
    tree.dispatch(2n, { kind: EventKind.key, key: chord });
    expectIntent(ControlEventKind.editorChanged);
    assert.equal(editor.value, "Draft水☾");
    tree.dispatch(3n, { kind: EventKind.key, key: { code: NamedKey.enter } });
    expectIntent(ControlEventKind.editorSubmitted);
    const repeat = tree.dispatch(4n, { kind: EventKind.key, key: { code: NamedKey.enter, state: KeyState.repeat } });
    assert.equal(repeat.consumed, true);
    assert.equal(tree.peekIntent(), undefined);
    tree.remove(view);

    const readOnly = tree.mountEditorView({ editor: reader, scrollKeys: true }, mountOptions);
    const viewport = { columns: 8, rows: 2 };
    tree.publish(viewport);
    tree.setFocus(readOnly);
    tree.scrollEditorView(readOnly, { columns: 99, rows: 99 });
    assert.deepEqual(tree.editorViewOffset(readOnly), { columns: 0, rows: 0 });
    tree.publish(viewport);
    assert.deepEqual(tree.editorViewOffset(readOnly), { columns: 0, rows: 2 });
    const paste = tree.dispatch(5n, { kind: EventKind.paste, text: "!" });
    assert.equal(paste.consumed, false);
    assert.equal(reader.value, document);
  } finally {
    tree.close();
    reader.close();
    bank.close();
    editor.close();
  }
});

test("retains mounted editor resources across close and reconciles native removals", async () => {
  const sdk = await loadNocturne(source);
  const other = await loadNocturne(source);
  const first = sdk.createTree();
  const second = sdk.createTree();
  const editor = sdk.createEditor("a");
  const bank = sdk.createEditorRegisters(1);
  const foreign = other.createEditorRegisters(1);
  const cause = new Error("disposal fixture");
  const scopeError = new Error("raw caller stopped after removal");
  try {
    editor.move("documentEnd");
    bank.write(0, "β");
    const bindings = [{ chord: { code: 118 }, action: EditorAction.registerPaste }];
    const keys = { registers: bank, bindings };
    assert.throws(() => first.mountEditorView({ editor, keys: { registers: foreign, bindings } }),
      { nativeName: "NT_RESULT_INVALID_ARGUMENT" });
    assert.throws(() => first.mountEditorView({ editor, keys: { registers: bank,
      bindings: [{ ...bindings[0], slot: 9 }] } }), { nativeName: "NT_RESULT_OUT_OF_RANGE" });
    assert.equal(editor.value, "a");
    const pane = first.mountBox("column");
    const view = first.mountEditorView({ editor, keys }, { parent: pane, focusable: true });
    first.mountComponent({
      measure() { return { columns: 1, rows: 1 }; },
      dispose() { throw cause; },
    }, { parent: pane });
    const mirror = second.mountEditorView({ editor, keys }, { focusable: true });
    first.publish({ columns: 12, rows: 3 });
    first.setFocus(view);
    first.withNative(() => {
      assert.throws(() => editor.value, { nativeName: "NT_RESULT_WRONG_PHASE" });
      assert.throws(() => bank.read(0), { nativeName: "NT_RESULT_WRONG_PHASE" });
      assert.throws(() => second.publish({ columns: 12, rows: 2 }), { nativeName: "NT_RESULT_WRONG_PHASE" });
    });
    editor.close();
    bank.close();
    assert.throws(() => editor.value, { nativeName: "NT_RESULT_STALE_REFERENCE" });
    assert.throws(() => bank.read(0), { nativeName: "NT_RESULT_STALE_REFERENCE" });
    assert.equal(first.dispatch(1n, { kind: EventKind.key, key: { code: 118 } }).consumed, true);
    assert.throws(() => first.remove(pane), error =>
      error instanceof CallError && error.callbacks[0].cause === cause);
    second.publish({ columns: 12, rows: 2 });
    assert.equal(second.frame.cell({ x: 1, y: 0 }).glyph, "β");
    assert.throws(() => second.withNative(scope => {
      scope.arena.result("nt_tree_remove", scope.handle(), scope.node(mirror));
      throw scopeError;
    }), error => error === scopeError);
    assert.throws(() => second.editorViewOffset(mirror), { nativeName: "NT_RESULT_STALE_REFERENCE" });
  } finally {
    second.close();
    first.close();
    foreign.close();
    bank.close();
    editor.close();
  }
});
