#!/usr/bin/env python3
"""Record a release test command and its execution provenance."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess


HEADER = b"NOCTURNE PROOF RUN "
TRAILER = b"NOCTURNE PROOF EXIT "
KINDS = {"build", "inspection", "runtime"}
RELEASE_TARGETS = {
    "darwin-arm64", "linux-arm64-gnu", "linux-arm64-musl",
    "linux-x86_64-musl", "windows-x86_64-msvc",
}
RUNTIME_TARGETS = RELEASE_TARGETS | {"linux-x86_64-gnu", "windows-x86_64-mingw"}


def host_target():
    system = platform.system().lower()
    if system.startswith(("msys_nt", "cygwin_nt", "mingw")):
        system = "windows"
    architecture = {"aarch64": "arm64", "amd64": "x86_64"}.get(
        platform.machine().lower(), platform.machine().lower())
    target = f"{system}-{architecture}"
    if system == "linux":
        try:
            glibc = os.confstr("CS_GNU_LIBC_VERSION")
        except (OSError, ValueError):
            glibc = None
        if glibc:
            return f"{target}-gnu"
        try:
            result = subprocess.run(("ldd", "--version"), stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            if b"musl" in result.stdout.lower():
                return f"{target}-musl"
        except FileNotFoundError:
            pass
    return target


def runtime_matches(target, host):
    alias = "windows-x86_64" if target.startswith("windows-x86_64-") else target
    return target in RUNTIME_TARGETS and host == alias


def file_sha256(path):
    with path.open("rb") as stream:
        checksum = hashlib.sha256()
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            checksum.update(block)
        return checksum.hexdigest()


def run_capture(source, kind, target, toolchain, command, log, exit_file, artifacts, names):
    if not toolchain.strip() or not command:
        raise ValueError("Provide a toolchain and a command")
    host = host_target()
    if kind == "runtime" and not runtime_matches(target, host):
        raise ValueError(f"The runtime target {target} does not match {host}")
    if log == exit_file or log.exists() or exit_file.exists():
        raise ValueError("Provide distinct new log and exit paths")
    if names and artifacts is None:
        raise ValueError("Provide an artifact directory when naming artifacts")
    if artifacts is not None:
        artifacts = artifacts.resolve(strict=True)
    files = {}
    for name in names:
        path = (artifacts / name).resolve(strict=True)
        if Path(name).is_absolute() or not path.is_relative_to(artifacts) or path == artifacts:
            raise ValueError(f"Invalid artifact path: {name}")
        files[name] = path
    recorded = {name: file_sha256(path) for name, path in files.items()}
    pin = source
    if not re.fullmatch(r"(?:[0-9a-f]{40}|[0-9a-f]{64})", pin):
        pin = subprocess.check_output(("git", "rev-parse", "--verify", "--end-of-options",
                                       f"{source}^{{commit}}"), text=True).strip()
    metadata = {"kind": kind, "target": target, "host": host, "toolchain": toolchain,
                "command": command, "source_commit": pin, "distribution_artifacts": recorded}
    with log.open("xb") as output:
        output.write(HEADER + json.dumps(metadata, sort_keys=True).encode() + b"\n")
        output.flush()
        result = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT).returncode
        if any(file_sha256(path) != recorded[name] for name, path in files.items()):
            output.write(b"\nDistribution artifact changed during execution.\n")
            if result == 0:
                result = 1
        output.write(b"\n" + TRAILER + str(result).encode() + b"\n")
    with exit_file.open("x") as output:
        output.write(f"{result}\n")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-commit", required=True,
                        help="exact source commit, or a Git reference from a source checkout")
    parser.add_argument("--kind", choices=sorted(KINDS), required=True)
    parser.add_argument("--target", required=True)
    parser.add_argument("--toolchain", required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--exit-file", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path)
    parser.add_argument("--artifact", action="append", default=[])
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    try:
        raise SystemExit(run_capture(args.source_commit, args.kind, args.target, args.toolchain, command,
                                     args.log, args.exit_file, args.artifacts, args.artifact))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Nocturne proof execution failed: {error}\n")


if __name__ == "__main__":
    main()
