#!/usr/bin/env python3
"""Verify an installed consumer's suggested settings and rendered fallbacks."""

import errno
import fcntl
import os
from pathlib import Path
import pty
import re
import select
import shlex
import struct
import subprocess
import sys
import tempfile
import termios
import time


UNICODE = "café 界 ⠿ ▀|"
ASCII = "caf? ?? ? ?|"
BASIC_UNICODE = "café 界 ? ?|"
ANSI = re.compile(rb"\x1b\[[?0-9;]*[HJhlm]")
ROW = re.compile(rb"\x1b\[[0-9]+;1H")
RGB = b"38;2;95;175;215"
INDEX = b"38;5;74"


def environment(term="xterm-256color", color="", locale="C.UTF-8", no_color=""):
    return dict(os.environ, TERM=term, COLORTERM=color, LC_ALL=locale, LC_CTYPE="",
                LANG="C.UTF-8", NO_COLOR=no_color)


def pty_capture(executable, env, *args, input_tty=True):
    master, slave = pty.openpty()
    try:
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 3, 24, 0, 0))
        completed = subprocess.run([executable, *args], env=env, stdout=slave,
                                   stdin=slave if input_tty else subprocess.DEVNULL,
                                   stderr=subprocess.PIPE, timeout=8, check=True)
        display = bytearray()
        while select.select([master], [], [], 0)[0]:
            try:
                chunk = os.read(master, 65536)
            except OSError as failure:
                if failure.errno != errno.EIO:
                    raise
                break
            if not chunk:
                break
            display.extend(chunk)
        return bytes(display), completed.stderr
    finally:
        os.close(master)
        os.close(slave)


def check_report(raw, expected):
    report = tuple(map(int, raw.split()))
    assert report == expected, (expected, report)


def check_rows(raw, expected, *, active=False):
    rows = [line.rstrip() for line in raw.decode().splitlines() if line.strip()]
    assert rows == ["Queue active" if active else "Queue ready", expected], rows


def check_paint(raw, mode, glyphs, *, active=False):
    if mode == "plain":
        assert b"\x1b" not in raw, raw
        check_rows(raw, glyphs, active=active)
        return
    assert b"\x1b[2J" in raw, raw
    if mode == "truecolor":
        assert RGB in raw and INDEX not in raw, raw
    elif mode == "indexed":
        assert INDEX in raw and RGB not in raw, raw
    else:
        assert b"38;" not in raw and b"48;" not in raw, raw
    cleared = ANSI.sub(b"", ROW.sub(b"\n", raw))
    assert b"\x1b" not in cleared, raw
    check_rows(cleared, glyphs, active=active)


def capture_profiles(executable):
    cases = (
        ("truecolor", environment(color="truecolor"), (1,1,1,1,2,1,1,1,1,1), UNICODE, (), True),
        ("indexed", environment(), (1,1,1,1,1,1,1,1,1,1), UNICODE, (), True),
        ("mono", environment(color="truecolor", no_color="0"), (1,1,1,1,0,1,1,1,1,1), UNICODE, (), True),
        ("mono", environment(term="vt100", locale="C"), (1,1,1,1,0,0,0,0,0,0), ASCII, ("full",), True),
        ("mono", environment(term="vt100", locale="C@variant.UTF-8"), (1,1,1,1,0,0,0,0,0,0), ASCII, (), True),
        ("mono", environment(term="vt100"), (1,1,1,1,0,1,0,0,0,0), BASIC_UNICODE, (), True),
        ("plain", environment(term="dumb"), (1,1,0,0,0,1,1,1,0,0), UNICODE, (), True),
        ("plain", environment(term="custom-256color", color="truecolor"), (1,1,0,0,0,1,1,1,0,0), UNICODE, (), True),
        ("truecolor", environment(term="dumb", locale="C", no_color="1"), (1,1,1,1,2,1,1,1,1,1),
         UNICODE, ("hints",), True),
        ("indexed", environment(), (0,1,0,1,1,1,1,1,0,0), UNICODE, (), False),
    )
    for mode, env, report, glyphs, args, input_tty in cases:
        output, diagnostics = pty_capture(executable, env, *args, input_tty=input_tty)
        check_report(diagnostics, report)
        check_paint(output, mode, glyphs, active=args == ("full",))
        print(f"PASS PTY {mode} TERM={env['TERM']} arguments={args} input_tty={input_tty} {glyphs}")

    pipe = subprocess.run([executable], env=environment(color="truecolor"), stdin=subprocess.DEVNULL,
                          capture_output=True, timeout=8, check=True)
    check_report(pipe.stderr, (0,0,0,0,0,1,1,1,0,0))
    check_paint(pipe.stdout, "plain", UNICODE)
    print("PASS redirected pipe: readable Unicode rows without terminal controls")


def tmux_profiles(executable, tmux):
    with tempfile.TemporaryDirectory(prefix="nocturne-m5-terminal-", dir="/tmp") as temporary:
        directory = Path(temporary)
        server = (tmux, "-u", "-S", str(directory / "server"), "-f", "/dev/null")
        env = dict(os.environ)
        env.pop("TMUX", None)

        def run(*arguments):
            return subprocess.run([*server, *arguments], env=env, capture_output=True,
                                  timeout=8, check=True).stdout

        try:
            for name, color, no_color, sample in (("truecolor", "truecolor", "", RGB),
                                                  ("indexed", "", "", INDEX),
                                                  ("mono", "", "1", None)):
                result, report = directory / f"{name}.exit", directory / f"{name}.report"
                options = shlex.join(["env", "TERM=tmux-256color", f"COLORTERM={color}",
                                      f"NO_COLOR={no_color}", "LC_ALL=C.UTF-8", executable])
                command = (f"{options} 2>{shlex.quote(str(report))}; "
                           f"printf '%s' \"$?\" >{shlex.quote(str(result))}; read -r unused")
                run("new-session", "-d", "-x", "24", "-y", "5", "-s", name, "/bin/sh", "-c", command)
                deadline = time.monotonic() + 8
                while not result.exists() or not result.stat().st_size:
                    if time.monotonic() >= deadline:
                        raise TimeoutError(f"tmux {name} consumer did not report its exit")
                    time.sleep(0.025)
                assert result.read_text() == "0", report.read_bytes()
                plain = run("capture-pane", "-t", name + ":0.0", "-p")
                check_rows(plain, UNICODE)
                styles = run("capture-pane", "-t", name + ":0.0", "-e", "-p")
                if sample:
                    assert sample in styles, styles
                else:
                    assert b"38;" not in styles and b"48;" not in styles, styles
                print(f"PASS tmux {name} visible cells:\n{plain.decode().rstrip()}\nstyle={sample!r}")
        finally:
            subprocess.run([*server, "kill-server"], env=env, capture_output=True, timeout=8, check=False)


if __name__ == "__main__":
    executable = str(Path(sys.argv[1]).resolve())
    if len(sys.argv) == 3:
        tmux_profiles(executable, sys.argv[2])
    else:
        capture_profiles(executable)
