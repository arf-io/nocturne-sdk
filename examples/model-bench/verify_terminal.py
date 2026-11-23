#!/usr/bin/env python3
"""Verify the installed Model Bench with real tmux terminal input and output."""
import argparse
import fcntl
import os
from pathlib import Path
import re
import shlex
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import termios
import time


def verify(executable, tmux, evidence):
    with tempfile.TemporaryDirectory(prefix="model-bench-pty-", dir="/tmp") as directory:
        private = Path(directory)
        base = (tmux, "-u", "-f", "/dev/null", "-S", str(private / "tmux.sock"))
        env = dict(os.environ)
        for name in ("TMUX", "TMUX_PANE", "NO_COLOR"):
            env.pop(name, None)
        env.update(LC_ALL="C.UTF-8", COLORTERM="truecolor")

        def command(*args):
            return subprocess.run((*base, *args), capture_output=True, check=True,
                                  env=env, timeout=8, text=True).stdout

        def wait(target, action, predicate):
            deadline = time.monotonic()+8
            previous = None
            while True:
                frame = command("capture-pane", "-p", "-t", target)
                if frame == previous and predicate(frame):
                    return frame
                previous = frame
                if time.monotonic() >= deadline:
                    raise AssertionError(f"{action} was not observed\n{frame}")
                time.sleep(.025)

        def write(name, text):
            if evidence:
                (evidence / name).write_text(text, encoding="utf-8")

        def keys(target, *names):
            command("send-keys", "-t", target, *names)

        def send_bytes(target, data):
            keys(target, "-H", *(f"{value:02x}" for value in data))

        def visible_model(frame, width, ascii_mesh=False):
            pixels = []
            for y,row in enumerate(frame.splitlines()):
                for x,c in enumerate(row):
                    covered = c == "#" if ascii_mesh else "\u2801" <= c <= "\u28ff"
                    if x > width//2 and covered:
                        pixels.append((x,y,c))
            return pixels

        def state(frame, focus, orbit, height):
            return re.search(rf"{focus}\s*\|\s*Orbit {orbit}\s*\|\s*{height}\b",frame) is not None

        def launch(profile):
            output = private / profile
            output.mkdir()
            readiness = shlex.join((sys.executable, "-c",
                "import fcntl, struct, termios; "
                "fcntl.ioctl(0, termios.FIONREAD, struct.pack('i', 0))"))
            extra = ("COLORTERM=truecolor",) if profile == "truecolor" else ("NO_COLOR=1",)
            terminal = "TERM=vt100" if profile == "basic" else "TERM=tmux-256color"
            file = lambda name: shlex.quote(str(output / name))
            record_pid = f"printf '%s\\n' \"$$\" > {file('pid')}; exec \"$@\""
            run = shlex.join(("/bin/sh", "-c", record_pid, "model-bench", "env", "-u", "COLORTERM", "-u", "NO_COLOR",
                              terminal, "LC_ALL=C.UTF-8", *extra, str(executable)))
            shell = (
                f"{readiness} || exit 90; stty -g > {file('before')}; "
                f"{run} 2>{file('stderr')}; printf '%s\\n' \"$?\" > {file('exit')}; "
                f"stty -g > {file('immediate')}; {readiness} || exit 91; "
                f"stty -g > {file('after')}; printf 'Terminal restored\\n'; read -r done"
            )
            command("new-session", "-d", "-x", "72", "-y", "18", "-s", profile, "/bin/sh", "-c", shell)
            target = f"{profile}:0.0"
            try:
                frame = wait(target,"initial model and editor",lambda f: "Model bench" in f and
                             "Material: clay" in f and "Mesh view" in f and state(f,"Editor",0,"Level")
                             and visible_model(f,72,profile == "basic"))
            except AssertionError as error:
                raise AssertionError(f"{error}\n{(output / 'stderr').read_text()}") from error
            assert command("display-message","-p","-t",target,"#{alternate_on}").strip() == "1"
            write(f"{profile}-open.txt",frame)
            return target,output,frame

        def stop(profile,target,output):
            keys(target,"C-q")
            restored = wait(target,"terminal restoration",lambda f: "Terminal restored" in f)
            before,after = ((output / key).read_text().strip() for key in ("before","after"))
            assert before and before == after,(before,after)
            assert (output / "exit").read_text().strip() == "0",(output / "stderr").read_text()
            assert not (output / "stderr").read_text(),(output / "stderr").read_text()
            assert command("display-message","-p","-t",target,"#{alternate_on}").strip() == "0"
            write(f"{profile}-exit.txt",f"exit=0\nalternate_on=0\nbefore={before}\nafter={after}\n{restored}")
            print(f"PASS {profile} exit and tty restoration")

        def resize(target,width,height):
            command("resize-window","-t",target,"-x",str(width),"-y",str(height))

        def resize_with_input(target,output,width,height,data):
            pid = int((output / "pid").read_text())
            tty = command("display-message","-p","-t",target,"#{pane_tty}").strip()
            fd = os.open(tty,os.O_RDONLY | os.O_NOCTTY)
            os.kill(pid,signal.SIGSTOP)
            try:
                resize(target,width,height)
                deadline = time.monotonic()+3
                # tmux updates its window dimensions before it applies them
                # to the tty. Queue input only after the descriptor sees them.
                while True:
                    rows,cols,_,_ = struct.unpack("HHHH",fcntl.ioctl(fd,termios.TIOCGWINSZ,b"\0"*8))
                    if (cols,rows) == (width,height):
                        break
                    assert time.monotonic() < deadline,(cols,rows,width,height)
                    time.sleep(.01)
                send_bytes(target,data)
            finally:
                os.close(fd)
                os.kill(pid,signal.SIGCONT)

        try:
            target,output,initial = launch("truecolor")
            command("send-keys","-t",target,"-l","ridge café")
            edited = wait(target,"UTF-8 editor input",lambda f: "ridge café" in f and state(f,"Editor",0,"Level"))
            keys(target,"BSpace")
            edited = wait(target,"cluster backspace",lambda f: "ridge caf" in f and "ridge café" not in f)
            write("truecolor-edited.txt",edited)
            assert visible_model(initial,72) == visible_model(edited,72)
            print("PASS UTF-8 stock editor beside unchanged spatial view")

            send_bytes(target,b"\t\x1b[C")
            orbited = wait(target,"camera orbit",lambda f: state(f,"View",1,"Level") and "ridge caf" in f)
            assert visible_model(orbited,72) and visible_model(orbited,72) != visible_model(edited,72)
            write("truecolor-orbit.txt",orbited)
            keys(target,"Enter")
            raised = wait(target,"mesh displacement",lambda f: state(f,"View",1,"Raised"))
            assert visible_model(raised,72) and visible_model(raised,72) != visible_model(orbited,72)
            write("truecolor-raised.txt",raised)
            print("PASS camera and geometry changes alter the spatial glyphs")

            pixels = visible_model(raised,72)
            x,y,_ = pixels[len(pixels)//2]
            send_bytes(target,f"\x1b[<0;{x+1};{y+1}M\x1b[<0;{x+1};{y+1}m".encode())
            picked = wait(target,"typed face selection",lambda f: re.search(r"Object 401 / face 1[0-3]",f))
            write("truecolor-picked.txt",picked)
            print("PASS pointer selects a published mesh face by object and primitive IDs")

            resize(target,1,1)
            wait(target,"view hidden at 1x1",lambda f: "Mesh view" not in f)
            time.sleep(.10)
            resize_with_input(target,output,58,14,b"\x1b[C")
            restored = wait(target,"same-step view input on resize",lambda f: "ridge caf" in f and
                            state(f,"View",2,"Raised") and visible_model(f,58))
            write("truecolor-resized.txt",restored)
            print("PASS resize restores view focus before input and preserves editor text")
            stop("truecolor",target,output)

            target,output,_ = launch("mono")
            resize(target,1,1)
            wait(target,"editor hidden at 1x1",lambda f: "Notes" not in f)
            time.sleep(.10)
            resize_with_input(target,output,64,16,b"after resize")
            restored = wait(target,"same-step editor input on resize",lambda f: "after resize" in f and
                            state(f,"Editor",0,"Level") and visible_model(f,64))
            write("mono-resized.txt",restored)
            print("PASS monochrome composition and same-step editor input after resize")
            stop("mono",target,output)

            target,output,initial = launch("basic")
            send_bytes(target,b"\t\x1b[C")
            orbit = wait(target,"cell-only monochrome orbit",
                         lambda f: state(f,"View",1,"Level") and visible_model(f,72,True))
            assert visible_model(initial,72,True) != visible_model(orbit,72,True)
            write("basic-orbit.txt",orbit)
            keys(target,"Enter")
            raised = wait(target,"cell-only monochrome lift",
                          lambda f: state(f,"View",1,"Raised") and visible_model(f,72,True))
            assert visible_model(orbit,72,True) != visible_model(raised,72,True)
            write("basic-raised.txt",raised)
            print("PASS cell-only monochrome mesh uses visible ASCII coverage")
            stop("basic",target,output)
        finally:
            subprocess.run((*base,"kill-server"),capture_output=True,env=env,timeout=5)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable",type=Path)
    parser.add_argument("--tmux",default=shutil.which("tmux"))
    parser.add_argument("--evidence",type=Path)
    args = parser.parse_args()
    if not args.tmux:
        parser.error("tmux is required")
    if args.evidence:
        args.evidence.mkdir(parents=True,exist_ok=True)
    verify(args.executable.resolve(),args.tmux,args.evidence)


if __name__ == "__main__":
    main()
