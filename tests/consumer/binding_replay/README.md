# Installed binding replay

The fixture mounts a stock Unicode label and a custom meter in a column.
It uses public C, Rust, Python, Go, and TypeScript entry points to compare
published cell geometry, caller time, a reserved key and its text, a delivered
key and its counted intent, a paint failure and recovery, and 514 disposals.
Each program also calls the native tween sampler at the same six times and
checks a full-width iteration count and an invalid configuration.

The comparison requires exact integer and Unicode observations. It allows
absolute error up to `2e-14` in each sampled value and uneased progress for
this finite fixture. It checks the sine-easing formula and compares each
binding with C. See the [release verification guide](../../../docs/RELEASES.md).

From the repository root, run the macOS arm64 verifier against an exact Git
commit and fresh scratch directory. Supply a full installed native C SDK
and an installed wasm32-wasi SDK built with the supplied CMake toolchain.
The local npm cache must contain the locked TypeScript compiler. The Python
wheelhouse supplies the build and CFFI runtime dependencies. The notices
directory contains the licensing files for the WASI runtime used by Zig.

```sh
zsh tests/consumer/binding_replay/verify.zsh COMMIT SCRATCH \
  NATIVE_SDK WASM_SDK WASM_TOOLCHAIN NPM_CACHE WHEELHOUSE WASI_NOTICES
```

The verifier builds C against the installed prefix, extracts the locally
packed Rust crate, installs the Python wheel and its dependencies into
a fresh package directory,
resolves Go from a local module proxy, and installs the npm tarball in a
separate project. It writes the five traces to `SCRATCH/traces` and the package
checksums to `SCRATCH/artifacts/SHA256SUMS`.
The Go consumer's generated `cgo-build-flags.zsh` contains the compiler flags,
linker flags and build tags for the selected C SDK.
