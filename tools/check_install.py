#!/usr/bin/env python3
"""Build a separate C/C++ consumer using only an installed SDK package."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parent.parent
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / 'build'

def run(*args):
    subprocess.run([str(arg) for arg in args], check=True)

with tempfile.TemporaryDirectory(prefix='nocturne-consumer-') as directory:
    temporary = Path(directory)
    prefix = temporary / 'prefix'
    source = temporary / 'consumer'
    shutil.copytree(root / 'tests/consumer', source)
    run('cmake', '--install', build, '--prefix', prefix)
    run('cmake', '-S', source, '-B', temporary / 'build', f'-DCMAKE_PREFIX_PATH={prefix}')
    run('cmake', '--build', temporary / 'build')
    run('ctest', '--test-dir', temporary / 'build', '--output-on-failure')
print('PASS: independent installed C and C++ consumers')
