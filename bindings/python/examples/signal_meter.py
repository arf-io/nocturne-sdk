"""A keyboard-controlled signal meter built using the installed wheel."""

import sys

from nocturne import Component, Input, Tree
from nocturne.raw import lib


WIDTH = 12
INK = (100, 230, 190, 255)
BACKGROUND = (24, 32, 46, 255)


class SignalMeter(Component):
    def __init__(self):
        self.level = 4

    def measure(self, context, limits):
        return WIDTH, 1

    def paint(self, context, assigned):
        for column in range(min(WIDTH, assigned[0])):
            glyph = "#" if column < self.level else "."
            context.glyph((column, 0), glyph, 1, INK, BACKGROUND)

    def event(self, event):
        key = event.key
        if key is None or key[2] != lib.NT_KEY_PRESS:
            return
        if key[0] == ord("+"):
            self.level = min(WIDTH, self.level + 1)
        elif key[0] == ord("-"):
            self.level = max(0, self.level - 1)
        else:
            return
        event.consume()
        event.invalidate()


def render(tree):
    tree.publish(WIDTH, 2)
    frame = tree.frame
    width, height = frame.size
    for row in range(height):
        column = 0
        line = []
        while column < width:
            cell = frame.cell((column, row))
            line.append(cell.glyph if cell.columns else " ")
            column += max(1, cell.columns)
        print("".join(line), flush=True)


def main():
    with Tree() as tree:
        root = tree.mount_box(kind=lib.NT_BOX_LAYOUT_COLUMN)
        tree.mount_label("Signal level", parent=root, foreground=INK, background=BACKGROUND)
        meter = tree.mount(SignalMeter(), parent=root, focusable=True, hit_testable=False)
        now = 0
        tree.tick(now)
        render(tree)
        tree.set_focus(meter)
        for line in sys.stdin:
            key = line[:1]
            if key == "q":
                break
            if key in ("+", "-"):
                now += 50_000
                tree.tick(now)
                tree.dispatch(now, Input.key(key))
                render(tree)


if __name__ == "__main__":
    main()
