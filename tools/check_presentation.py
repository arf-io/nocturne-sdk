#!/usr/bin/env python3
"""Exercise an installed Unicode consumer in a real tmux terminal."""
import argparse
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent


def run(*command, capture=False, env=None):
    result = subprocess.run([str(item) for item in command], check=True, env=env,
                            stdout=subprocess.PIPE if capture else None)
    return result.stdout.decode("utf-8") if capture else None


def check_frame(text, shows_wide):
    rows = text.splitlines()
    if len(rows) < 4:
        raise RuntimeError(f"Missing visible rows (wide={shows_wide}): {rows!r}")
    ink = rows[1]
    if not (ink.startswith(" |") and ink.endswith("|") and
            "café" in ink and "בא" in ink and ("界" in ink) == shows_wide and
            rows[2].strip() == ("Clipped text" if shows_wide else "lipped text") and
            re.search(r"[\u2801-\u28ff]", rows[3])):
        raise RuntimeError(f"Unexpected visible rows (wide={shows_wide}): {rows!r}")


def present(tmux, executable, folder, env):
    exit_file = folder / "terminal-exit"
    target = "view:0.0"
    command = f"{shlex.quote(str(executable))} move ansi; " \
              f"printf '%s' \"$?\" > {shlex.quote(str(exit_file))}; sleep 30"
    run(*tmux, "new-session", "-d", "-x", "22", "-y", "6", "-s", "view",
        "/bin/sh", "-c", command, env=env)
    for step in (0, 1):
        deadline = time.monotonic() + 12
        while True:
            rows = run(*tmux, "capture-pane", "-p", "-t", target, capture=True, env=env)
            try:
                check_frame(rows, step != 0)
                break
            except RuntimeError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.05)
        styled = run(*tmux, "capture-pane", "-e", "-p", "-t", target, capture=True, env=env)
        if "\x1b[" not in styled:
            raise RuntimeError("Terminal did not receive styled attributes")
        print(f"Installed tmux PTY placement {step}")
        print(rows.rstrip())
        print("", flush=True)
        if step == 0:
            run(*tmux, "send-keys", "-t", target, "Enter", env=env)
    deadline = time.monotonic() + 12
    while not exit_file.exists() or not exit_file.stat().st_size:
        if time.monotonic() >= deadline:
            raise RuntimeError("No terminal consumer exit result")
        time.sleep(0.05)
    exit_code = int(exit_file.read_text())
    if exit_code != 0:
        raise RuntimeError(f"Installed terminal consumer exited {exit_code}")
    print(f"Installed terminal consumer exit {exit_code}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("native_build", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="nocturne-presentation-", dir="/tmp") as tmp:
        folder = Path(tmp)
        prefix = folder / "installed"
        source = folder / "consumer"
        build = folder / "build"
        run("cmake", "--install", args.native_build.resolve(), "--prefix", prefix)
        prefix.rename(folder / "relocated")
        shutil.copytree(ROOT / "tests" / "consumer", source)
        run("cmake", "-S", source, "-B", build, f"-DCMAKE_PREFIX_PATH={folder / 'relocated'}")
        run("cmake", "--build", build, "--target", "presentation_consumer")
        executable = build / "presentation_consumer"
        for step in (0, 1):
            plain = run(executable, step, "plain", capture=True)
            if "\x1b" in plain:
                raise RuntimeError("Noninteractive output contained terminal controls")
            check_frame("\n".join(line.rstrip() for line in plain.splitlines()), step != 0)
            print(f"Installed noninteractive placement {step} exit 0")
        env = dict(os.environ, LC_ALL="en_US.UTF-8")
        env.pop("TMUX", None)
        tmux = ("tmux", "-u", "-S", str(folder / "tmux.sock"), "-f", "/dev/null")
        try:
            present(tmux, executable, folder, env)
        finally:
            subprocess.run([*tmux, "kill-server"], env=env, capture_output=True, check=False)
    print("PASS: relocated installed frame sink through an actual terminal")


if __name__ == "__main__":
    main()
