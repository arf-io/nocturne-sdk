"""Run the shared component replay against an installed wheel."""

from nocturne import CallError, Component, Easing, Input, NativeError, Repeat, Tree, sample_tween
from nocturne.raw import lib


INK = (230, 240, 255, 255)
BACKGROUND = (16, 24, 32, 255)
PAYLOAD = b"x\0\xc3\xa9"


class Meter(Component):
    def __init__(self):
        self.level = 4
        self.keys = self.updates = self.elapsed = self.disposals = 0
        self.fail_paint = False

    def measure(self, context, limits):
        return 5, 1

    def update(self, info):
        self.updates += 1
        self.elapsed = info.elapsed
        return lib.NT_INVALIDATE_PAINT, info.now + 250 if self.updates == 1 else None

    def paint(self, paint, assigned):
        if self.fail_paint:
            raise ValueError("fixture paint refusal")
        paint.glyph((0, 0), str(self.level), 1, INK, BACKGROUND)
        paint.glyph((1, 0), "界", 2, INK, BACKGROUND)

    def event(self, event):
        if event.key is None or event.key[0] != ord("+"):
            return
        self.keys += 1
        self.level += 1
        event.consume()
        event.invalidate()
        event.emit(41, PAYLOAD)

    def dispose(self):
        self.disposals += 1


def record_frame(tree, stage):
    frame = tree.frame
    label = frame.cell((2, 0))
    meter = frame.cell((2, 1))
    level = frame.cell((0, 1))
    print(stage, *frame.size, *label.lead, label.columns, label.glyph,
          *meter.lead, meter.columns, meter.glyph, level.glyph)


def replay():
    meter = Meter()
    with Tree() as tree:
        parent = tree.mount_box(kind=lib.NT_BOX_LAYOUT_COLUMN)
        tree.mount_label("A👩‍💻", parent=parent, foreground=INK, background=BACKGROUND)
        node = tree.mount(meter, parent=parent, focusable=True, hit_testable=True)
        tree.tick(1000)
        tree.publish(7, 3)
        record_frame(tree, "initial")
        deadline = tree.next_update
        tree.set_focus(node)
        reserved = tree.dispatch(1000, Input.key("+", text="!"), reserved=[(ord("+"), 0)])
        assert meter.keys == 0 and tree.peek_intent() is None
        delivery = tree.dispatch(1000, Input.key("+"))
        intent = tree.peek_intent()
        assert intent.source == node and intent.payload == PAYLOAD
        print("event", int(reserved.reserved), int(reserved.consumed), int(reserved.text_delivered),
              int(delivery.target == node), int(delivery.consumed), int(delivery.text_delivered),
              meter.keys, intent.kind, len(intent.payload))
        tree.pop_intent()
        assert tree.peek_intent() is None
        tree.tick(1250)
        print("clock", deadline, meter.updates, meter.elapsed)
        meter.fail_paint = True
        try:
            tree.publish(5, 2)
        except CallError as failure:
            assert len(failure.callbacks) == 1 and isinstance(failure.callbacks[0].cause, ValueError)
            print("failure", failure.native.code)
        else:
            raise AssertionError("failed paint published")
        record_frame(tree, "held")
        meter.fail_paint = False
        tree.publish(5, 2)
        record_frame(tree, "updated")
        tree.remove(node)
        for _ in range(512):
            next_node = tree.mount(meter, parent=parent, focusable=True, hit_testable=True)
            assert next_node != node
            tree.remove(next_node)
        tree.mount(meter, parent=parent, focusable=True, hit_testable=True)
    print("dispose", meter.disposals)


def sample_animation():
    for elapsed in (0, 100, 433, 1100, 1433, 2100):
        sample = sample_tween(-3.25, 8.5, 1000, elapsed, delay_us=100,
                              easing=Easing.SINE, repeat=Repeat.PING_PONG)
        print("sample", elapsed, format(sample.value, ".17g"), format(sample.progress, ".17g"),
              sample.iteration, int(sample.phase), sample.direction)
    loop = sample_tween(0, 1, 1, 2**64 - 1, repeat=Repeat.LOOP)
    print("iteration", loop.iteration)
    try:
        sample_tween(0, 1, 0, 0, repeat=Repeat.LOOP)
    except NativeError as failure:
        print("invalid", failure.code)
    else:
        raise AssertionError("zero-duration loop accepted")


if __name__ == "__main__":
    replay()
    sample_animation()
