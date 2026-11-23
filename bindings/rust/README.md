# Nocturne for Rust

Build against the installed Nocturne C SDK. Set `NOCTURNE_SDK_PREFIX` to its
prefix and place CMake and libclang on the build host. Set `LIBCLANG_PATH`
when libclang lives outside its normal search locations. The Cargo build
generates the `raw` declarations from the installed public headers and links
the static targets exported by that SDK. The crate checks the installed C
ABI version when creating an owned tree or text object.

```toml
[dependencies]
nocturne = { path = "/path/to/nocturne-sdk/bindings/rust" }
```

```sh
export NOCTURNE_SDK_PREFIX=/path/to/nocturne-install
cargo test --manifest-path bindings/rust/Cargo.toml --locked
```

| Cargo feature | Installed component |
| --- | --- |
| Default | `nocturne` |
| `posix-host` | `posix_host` |
| `graph` | `graph` |
| `spatial` | `spatial` |
| `simulation` | `simulation` and `spatial` |

The enabled features require their installed components at configure time.
The `raw` module accepts the same inputs and follows the same borrowing and
ownership contracts as the corresponding C headers.

`Tree` owns its native handle and serializes operations through mutable Rust
access. A `Node` belongs to its original live tree. `Frame` and `Intent` borrow
committed native storage through an immutable tree borrow. Read individual
cells and counted payloads while those borrows are live. `Text` owns validated
UTF-8 and uses the SDK's byte spans and grapheme IDs.

Use `Tree::dispatch` for normalized keys, text, pointer input, wheel input,
resize and cancellation. The caller supplies the timestamp and any reserved
key chords or default focus navigation. `DispatchError::delivery` describes
an admitted event even if a callback failed. A pre-admission refusal has no
delivery report. Component callbacks can read keys, pointer input and local
coordinates from the current `Event`.

Mount a stock text label with `Tree::mount_label` and replace its options
with `Tree::set_label`. The native label owns copies of the supplied text.
`Tree::mount_native` and `Tree::call_native` let a Rust tree use SDK controls
and optional components directly through the installed C declarations.
These methods are unsafe because the caller supplies the native operation
and is responsible for its argument and ownership contracts.

Implement `Component` to participate in native measure, layout, update,
paint and event processing. A successful mount transfers the Rust value to
the tree. Removing the mount releases that value. A Rust panic in a callback
is captured with unwinding enabled and is reported after the originating C
operation in `CallError`. Its `native` field carries a native error when one
occurred. A committed focus notification can produce a callback failure with
`native` empty. `Tree::close` reports failures from teardown and releases the
tree. Dropping the tree also releases it.

The [component test](tests/component.rs) mounts an original Rust component
inside a native layout and exercises explicit time, counted intents,
graphemes, callback failures and disposal.

Run the [Signal example](examples/signal.rs) for a stock title and a custom
meter driven by normalized key input. Enter `+`, `-`, or `q` on separate
lines. The application renders a plain cell snapshot after each change.

```sh
cargo run --manifest-path bindings/rust/Cargo.toml --example signal
```
