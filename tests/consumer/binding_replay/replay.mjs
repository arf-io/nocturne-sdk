import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { CallError, Easing, EventKind, NativeError, Repeat, loadNocturne } from "@arf-io/nocturne-sdk";

const ink = { foreground: [230, 240, 255, 255], background: [16, 24, 32, 255] };
const payload = new TextEncoder().encode("x\0é");

class Meter {
  level = 4;
  keys = 0;
  updates = 0;
  elapsed = 0n;
  disposals = 0;
  failPaint = false;

  measure() {
    return { columns: 5, rows: 1 };
  }

  update({ now, elapsed }) {
    this.updates++;
    this.elapsed = elapsed;
    return { invalidate: "paint", next: this.updates === 1 ? now + 250n : undefined };
  }

  paint(paint) {
    if (this.failPaint) throw new Error("fixture paint refusal");
    paint.glyph({ x: 0, y: 0 }, String(this.level), 1, ink);
    paint.glyph({ x: 1, y: 0 }, "界", 2, ink);
  }

  event(event) {
    if (event.key?.code !== 43) return;
    this.keys++;
    this.level++;
    event.consume();
    event.invalidate();
    event.emit(41, payload);
  }

  dispose() {
    this.disposals++;
  }
}

function frameRecord(tree, stage) {
  const frame = tree.frame;
  const label = frame.cell({ x: 2, y: 0 });
  const meter = frame.cell({ x: 2, y: 1 });
  const level = frame.cell({ x: 0, y: 1 });
  console.log(stage, frame.size.columns, frame.size.rows, label.lead.x, label.lead.y,
    label.columns, label.glyph, meter.lead.x, meter.lead.y, meter.columns, meter.glyph, level.glyph);
}

function replay(sdk) {
  const meter = new Meter();
  const tree = sdk.createTree();
  try {
    const parent = tree.mountBox("column");
    tree.mountLabel("A👩‍💻", { parent }, ink);
    const options = { parent, focusable: true, hitTestable: true };
    let node = tree.mountComponent(meter, options);
    tree.tick(1000n);
    tree.publish({ columns: 7, rows: 3 });
    frameRecord(tree, "initial");
    const deadline = tree.nextUpdate;
    tree.setFocus(node);

    const key = { kind: EventKind.key, key: { code: 43 } };
    const reserved = tree.dispatch(1000n, { ...key, text: "!" }, { reserved: [key.key] });
    assert.equal(meter.keys, 0);
    assert.equal(tree.peekIntent(), undefined);
    const delivery = tree.dispatch(1000n, key);
    const intent = tree.peekIntent();
    assert.deepEqual(intent.source, node);
    assert.deepEqual(intent.payload, payload);
    console.log("event", Number(reserved.reserved), Number(reserved.consumed), Number(reserved.textDelivered),
      Number(delivery.target?.slot === node.slot && delivery.target?.generation === node.generation),
      Number(delivery.consumed), Number(delivery.textDelivered), meter.keys, intent.kind, intent.payload.length);
    tree.popIntent();
    assert.equal(tree.peekIntent(), undefined);

    tree.tick(1250n);
    console.log("clock", String(deadline), meter.updates, String(meter.elapsed));
    meter.failPaint = true;
    const nextSize = { columns: 5, rows: 2 };
    assert.throws(() => tree.publish(nextSize), failure => {
      assert(failure instanceof CallError);
      assert.equal(failure.callbacks.length, 1);
      assert.equal(failure.callbacks[0].cause.message, "fixture paint refusal");
      console.log("failure", failure.native.code);
      return true;
    });
    frameRecord(tree, "held");
    meter.failPaint = false;
    tree.publish(nextSize);
    frameRecord(tree, "updated");
    tree.remove(node);
    for (let i = 0; i < 512; i++) {
      const next = tree.mountComponent(meter, options);
      assert.notDeepEqual(next, node);
      tree.remove(next);
    }
    tree.mountComponent(meter, options);
  } finally {
    tree.close();
  }
  console.log("dispose", meter.disposals);
}

function sampleAnimation(sdk) {
  const tween = { from: -3.25, to: 8.5, durationUs: 1000n, delayUs: 100n,
    easing: Easing.sine, repeat: Repeat.pingPong };
  for (const elapsed of [0n, 100n, 433n, 1100n, 1433n, 2100n]) {
    const { value, progress, iteration, phase, direction } = sdk.sampleTween(tween, elapsed);
    console.log("sample", String(elapsed), value.toPrecision(17), progress.toPrecision(17),
      String(iteration), phase, direction);
  }
  const loop = { from: 0, to: 1, durationUs: 1n, repeat: Repeat.loop };
  console.log("iteration", String(sdk.sampleTween(loop, 2n ** 64n - 1n).iteration));
  assert.throws(() => sdk.sampleTween({ ...loop, durationUs: 0n }, 0n), failure => {
    assert(failure instanceof NativeError);
    console.log("invalid", failure.code);
    return true;
  });
}

const wasm = new URL(import.meta.resolve("@arf-io/nocturne-sdk/nocturne.wasm"));
const sdk = await loadNocturne(await readFile(wasm));
replay(sdk);
sampleAnimation(sdk);
