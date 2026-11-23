#!/usr/bin/env python3
"""Drive the Aurora Survey consumer through a real terminal."""

import os
from pathlib import Path
import sys

from posix_host import Terminal


TEXT = "e\u0301界".encode()
PASTE = b"\x1b[200~" + TEXT + b"\x1b[201~"


def finish(tty):
    os.write(tty.commands, b"q\n")
    assert tty.read_line() == b"DONE"
    tty.finish()


def full(executable):
    with Terminal(executable) as tty:
        tty.set_size(72, 18)
        first = tty.step(0, resized=1, focus=1, left=2, falling=1, piles=0,
                         releases=0, title_row=2, document="")
        assert first["graph"] > 0 and first["plot"] > 0, first
        tty.drain_display()
        assert all(glyph in tty.display for glyph in (b"Aube", "界".encode(), b"Survey notes"))

        tty.step(1000, b"\x12", focus=1, left=2)
        tty.set_size(60, 16)
        moving = tty.step(41000, PASTE, resized=1, columns=60, rows=16, focus=1,
                          document=TEXT.hex(), falling=1, title_row=1)
        assert 2 < moving["left"] < 11, moving
        assert moving["graph"] > 0 and moving["plot"] > 0, moving
        print("PASS Unicode text moved and UTF-8 paste reached the focused editor during panel animation and resize")

        tty.step(100000, b"\x07", left=11, focus=2, falling=0, piles=1, mass=4, releases=0)
        tty.step(120000, b"\x1b[C", focus=2, selected=1, selections=1, piles=1, mass=4)
        tty.step(180000, b"\x1b[C", focus=2, selected=2, selections=2, piles=1, title_row=0)
        tty.step(220000, b"\x05", focus=1, piles=0, releases=1, mass=0, document=TEXT.hex())
        active = tty.step(260000, b"!", focus=1, document=(TEXT+b"!").hex(), releases=1)
        assert active["graph"] > 0 and active["plot"] > 0, active
        print("PASS graph selection and plots stayed live while two falling particles formed a pile and re-emitted")

        gone = tty.step(270000, b"\x04", focus=1, removals=1, piles=0, releases=0, title_row=-1, next=0)
        tty.step(900000, b"Z", focus=1, removals=1, updates=gone["updates"], next=0,
                 document=(TEXT+b"!Z").hex())
        finish(tty)
        print("PASS removing the weather panel retired its work and kept editor focus and terminal restoration")


def reduced(executable):
    with Terminal(executable, "reduced") as tty:
        tty.set_size(72, 18)
        tty.step(0, falling=1, title_row=0, focus=1)
        tty.step(1000, b"\x12", focus=1)
        tty.step(41000, PASTE, left=11, falling=1, title_row=0, focus=1, document=TEXT.hex())
        tty.step(100000, falling=0, piles=1, mass=4, title_row=0, focus=1)
        tty.step(220000, piles=0, releases=1, title_row=0)
        tty.step(340000, releases=0, next=0, title_row=0, document=TEXT.hex())
        finish(tty)
        print("PASS reduced motion settled the panel and text while preserving pile and release lifetimes")


def resize_focus(executable):
    for motion in ((), ("reduced",)):
        for owner in (1, 2):
            with Terminal(executable, *motion) as tty:
                tty.set_size(72, 18)
                tty.step(0, focus=1)
                tty.step(1000, b"a", focus=1, document="61")
                if owner == 2:
                    tty.step(2000, b"\x07", focus=2)
                tty.step(150000, focus=owner, piles=1, mass=4)

                tty.set_size(1, 1)
                tty.step(160000, b"\x05\x07", columns=1, rows=1, focus=0)
                tty.set_size(72, 18)
                if owner == 1:
                    shown = tty.step(250000, b"b", columns=72, rows=18, focus=1, releases=1, document="6162")
                else:
                    shown = tty.step(250000, b"\x1b[C", columns=72, rows=18, focus=2, releases=1,
                                     selected=1, selections=1, document="61")
                assert shown["graph"] and shown["plot"], shown
                tty.step(350000, focus=owner, releases=0, next=0)
                finish(tty)
    print("PASS both motion modes routed editor and graph input in the same batch that restored terminal size")


if __name__ == "__main__":
    target = str(Path(sys.argv[1]).resolve())
    full(target)
    reduced(target)
    resize_focus(target)
