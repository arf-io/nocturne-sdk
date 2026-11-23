import { readFile } from "node:fs/promises";
import { createInterface } from "node:readline";
import { EventKind, KeyState, loadNocturne, writeTerminalRows } from "@arf-io/nocturne-sdk";

const width = 12;
const size = { columns: width, rows: 2 };
const palette = { foreground: [100, 230, 190, 255], background: [24, 32, 46, 255] };

class SignalMeter {
  #level = 4;

  measure() {
    return { columns: width, rows: 1 };
  }

  paint(paint, assigned) {
    for (let x = 0; x < Math.min(width, assigned.columns); x++) {
      paint.glyph({ x, y: 0 }, x < this.#level ? "#" : ".", 1, palette);
    }
  }

  event(event) {
    const key = event.key;
    if (!key || key.state !== KeyState.press) return;
    if (key.code === 43) {
      this.#level = Math.min(width, this.#level + 1);
    } else if (key.code === 45) {
      this.#level = Math.max(0, this.#level - 1);
    } else {
      return;
    }
    event.consume();
    event.invalidate();
  }
}

function render(tree) {
  tree.publish(size);
  writeTerminalRows(tree.frame, row => process.stdout.write(row + "\n"));
}

const wasmFile = new URL(import.meta.resolve("@arf-io/nocturne-sdk/nocturne.wasm"));
const sdk = await loadNocturne(await readFile(wasmFile));
const tree = sdk.createTree();
try {
  const parent = tree.mountBox("column");
  tree.mountLabel("Signal level", { parent }, palette);
  const meter = tree.mountComponent(new SignalMeter(), { parent, focusable: true });
  let now = 0n;
  tree.tick(now);
  render(tree);
  tree.setFocus(meter);
  for await (const line of createInterface({ input: process.stdin, crlfDelay: Infinity })) {
    const key = line[0];
    if (key === "q") break;
    if (key !== "+" && key !== "-") continue;
    now += 50_000n;
    tree.tick(now);
    tree.dispatch(now, { kind: EventKind.key, key: { code: key.codePointAt(0) } });
    render(tree);
  }
} finally {
  tree.close();
}
