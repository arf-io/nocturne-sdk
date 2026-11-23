# Build a terminal app with Nocturne

Field Log is a small C99 application for keeping notes during a session.
It lets you edit a draft, save with Enter or a focused button, navigate the
saved list, and read a guide beside it. A custom component updates the
pulse in the header while you work. Notes are stored in memory for the
life of the process. [Field Log source](../examples/field-log/main.c)

For native adapters, see the [C ABI guide](ABI.md) for installed component
discovery, ownership, callback lifetime and conformance. The
[installation and compatibility guide](COMPATIBILITY.md) covers native
prefixes, optional components and setup problems. The [binding guide](BINDINGS.md)
links to language-specific consumers.

## Install or unpack the SDK

Use a POSIX terminal, a C compiler and CMake 3.20 or newer. Build and
install from the SDK checkout.

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DNOCTURNE_BUILD_POSIX_HOST=ON
cmake --build build-release --parallel
cmake --install build-release --prefix "$PWD/nocturne-install"
sdk="$PWD/nocturne-install"
example="$PWD/examples/field-log"
```

With a complete release archive for your platform, unpack it and use
its installed prefix and bundled example. This example uses the macOS
arm64 archive name.

```sh
archive=nocturne-sdk-1.0.0-darwin-arm64-complete
tar -xzf "$archive.tar.gz"
sdk="$PWD/$archive"
example="$sdk/share/nocturne/examples/field-log"
```

## Build the standalone application

Build in a new directory so CMake resolves the installed package.

```sh
cp -R "$example" ./field-log-app
cmake -S ./field-log-app -B ./field-log-build -DCMAKE_PREFIX_PATH="$sdk"
cmake --build ./field-log-build
./field-log-build/field_log
```

Type a note and press **Enter** to save. **Tab** and **Shift+Tab** move
focus between the draft, Save button, saved notes, guide and guide
scrollbar. Use **Up** and **Down** in the list and **Page Up** and
**Page Down** in the guide. In the draft, **Up** recalls a previously
submitted note and **Ctrl+Z** undoes an edit. **Ctrl+Q** quits and
restores the terminal. Pass `--reduced-motion` to keep the pulse steady.
Resize the terminal while the app is open to see the layout adapt.
In monochrome, the footer names the active control and shows the draft's
one-based grapheme insertion position while the draft has focus.

The optional terminal check requires Python 3 and tmux. It drives the
same executable in a separate tmux server and compares the tty settings
before and after quit.

```sh
python3 ./field-log-app/verify_terminal.py ./field-log-build/field_log
```

## Build your own component tree

1. Use `find_package(Nocturne 1.0 CONFIG REQUIRED)` and link
   `Nocturne::posix_host` for the POSIX adapter. Windows applications
   can use [the Windows console host](WINDOWS-HOST.md). Link
   `Nocturne::nocturne` for the portable core with a host of your own.
2. Create an `NtTree`. Mount a box layout or another container, then add
   your components. Set `focusable` for keyboard controls and
   `hit_testable` for their pointer regions.
3. Create any editors before mounting their views. An editor view borrows
   its editor. Pass explicit key bindings for editing commands. Use the
   selected terminal capability report for width and output choices.
   With indexed output, supply a palette such as `nt_xterm256_palette`
   and keep its storage alive until the host closes.
4. Open the tty adapter, pass it monotonic microseconds and call
   `nt_posix_host_step`. Keep the caller's clock and polling policy
   explicit. The adapter handles input, layout at terminal resizes and
   presentation. Use tree update and input deadlines to bound the wait.
5. Drain `nt_tree_peek_intent` and decode control intents before popping
   them. The host's optional `after_input` callback lets the app apply
   actions before the next input event. Field Log copies and clears the
   draft there so each Enter saves the text preceding it, even when
   several submissions arrive together. It updates the list and progress
   and records editor history explicitly.
6. Close and destroy the host before destroying the tree. Destroy borrowed
   editors and other app-owned component state after removing their mounts
   or destroying the tree.

[Controls and runnable fixtures](CONTROL-REFERENCE.md) · [Custom component tutorial](CUSTOM-COMPONENT.md) · [API contracts](CONTRACTS.md)

## Optional surface simulation

Surface Lab samples waves, computes hull displacement and buoyancy, and
advances a caller-owned body snapshot. It passes the results to a stock
panel, a stock line plot, and a small paint component. Build the standalone
example from `share/nocturne/examples/surface-lab` in the alpha prefix or
`examples/surface-lab` in the source checkout. It requires
`Nocturne::simulation` and presents a plain frame for the chosen step count.
