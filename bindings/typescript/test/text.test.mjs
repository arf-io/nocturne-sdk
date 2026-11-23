import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { EditorAction, KeyModifier, KeyState, loadNocturne, NamedKey, NativeError } from "../dist/index.js";

const source = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));

test("edits complete graphemes with a typed editor and reversible history", async () => {
  const sdk = await loadNocturne(source);
  const initial = "👩‍💻Z";
  const editor = sdk.createEditor(initial, { singleLine: true });
  try {
    editor.setUndoLimit(2);
    editor.move("nextCluster", true);
    assert.equal(editor.selection.active.offset, new TextEncoder().encode("👩‍💻").length);
    editor.replaceSelection("☾");
    assert.throws(() => editor.replaceSelection("\n"), { nativeName: "NT_RESULT_INVALID_ARGUMENT" });
    assert.equal(editor.value, "☾Z");
    assert.equal(editor.undo(), true);
    assert.equal(editor.value, initial);
    assert.equal(editor.redo(), true);
    assert.equal(editor.value, "☾Z");
  } finally {
    editor.close();
  }
  assert.throws(() => editor.value, { nativeName: "NT_RESULT_STALE_REFERENCE" });
});

test("restores the draft and maps editor keys through an owned register bank", async () => {
  const sdk = await loadNocturne(source);
  const other = await loadNocturne(source);
  const draft = "draft 👩‍💻";
  const editor = sdk.createEditor(draft, { singleLine: true });
  const bank = sdk.createEditorRegisters(2);
  const foreign = other.createEditorRegisters(2);
  try {
    assert.equal(typeof bank.withBorrow, "undefined");
    const selection = { anchor: { offset: 0, affinity: "leading" }, active: { offset: 5, affinity: "trailing" } };
    editor.setSelection(selection);
    assert.equal(editor.selectedText, "draft");
    assert.throws(() => editor.addHistory("disabled"), { nativeName: "NT_RESULT_UNSUPPORTED_CAPABILITY" });
    editor.setHistoryLimit(2);
    for (const value of ["discarded", "", "命令"]) editor.addHistory(value);
    assert.equal(editor.historyCount, 2);
    assert.equal(editor.historyEntry(0), "命令");
    assert.equal(editor.historyEntry(1), "");
    assert.throws(() => editor.addHistory("invalid\n"), { nativeName: "NT_RESULT_INVALID_ARGUMENT" });
    for (const [older, value] of [[true, "命令"], [true, ""], [false, "命令"], [false, draft]]) {
      assert.equal(editor.stepHistory(older), true);
      assert.equal(editor.value, value);
    }
    assert.deepEqual(editor.selection, selection);

    assert.deepEqual(bank.read(0), { text: "", occupied: false });
    bank.write(0, "");
    assert.deepEqual(bank.read(0), { text: "", occupied: true });
    const copy = { code: 99, modifiers: KeyModifier.control };
    const paste = { code: 118, modifiers: KeyModifier.control };
    const undo = { code: 122, modifiers: KeyModifier.control };
    const redo = { code: NamedKey.f12, modifiers: KeyModifier.shift | KeyModifier.control };
    const bindings = [
      { chord: copy, action: EditorAction.registerCopy, slot: 1 },
      { chord: paste, action: EditorAction.registerPaste, slot: 1 },
      { chord: undo, action: EditorAction.undo },
      { chord: redo, action: EditorAction.redo },
    ];
    const map = { bindings, registers: bank };
    assert.equal(editor.handleKey(copy), false);
    assert.equal(editor.handleKey({ ...copy, state: KeyState.repeat }, map), false);
    assert.throws(() => editor.handleKey(copy, { bindings, registers: foreign }),
      { nativeName: "NT_RESULT_INVALID_ARGUMENT" });
    assert.throws(() => editor.handleKey(copy, { registers: bank, get bindings() {
      bank.close();
      return bindings;
    } }), { nativeName: "NT_RESULT_WRONG_PHASE" });
    assert.equal(editor.handleKey(copy, map), true);
    assert.deepEqual(bank.read(1), { text: "draft", occupied: true });
    editor.setUndoLimit(2);
    editor.move("documentEnd");
    assert.equal(editor.handleKey(paste, map), true);
    assert.equal(editor.value, draft + "draft");
    assert.deepEqual(editor.undoState, { undoCount: 1, redoCount: 0 });
    assert.equal(editor.handleKey(undo, map), true);
    assert.equal(editor.value, draft);
    assert.deepEqual(editor.undoState, { undoCount: 0, redoCount: 1 });
    assert.equal(editor.handleKey(redo, map), true);
    assert.equal(editor.value, draft + "draft");
    bank.close();
    assert.throws(() => editor.handleKey(copy, map), { nativeName: "NT_RESULT_STALE_REFERENCE" });
    assert.throws(() => bank.read(1), { nativeName: "NT_RESULT_STALE_REFERENCE" });
  } finally {
    foreign.close();
    bank.close();
    editor.close();
  }
});

test("owns counted UTF-8 and preserves grapheme identity", async () => {
  const sdk = await loadNocturne(source);
  assert.equal(sdk.abiVersion, 1);
  const text = sdk.createText("x\0👩‍💻");
  try {
    assert.equal(text.value, "x\0👩‍💻");
    assert.equal(text.count, 3);
    const cluster = text.cluster(2);
    assert.deepEqual([cluster.begin, cluster.end], [2, 13]);
    assert.ok(cluster.id > 0n);
    assert.throws(() => text.replace(3, 4, "?"), { nativeName: "NT_RESULT_INVALID_ARGUMENT" });
    text.replace(0, 1, "!");
    assert.deepEqual(text.cluster(2), cluster);
    assert.equal(text.value, "!\0👩‍💻");
  } finally {
    text.close();
  }
  text.close();
  assert.throws(() => text.value, { nativeName: "NT_RESULT_STALE_REFERENCE" });
});

test("rejects malformed bytes and accepts independent module instances", async () => {
  const module = await WebAssembly.compile(source);
  const a = await loadNocturne(module);
  const b = await loadNocturne(module);
  assert.throws(() => a.createText(new Uint8Array([0xc0, 0x80])),
    error => error instanceof NativeError && error.nativeName === "NT_RESULT_INVALID_ARGUMENT");
  assert.throws(() => a.createText("x\ud800"), { nativeName: "NT_RESULT_INVALID_ARGUMENT" });
  const text = a.createText("");
  const other = b.createText("\ufeffseparate");
  try {
    assert.equal(text.value, "");
    assert.equal(other.value, "\ufeffseparate");
  } finally {
    text.close();
    other.close();
  }
});
