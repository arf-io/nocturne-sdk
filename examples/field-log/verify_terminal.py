#!/usr/bin/env python3
"""Drive the installed example in an isolated tmux terminal and keep evidence."""
import argparse
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time


def verify(executable, tmux, evidence):
    with tempfile.TemporaryDirectory(prefix="field-log-terminal-", dir="/tmp") as directory:
        private = Path(directory)
        socket = str(private / "tmux.sock")
        base = (tmux, "-u", "-f", "/dev/null", "-S", socket)
        env = dict(os.environ)
        for key in ("TMUX", "TMUX_PANE", "NO_COLOR"):
            env.pop(key, None)
        env.update(LC_ALL="C.UTF-8", COLORTERM="truecolor")

        def command(*args):
            return subprocess.run((*base, *args), capture_output=True, check=True,
                                  env=env, timeout=8, text=True).stdout

        def capture(target):
            return command("capture-pane", "-t", target, "-p")

        def await_capture(target, description, predicate):
            deadline = time.monotonic() + 5
            while True:
                frame = capture(target)
                if predicate(frame):
                    return frame
                if time.monotonic() >= deadline:
                    raise AssertionError(f"{description} not observed\n{frame}")
                time.sleep(.025)

        def write(name, content):
            if evidence:
                (evidence / name).write_text(content, encoding="utf-8")

        def key(target, *keys):
            command("send-keys", "-t", target, *keys)

        def type_text(target, text):
            command("send-keys", "-t", target, "-l", text)

        def open_log(session, *args, color="truecolor"):
            state = private / session
            state.mkdir()
            profile = {"truecolor": ("COLORTERM=truecolor",), "indexed": (), "mono": ("NO_COLOR=1",)}
            options = shlex.join(("env", "-u", "COLORTERM", "-u", "NO_COLOR",
                                  "TERM=tmux-256color", "LC_ALL=C.UTF-8",
                                  *profile[color], str(executable), *args))
            # Canonical mode can temporarily set PENDIN on Darwin. Querying
            # readiness lets the line discipline settle without eating input.
            readiness = shlex.join((sys.executable, "-c",
                "import fcntl, struct, termios; "
                "fcntl.ioctl(0, termios.FIONREAD, struct.pack('i', 0))"))
            start = (
                f"{readiness} || exit 90; "
                f"stty -g > {shlex.quote(str(state / 'before'))}; "
                f"{options} 2>{shlex.quote(str(state / 'stderr'))}; "
                f"printf '%s\\n' \"$?\" > {shlex.quote(str(state / 'exit'))}; "
                f"stty -g > {shlex.quote(str(state / 'immediate'))}; "
                f"{readiness} || exit 91; "
                f"stty -g > {shlex.quote(str(state / 'after'))}; "
                "printf 'Terminal restored\\n'; read -r done"
            )
            command("new-session", "-d", "-x", "72", "-y", "15", "-s", session,
                    "/bin/sh", "-c", start)
            target = f"{session}:0.0"
            frame = await_capture(target, "initial frame", lambda f: "Field log" in f
                                  and "Survey marker" in f and "Field notebook" in f
                                  and phase(f) is not None and progress(f, 72, 3))
            assert command("display-message", "-p", "-t", target, "#{alternate_on}").strip() == "1"
            return target, state, frame

        def close_log(target, state):
            key(target, "C-q")
            frame = await_capture(target, "shell again", lambda f: "Terminal restored" in f)
            assert command("display-message", "-p", "-t", target, "#{alternate_on}").strip() == "0"
            assert (state / "exit").read_text().strip() == "0", (state / "stderr").read_text()
            before = (state / "before").read_text().strip()
            after = (state / "after").read_text().strip()
            immediate = (state / "immediate").read_text().strip()
            assert before and before == after, (before, after)
            assert not (state / "stderr").read_text(), (state / "stderr").read_text()
            write(f"{state.name}-exit.txt",
                  f"exit=0\nalternate_on=0\nbefore={before}\nimmediate={immediate}\nafter={after}\n{frame}")
            print(f"PASS {state.name}: exit 0, alternate screen off, tty flags restored")

        def phase(frame):
            match = re.search(r"Pulse ([0-3])", frame)
            return match[1] if match else None

        def progress(frame, width, count):
            expected = width * count // 8
            rows = frame.splitlines()
            return len(rows) >= 2 and rows[-2] == "#" * expected + "-" * (width - expected)

        try:
            target, state, first = open_log("full")
            assert phase(first) is not None
            second = await_capture(target, "advancing clock", lambda f: phase(f) not in (None, phase(first)))
            write("full-open.txt", first)
            write("full-animation.txt", second)
            print(f"PASS animation: Pulse {phase(first)} -> {phase(second)}")

            type_text(target, "lichen café")
            await_capture(target, "UTF-8 editing", lambda f: "lichen café" in f.splitlines()[3])
            key(target, "BSpace")
            await_capture(target, "cluster deletion",
                          lambda f: "lichen caf" in f.splitlines()[3] and "café" not in f)
            key(target, "C-z")
            await_capture(target, "undo", lambda f: "lichen café" in f.splitlines()[3])
            key(target, "Enter")
            await_capture(target, "Enter saves", lambda f: "Note saved." in f and progress(f, 72, 4)
                          and "lichen café" not in f.splitlines()[3] and "lichen café" in f)

            type_text(target, "cloud edge")
            await_capture(target, "second draft", lambda f: "cloud edge" in f.splitlines()[3])
            key(target, "Tab", "Enter")
            saved = await_capture(target, "button saves", lambda f: "cloud edge" not in f.splitlines()[3]
                                  and "cloud edge" in f and progress(f, 72, 5))
            write("full-saved.txt", saved)
            key(target, "Tab", "Up")
            selected = await_capture(target, "list selection", lambda f: "Selected note 4." in f)
            write("full-selected.txt", selected)
            key(target, "Tab", "PageDown")
            paged = await_capture(target, "reader page", lambda f: "Ctrl+Q closes the log." in f)
            write("full-paged.txt", paged)
            key(target, "Tab", "Home")
            await_capture(target, "scrollbar Home", lambda f: "Field notebook" in f
                          and "Ctrl+Q closes the log." not in f)
            print("PASS input: UTF-8 deletion and undo, Enter and button submission, selection, reader and scrollbar")

            command("resize-window", "-t", target, "-x", "59", "-y", "12")
            key(target, "Tab")
            type_text(target, "after resize")
            resized = await_capture(target, "input during resize", lambda f: len(f.splitlines()) == 12
                                    and len(f.splitlines()[0]) == 59 and progress(f, 59, 5)
                                    and "after resize" in f.splitlines()[3] and phase(f) is not None)
            dims = command("display-message", "-p", "-t", target, "#{pane_width}x#{pane_height}").strip()
            assert dims == "59x12", dims
            pulse = await_capture(target, "animation after resize", lambda f: "after resize" in f
                                  and phase(f) not in (None, phase(resized)))
            write("full-resized.txt", resized)
            write("full-resized-animation.txt", pulse)
            key(target, "Enter")
            finished = await_capture(target, "save after resize", lambda f: "Note saved." in f
                                     and "after resize" not in f.splitlines()[3] and "after resize" in f
                                     and progress(f, 59, 6))
            write("full-finished.txt", finished)
            print(f"PASS resize: {dims}, input persisted, Pulse {phase(resized)} -> {phase(pulse)}")

            command("set-buffer", "-b", "field-log-notes", "one\rsecond\r")
            command("paste-buffer", "-r", "-d", "-b", "field-log-notes", "-t", target)
            batched = await_capture(target, "two submissions from one read", lambda f:
                                    progress(f, 59, 8) and "onesecond" not in f
                                    and re.search(r"\bone\b", f) and "second" in f and "Note saved." in f)
            write("full-batched-saves.txt", batched)
            print("PASS batched input: two separate notes saved, capacity 8/8")
            close_log(target, state)

            target, state, steady = open_log("reduced", "--reduced-motion")
            assert phase(steady) == "0", steady
            time.sleep(.4)
            later = capture(target)
            assert phase(later) == "0", later
            write("reduced-steady.txt", later)
            print("PASS reduced motion: Pulse 0 remained steady")
            close_log(target, state)

            for color in ("indexed", "mono"):
                target, state, frame = open_log(color, "--reduced-motion", color=color)
                styled = command("capture-pane", "-t", target, "-p", "-e")
                if color == "indexed":
                    assert "38;5;" in styled and "38;2;" not in styled, styled
                else:
                    assert "38;" not in styled and "48;" not in styled, styled
                    cursor = await_capture(target, "monochrome draft focus", lambda f:
                                           "Focus Draft  |  Cursor 1" in f)
                    write("mono-draft-focus.txt", cursor)
                    type_text(target, "aé")
                    await_capture(target, "monochrome edited draft", lambda f:
                                  "aé" in f.splitlines()[3] and "Focus Draft  |  Cursor 3" in f)
                    key(target, "Left")
                    moved = await_capture(target, "monochrome cursor movement", lambda f:
                                          "aé" in f.splitlines()[3] and "Focus Draft  |  Cursor 2" in f)
                    write("mono-draft-cursor.txt", moved)
                    for focus in ("Save now", "Saved notes", "Field guide", "Guide scrollbar"):
                        key(target, "Tab")
                        frame = await_capture(target, f"monochrome {focus} focus", lambda f:
                                              f"Focus {focus}  |" in f)
                    print("PASS monochrome focus traversal and grapheme cursor cue")
                write(f"{color}-frame.txt", frame)
                write(f"{color}-styles.txt", repr(styled) + "\n")
                print(f"PASS terminal profile: {color} text and styles")
                close_log(target, state)
        finally:
            subprocess.run((*base, "kill-server"), env=env, capture_output=True, timeout=8, check=False)


def main():
    if not __debug__:
        raise RuntimeError("run the proof with Python assertions enabled")
    arguments = argparse.ArgumentParser(description=__doc__)
    arguments.add_argument("executable", type=Path)
    arguments.add_argument("--tmux", default=shutil.which("tmux"))
    arguments.add_argument("--evidence", type=Path)
    options = arguments.parse_args()
    if not options.tmux:
        arguments.error("tmux is required")
    if options.evidence:
        options.evidence.mkdir(parents=True, exist_ok=True)
    verify(options.executable.resolve(strict=True), options.tmux, options.evidence)


if __name__ == "__main__":
    main()
