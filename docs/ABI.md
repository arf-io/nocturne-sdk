# C ABI for language bindings

Build adapters against the installed C headers and libraries for their target.
Include `<nocturne/abi.h>` and compare `nt_abi_version()` with the header's
`NT_ABI_VERSION` before constructing native objects. The ABI version is `1`.
It identifies the native calling contract independently of the CMake package
version and the Unicode data version. Changes that break the native calling
contract require a new ABI version. Public C structs follow the target's C
ABI for alignment, enum representation, pointers, `size_t` and `bool`. Use
the target C compiler or declarations generated from those headers for
structs passed by value. Keep pointers, padding and native enums out of
serialized state and wire formats.

## Link the installed components

`find_package(Nocturne 0.1 CONFIG REQUIRED COMPONENTS nocturne)` loads the
portable core. `Nocturne_INSTALLED_COMPONENTS` lists the installed CMake
component names. `Nocturne_<component>_FOUND` is a boolean for each of
`nocturne`, `posix_host`, `windows_host`, `graph`, `spatial` and `simulation`. Add the names
to `COMPONENTS` to require them or `OPTIONAL_COMPONENTS` to probe them. Include
and link optional code through its `Nocturne::<component>` target. In a fresh
prefix, an omitted optional component has no installed headers or library.
Simulation requires spatial. The static targets use position-independent
code where the toolchain supports it, so adapters can link them into a
loadable module. Report optional capabilities to users based on the targets
the adapter linked. Keep native handles with the adapter that created them.
If several adapters bundle the core, use platform export controls to expose
only the adapter's entry points and avoid native symbol interposition.

## Own native handles and scope borrows

| Native lifetime | Adapter behavior |
|---|---|
| Successful `*_create` or `*_open` publishes an owned handle | Pair it with the matching destroy API and expose explicit disposal. Keep referenced parents, callbacks and resources live until their documented use ends. |
| `NtNodeId` names a mount inside a live tree | Keep it tied to the owning tree. Removal retires the subtree. Invalidate node wrappers when the tree is destroyed because a new allocation can reuse its address. Compare `NtOwnerToken` values only within their tree. |
| `nt_tree_frame` borrows the last committed frame | A successful publish or tree destruction ends the borrow. Failed publish preserves it. A binding can expose a scoped viewer or an explicit copy. |
| `nt_frame_cell` returns a borrowed glyph | End the borrow before the next successful frame mutation or destruction. A wide continuation points to the same lead and glyph. |
| `nt_tree_peek_intent` borrows a queued payload | Copy values that need to survive pop or tree destruction. The intent owns the bytes produced by the callback until this borrow ends. |
| `NtAllocator` is copied into the object | Keep the allocator context and callbacks valid through destruction. Release uses the allocator and the original allocation size and alignment. |

The individual API defines the validity window for its inputs and results.
The caller serializes access to each native object. Independent objects can
be driven separately. See the [component and host protocol](COMPONENT-PROTOCOL.md)
for tree, frame and input ownership.

## Callbacks and errors

Component callbacks run synchronously during the caller's native operation.
`nt_tree_mount` copies the callback table on success and retains its state
pointer. Keep language-side state and callback entry points rooted until the
registered disposer returns. A failed mount leaves that obligation with the
caller. The tree calls disposal once per successful mount with a disposer.
Subtree removal and tree destruction dispose descendants before their parent.
Callback contexts, event inputs and borrowed callback arguments expire when
the callback returns. Reentrant structural mutations return
`NT_RESULT_WRONG_PHASE`. Drive time and publish from the host operation.

Preserve the result number returned by a native call. `nt_result_name`
returns the C identifier for a known `NtResult` number and `NULL` for an
unknown number. A binding can translate known numbers to named errors and
retain the numeric value for forward compatibility. Where the language's
enum can hold only declared values, carry the result as an integer. A C
trampoline can convert `NtResult` to `int32_t` before the language boundary.
Interpret other result types, such as `NtStatus` from animation, under
their own contracts.

Catch language exceptions or panics before returning through C. Give each
adapter call its own place to save the original cause, with a lifetime
that covers any component disposal during the call. Return
`NT_RESULT_CALLBACK_FAILED` from a callback that returns `NtResult`. A
void disposer must finish releasing its resources before returning.

Read and clear the saved cause when the originating C call returns, and
report it through the adapter even when C returns `NT_RESULT_OK` or has a
void return. Failing focus and pointer-cancel notifications discard their
staged output while the enclosing operation keeps its own result and any
committed changes. One call can deliver several notifications. Keep the
application-visible error associated with that call, and start the next
call with no saved cause. Apply the operation's documented failure
semantics. For example, failed paint preserves the committed frame while
callback-owned working state can already have changed.

## Text and shared conformance

`NtByteView` is a counted byte slice. Text APIs report UTF-8 byte spans and
use the pinned SDK grapheme and width rules. UTF-8 validation and intent
payloads accept embedded zero bytes. Individual rendering and input APIs
apply their own restrictions. Pass explicit lengths, preserve complete
graphemes, and distinguish byte offsets, grapheme positions and terminal
cell coordinates. Empty views can be represented by `{NULL, 0}` where the
receiving API accepts an empty input. Use `nt_unicode_version()` for the
pin and the [text contracts](CONTRACTS.md) for invalid input and rendering.

The installed `abi_conformance_consumer` CTest builds a loadable C module
from a clean SDK prefix and invokes it through a C++11 executable. Run that
executable to get a fixture-versioned JSON record of the measured behavior.
The [fixture source](../tests/consumer/abi_conformance.c) specifies the
scenario for language adapters. It covers explicit time, geometry and wide
glyphs, a counted payload with an embedded zero, numeric sampling, callback
failure, reference invalidation and repeated disposal.
