#!/usr/bin/env python3
"""Verify a moving notification and docked panel through the POSIX host."""

import os
from pathlib import Path
import sys

from posix_host import Terminal


def check(executable):
    with Terminal(executable) as tty:
        tty.set_size(42, 12)
        tty.step(0, left=2, dock=34, resized=1, capture=0)
        tty.drain_display()
        assert "é".encode() in tty.display and b"Reserve" in tty.display

        tty.step(1, b"\x1b[<0;6;6M", left=2, focus=1, capture=1, actions=0)
        tty.step(2, b"\x12", left=2, focus=1, capture=1)
        tty.set_size(36, 11)
        tty.step(20002, left=6, dock=28, resized=1, focus=1, capture=1)
        tty.step(20003, b"\x1b[<0;10;6m", left=6, focus=1, capture=0, actions=1)
        print("PASS typed notification action through live pointer capture during window animation and terminal resize")

        tty.step(20004, b"\x1b[<0;10;6M", capture=1, actions=1)
        tty.step(20005, b"\x18", left=-1, focus=2, capture=0, actions=1)
        tty.step(20006, b"\x1b[<0;10;6m", left=-1, focus=2, capture=0, actions=1)
        os.write(tty.commands, b"q\n")
        assert tty.read_line() == b"DONE"
        tty.finish()
        print("PASS removal cancelled capture, focus transferred to the dock, and tty modes were restored")


if __name__ == "__main__":
    check(str(Path(sys.argv[1]).resolve()))
