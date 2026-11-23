#!/usr/bin/env python3
"""Run the real motion example in a PTY; check completion and SIGINT cleanup."""
import errno
import json
import os
from pathlib import Path
import pty
import select
import signal
import subprocess
import sys
import time

executable = Path(sys.argv[1] if len(sys.argv) > 1 else 'build/nocturne_motion').resolve()

def capture(interrupt):
    master, slave = pty.openpty()
    process = subprocess.Popen([str(executable), '--animate'], stdin=slave, stdout=slave,
                               stderr=slave, start_new_session=True)
    os.close(slave)
    started = time.monotonic()
    data = bytearray()
    interrupted = False
    try:
        while True:
            elapsed = time.monotonic() - started
            if elapsed > 12:
                raise RuntimeError('Example did not exit before deadline')
            if interrupt and not interrupted and elapsed >= .3:
                process.send_signal(signal.SIGINT)
                interrupted = True
            if select.select([master], [], [], .1)[0]:
                try:
                    chunk = os.read(master, 65536)
                except OSError as error:
                    if error.errno != errno.EIO:
                        raise
                    chunk = b''
                if not chunk:
                    break
                data.extend(chunk)
            elif process.poll() is not None:
                break
        code = process.wait(timeout=2)
        if code != 0:
            raise RuntimeError(f'Example exit {code}: {bytes(data[-1000:])!r}')
        frames = data.count(b'Nocturne components / motion')
        if frames < 2 or b'\x1b[?1049h' not in data or b'\x1b[?25l' not in data:
            raise RuntimeError('The animated terminal path did not run')
        if not data.endswith(b'\x1b[0m\x1b[?25h\x1b[?1049l'):
            raise RuntimeError('Terminal state was not restored')
        return {'mode': 'interrupt' if interrupt else 'completion', 'exit': code,
                'frames': frames, 'cursor_and_screen_restored': True}
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        os.close(master)

for interrupt in (False, True):
    print(json.dumps(capture(interrupt)))
