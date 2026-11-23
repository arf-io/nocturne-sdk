# Install and use a compatible SDK

Install the SDK into a prefix for your operating system, architecture and
native toolchain. Then configure the application against that prefix. A
source install provides the C headers, static libraries, CMake package
metadata and applicable notices. Rust, Python and Go use those native
artifacts. TypeScript uses the installed npm package and its WASI reactor.
[Language packages and coverage](BINDINGS.md)

## Build and install the native SDK

From the SDK source directory, run the Release build, tests and install
into a fresh prefix. The same commands work with CMake's single- and
multi-configuration generators. The example below uses a POSIX shell.
On Windows, pass an absolute prefix appropriate to your shell.

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --config Release --parallel
ctest --test-dir build-release -C Release --output-on-failure
cmake --install build-release --config Release --prefix "$PWD/nocturne-install"
```

Configure the downstream project with `CMAKE_PREFIX_PATH` set to that
prefix. Request the components the application needs.

```cmake
find_package(Nocturne CONFIG REQUIRED COMPONENTS nocturne)
target_link_libraries(my_app PRIVATE Nocturne::nocturne)
```

| Installed component | CMake target | Scope |
| --- | --- | --- |
| `nocturne` | `Nocturne::nocturne` | Portable component tree, frames, input decoding, controls, animation, text and presentation. |
| `posix_host` | `Nocturne::posix_host` | POSIX tty ownership and polling. |
| `windows_host` | `Nocturne::windows_host` | Win32 console ownership and polling. |
| `graph` | `Nocturne::graph` | Graph layout, views and matching public types. |
| `spatial` | `Nocturne::spatial` | Geometry, transforms, cameras, clipping, meshes and surface contact. |
| `simulation` | `Nocturne::simulation` | Surface and fluid simulation. Requires `spatial`. |

The default native build includes the extensions and the terminal adapter
available for its target. To install just the portable core, configure a
separate build directory and prefix with these options.

```sh
cmake -S . -B build-minimal -DCMAKE_BUILD_TYPE=Release \
  -DNOCTURNE_BUILD_GRAPH=OFF -DNOCTURNE_BUILD_SPATIAL=OFF \
  -DNOCTURNE_BUILD_SIMULATION=OFF -DNOCTURNE_BUILD_POSIX_HOST=OFF \
  -DNOCTURNE_BUILD_WINDOWS_HOST=OFF
```

`Nocturne_INSTALLED_COMPONENTS` reports the package contents. Use
`OPTIONAL_COMPONENTS` to probe an optional feature, then test for its
imported target before linking. Use a fresh destination when switching
from a complete install to a minimal one so that installed files describe
the selected build. [Dependency table](PACKAGE-DEPENDENCIES.md)

## Runtime and source compatibility

Match the application to the installed headers and libraries. The CMake
package uses same-minor compatibility for numeric version requests, with
the installed version at least as recent as the request. A version-free
`find_package` resolves the selected prefix. It is appropriate when the
application already pins that prefix.

For a binary adapter, compare `nt_abi_version()` with the installed
`NT_ABI_VERSION` before creating handles. The current ABI version is
`1`. Public declarations and documented behavior are the compatibility
surface. A native calling-contract break requires a new ABI version.
Keep handles, callback storage, and borrowed frame or text views within
the lifetime rules of their originating SDK. Serialize access to each
native object. [ABI and lifetime guide](ABI.md)

Unicode behavior uses the SDK's pinned Unicode data. Applications can query
`nt_unicode_version()` in C. Treat grapheme boundaries, bidi ordering and
terminal widths as properties of the selected SDK data and width policy.
Persist application text as counted UTF-8 and application state in a
documented format for that state. [Text contracts](CONTRACTS.md#terminal-text-layout-and-caret-mapping)

| Platform evidence for the current candidate | Terminal path |
| --- | --- |
| macOS arm64 | Release and ASan/UBSan native and relocated tests, five installed language consumers and POSIX tty profiles. |
| Linux arm64 and x86_64 | Alpine native and relocated tests and five installed language consumers on both architectures. Debian arm64 core and Python checks, tmux, and xterm 398 truecolor, indexed and monochrome checks. |
| Windows x86_64 | Win32 native and relocated C/C++ builds, Go and Rust executables, the CPython extension, and a controlled console model. Actual Windows console and package execution are pending. |
| wasm32-wasi | The TypeScript reactor runs with a WebAssembly-enabled JavaScript host and caller-supplied I/O. |

On Linux, use a wheel built for the environment's architecture, Python
version and C library. Choose the terminal color and width policy for the
actual display. POSIX capability suggestions use the supplied terminal
hints. Windows uses native console records and VT output, with pointer
delivery depending on the console's input settings. [POSIX host contract](CONTRACTS.md#posix-terminal-host) · [Windows host contract](WINDOWS-HOST.md)

Monochrome output retains glyphs and drops cell colors. Use a text or
glyph cue for state such as focus, selection or a draft caret when the
terminal reports monochrome. Field Log's footer illustrates this approach.
[Field Log quickstart](QUICKSTART.md)

## Resolve common setup problems

| Symptom | What to check |
| --- | --- |
| CMake finds another SDK installation | Configure in a fresh downstream build directory with the absolute `CMAKE_PREFIX_PATH` of the selected SDK. Check the imported targets and `Nocturne_INSTALLED_COMPONENTS`. |
| CMake reports a required component missing | Select that component when building the SDK and install into a fresh prefix. A portable-core consumer can ask CMake to discover optional components without requiring them. |
| A binding uses an old header or archive | Point Python and Rust at the current `NOCTURNE_SDK_PREFIX` and rebuild. Rerun Go's `nocturne-config` and apply its generated build flags. |
| Rust cannot generate declarations | Set `LIBCLANG_PATH` to the directory containing the compatible libclang and confirm that CMake resolves the SDK for the Cargo target. |
| Python cannot preprocess the SDK headers on Windows | Start an x64 Visual Studio developer shell with the Windows SDK and select a matching C SDK prefix. |
| TypeScript loads the wrong module | Use the module bytes associated with the installed JavaScript package and load them through its documented host API. |
| A terminal glyph clips or shows a replacement | Check the negotiated width and glyph capabilities. Use plain, monochrome or indexed presentation for the capabilities the host reports. |

Installed SDK files include the MIT and Unicode licenses. The TypeScript
package also carries the notices for its linked WASI runtime. Keep the
notices with redistributed SDK artifacts. [MIT license](../LICENSE) · [Unicode data notice](../unicode/17.0.0/LICENSE.txt)
