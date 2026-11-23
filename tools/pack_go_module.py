#!/usr/bin/env python3
"""Create an offline Go module proxy from a pinned Nocturne source tree."""

import argparse
from datetime import datetime, timezone
from io import BytesIO
import json
from pathlib import Path
import re
import subprocess
import tarfile
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo


ROOT = Path(__file__).resolve().parents[1]
SUBTREE = "bindings/go/"


def git(*args):
    return subprocess.check_output(("git", "-C", str(ROOT), *args))


def package(commit, destination):
    pin = git("rev-parse", "--verify", "--end-of-options", f"{commit}^{{commit}}").decode().strip()
    build = git("show", f"{pin}:CMakeLists.txt")
    version = re.search(rb"^project\(Nocturne VERSION (\d+\.\d+\.\d+) ", build, re.MULTILINE)
    if version is None:
        raise ValueError("Missing native SDK version")
    version = "v" + version[1].decode()
    mod_file = git("show", f"{pin}:{SUBTREE}go.mod")
    module = re.search(rb"^module ([A-Za-z0-9./_-]+)$", mod_file, re.MULTILINE)
    if module is None:
        raise ValueError("Missing Go module path")
    module = module[1].decode()
    timestamp = git("show", "-s", "--format=%cI", pin).decode().strip()
    stamp = datetime.fromisoformat(timestamp).astimezone(timezone.utc).isoformat().replace("+00:00", "Z")
    proxy = destination / module / "@v"
    proxy.mkdir(parents=True)
    archive = git("archive", pin, SUBTREE.rstrip("/"))
    with tarfile.open(fileobj=BytesIO(archive)) as source, ZipFile(proxy / f"{version}.zip", "w") as result:
        for item in source.getmembers():
            if not item.isfile() or not item.name.startswith(SUBTREE):
                continue
            name = item.name.removeprefix(SUBTREE)
            entry = ZipInfo(f"{module}@{version}/{name}")
            entry.create_system = 3
            entry.external_attr = (0o100000 | item.mode) << 16
            with source.extractfile(item) as contents:
                result.writestr(entry, contents.read(), compress_type=ZIP_DEFLATED)
    (proxy / f"{version}.mod").write_bytes(mod_file)
    (proxy / f"{version}.info").write_text(json.dumps({"Version": version, "Time": stamp}) + "\n")
    (proxy / "list").write_text(version + "\n")
    print(proxy)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("commit")
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    try:
        package(args.commit, args.destination)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Nocturne Go package failed: {error}\n")


if __name__ == "__main__":
    main()
