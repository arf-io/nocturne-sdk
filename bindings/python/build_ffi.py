from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

from cffi import FFI


COMPONENTS = ("posix_host", "graph", "spatial", "simulation")
OPTIONAL_HEADERS = {
    "posix_host.h": "posix_host",
    "windows_host.h": "windows_host",
    "graph.h": "graph",
    "spatial.h": "spatial",
    "spatial_mesh.h": "spatial",
    "spatial_surface.h": "spatial",
    "simulation.h": "simulation",
}
C_LINE_MARKER = re.compile(r'^#\s*(?:line\s+)?\d+\s+"([^"]+)"')
BRIDGE = """
extern "Python" {
    NtResult nocturne_py_measure(const void *, NtMeasureContext *, NtConstraints, NtCellSize *);
    NtResult nocturne_py_layout(const void *, NtLayoutContext *, NtCellSize);
    NtResult nocturne_py_update(void *, NtUpdateInfo, NtUpdateReply *);
    NtResult nocturne_py_paint(const void *, NtPaintContext *, NtCellSize);
    NtResult nocturne_py_event(void *, NtEventContext *, const NtEvent *, NtEventReply *);
    void nocturne_py_dispose(void *);
}
"""


def components():
    selected = {name.strip() for name in os.environ.get("NOCTURNE_PYTHON_COMPONENTS", "").split(",")}
    selected.discard("")
    unknown = selected.difference(COMPONENTS)
    if unknown:
        raise ValueError(f"unknown Nocturne components: {', '.join(sorted(unknown))}")
    if "simulation" in selected:
        selected.add("spatial")
    return tuple(name for name in COMPONENTS if name in selected)


def resolve_sdk(root, selected):
    prefix = os.environ.get("NOCTURNE_SDK_PREFIX")
    if not prefix:
        raise ValueError("set NOCTURNE_SDK_PREFIX to the installed Nocturne C SDK")
    with tempfile.TemporaryDirectory(prefix="nocturne-python-sdk-") as temporary:
        path = Path(temporary)
        subprocess.run(
            ["cmake", "-S", str(root / "native"), "-B", str(path), "-DCMAKE_BUILD_TYPE=Release",
             f"-DNOCTURNE_SDK_PREFIX={Path(prefix).resolve(strict=True)}",
             f"-DNOCTURNE_PYTHON_COMPONENTS={';'.join(selected)}"],
            check=True,
        )
        result = path / "Release"
        include_dirs = (result / "include-dirs").read_text().splitlines()
        archives = (result / "archives").read_text().splitlines()
        compiler = (result / "compiler").read_text().strip()
        frontend = (result / "compiler-frontend").read_text().strip()
    return compiler, frontend, include_dirs, archives


def preprocess(compiler, frontend, includes, source):
    if frontend == "MSVC":
        with tempfile.TemporaryDirectory(prefix="nocturne-python-headers-") as temporary:
            translation_unit = Path(temporary) / "api.c"
            translation_unit.write_text(source, encoding="utf-8")
            return subprocess.run(
                [compiler, "/nologo", "/E", "/TC", "/std:c11",
                 *(f"/I{directory}" for directory in includes), str(translation_unit)],
                capture_output=True, text=True, check=True,
            ).stdout
    return subprocess.run(
        [compiler, "-E", "-std=c99", "-x", "c", *(f"-I{directory}" for directory in includes), "-"],
        input=source, capture_output=True, text=True, check=True,
    ).stdout


def installed_api(compiler, frontend, includes, selected):
    public = {(Path(directory) / "nocturne").resolve() for directory in includes}
    names = sorted({
        header.name for directory in public for header in directory.glob("*.h")
        if header.name not in OPTIONAL_HEADERS or OPTIONAL_HEADERS[header.name] in selected
    })
    source = "".join(f"#include <nocturne/{name}>\n" for name in names)
    output = preprocess(compiler, frontend, includes, source)
    declarations = []
    header = None
    for line in output.splitlines():
        location = C_LINE_MARKER.match(line)
        if location:
            header = Path(location[1]).parent.resolve() in public
        elif header:
            declarations.append(line)
    return source, "\n".join(declarations)


def make_builder():
    selected = components()
    compiler, frontend, includes, archives = resolve_sdk(Path(__file__).resolve().parent, selected)
    source, declarations = installed_api(compiler, frontend, includes, selected)
    flags = sum(1 << bit for bit, name in enumerate(COMPONENTS) if name in selected)
    source += f"\n#define NT_PYTHON_COMPONENTS_MASK {flags}\n"
    builder = FFI()
    constants = """
#define NT_ABI_VERSION ...
#define NT_CHOICE_NONE ...
#define NT_BIDI_REMOVED_LEVEL ...
#define NT_PYTHON_COMPONENTS_MASK ...
"""
    builder.cdef(declarations + constants + BRIDGE)
    linker = []
    if sys.platform == "darwin":
        linker.append("-Wl,-exported_symbol,_PyInit__native")
    elif sys.platform.startswith("linux"):
        linker.append("-Wl,--exclude-libs,ALL")
    builder.set_source(
        "nocturne._native", source,
        include_dirs=includes, extra_objects=archives, extra_link_args=linker,
        libraries=[] if os.name == "nt" else ["m"],
    )
    return builder


builder = make_builder()
