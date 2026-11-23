import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import {
  CallError, ControlEventKind, ControlIntentKind, DispatchError, Easing, EventKind, loadNocturne, NamedKey, Property,
} from "../dist/index.js";

const source = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));
const size = { columns: 4, rows: 1 };

test("composes and updates typed stock controls", async () => {
  const sdk = await loadNocturne(source);
  const tree = sdk.createTree();
  const ink = { foreground: [255, 255, 255, 255], background: [0, 0, 0, 0] };
  const palette = { normal: ink, focused: ink, engaged: ink };
  try {
    const parent = tree.mountBox("column");
    const options = { parent, focusable: true };
    const button = tree.mountButton("Open ☾", options, palette);
    const checkbox = tree.mountCheckbox("Archive", false, options, palette);
    const progress = { value: 5, maximum: 10, filled: ink, remaining: ink };
    const bar = tree.mountProgress(progress, { parent });
    tree.publish({ columns: 16, rows: 4 });
    tree.setFocus(checkbox);
    tree.dispatch(0n, { kind: EventKind.key, key: { code: 32 } });
    assert.equal(tree.checkbox(checkbox), true);
    tree.setCheckbox(checkbox, false);
    assert.equal(tree.checkbox(checkbox), false);
    tree.setProgress(bar, { ...progress, value: 6 });
    tree.remove(button);
    tree.remove(checkbox);
    assert.throws(() => tree.checkbox(checkbox), { nativeName: "NT_RESULT_STALE_REFERENCE" });
  } finally {
    tree.close();
  }
});

test("drives typed indicators with caller time and reads normalized control state", async () => {
  const sdk = await loadNocturne(source);
  const tree = sdk.createTree();
  try {
    const parent = tree.mountBox("column");
    const options = { parent, focusable: true, hitTestable: true };
    const style = { foreground: [150, 210, 255, 255], background: [2, 3, 4, 255] };
    const blank = { foreground: [0, 0, 0, 0], background: [0, 0, 0, 0] };
    const phaseUs = (1n << 53n) + 1n;
    const cursorOptions = { style, phaseUs, visible: true };
    const cursor = tree.mountCursor(cursorOptions, options);
    const wait = tree.mountWait({ style, phaseUs, ascii: true }, options);
    const barOptions = { content: 10, viewport: 3, offset: 99, vertical: true, thumb: style, track: blank };
    const bar = tree.mountScrollbar(barOptions, options);
    assert.deepEqual(tree.scrollbar(bar), { ...barOptions, offset: 7 });
    tree.setScrollbar(bar, { ...barOptions, offset: 2 });
    const progressOptions = { value: 3, maximum: 9, filled: style, remaining: blank, vertical: false };
    const progress = tree.mountProgress(progressOptions, options);
    assert.deepEqual(tree.progress(progress), progressOptions);
    const button = tree.mountButton("Check", options);

    const frameSize = { columns: 16, rows: 16 };
    const glyph = node => tree.frame.cell(tree.geometry(node).bounds.origin).glyph;
    tree.tick(phaseUs - 1n);
    tree.publish(frameSize);
    assert.equal(glyph(cursor), "|");
    assert.equal(glyph(wait), "|");
    tree.tick(phaseUs);
    tree.publish(frameSize);
    assert.notEqual(glyph(cursor), "|");
    assert.equal(glyph(wait), "/");
    assert.equal(tree.nextUpdate, 2n * phaseUs);
    tree.tick(2n * phaseUs, "reduced");
    tree.publish(frameSize);
    assert.equal(glyph(cursor), "|");
    assert.equal(glyph(wait), "|");
    tree.setCursor(cursor, { ...cursorOptions, visible: false });
    tree.setWait(wait, { style });
    tree.publish(frameSize);
    assert.notEqual(glyph(cursor), "|");
    assert.equal(glyph(wait), "⠋");

    tree.setFocus(bar);
    tree.dispatch(2n * phaseUs + 1n, { kind: EventKind.key, key: { code: NamedKey.down } });
    assert.equal(tree.scrollbar(bar).offset, 3);
    const intent = tree.peekIntent();
    assert.equal(tree.sameNode(intent.source, bar), true);
    assert.deepEqual(sdk.decodeControlIntent(intent), { kind: ControlEventKind.scrollChanged, value: 3n });
    const pointer = { pointer: 1n, button: 1, at: tree.geometry(button).bounds.origin };
    tree.dispatch(2n * phaseUs + 2n, { kind: EventKind.pointerDown, ...pointer, pressed: 1 });
    assert.equal(tree.buttonPressed(button), true);
    tree.dispatch(2n * phaseUs + 3n, { kind: EventKind.pointerUp, ...pointer });
    assert.equal(tree.buttonPressed(button), false);
  } finally {
    tree.close();
  }
});

test("copies choice entries, skips disabled rows and decodes stock-control intents", async () => {
  const sdk = await loadNocturne(source);
  const tree = sdk.createTree();
  try {
    const parent = tree.mountBox("column");
    const options = { parent, focusable: true };
    const label = new TextEncoder().encode("α");
    const choice = tree.mountChoice({ kind: "select", selected: 0, visibleRows: 2,
      entries: [{ label }, { label: "Unavailable", disabled: true }, { label: "β" }] }, options);
    label.fill(0);
    const item = tree.mountItem("Confirm", false, options);
    tree.publish({ columns: 20, rows: 6 });
    assert.equal(tree.frame.cell({ x: 2, y: 0 }).glyph, "α");
    tree.setFocus(choice);
    const press = (now, code) => tree.dispatch(now, { kind: EventKind.key, key: { code } });
    press(1n, NamedKey.enter);
    assert.deepEqual(tree.choice(choice), { selected: 0, expanded: true });
    press(2n, NamedKey.down);
    const intent = tree.peekIntent();
    assert.equal(tree.sameNode(intent.source, choice), true);
    assert.equal(intent.kind, ControlIntentKind);
    assert.deepEqual(sdk.decodeControlIntent(intent), { kind: ControlEventKind.choiceChanged, value: 2n });
    tree.popIntent();
    assert.throws(() => tree.setChoice(choice, 1), { nativeName: "NT_RESULT_INVALID_ARGUMENT" });
    assert.throws(() => tree.configureChoice(choice,
      { kind: "radio", entries: [{ label: new Uint8Array([0xff]) }], selected: 0 }),
    { nativeName: "NT_RESULT_INVALID_ARGUMENT" });
    assert.deepEqual(tree.choice(choice), { selected: 2, expanded: true });
    press(3n, NamedKey.escape);
    tree.setChoice(choice);
    assert.deepEqual(tree.choice(choice), { selected: undefined, expanded: false });
    tree.configureChoice(choice, { kind: "radio", entries: [{ label: "Ready" }], selected: 0 });
    assert.deepEqual(tree.choice(choice), { selected: 0, expanded: false });
    const growing = [{ get label() {
      growing.push({ label: "Later" });
      return "Ready";
    } }];
    tree.configureChoice(choice, { kind: "list", entries: growing, selected: 0 });
    assert.equal(growing.length, 2);
    assert.throws(() => tree.setChoice(choice, 1), { nativeName: "NT_RESULT_OUT_OF_RANGE" });
    tree.setItem(item, true);
    assert.equal(tree.item(item), true);
    assert.equal(tree.peekIntent(), undefined);
    assert.throws(() => sdk.decodeControlIntent({ kind: ControlIntentKind, payload: new Uint8Array([3]) }),
      { nativeName: "NT_RESULT_INVALID_ARGUMENT" });
  } finally {
    tree.close();
  }
});

test("reads published geometry and routes typed tree operations", async () => {
  const sdk = await loadNocturne(source);
  const tree = sdk.createTree();
  const otherTree = sdk.createTree();
  try {
    const root = tree.mountBox("row");
    const left = tree.mountBox("stack", { parent: root });
    const right = tree.mountBox("stack", { parent: root });
    const node = tree.mountLabel("A", { parent: left, focusable: true, hitTestable: true });
    tree.publish({ columns: 4, rows: 1 });
    const geometry = tree.geometry(node);
    assert.equal(tree.sameNode(geometry.parent, left), true);
    assert.equal(geometry.placed, true);
    assert.equal(geometry.enabled, true);
    assert.equal(tree.sameNode(tree.hit(geometry.bounds.origin), node), true);
    assert.equal(tree.hit({ x: -1, y: 0 }), undefined);
    tree.focusAdvance();
    assert.equal(tree.sameNode(tree.focus, node), true);
    const delivery = tree.dispatch(0n, { kind: EventKind.key, key: { code: 65 } });
    assert.equal(tree.sameNode(delivery.target, node), true);
    assert.equal(tree.sameNode(tree.focus, { ...node }), false);
    assert.equal(tree.sameNode(root, otherTree.mountBox("row")), false);
    assert.equal(tree.sameNode(undefined, undefined), false);
    tree.move(node, right);
    tree.invalidate(node, "paint");
    tree.publish({ columns: 4, rows: 1 });
    assert.equal(tree.sameNode(tree.geometry(node).parent, right), true);
    tree.remove(node);
    const replacement = tree.mountLabel("B", { parent: right });
    assert.equal(tree.sameNode(node, replacement), false);
  } finally {
    otherTree.close();
    tree.close();
  }
});

test("composes a typed panel and publishes clamped viewport scrolling", async () => {
  const tree = (await loadNocturne(source)).createTree();
  try {
    const panel = tree.mountPanel({ title: "View", framed: true, ascii: true });
    const view = tree.mountViewport({ scrollColumns: true }, { parent: panel });
    const label = tree.mountLabel("abcdef", { parent: view });
    tree.publish({ columns: 5, rows: 3 });
    tree.setViewport(view, { scrollColumns: true, requested: { columns: 4, rows: 0 } });
    assert.deepEqual(tree.viewport(view).child, label);
    assert.equal(tree.viewport(view).effective.columns, 0);
    tree.publish({ columns: 5, rows: 3 });
    const published = tree.viewport(view);
    assert.equal(published.viewport.columns, 3);
    assert.equal(published.content.columns, 6);
    assert.equal(published.effective.columns, 3);
  } finally {
    tree.close();
  }
});

test("reads and writes typed stock properties with native validation", async () => {
  const tree = (await loadNocturne(source)).createTree();
  try {
    const parent = tree.mountBox("column");
    const label = tree.mountLabel("initial", { parent });
    const checkbox = tree.mountCheckbox("Done", false, { parent });
    const text = { kind: "text", value: "☾\0notes" };
    tree.setProperty(label, Property.text, text);
    assert.deepEqual(tree.property(label, Property.text), text);
    const on = { kind: "bool", value: true };
    tree.setProperty(checkbox, Property.checked, on);
    assert.throws(() => tree.setProperty(checkbox, Property.checked, text));
    assert.throws(() => tree.setProperty(checkbox, Property.checked, { kind: "bool", value: 2 }), TypeError);
    assert.deepEqual(tree.property(checkbox, Property.checked), on);
  } finally {
    tree.close();
  }
});

test("composes a typed window and publishes caller-driven placement animation", async () => {
  const tree = (await loadNocturne(source)).createTree();
  const size = { columns: 20, rows: 8 };
  try {
    const workspace = tree.mountWindowWorkspace(size);
    const floating = { origin: { x: 0, y: 0 }, size: { columns: 12, rows: 5 } };
    const window = tree.mountWindow({ placement: { floating }, title: "Notes" }, { parent: workspace });
    tree.mountLabel("Status", { parent: window });
    tree.publish(size);
    const destination = { ...floating, origin: { x: 4, y: 2 } };
    tree.animateWindow(window, { destination, startUs: 0n, durationUs: 100n });
    tree.tick(50n);
    tree.publish(size);
    const halfway = tree.window(window);
    assert.equal(halfway.animating, true);
    assert.deepEqual(halfway.published.parent, workspace);
    assert.deepEqual(halfway.published.bounds.origin, { x: 2, y: 1 });
    tree.tick(100n);
    tree.publish(size);
    const settled = tree.window(window);
    assert.equal(settled.animating, false);
    assert.deepEqual(settled.published.bounds, destination);
  } finally {
    tree.close();
  }
});

test("publishes typed plot options and caller-timed particle recipes", async () => {
  const tree = (await loadNocturne(source)).createTree();
  try {
    const parent = tree.mountBox("column");
    const plot = { kind: "line", data: [{ x: 0, y: 0 }, { x: 1, y: 2 }],
      ascii: true, preferred: { columns: 8, rows: 3 } };
    const chart = tree.mountPlot(plot, { parent });
    plot.data[0].y = NaN;
    assert.throws(() => tree.setPlot(chart, plot), { nativeName: "NT_RESULT_INVALID_ARGUMENT" });
    const preferred = { columns: 8, rows: 2 };
    const effect = tree.mountParticleEffect({ recipe: "snow", seed: 9n, startUs: 1000n, preferred }, { parent });
    tree.tick(1000n);
    tree.publish({ columns: 8, rows: 6 });
    assert.equal(tree.geometry(chart).placed, true);
    assert.equal(tree.geometry(effect).placed, true);
    tree.setParticleEffect(effect, { recipe: "rain", startUs: 1050n, preferred });
    tree.remove(effect);
  } finally {
    tree.close();
  }
});

test("projects and clips plots, reveals sampled data and displays exact fixed-point digits", async () => {
  const sdk = await loadNocturne(source);
  const domain = { x: { min: 0, max: 2 }, y: { min: 0, max: 2 } };
  const cells = { origin: { x: 2, y: 3 }, size: { columns: 8, rows: 4 } };
  assert.deepEqual(sdk.projectPlot({ domain, cells }, { x: 1, y: 1 }), { x: 6, y: 5 });
  assert.deepEqual(sdk.clipPlotSegment(domain, { first: { x: -1, y: 1 }, last: { x: 3, y: 1 } }),
    { first: { x: 0, y: 1 }, last: { x: 2, y: 1 } });
  assert.equal(sdk.clipPlotSegment(domain, { first: { x: 0, y: 3 }, last: { x: 2, y: 3 } }), undefined);

  const tree = sdk.createTree();
  try {
    const startUs = (1n << 53n) + 1n;
    const preferred = { columns: 12, rows: 4 };
    const options = {
      kind: "scatter", sampling: "braille", domain, preferred, dataLimit: 3,
      data: [{ x: .25, y: .25 }, { x: 1, y: 1 }, { x: 1.75, y: 1.75 }],
      reveal: { startUs, durationUs: 120n, easing: Easing.smooth },
    };
    const plot = tree.mountPlot(options);
    const brailleCount = () => {
      tree.publish(preferred);
      let count = 0;
      for (let y = 0; y < preferred.rows; y++) {
        for (let x = 0; x < preferred.columns; x++) {
          const glyph = tree.frame.cell({ x, y }).glyph.codePointAt(0) ?? 0;
          if (glyph > 0x2800 && glyph <= 0x28ff) count++;
        }
      }
      return count;
    };
    tree.tick(startUs - 1n);
    assert.equal(tree.nextUpdate, startUs);
    assert.equal(brailleCount(), 0);
    tree.tick(startUs + 120n);
    assert.equal(brailleCount(), 3);
    const strided = { ...options, sampleStride: 2 };
    tree.setPlot(plot, strided);
    assert.equal(brailleCount(), 2);
    assert.throws(() => tree.setPlot(plot, { ...strided, dataLimit: 2 }),
      { nativeName: "NT_RESULT_OUT_OF_RANGE" });
    assert.equal(brailleCount(), 2);
    const growing = [{ get x() {
      growing.push({ x: 1.75, y: 1.75 });
      return .25;
    }, y: .25 }];
    tree.setPlot(plot, { ...options, data: growing, dataLimit: 1 });
    assert.equal(growing.length, 2);
    assert.equal(brailleCount(), 1);

    tree.remove(plot);
    const value = -(1n << 63n);
    const digits = tree.mountDigits({ value, fractionalDigits: 18, ascii: true });
    const displayed = () => tree.property(digits, Property.value);
    assert.deepEqual(displayed(), { kind: "signed", value });
    assert.throws(() => tree.setDigits(digits, { value: 17n, fractionalDigits: 19 }),
      { nativeName: "NT_RESULT_OUT_OF_RANGE" });
    assert.throws(() => tree.setDigits(digits, { value: 17n, fractionalDigits: 256 }), RangeError);
    assert.throws(() => tree.setDigits(digits, { value: 1n << 63n }), RangeError);
    assert.deepEqual(displayed(), { kind: "signed", value });
    tree.setDigits(digits, { value: 17n, fractionalDigits: 2, ascii: true });
    assert.deepEqual(displayed(), { kind: "signed", value: 17n });
  } finally {
    tree.close();
  }
});

test("composes native controls and borrows one published cell at a time", async () => {
  const sdk = await loadNocturne(source);
  const tree = sdk.createTree();
  const other = sdk.createTree();
  try {
    assert.equal(tree.frame, undefined);
    const parent = tree.mountBox("column");
    const style = { foreground: [23, 101, 208, 255], background: [1, 2, 3, 255] };
    const node = tree.mountLabel("🌙", { parent }, style);
    tree.publish(size);
    const frame = tree.frame;
    assert.deepEqual(frame.size, size);
    const cell = frame.cell({ x: 1, y: 0 });
    assert.equal(cell.glyph, "🌙");
    assert.deepEqual(cell.lead, { x: 0, y: 0 });
    assert.equal(cell.columns, 2);
    assert.deepEqual(cell.style, style);
    assert.throws(() => other.setLabel(node, "cross-tree"), { nativeName: "NT_RESULT_INVALID_ARGUMENT" });
    tree.setLabel(node, "Q", style);
    tree.publish(size);
    assert.throws(() => frame.cell({ x: 0, y: 0 }), { nativeName: "NT_RESULT_STALE_REFERENCE" });
    assert.equal(tree.frame.cell({ x: 0, y: 0 }).glyph, "Q");
    tree.remove(node);
    assert.throws(() => tree.setLabel(node, "removed"), { nativeName: "NT_RESULT_STALE_REFERENCE" });
  } finally {
    tree.close();
    other.close();
  }
  tree.close();
  assert.throws(() => tree.frame, { nativeName: "NT_RESULT_STALE_REFERENCE" });
});

test("refuses reentrant modification while reading host arguments", async () => {
  const tree = (await loadNocturne(source)).createTree();
  try {
    assert.throws(() => tree.mountBox("stack", { get parent() {
      tree.close();
    } }), { nativeName: "NT_RESULT_WRONG_PHASE" });
    tree.mountLabel("Q");
    tree.publish(size);
    const frame = tree.frame;
    assert.throws(() => frame.cell({ get x() {
      tree.close();
      return 0;
    }, y: 0 }), { nativeName: "NT_RESULT_WRONG_PHASE" });
    assert.equal(frame.cell({ x: 0, y: 0 }).glyph, "Q");
  } finally {
    tree.close();
  }
});

test("runs custom callbacks with scoped contexts and reports native notifications", async () => {
  const tree = (await loadNocturne(source)).createTree();
  const style = { foreground: [230, 240, 250, 255], background: [0, 0, 0, 0] };
  const focusError = new Error("focus callback failed");
  let retainedPaint;
  let updates = 0;
  let disposals = 0;
  const component = {
    measure() {
      return { columns: 2, rows: 1 };
    },
    update(info) {
      assert.equal(info.now, 1000n);
      updates++;
      return { invalidate: "paint", next: info.now + 250n };
    },
    paint(paint) {
      retainedPaint = paint;
      paint.glyph({ x: 0, y: 0 }, "👩‍💻", 2, style);
    },
    event(event) {
      if (event.kind === EventKind.focusGain) throw focusError;
    },
    dispose() {
      disposals++;
    },
  };
  try {
    const parent = tree.mountBox("stack");
    const node = tree.mountComponent(component, { parent, focusable: true });
    tree.tick(1000n);
    assert.equal(tree.nextUpdate, 1250n);
    tree.publish(size);
    assert.equal(updates, 1);
    assert.equal(tree.frame.cell({ x: 1, y: 0 }).glyph, "👩‍💻");
    assert.throws(() => retainedPaint.glyph({ x: 0, y: 0 }, "!", 1, style),
      { nativeName: "NT_RESULT_STALE_REFERENCE" });
    assert.throws(() => tree.setFocus(node), error =>
      error instanceof CallError && error.native === null &&
      error.callbacks[0].phase === "event" && error.callbacks[0].cause === focusError);
    tree.setFocus(node);
    tree.remove(node);
    for (let index = 0; index < 512; index++) tree.remove(tree.mountComponent(component, { parent }));
    tree.mountComponent(component, { parent });
  } finally {
    tree.close();
  }
  assert.equal(disposals, 514);
});

test("dispatches normalized input and retains partial delivery on callback failure", async () => {
  const tree = (await loadNocturne(source)).createTree();
  let retainedEvent;
  const signal = {
    measure() { return { columns: 2, rows: 1 }; },
    event(event) {
      retainedEvent = event;
      if (event.key?.code === 33) {
        event.emit(10, "discard");
        throw new Error("rejected key");
      }
      if (event.kind === EventKind.pointerDown) {
        assert.deepEqual(event.localPosition, { x: 0, y: 0 });
        event.capture("take");
      }
      if (event.kind === EventKind.paste) {
        event.emit(11, event.text);
        event.consume();
      }
    },
  };
  try {
    const node = tree.mountComponent(signal, { focusable: true, hitTestable: true });
    tree.publish(size);
    tree.setFocus(node);
    assert.deepEqual(tree.focus, node);
    const key = { kind: EventKind.key, key: { code: 33 }, text: "!" };
    assert.throws(() => tree.dispatch(1n, key), error => error instanceof DispatchError &&
      error.delivery.admitted && error.delivery.target.generation === node.generation &&
      error.cause.native.nativeName === "NT_RESULT_CALLBACK_FAILED");
    assert.equal(tree.peekIntent(), undefined);
    assert.equal(tree.dispatch(1n, key, { reserved: [{ code: 33 }] }).reserved, true);
    const pointer = { pointer: 7n, at: { x: 0, y: 0 }, button: 1, pressed: 1 };
    assert.throws(() => tree.dispatch(1n, { kind: EventKind.pointerUp, ...pointer }),
      error => error instanceof DispatchError && !error.delivery.admitted);
    tree.dispatch(1n, { kind: EventKind.pointerDown, ...pointer });
    assert.deepEqual(tree.capture(7n), node);
    tree.dispatch(1n, { kind: EventKind.pointerCancel, pointer: 7n });
    assert.equal(tree.capture(7n), undefined);
    const text = "x\0👩‍💻";
    assert.equal(tree.dispatch(1n, { kind: EventKind.paste, text }).consumed, true);
    const intent = tree.peekIntent();
    assert.equal(intent.kind, 11);
    assert.deepEqual(intent.payload, new TextEncoder().encode(text));
    tree.popIntent();
    assert.equal(tree.peekIntent(), undefined);
    assert.throws(() => retainedEvent.consume(), { nativeName: "NT_RESULT_STALE_REFERENCE" });
  } finally {
    tree.close();
  }
});
