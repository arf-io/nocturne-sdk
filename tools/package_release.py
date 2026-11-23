#!/usr/bin/env python3
"""Build a local Nocturne native SDK archive from an exact Git commit."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from package_support import archive_prefix, check_markdown_links, native_sdk_version


ROOT = Path(__file__).resolve().parents[1]
GUIDES = (
    "ABI.md", "BINDINGS.md", "COMPATIBILITY.md", "COMPONENT-PROTOCOL.md",
    "CONTRACTS.md", "CONTROL-REFERENCE.md", "CUSTOM-COMPONENT.md",
    "PACKAGE-DEPENDENCIES.md", "PANEL-RECIPES.md", "QUICKSTART.md",
    "RELEASES.md", "TEXT-RECIPES.md", "WINDOWS-HOST.md", "component-contract.schema.json",
    "component-contracts.json", "package-dependencies.json",
)
EXAMPLES = ("field-log", "model-bench", "signal-c", "surface-lab")
EXTENSIONS = ("GRAPH", "SPATIAL", "SIMULATION", "POSIX_HOST", "WINDOWS_HOST")


def git(*args):
    return subprocess.check_output(("git", "-C", str(ROOT), *args), text=True).strip()


def cmake_setting(path, key):
    match = re.search(rf'^set\({key} "([^"]*)"\)$', path.read_text(), re.MULTILINE)
    if not match:
        raise ValueError(f"Missing configured CMake setting {key}")
    return match[1]


def installed_build(build, profile):
    configurations = build / "CMakeFiles"
    systems = list(configurations.glob("*/CMakeSystem.cmake"))
    if len(systems) != 1:
        raise ValueError("Expected one CMake target system in the clean build")
    system = systems[0]
    compiler = system.with_name("CMakeCCompiler.cmake")
    operating_system = cmake_setting(system, "CMAKE_SYSTEM_NAME").lower()
    processor = cmake_setting(system, "CMAKE_SYSTEM_PROCESSOR").lower()
    processor = {"aarch64": "arm64", "amd64": "x86_64", "x64": "x86_64"}.get(processor, processor)
    compiler_id = cmake_setting(compiler, "CMAKE_C_COMPILER_ID")
    frontend = cmake_setting(compiler, "CMAKE_C_COMPILER_FRONTEND_VARIANT")
    info = {
        "profile": profile,
        "system": operating_system,
        "processor": processor,
        "compiler": compiler_id,
        "compiler_frontend": frontend,
        "compiler_version": cmake_setting(compiler, "CMAKE_C_COMPILER_VERSION"),
        "cmake_version": subprocess.check_output(("cmake", "--version"), text=True).splitlines()[0],
    }
    if operating_system == "linux" and compiler_id in ("GNU", "Clang"):
        driver = cmake_setting(compiler, "CMAKE_C_COMPILER")
        info["compiler_target"] = subprocess.check_output((driver, "-dumpmachine"), text=True).strip()
    return info


def install_consumer_material(source, prefix):
    material = prefix / "share/nocturne"
    docs = material / "docs"
    docs.mkdir(parents=True)
    for name in GUIDES:
        shutil.copyfile(source / "docs" / name, docs / name)
    shutil.copytree(source / "docs/recipes", docs / "recipes")
    for name in EXAMPLES:
        shutil.copytree(source / "examples" / name, material / "examples" / name)
    shutil.copytree(source / "tests/consumer", material / "tests/consumer")
    for language in ("rust", "python", "go", "typescript"):
        binding = material / "bindings" / language
        binding.mkdir(parents=True)
        shutil.copyfile(source / "bindings" / language / "README.md", binding / "README.md")
    for relative in ("bindings/rust/examples/signal.rs", "bindings/rust/tests/component.rs"):
        target = material / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source / relative, target)
    unicode = material / "unicode/17.0.0"
    unicode.mkdir(parents=True)
    shutil.copyfile(source / "unicode/README.md", unicode.parent / "README.md")
    for name in ("LICENSE.txt", "SOURCES.json"):
        shutil.copyfile(source / "unicode/17.0.0" / name, unicode / name)
    shutil.copyfile(source / "LICENSE", material / "LICENSE")
    (material / "tools").mkdir()
    shutil.copyfile(source / "tools/check_presentation.py", material / "tools/check_presentation.py")
    check_markdown_links(material)


def package(reference, profile, output_directory, generator, toolchain):
    commit = git("rev-parse", "--verify", "--end-of-options", f"{reference}^{{commit}}")
    epoch = int(git("show", "-s", "--format=%ct", commit))
    with tempfile.TemporaryDirectory(prefix="nocturne-release-") as directory:
        work = Path(directory)
        snapshot, source, build = work / "source.tar", work / "source", work / "build"
        source.mkdir()
        git("archive", "--format=tar", f"--output={snapshot}", commit)
        subprocess.run(("tar", "-xf", str(snapshot), "-C", str(source)), check=True)
        version = native_sdk_version((source / "CMakeLists.txt").read_text())
        build_options = ["-DCMAKE_BUILD_TYPE=Release", "-DBUILD_TESTING=OFF",
                         "-DNOCTURNE_BUILD_EXAMPLES=OFF"]
        if profile == "minimal":
            build_options.extend(f"-DNOCTURNE_BUILD_{part}=OFF" for part in EXTENSIONS)
        if generator:
            build_options += ["-G", generator]
        if toolchain:
            build_options.append(f"-DCMAKE_TOOLCHAIN_FILE={toolchain}")
        env = dict(os.environ, LC_ALL="C", SOURCE_DATE_EPOCH=str(epoch), ZERO_AR_DATE="1")
        subprocess.run(("cmake", "-S", str(source), "-B", str(build), *build_options),
                       check=True, env=env)
        info = installed_build(build, profile)
        platform = f"{info['system']}-{info['processor']}"
        # Include the Windows compiler family because MinGW and MSVC archives
        # have different runtime and toolchain requirements.
        if info["system"] == "windows":
            frontend = info["compiler_frontend"]
            platform += "-msvc" if frontend == "MSVC" else "-mingw"
        if info["system"] == "linux":
            triple = info.get("compiler_target", "")
            if "musl" in triple:
                platform += "-musl"
            elif "gnu" in triple:
                platform += "-gnu"
        name = f"nocturne-sdk-{version}-{platform}-{profile}"
        prefix = work / name
        output = output_directory / f"{name}.tar.gz"
        if output.exists():
            raise FileExistsError(output)
        subprocess.run(("cmake", "--build", str(build), "--config", "Release"),
                       check=True, env=env)
        subprocess.run(("cmake", "--install", str(build), "--config", "Release", "--prefix", str(prefix)),
                       check=True, env=env)
        exports = list(prefix.rglob("NocturneTargets.cmake"))
        if len(exports) != 1:
            raise ValueError("Expected one installed CMake target export")
        components = re.findall(r"^add_library\(Nocturne::([a-z_]+) STATIC IMPORTED\)$",
                                exports[0].read_text(), re.MULTILINE)
        expected = {"nocturne"} if profile == "minimal" else {"nocturne", "graph", "spatial", "simulation"}
        if profile == "complete":
            if info["system"] in ("darwin", "linux"):
                expected.add("posix_host")
            if info["system"] == "windows":
                expected.add("windows_host")
        if not expected <= set(components) or (profile == "minimal" and set(components) != expected):
            raise ValueError(f"Installed {profile} components differ from the requested profile: {components}")
        install_consumer_material(source, prefix)
        info.update(version=version, source_commit=commit, components=sorted(components))
        (prefix / "BUILD-INFO.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
        (prefix / "SOURCE-COMMIT.txt").write_text(commit + "\n", encoding="utf-8")
        (prefix / "README.txt").write_text(
            f"Nocturne SDK {version} ({profile})\n\n"
            "Set CMAKE_PREFIX_PATH to the extracted directory.\n"
            "Start with share/nocturne/docs/QUICKSTART.md and COMPATIBILITY.md.\n"
            "This archive contains native SDK libraries for the configured target.\n"
            "BUILD-INFO.json records the target, toolchain and exact source.\n",
            encoding="utf-8",
        )
        archive_prefix(prefix, output, epoch)
    with output.open("rb") as artifact:
        checksum = hashlib.sha256()
        for block in iter(lambda: artifact.read(1024 * 1024), b""):
            checksum.update(block)
    print(f"{checksum.hexdigest()}  {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--commit", default="HEAD")
    parser.add_argument("--profile", required=True, choices=("complete", "minimal"))
    parser.add_argument("--output-directory", required=True, type=Path)
    parser.add_argument("--generator")
    parser.add_argument("--toolchain", type=Path)
    args = parser.parse_args()
    try:
        package(args.commit, args.profile, args.output_directory.absolute(), args.generator,
                args.toolchain.resolve(strict=True) if args.toolchain else None)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Nocturne package build failed: {error}\n")


if __name__ == "__main__":
    main()
