"""Shared local SDK archive writer."""

import gzip
import os
import re
import tarfile
import tempfile


def native_sdk_version(cmake_source):
    match = re.search(r"^project\(Nocturne VERSION (\d+\.\d+\.\d+) ",
                      cmake_source, re.MULTILINE)
    if not match:
        raise ValueError("The clean source snapshot has no native SDK version")
    return match[1]


def check_markdown_links(root):
    root = root.resolve()
    for file in root.rglob("*.md"):
        body = file.read_text(encoding="utf-8")
        for link in re.findall(r"\]\(([^\s)#]+)(?:#[^\s)]*)?\)", body):
            if "://" in link or link.startswith("mailto:"):
                continue
            target = (file.parent / link).resolve()
            if not target.is_relative_to(root) or not target.exists():
                raise ValueError(f"Missing document link in {file.relative_to(root)}: {link}")


def archive_prefix(prefix, output, epoch):
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=output.parent, delete=False) as raw:
        temporary = raw.name
        try:
            with gzip.GzipFile(fileobj=raw, mode="wb", filename="", mtime=epoch) as compressed:
                with tarfile.open(fileobj=compressed, mode="w", format=tarfile.PAX_FORMAT) as archive:
                    for path in (prefix, *sorted(prefix.rglob("*"))):
                        relative = path.relative_to(prefix.parent).as_posix()
                        member = archive.gettarinfo(str(path), arcname=relative)
                        member.uid = member.gid = 0
                        member.uname = member.gname = ""
                        member.mtime = epoch
                        member.mode = 0o755 if member.isdir() or member.mode & 0o111 else 0o644
                        if member.isfile():
                            with path.open("rb") as content:
                                archive.addfile(member, content)
                        else:
                            archive.addfile(member)
            raw.flush()
            os.chmod(temporary, 0o644)
            raw.close()
            os.link(temporary, output)
        finally:
            raw.close()
            os.unlink(temporary)
