# Nocturne for Python

Build the wheel using an installed Nocturne C SDK, CMake and a C compiler.
Set `NOCTURNE_SDK_PREFIX` to the SDK prefix. The wheel embeds selected
native archives and generates its CFFI declarations from their installed
public headers. An empty `NOCTURNE_PYTHON_COMPONENTS` selects the core.
Select optional components with a comma-separated list of `posix_host`,
`graph`, `spatial`, and `simulation`. Simulation also selects spatial.
On Windows, build from an x64 Visual Studio developer shell with the
Windows SDK available and use a C SDK built for the same toolchain.
Select `graph,spatial,simulation` for the portable optional modules.

```sh
export NOCTURNE_SDK_PREFIX=/path/to/nocturne-install
python -m pip wheel ./bindings/python --no-deps --wheel-dir ./wheels
```

Install the generated platform wheel into your Python environment.
The wheel includes the SDK's MIT license and the Unicode license for the
tables embedded in its native extension.
Run `python bindings/python/examples/signal_meter.py` and enter `+` or `-`
to change the level, or `q` to quit. The example uses the same installed
package as any other application.
`nocturne.available_components()` reports the native components linked into
the wheel. `nocturne.raw` exposes their native CFFI types and functions.
Use `Text` as a context manager for owned, counted UTF-8. Its cluster spans
use native byte offsets. The text value can contain embedded zero bytes.

Use `Tree` as a context manager for an owned native tree. Mount a
`Component` beneath an ordinary layout or as the root. Supply a clock
to `tick`, publish an explicit frame size, and dispatch normalized `Input`
values. `DispatchError.delivery` describes an admitted event through any
callback failure. A `Frame` checks that its native publication is still
current. Reading a cell copies only the requested grapheme. The `Text`
and `Tree` objects serialize their own native operations. Mount and update
the stock label with `mount_label` and `set_label`. For optional controls,
`mount_native` and `call_native` scope access to native operations under the
same tree lock and retain the native component in the Python tree.

Callback contexts expire when their callback returns. Python exceptions
from callbacks arrive in `CallError` after the native call. A focus
notification can fail after focus has committed, so the error carries
the Python cause and an empty native result. Closing the tree disposes
its mounted components and reports any failure from teardown.
