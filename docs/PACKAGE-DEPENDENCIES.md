# SDK package dependencies

The [package graph](package-dependencies.json) defines permitted hard module
edges. It describes ownership and dependency direction across milestones.
Several modules can share one library or install component. The currently
installed CMake target for the portable core is `Nocturne::nocturne`.
`Nocturne::posix_host` is a separate target on POSIX terminal platforms.
`Nocturne::windows_host` is a separate target for the [Windows console](WINDOWS-HOST.md).
Set `NOCTURNE_BUILD_WINDOWS_HOST=OFF` to omit its library, header and CMake component.
`Nocturne::graph` is a separate portable target for graph layout and
mounted graph views. It links the core. Set `NOCTURNE_BUILD_GRAPH=OFF`
to omit its library, header and CMake component.
`Nocturne::spatial` installs vectors, transforms, distance-sampled paths,
cameras, mesh rendering, swept contact and mounted raster paint. It links
the core. Set `NOCTURNE_BUILD_SPATIAL=OFF` to omit its library, three public
headers and CMake component.
`Nocturne::simulation` installs waves, flow, spray, surface transport,
hull immersion, buoyancy, fluid exchange and floating-body steps. It links
`Nocturne::spatial`. Set `NOCTURNE_BUILD_SIMULATION=OFF` to omit its library,
public header and CMake component while retaining spatial. Disabling spatial
also disables simulation.
The package graph records the first delivery milestone of each module.

Base provides common status, geometry, allocator and identifier vocabulary
for new APIs. Motion keeps the usable M0 time and path APIs. Text depends on base. Cell storage uses base for value types and
text for validating complete renderable clusters. Paint composes those layers
through frames and an explicit sink. Paint also uses motion's easing, tween and
keyframe sampling for animated fields and opacity. The portable component tree builds on paint and shared
event types. The tree owns eligibility and input state because frame
publication and removal can invalidate focus or capture synchronously.
Tree-level state transitions deliver typed cancellation/loss callbacks.
M5 routing consumes the tree's committed path and input-state services and
supplies selection, consumption and traversal policy. The terminal host
handles platform I/O and owns an explicit sink and driver. Application code
may supply its own driver. The POSIX target links the core and can be omitted
by setting `NOCTURNE_BUILD_POSIX_HOST=OFF`. A fresh install omits its header
and library in that mode. The [C ABI guide](ABI.md) lists the CMake discovery
variables used by language adapters.

Optional 3D geometry and simulation build downstream of portable components.
A simulation consumer can opt into the simulation package and its spatial
dependency. A core consumer can build, install and run with both absent. An
extension may use base units, time, paint, and component hooks without
extending the core's node dispatch. Language SDKs expose equivalent optional
modules through optional dependencies or capability discovery. Standard
bindings themselves require the portable core. A TypeScript/WASM host passes
normalized input and cell frames across its bridge.

Controls, transitions, property binding and styling, data views and visual
effects are public component families. Direct imperative component usage
works without creating a binding or loading a stylesheet. The SDK contains
no dependency on a language adapter or example. Host-specific APIs stay
outside the portable core. Examples may choose a host, load optional modules
and provide their own content, identity, event policies, and application
state. Their source, assets and private headers are outside the public link
and include graph.
