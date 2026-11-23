import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { loadNocturne } from "../dist/index.js";

const source = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));

function pointer(arena, value) {
  const slot = arena.value("pointer");
  arena.setPointer(slot, value);
  return slot;
}

function set(arena, value, type, field, bits) {
  const { offset, size } = arena.api.field(type, field);
  arena.setUnsigned(value + offset, size, BigInt(bits));
}

function get(arena, value, type, field) {
  const { offset, size } = arena.api.field(type, field);
  return arena.unsigned(value + offset, size);
}

test("mounts and configures stock controls alongside a managed component", async () => {
  const sdk = await loadNocturne(source);
  const tree = sdk.createTree();
  const other = sdk.createTree();
  const ink = { foreground: [240, 240, 240, 255], background: [0, 0, 0, 0] };
  try {
    const foreign = other.mountBox("column");
    const parent = tree.mountBox("column");
    tree.mountComponent({
      measure() { return { columns: 1, rows: 1 }; },
      paint(paint) { paint.glyph({ x: 0, y: 0 }, "Ω", 1, ink); },
    }, { parent });
    let escaped;
    const [checkbox, progress] = tree.withNative(scope => {
      escaped = scope;
      const { arena } = scope;
      const handle = scope.handle();
      const parentID = scope.node(parent);
      const options = arena.value("NtMountOptions");
      set(arena, options, "NtMountOptions", "enabled", 1);
      set(arena, options, "NtMountOptions", "focusable", 1);
      set(arena, options, "NtMountOptions", "hit_testable", 1);
      const checkbox = arena.value("NtNodeId");
      arena.result("nt_checkbox_mount", handle, parentID, options, arena.byteView("Archive"),
        pointer(arena, 0), arena.value("_Bool"), pointer(arena, checkbox));
      const bar = arena.value("NtProgressOptions");
      set(arena, bar, "NtProgressOptions", "maximum", 10);
      set(arena, bar, "NtProgressOptions", "value", 5);
      const progress = arena.value("NtNodeId");
      arena.result("nt_progress_mount", handle, parentID, options, bar, pointer(arena, progress));
      assert.throws(() => scope.node(foreign), { nativeName: "NT_RESULT_INVALID_ARGUMENT" });
      assert.throws(() => tree.close(), { nativeName: "NT_RESULT_WRONG_PHASE" });
      return [scope.adopt(checkbox), scope.adopt(progress)];
    });
    assert.throws(() => escaped.handle(), { nativeName: "NT_RESULT_STALE_REFERENCE" });
    tree.publish({ columns: 16, rows: 4 });
    assert.equal(tree.frame.cell({ x: 0, y: 0 }).glyph, "Ω");
    tree.setFocus(checkbox);
    tree.withNative(scope => {
      const { arena } = scope;
      const checked = arena.value("_Bool");
      arena.setUnsigned(checked, 1, 1n);
      arena.result("nt_checkbox_set", scope.handle(), scope.node(checkbox), checked);
      const isChecked = arena.value("_Bool");
      arena.result("nt_checkbox_get", scope.handle(), scope.node(checkbox), pointer(arena, isChecked));
      assert.equal(arena.unsigned(isChecked, 1), 1n);
      const bar = arena.value("NtProgressOptions");
      arena.result("nt_progress_get", scope.handle(), scope.node(progress), pointer(arena, bar));
      assert.equal(get(arena, bar, "NtProgressOptions", "value"), 5n);
    });
    tree.remove(checkbox);
    tree.remove(progress);
  } finally {
    tree.close();
    other.close();
  }
});

test("uses compiled layouts for the raw editor and optional thermal exchange", async () => {
  const sdk = await loadNocturne(source);
  assert.deepEqual(sdk.raw.modules, sdk.availableComponents);
  assert.equal(sdk.raw.constant("NT_CHOICE_NONE"), 0xffffffffn);
  for (const name of sdk.raw.functions) {
    const { result, args } = sdk.raw.signature(name);
    if (result !== "void") assert.ok(sdk.raw.size(result), name);
    for (const arg of args) assert.ok(sdk.raw.size(arg.type), name);
  }
  sdk.raw.withArena(arena => {
    const editor = arena.value("pointer");
    arena.result("nt_editor_create", pointer(arena, 0), arena.byteView("note"), pointer(arena, 0), pointer(arena, editor));
    try {
      arena.result("nt_editor_replace_selection", editor, arena.byteView("☾"));
      const text = arena.call("nt_editor_text", editor);
      const view = arena.call("nt_text_bytes", text);
      const data = Number(get(arena, view, "NtByteView", "data"));
      const count = Number(get(arena, view, "NtByteView", "count"));
      assert.equal(new TextDecoder().decode(arena.read(data, count)), "☾note");
    } finally {
      arena.call("nt_editor_destroy", editor);
    }
    if (!sdk.raw.modules.includes("simulation")) {
      assert.throws(() => arena.call("nt_thermal_exchange"), { nativeName: "NT_RESULT_UNSUPPORTED_CAPABILITY" });
      return;
    }
    const { offset: capacity } = sdk.raw.field("NtThermalStore", "heat_capacity");
    const { offset: energy } = sdk.raw.field("NtThermalStore", "energy");
    const stores = arena.value("NtThermalPair");
    for (const field of ["first", "second"]) {
      const base = stores + sdk.raw.field("NtThermalPair", field).offset;
      arena.setFloat64(base + capacity, 2);
      arena.setFloat64(base + energy, 100);
    }
    const conductance = arena.value("double"), elapsed = arena.value("NtTimeUs");
    arena.setFloat64(conductance, 4);
    arena.setUnsigned(elapsed, 8, 1000000n);
    const result = arena.value("NtThermalPair");
    arena.result("nt_thermal_exchange", pointer(arena, stores), conductance, elapsed, pointer(arena, result));
    assert.equal(arena.float64(result + energy), 100);
  });
});

test("translates a native frame sink callback and expires its borrowed arguments", async () => {
  const sdk = await loadNocturne(source);
  const tree = sdk.createTree();
  const cause = new Error("sink refused");
  const callback = "NtFrameSink.present";
  const instant = 9007199254740993n;
  let received, escaped, fail = false;
  const callbacks = sdk.raw.callbacks({
    [callback](frame) {
      escaped = frame;
      received = frame.arena.unsigned(frame.argument(1), 8);
      if (fail) throw cause;
    },
  });
  const asynchronous = sdk.raw.callbacks({
    async [callback]() { throw cause; },
  });
  try {
    tree.mountLabel("☾");
    tree.publish({ columns: 2, rows: 1 });
    tree.withNative(({ arena, handle }) => {
      const sink = arena.value("NtFrameSink");
      set(arena, sink, "NtFrameSink", "context", callbacks.context);
      set(arena, sink, "NtFrameSink", "present", callbacks.function(callback));
      const frame = arena.call("nt_tree_frame", handle());
      const now = arena.value("NtTimeUs");
      arena.setUnsigned(now, 8, instant);
      arena.result("nt_frame_present", pointer(arena, sink), frame, now);
      assert.equal(received, instant);
      fail = true;
      assert.throws(() => arena.result("nt_frame_present", pointer(arena, sink), frame, now),
        { nativeName: "NT_RESULT_CALLBACK_FAILED" });
      assert.deepEqual(callbacks.takeErrors(), [cause]);
      assert.deepEqual(callbacks.takeErrors(), []);
      set(arena, sink, "NtFrameSink", "context", asynchronous.context);
      assert.throws(() => arena.result("nt_frame_present", pointer(arena, sink), frame, now),
        { nativeName: "NT_RESULT_CALLBACK_FAILED" });
      const rejected = asynchronous.takeErrors();
      assert.equal(rejected.length, 1);
      assert.equal(rejected[0] instanceof TypeError, true);
    });
    await new Promise(resolve => setImmediate(resolve));
    assert.throws(() => escaped.argument(1), { nativeName: "NT_RESULT_STALE_REFERENCE" });
  } finally {
    callbacks.close();
    asynchronous.close();
    tree.close();
  }
});

test("creates a timeline from a forward-declared node and its clip union", async () => {
  const sdk = await loadNocturne(source);
  sdk.raw.withArena(arena => {
    const root = arena.value("NtTimelineNode");
    const clip = root + sdk.raw.field("NtTimelineNode", "data.clip").offset;
    set(arena, clip, "NtTimelineClip", "channel", 1);
    set(arena, clip, "NtTimelineClip", "span_us", 100);
    const tween = clip + sdk.raw.field("NtTimelineClip", "source.tween").offset;
    set(arena, tween, "NtTween", "duration_us", 100);
    arena.setFloat64(tween + sdk.raw.field("NtTween", "to").offset, 20);
    const timeline = arena.value("pointer");
    arena.result("nt_timeline_create", pointer(arena, 0), pointer(arena, root),
      pointer(arena, 0), pointer(arena, timeline));
    try {
      const channel = arena.value("uint64_t"), elapsed = arena.value("NtTimeUs");
      arena.setUnsigned(channel, 8, 1n);
      arena.setUnsigned(elapsed, 8, 25n);
      const out = arena.value("NtTimelineSample");
      arena.result("nt_timeline_sample", timeline, channel, elapsed, pointer(arena, out));
      assert.equal(arena.float64(out + sdk.raw.field("NtTimelineSample", "value").offset), 5);
    } finally {
      arena.call("nt_timeline_destroy", timeline);
    }
  });
});

test("samples a typed timeline with a 64-bit channel and caller time", async () => {
  const sdk = await loadNocturne(source);
  const channel = (1n << 63n) + 17n;
  const timeline = sdk.createTimeline([
    { channel, spanUs: 100n, tween: { from: 0, to: 20, durationUs: 100n } },
    { channel, spanUs: 100n, tween: { from: 20, to: 40, durationUs: 100n } },
  ]);
  try {
    assert.equal(timeline.durationUs, 200n);
    const sample = timeline.sample(channel, 125n);
    assert.equal(sample.value, 25);
    assert.equal(sample.playheadUs, 125n);
    assert.equal(sample.sourceUs, 25n);
  } finally {
    timeline.close();
  }
  assert.throws(() => timeline.sample(channel, 125n), { nativeName: "NT_RESULT_STALE_REFERENCE" });
});

test("keeps the clip sequence when a property getter edits the source list", async () => {
  const sdk = await loadNocturne(source);
  const channel = 1n;
  const clips = [
    { channel, get spanUs() {
      clips.pop();
      return 100n;
    }, tween: { from: 0, to: 20, durationUs: 100n } },
    { channel, spanUs: 100n, tween: { from: 20, to: 40, durationUs: 100n } },
  ];
  const timeline = sdk.createTimeline(clips);
  try {
    assert.equal(clips.length, 1);
    assert.equal(timeline.durationUs, 200n);
    assert.equal(timeline.sample(channel, 125n).value, 25);
  } finally {
    timeline.close();
  }
});
