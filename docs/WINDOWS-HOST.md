# Windows console host

A C application that owns a Windows console can link
`Nocturne::windows_host` and use `nocturne/windows_host.h`. A Windows build
includes this optional component by default. The portable SDK and language
packages can also be driven directly by an application-owned host.

Pass the console input handle and active console screen-buffer handle,
normally from `GetStdHandle`, to `nt_windows_terminal_probe`. The probe makes
no changes to the console. It suggests monochrome output, Unicode text and
pointer records for a console pair with observable extended input settings.
Choose the color mode, glyph capabilities and width policy to match the
actual terminal. Indexed mode takes a borrowed palette in the terminal's
index order. The probe recognizes redirected output as noninteractive.
Applications can send plain rows to a redirected stream with the portable
presenter.

To open a live host, supply the console pair, a live component tree, an
explicit time in microseconds, and the capabilities. The active screen buffer
must support VT output. A legacy console that refuses VT mode returns
`NT_RESULT_UNSUPPORTED_CAPABILITY`. The host copies options and borrows the
tree, handles, palette and callback contexts. The application owns one host
per process and gives it exclusive input and output until close. This optional
host API is exposed through C. Foreign language applications can continue to
use their own terminal drivers and dispatch normalized input through their
binding. [Windows console modes](https://learn.microsoft.com/en-us/windows/console/getconsolemode)

Drive `nt_windows_host_step` on nondecreasing caller time. Each step ticks the
tree, queries the actual viewport, publishes a changed size before dispatching
that resize, drains a bounded batch of console records, and presents any newly
published frame. The optional resize callback runs after publication and
before input. The optional input callback runs after an admitted event and its
reserved-key handler. Both callbacks may update application state or use tree
operations. The host refuses recursive calls during a step. An idle step
performs no terminal writes. A new frame clears and paints the current viewport
in place. Input polling uses the queued record count with no wait, and console
writes use the synchronous Win32 API.
[Console input records](https://learn.microsoft.com/en-us/windows/console/readconsoleinput)

The host translates key presses, repeats and releases into Nocturne events.
Named keys use the Windows virtual key. Printable text comes from the
UTF-16 console records and is dispatched as counted UTF-8 associated with the
key. A valid surrogate pair can span two steps and receive a release
record between its two UTF-16 units. The host drops unmatched surrogates.
A character committed by Windows on Alt-numpad release is delivered as a
text event. Reserved chords use the exact normalized key and modifier mask
on presses, repeats and releases. To reserve a shortcut regardless of
Caps Lock or Num Lock, include its lock-modifier variants and use the key
state in the reserved callback to choose when to act. The Windows
processed-input flag is disabled during ownership. The console record API
has no dedicated bracketed paste event, so `bracketed_paste` must be false
for this adapter.

Pointer coordinates are relative to the visible viewport. The host tracks one
console mouse, maps the five console buttons, and accumulates partial wheel
deltas into vertical and horizontal detents. A console focus-loss record
cancels tree interaction and clears the host's key and pointer state. Delivery
of that record depends on the console host. Windows can withhold mouse
records while Quick Edit is active. The adapter temporarily disables Quick
Edit when the console exposes its extended settings. When those settings
are hidden, the adapter leaves them intact and pointer delivery follows
the existing console configuration.
[Windows console input mode](https://learn.microsoft.com/en-us/windows/console/setconsolemode)

Use the optional alternate screen to keep the previous display contents.
Close restores the saved input and output modes and the cursor setting if the
host hid it. Close is idempotent and cancels the active tree interaction.
Open failures after setup attempt restoration before returning. A processing
failure closes the host and reports both the processing result and
`cleanup_result`. A reversed clock or invalid dispatch policy leaves the
report and the session intact. Destroy the host before releasing its borrowed
objects.

The application owns process shutdown. To handle Ctrl+Break or a console-close
notification, register a Windows console control handler after attaching to
the console. Have the handler signal the thread driving the host and let that
thread call `nt_windows_host_close`. On console-close notifications, coordinate
completion before returning from the handler and within Windows' deadline.
Forced termination or an expired deadline can leave the shared console
settings changed. [Windows console control handlers](https://learn.microsoft.com/en-us/windows/console/handlerroutine)

The installed C test builds against the optional CMake component and uses a
private console to exercise viewport presentation, Unicode input, key
reservation, pointer capture, failure cleanup and mode restoration.
