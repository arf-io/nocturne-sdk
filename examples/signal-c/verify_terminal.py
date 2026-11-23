#!/usr/bin/env python3
"""Verify Signal's negotiated terminal profiles, input and cleanup."""

import os
from pathlib import Path
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import time


def verify(executable):
    pipe = subprocess.run((executable,), input="+\n+\n-\nq\n", text=True,
                          capture_output=True, check=True)
    assert not pipe.stderr
    assert pipe.stdout.splitlines() == [row for n in (4, 5, 6, 5)
                                        for row in ("Signal level", "#" * n + "." * (12 - n))]
    print("PASS redirected input and output retain the four plain frames")

    tmux = shutil.which("tmux")
    if not tmux:
        raise RuntimeError("tmux is required for the Signal terminal check")
    with tempfile.TemporaryDirectory(prefix="nocturne-signal-tty-", dir="/tmp") as directory:
        root = Path(directory)
        command = (tmux, "-u", "-S", str(root / "tmux.sock"), "-f", "/dev/null")
        env = {key: value for key, value in os.environ.items()
               if key not in ("TMUX", "TMUX_PANE", "NO_COLOR", "COLORTERM")}
        env["LC_ALL"] = "C.UTF-8"

        def run(*args):
            return subprocess.check_output((*command, *args), text=True, env=env, timeout=8)

        def wait(target, contains):
            deadline = time.monotonic() + 6
            while time.monotonic() < deadline:
                frame = run("capture-pane", "-p", "-t", target)
                if contains in frame:
                    return frame
                time.sleep(0.025)
            raise AssertionError(f"Signal did not display {contains!r} in {target}")

        profiles = [
            ("truecolor", "xterm-256color", ("COLORTERM=truecolor",), "38;2;"),
            ("indexed", "xterm-256color", (), "38;5;"),
            ("monochrome", "vt100", ("NO_COLOR=1",), None),
        ]
        try:
            for profile, term, options, style in profiles:
                work = root / profile
                work.mkdir()
                quote = lambda name: shlex.quote(str(work / name))
                ready = shlex.join((sys.executable, "-c", "import fcntl, struct, termios; "
                                    "fcntl.ioctl(0, termios.FIONREAD, struct.pack('i', 0))"))
                execute = shlex.join(("env", "-u", "COLORTERM", "-u", "NO_COLOR",
                                      f"TERM={term}", *options, "/bin/sh", "-c",
                                      'printf "%s\\n" "$$" > "$1"; exec "$2"',
                                      profile, str(work / "pid"), str(executable)))
                launch = (f"{ready} || exit 90; stty -g > {quote('before')}; "
                          f"{execute}; printf '%s\\n' \"$?\" > {quote('exit')}; "
                          f"{ready} || exit 91; stty -g > {quote('after')}; "
                          "printf 'Terminal restored\\n'; read -r done")
                target = f"{profile}:0.0"
                run("new-session", "-d", "-x", "30", "-y", "8", "-s", profile, "/bin/sh", "-c", launch)
                frame = wait(target, ">####........")
                assert "Signal level" in frame
                assert run("display-message", "-p", "-t", target, "#{alternate_on}").strip() == "1"
                styled = run("capture-pane", "-e", "-p", "-t", target)
                if style:
                    assert style in styled, styled
                    if profile == "indexed":
                        assert "38;2;" not in styled, styled
                else:
                    assert "38;" not in styled and "48;" not in styled, styled
                run("send-keys", "-t", target, "-l", "++-")
                wait(target, ">#####.......")
                if profile == "monochrome":
                    run("resize-window", "-t", target, "-x", "8", "-y", "8")
                    wait(target, ">#####..")
                    run("send-keys", "-t", target, "-l", "-")
                    wait(target, ">####...")
                    run("resize-window", "-t", target, "-x", "30", "-y", "8")
                    wait(target, ">####........")
                    print("PASS narrow monochrome Signal layout, input and focus after resize")
                    os.kill(int((work / "pid").read_text()), signal.SIGINT)
                    expected = 130
                else:
                    run("send-keys", "-t", target, "-l", "q")
                    expected = 0
                wait(target, "Terminal restored")
                assert int((work / "exit").read_text()) == expected
                assert (work / "before").read_text() == (work / "after").read_text()
                assert run("display-message", "-p", "-t", target, "#{alternate_on}").strip() == "0"
                print(f"PASS {profile} Signal focus, immediate input and tty restoration (exit {expected})")
        finally:
            subprocess.run((*command, "kill-server"), env=env, capture_output=True, check=False, timeout=8)


if __name__ == "__main__":
    verify(Path(sys.argv[1]).resolve())
