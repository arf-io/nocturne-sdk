import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { Property, loadNocturne } from "../dist/index.js";

const bytes = await readFile(new URL("../dist/nocturne.wasm", import.meta.url));
const text = value => ({ kind: "text", value });
const red = value => ({ kind: "color", value: [value, 0, 0, 255] });
const at = x => ({ x, y: 0 });
const columns = { columns: 6, rows: 1 };
const high = 1n << 63n;
const max = (1n << 64n) - 1n;
const paper = { foreground: [255, 255, 255, 255], background: [9, 0, 0, 255] };
const expectCode = (fn, code) => assert.throws(fn, { code });

test("owns style declarations and applies priority, specificity and live ancestry atomically", async () => {
  const sdk = await loadNocturne(bytes);
  const foreign = (await loadNocturne(bytes)).createStyleSheet([]);
  const tree = sdk.createTree();
  const group = { role: high };
  const card = { role: max, states: high };
  const selector = { subject: { role: max, require: high }, relation: "parent", ancestor: { role: high } };
  const rules = [
    { property: Property.text, value: text("default") },
    { selector, property: Property.text, value: text("group\0owned") },
    { selector, property: Property.foreground, value: red(21) },
  ];
  const base = sdk.createStyleSheet(rules);
  const late = sdk.createStyleSheet([{ selector, property: Property.foreground, value: red(31) }]);
  const forced = sdk.createStyleSheet([{ property: Property.foreground, override: true, value: red(44) }]);
  const bad = sdk.createStyleSheet([
    { property: Property.text, override: true, value: text("rolled back") },
    { property: Property.value, value: { kind: "unsigned", value: 99n } },
  ]);
  try {
    rules[1].value = text("mutated");
    const subject = { path: { element: card, ancestors: [group] } };
    const retained = sdk.resolveStyle([base], subject, Property.text);
    assert.deepEqual(retained, text("group\0owned"));
    assert.deepEqual(sdk.resolveStyle([base, late], subject, Property.foreground), red(31));
    assert.deepEqual(sdk.resolveStyle([forced, base, late], subject, Property.foreground), red(44));
    assert.equal(sdk.resolveStyle([base], subject, Property.background), undefined);
    expectCode(() => sdk.resolveStyle([base, foreign], subject, Property.foreground), 1);
    expectCode(() => sdk.resolveStyle([Object.create(Object.getPrototypeOf(base))], subject, Property.text), 1);
    expectCode(() => sdk.createStyleSheet([{ selector: { subject: { require: 8n, forbid: 8n } },
      property: Property.text, value: text("invalid") }]), 1);

    const root = tree.mountBox("column");
    const left = tree.mountBox("column", { parent: root });
    const label = tree.mountLabel("initial", { parent: left });
    const counter = tree.mountProgress({ value: 5, maximum: 12, filled: paper, remaining: paper }, { parent: root });
    const annotations = [{ node: left, element: group }, { node: label, element: card }];
    const name = { node: label, id: Property.text };
    tree.applyStyles([base, late], annotations, [name, { node: label, id: Property.foreground }]);
    assert.deepEqual(tree.property(label, Property.foreground), red(31));
    expectCode(() => tree.applyStyles([bad], annotations, [name, { node: counter, id: Property.value }]), 2);
    assert.deepEqual(tree.property(label, Property.text), retained);
    assert.deepEqual(tree.property(counter, Property.value), { kind: "unsigned", value: 5n });
    tree.move(label, root);
    tree.applyStyles([base], annotations, [name]);
    assert.deepEqual(tree.property(label, Property.text), text("default"));
    tree.remove(label);
    expectCode(() => tree.applyStyles([base], annotations, [name]), 5);
    base.close();
    assert.deepEqual(retained, text("group\0owned"));
    expectCode(() => sdk.resolveStyle([late, base], subject, Property.foreground), 5);
    assert.deepEqual(sdk.resolveStyle([late], subject, Property.foreground), red(31));
  } finally {
    tree.close();
    for (const sheet of [foreign, base, late, forced, bad]) sheet.close();
  }
});

test("overlays style sheets on Unicode clusters and mounted paint transactionally", async () => {
  const sdk = await loadNocturne(bytes);
  const source = "Ae\u0301界";
  const layout = sdk.layoutText(source);
  const wide = layout.cluster(2);
  const foreground = { property: Property.foreground, value: red(21) };
  const sheet = sdk.createStyleSheet([
    foreground,
    { selector: { textRegion: high, characters: { first: 0x301, last: 0x301 } },
      property: Property.foreground, value: red(79) },
    { selector: { clusterId: wide.id, bytes: { begin: 4, end: 7 } },
      property: Property.background, value: red(67) },
  ]);
  const fault = sdk.createStyleSheet([{ selector: { clusterId: wide.id },
    property: Property.foreground, value: text("invalid color") }]);
  const frame = sdk.createOffscreenFrame(columns);
  const tree = sdk.createTree();
  const styles = { sheets: [sheet], regions: [{ tag: high, bytes: { begin: 1, end: 4 } }] };
  const paintOptions = { base: { owner: high, color: paper } };
  try {
    assert.deepEqual(sdk.resolveStyle([sheet], { source, text: { begin: 1, end: 4 }, regions: styles.regions },
      Property.foreground), red(79));
    const surface = frame.surface();
    surface.paintStyledText(at(0), layout, paintOptions, styles);
    const cells = () => Array.from({ length: 6 }, (_, i) => frame.cell(at(i)));
    const before = cells();
    assert.deepEqual(before.slice(0, 4).map(cell => [cell.glyph, cell.owner, cell.style.foreground[0],
      cell.style.background[0]]), [
      ["A", high, 21, 9], ["e\u0301", high, 79, 9], ["界", high, 21, 67], ["界", high, 21, 67],
    ]);
    expectCode(() => surface.paintStyledText(at(0), layout, paintOptions, { sheets: [sheet, fault] }), 1);
    expectCode(() => surface.paintStyledText(at(0), layout, paintOptions, {
      sheets: [sheet], regions: [{ tag: 1n, bytes: { begin: 0, end: 8 } }],
    }), 2);
    assert.deepEqual(cells(), before);

    let escaped;
    tree.mountComponent({ paint(paint) {
      escaped = paint;
      paint.paintStyledText(at(0), layout, paintOptions, {
        ...styles,
        get path() {
          expectCode(() => paint.fill({ origin: at(0), size: columns }, [0, 0, 0, 0]), 6);
          return {};
        },
      });
    } });
    tree.publish(columns);
    const drawn = tree.frame.cell(at(1));
    assert.equal(drawn.glyph, "e\u0301");
    assert.equal(drawn.style.foreground[0], 79);
    assert.ok(drawn.owner > 0n && drawn.owner !== high);
    expectCode(() => escaped.paintStyledText(at(0), layout, paintOptions, styles), 5);
    expectCode(() => surface.paintStyledText(at(0), layout, paintOptions, {
      get sheets() { sheet.close(); return [sheet]; },
    }), 5);
    assert.deepEqual(cells(), before);
  } finally {
    tree.close();
    frame.close();
    layout.close();
    sheet.close();
    fault.close();
  }
});
