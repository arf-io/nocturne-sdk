# Nocturne release builds

Each native archive records the exact source commit, distribution profile,
target and toolchain in `BUILD-INFO.json`. Build and consume the package
with a compatible toolchain for its advertised target. On Linux, match
the C library as well as the architecture. Windows builds distinguish the
MSVC and MinGW toolchain families.
[Installation and compatibility](COMPATIBILITY.md)

## Build a native archive

Use Git, CMake, a C compiler, Python 3.10 or newer and a tar executable.
Run the archive builder at the source checkout. It configures and builds
a clean copy of the named commit.

```sh
pin=$(git rev-parse HEAD)
python3 tools/package_release.py --commit "$pin" --profile complete \
  --output-directory release
python3 tools/package_release.py --commit "$pin" --profile minimal \
  --output-directory release
```

The complete profile installs portable core, graph, spatial and simulation
along with the target's terminal adapter, where available. The minimal
profile contains the portable core. Each archive includes the C/C++ API,
CMake package, notices, documentation, standalone examples and installed
consumer fixtures. Optional examples require their corresponding targets.

The archive entry order, timestamps, owner metadata and permissions are
stable across fresh builds with the same source and toolchain. The builder
prints each checksum. Compare it with the recorded release checksum before
extracting a distributed archive.

## Verify the extracted SDK

Point CMake at the extracted package and build the bundled standalone
consumers. This example uses the complete macOS arm64 archive name.

```sh
tar -xzf release/nocturne-sdk-1.0.0-darwin-arm64-complete.tar.gz
sdk="$PWD/nocturne-sdk-1.0.0-darwin-arm64-complete"
cmake -S "$sdk/share/nocturne/tests/consumer" -B release-consumer \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$sdk"
cmake --build release-consumer --config Release
ctest --test-dir release-consumer -C Release --output-on-failure
```

## Build language packages

Rust and Go consume a compatible installed native SDK. Python wheels
link the selected native archives and carry the relevant notices. The
TypeScript package builds its reactor from a WASI SDK prefix and includes
the Unicode and WASI runtime notices. Its linked optional modules depend
on the WASI prefix selected for the build.
[Language prerequisites and walkthroughs](BINDINGS.md)

To prepare an offline Go proxy for the pinned source, run
`python3 tools/pack_go_module.py "$pin" release/go`. The submodule version
matches the source SDK version. For reproducible macOS Python wheels,
use the same Python, build dependencies and native SDK, set
`SOURCE_DATE_EPOCH` to the source commit timestamp, and configure the
extension with `CFLAGS=-g0` to omit local debug-map metadata.

The [installed language replay](../tests/consumer/binding_replay) compares
public observations across the five packages. Use both native profiles
to verify optional-component discovery. Release proof records identify
the tested targets, commands, exact pins and artifact checksums.

`tools/record_proof_execution.py` records the command, actual host,
toolchain, source and artifact digests alongside its output and exit
status. Use one log and exit record for each execution. Runtime proofs
distinguish Linux GNU libc and musl and Windows MSVC and MinGW. A cross
build supplies build evidence. Runtime qualification runs on the
advertised target with the relevant native archive. The release
manifest checks every distribution file against its recorded executions.
Earlier milestone proofs keep the committed evidence and artifact
digests from their original proof runs.

## Source identity for distribution

Build public distribution archives from the public source checkout.
Ship the source snapshot and its checksum with the binary packages.
The commit in each archive then identifies the same source tree that
recipients can inspect. Internal candidate builds record their own
source commit and keep a separate mapping to the public snapshot.
