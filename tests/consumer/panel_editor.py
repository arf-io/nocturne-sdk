#!/usr/bin/env python3
"""Exercise an animated panel and a focused editor on a real POSIX terminal."""

import os
from pathlib import Path
import sys

from posix_host import Terminal


ACCENT = "e\u0301".encode()
ASTRONAUT = "👩‍🚀".encode()
WIDE = "界".encode()
PASTE = b"\x1b[200~" + ACCENT + b"\x1b[201~"


def animate(executable):
    with Terminal(executable) as tty:
        tty.step(0, resized=1, accent=0, astronaut=1, wide=3, z_row=1, running=1)
        tty.drain_display()
        assert all(glyph in tty.display for glyph in (ACCENT, ASTRONAUT, WIDE))

        tty.step(50000, PASTE, events=1, document=ACCENT.hex(), accent=1, astronaut=2,
                 wide=4, z_row=-1, left=1, width=5)
        tty.set_size(5, 4)
        tty.step(160000, b"A", resized=1, columns=5, rows=4, accent=3, astronaut=-1,
                 wide=-1, left=3, width=3, document=(ACCENT + b"A").hex())
        tty.step(170000, b"\x1b[<0;4;1M", capture=1)
        tty.step(180000, b"\x12", interruptions=1, capture=1, running=1,
                 document=(ACCENT + b"A").hex())
        print("PASS moving and resizing panel clipped a split wide glyph while the editor accepted UTF-8")

        tty.set_size(12, 5)
        tty.step(450000, b"\x7f", resized=1, columns=12, rows=5, accent=0, astronaut=1,
                 wide=3, z_row=0, capture=1, left=0, width=6, document=ACCENT.hex())
        tty.step(451000, b"\x12", interruptions=2, capture=1, running=1)
        tty.step(501000, b"\x04", disposals=1, cancels=1, capture=0,
                 accent=-1, astronaut=-1, wide=-1, z_row=-1, updates=8)
        tty.step(3000000, b"\x1a", disposals=1, cancels=1, capture=0, updates=8,
                 document=(ACCENT + b"A").hex())
        os.write(tty.commands, b"q\n")
        assert tty.read_line() == b"DONE"
        tty.finish()
        print("PASS retarget preserved velocity, removal cancelled capture and updates, editor undo retained focus")


def reduced(executable):
    with Terminal(executable, "reduced") as tty:
        tty.step(0, accent=4, astronaut=-1, wide=-1, z_row=-1, left=4, width=2,
                 running=0, updates=1)
        tty.step(50000, PASTE, updates=1, running=0, accent=4, left=4, width=2,
                 document=ACCENT.hex())
        tty.step(160000, b"A", updates=1, running=0, document=(ACCENT + b"A").hex())
        os.write(tty.commands, b"q\n")
        assert tty.read_line() == b"DONE"
        tty.finish()
        print("PASS reduced motion selected the target at onset and editing produced the same bytes")


if __name__ == "__main__":
    target = str(Path(sys.argv[1]).resolve())
    animate(target)
    reduced(target)
