#!/usr/bin/env python3
"""Drive an installed POSIX host consumer through a real pseudoterminal."""

import fcntl
import json
import os
from pathlib import Path
import pty
import select
import signal
import struct
import subprocess
import sys
import termios
import time


ENABLE = b"\x1b[?1049h\x1b[?25l\x1b[?1004h\x1b[?2004h\x1b[?1002h\x1b[?1006h"
DISABLE = b"\x1b[0m\x1b[?1006l\x1b[?1002l\x1b[?2004l\x1b[?1004l\x1b[?25h\x1b[?1049l"
PRESS = b"\x1b[<0;2;2M"


class Terminal:
    def __init__(self, executable, *options):
        self.shared_output = "alias-output" not in options
        self.master, self.slave = pty.openpty()
        self.set_size(10, 4)
        self.original_termios = termios.tcgetattr(self.slave)
        # Some kernels record whether the open description has written data.
        assert os.write(self.slave, b".") == 1 and os.read(self.master, 1) == b"."
        self.original_flags = fcntl.fcntl(self.slave, fcntl.F_GETFL)
        command_fd, self.commands = os.pipe()
        try:
            self.process = subprocess.Popen(
                [executable, str(command_fd), *options], stdin=self.slave, stdout=self.slave,
                stderr=subprocess.PIPE, pass_fds=(command_fd,), start_new_session=True,
            )
        finally:
            os.close(command_fd)
        self.control = bytearray()
        self.display = bytearray()

    def __enter__(self):
        try:
            assert self.read_line() == b"READY"
            self.drain_display()
            assert self.display == ENABLE, self.display
            raw = termios.tcgetattr(self.slave)
            assert not raw[3] & (termios.ICANON | termios.ECHO | termios.ISIG)
            assert not raw[1] & termios.OPOST
            flags = fcntl.fcntl(self.slave, fcntl.F_GETFL)
            assert bool(flags & os.O_NONBLOCK) == self.shared_output
            return self
        except BaseException:
            self.__exit__(*sys.exc_info())
            raise

    def __exit__(self, *_):
        if self.process.poll() is None:
            self.process.kill()
            self.process.wait(timeout=3)
        self.process.stderr.close()
        os.close(self.commands)
        os.close(self.slave)
        if self.master is not None:
            os.close(self.master)

    def set_size(self, columns, rows):
        fcntl.ioctl(self.slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, columns, 0, 0))

    def read_line(self):
        deadline = time.monotonic() + 5
        error = self.process.stderr.fileno()
        while b"\n" not in self.control:
            sources = [error] if self.master is None else [error, self.master]
            ready, _, _ = select.select(sources, [], [], max(0, deadline - time.monotonic()))
            if not ready or time.monotonic() >= deadline:
                raise TimeoutError(f"Consumer report deadline expired. Last display bytes: {self.display[-100:]!r}")
            if self.master in ready:
                self.display.extend(os.read(self.master, 65536))
            if error in ready:
                chunk = os.read(error, 65536)
                if not chunk:
                    raise RuntimeError(f"Consumer exited {self.process.poll()}. Report: {self.control!r}")
                self.control.extend(chunk)
        line, _, extra = self.control.partition(b"\n")
        self.control = extra
        return bytes(line)

    def drain_display(self):
        while self.master is not None and select.select([self.master], [], [], 0)[0]:
            data = os.read(self.master, 65536)
            if not data:
                break
            self.display.extend(data)

    def send_input(self, data):
        assert os.write(self.master, data) == len(data)
        assert select.select([self.slave], [], [], 2)[0], f"TTY did not make input {data!r} readable"

    def step(self, now, data=b"", **expected):
        if data:
            self.send_input(data)
        os.write(self.commands, f"s {now}\n".encode())
        return self.expect(**({"result": 0} | expected))

    def expect(self, **expected):
        line = self.read_line()
        try:
            report = json.loads(line)
        except ValueError as exc:
            raise RuntimeError(f"Unexpected consumer report {line!r}") from exc
        for field, value in expected.items():
            assert report[field] == value, (field, value, report)
        return report

    def finish(self, restore=None, controlling=False):
        if restore is None:
            restore = self.master is not None
        if restore:
            self.drain_display()
            assert self.display.endswith(DISABLE), self.display[-100:]
            # Darwin sets PENDIN on return to canonical mode. A byte-count
            # query lets the line discipline reprocess its queue without consuming it.
            fcntl.ioctl(self.slave, termios.FIONREAD, struct.pack("i", 0))
            restored = termios.tcgetattr(self.slave)
            assert restored == self.original_termios, (self.original_termios, restored)
        flags = fcntl.fcntl(self.slave, fcntl.F_GETFL)
        assert flags == self.original_flags, (self.original_flags, flags)
        if controlling:
            os.write(self.commands, b"d")
        assert self.process.wait(timeout=3) == 0
        assert self.process.stderr.read() == b""
        assert not self.control, self.control

    def initialize(self):
        self.step(10, events=0, resized=1, presented=1, columns=10, rows=4, updates=1, resize_events=1)


def normal(executable):
    with Terminal(executable) as tty:
        tty.initialize()
        tty.step(11, b"\xc3", events=0, presented=0, keys=0)
        tty.step(12, b"\xa9", events=1, keys=1, last_key=0xe9, texts=1)
        tty.step(13, b"\x1b[200~a\0\xc3", events=0, pastes=0)
        tty.step(14, b"\xa9\n\x1b[201~", events=1, pastes=1)
        tty.step(15, b"x", events=1, reserved=1, keys=1, texts=1)
        tty.step(16, b"\x1b[8;20;20t", events=0, resized=0, columns=10, rows=4)
        tty.step(17, PRESS, downs=1, capture=1)
        malformed = b"\x1b[<0;0;2M\x1b[?999J\xc0\xaf\x1b[200~\xc0\xaf\x1b[201~"
        tty.step(17, malformed, events=0, keys=1, texts=1, pastes=1, downs=1, capture=1)
        tty.step(18, b"\x1b[<32;100;50M", moves=1, capture=1)
        tty.step(19, b"\x1b[<65;2;2M", wheels=1, capture=1)
        tty.step(20, b"\x1b[<0;100;50m", ups=1, capture=0, cancels=0)
        tty.step(21, b"\x03", signal=0, events=1, reserved=2, keys=1, texts=1)
        tty.step(40, b"\x1b", keys=1, deadline=65)
        tty.step(64, keys=1, deadline=65)
        tty.step(65, keys=2, last_key=0x110000, deadline=0)
        tty.set_size(12, 6)
        tty.step(66, resized=1, presented=1, columns=12, rows=6, resize_events=2, updates=1)
        tty.step(100010, resized=0, presented=1, updates=2)
        tty.step(100011, PRESS, downs=2, capture=1)
        os.write(tty.commands, b"q\n")
        tty.expect(result=0, capture=0, cancels=1, focus_losses=1)
        tty.finish()
        color = b"\x1b[38;2;125;220;80;48;2;15;10;8m"
        assert color + b"A" in tty.display
        assert color + b"B" in tty.display
        assert tty.display.count(b"\x1b[2J") == 3
        print("PASS normal: fragmented UTF-8, paste, exact shortcut reservation, malformed input during capture, "
              "drag and wheel, Escape deadline, ioctl resize, clocked repaint and reverse restoration")


def callback_failure(executable):
    with Terminal(executable) as tty:
        tty.initialize()
        tty.step(11, PRESS, downs=1, capture=1)
        tty.step(12, b"!", result=2, cleanup=0, capture=0, cancels=1, focus_losses=1,
                 input_callbacks=1)
        tty.finish()
        print("PASS callback failure: preserved processing error and restored terminal and signals")


def input_completion(executable):
    with Terminal(executable, "after-input-failure") as tty:
        tty.initialize()
        tty.step(11, "éxé".encode(), events=3, keys=2, texts=2, reserved=1,
                 input_callbacks=3, reserved_callbacks=1)
        tty.step(12, "éé".encode(), result=2, cleanup=0, events=1, keys=3, texts=3,
                 input_callbacks=4, focus_losses=1)
        tty.finish()
        print("PASS input completion: per-event tree access and policy ordering; failure stops the batch and restores tty")

def resize_completion(executable):
    with Terminal(executable, "after-resize-failure") as tty:
        tty.initialize()
        tty.set_size(12, 6)
        tty.step(11, "é".encode(), result=2, cleanup=0, resized=1, columns=12, rows=6,
                 presented=0, resize_events=2, events=0, keys=0, input_callbacks=0, focus_losses=1)
        tty.finish()
        print("PASS resize completion: committed geometry and resize event precede policy, callback failure skips input and restores tty")

def controlling_alias(executable, direction):
    with Terminal(executable, f"alias-{direction}") as tty:
        tty.initialize()
        tty.step(11, "é".encode(), keys=1, texts=1)
        os.write(tty.commands, b"q\n")
        tty.expect(result=0, focus_losses=1)
        tty.finish(controlling=True)
        print(f"PASS controlling tty alias: /dev/tty {direction} and direct slave share ownership")


def signal_exit(executable, number):
    with Terminal(executable) as tty:
        tty.initialize()
        tty.step(11, PRESS, downs=1, capture=1)
        tty.process.send_signal(number)
        tty.step(12, signal=number, cleanup=0, capture=0, cancels=1, focus_losses=1)
        tty.finish()
        print(f"PASS {number.name}: signal observed by explicit step and terminal restored")


def hangup(executable):
    with Terminal(executable) as tty:
        tty.initialize()
        tty.step(11, PRESS, downs=1, capture=1)
        os.close(tty.master)
        tty.master = None
        tty.step(12, result=8, cleanup=8, capture=0, cancels=1, focus_losses=1)
        tty.finish()
        print("PASS transport hangup: terminal I/O failure closed the host and cancelled interaction")


if __name__ == "__main__":
    executable = str(Path(sys.argv[1]).resolve())
    normal(executable)
    controlling_alias(executable, "input")
    controlling_alias(executable, "output")
    callback_failure(executable)
    input_completion(executable)
    resize_completion(executable)
    for number in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP, signal.SIGQUIT):
        signal_exit(executable, number)
    hangup(executable)
