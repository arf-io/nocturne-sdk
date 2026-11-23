# Nocturne for TypeScript and WebAssembly

Build using Node, CMake, a WASI C toolchain, and an installed
`wasm32-wasi` build of Nocturne. Use the same CMake toolchain
for the C installation and this bridge.

```sh
cd bindings/typescript
npm ci
NOCTURNE_SDK_PREFIX=/path/to/wasi-install \
NOCTURNE_WASI_NOTICE_DIR=/path/to/runtime-notices \
NOCTURNE_WASI_TOOLCHAIN=/path/to/wasi-toolchain.cmake npm run build
npm test
npm pack
```

The package includes JavaScript, TypeScript declarations and the
WebAssembly module. It includes the Unicode license from the installed
C SDK. Set `NOCTURNE_UNICODE_LICENSE` if the SDK uses a custom license
location. Packaging checks for the runtime notices. Point
`NOCTURNE_WASI_NOTICE_DIR` to the notices for the selected toolchain's
linked runtime libraries when preparing the distributable.
Load the module bytes with the loader appropriate to the
host, then call `loadNocturne`. The loader uses WebAssembly, Web Crypto,
and the standard UTF-8 encoders and decoders.

```ts
import { loadNocturne } from "@arf-io/nocturne-sdk";

const sdk = await loadNocturne(wasmBytes);
const text = sdk.createText("👩‍💻");
try {
  console.log(text.cluster(0));
} finally {
  text.close();
}
```

`Text` owns counted UTF-8 in the native instance. Grapheme spans use byte
offsets. The value can contain embedded zero bytes and leading U+FEFF.
The binding refuses malformed bytes and unpaired UTF-16 surrogates.
Read the stable result number and its native identifier from
`NativeError`. Separate loader calls create separate native instances.

Use `sdk.lineBreaks`, `sdk.scriptRuns` and `sdk.createBidi` for
Unicode analysis with UTF-8 byte offsets, including when the input
is a JavaScript string. Inspect logical scalars with `bidi.scalar`.
Use `bidi.reorderLine` on a half-open byte range within one paragraph
for visual order and mirrored scalars. Close the analysis when
finished. `sdk.unicodeVersion` reports the loaded data version.

`sdk.measureGlyph` reports a single grapheme's width and presentation.
Use `sdk.layoutText` with counted UTF-8, or `sdk.createTextLayout`
to snapshot a `Text` and retain its cluster IDs. Layouts own their
source, glyphs and geometry. Inspect visual entries with
`layout.cluster`, and use `layout.hit` and `layout.caretPosition`
to map between grid edges and UTF-8 byte carets. Close the layout
when finished.

For example, `sdk.layoutText("ab cd", { wrap: "word", bounds: {
columns: { min: 0, max: 4 }, rows: { min: 0 } } })` wraps at byte
offset 3. A leading caret maps to the second row, and a trailing
caret at the same byte maps to the first row.

Draw the layout with `surface.paintText` or `paint.paintText` in a
mounted paint callback. Supply a base style and optional byte spans.
The last span containing the first byte of a cluster supplies its
style. Attach solid, gradient or palette `PaintSource` values to a
style to sample either color channel in layout coordinates. Use
opacity one for full opacity. `sdk.samplePaint` and
`sdk.samplePaintRGBA` accept the same caller clock and motion
policy. A failed draw preserves the destination. Mounted draws use
the component owner.

Use `sdk.compositeColor` for continuous straight-alpha channels or
`sdk.compositeRGBA` for quantized output. Omitted options select normal
source-over in linear sRGB. In a partial options object, an omitted `space`
also selects linear sRGB. Set `space: ColorSpace.srgb` for encoded sRGB.
Set a blend mode, Porter–Duff operator,
color space and gamut policy as needed. Give dissolve a `bigint` seed
and sample index. The parse helpers accept native canonical lowercase
names.

`sdk.defaultRippleField` and `sdk.defaultWaveField` return descriptors
for `sampleField`, `fieldColor` and `fieldDeadline`. Supply continuous
coordinates and a `bigint` clock in microseconds. In a mounted paint
callback, `paint.drawField` samples solid or palette backgrounds at
cell centers inside the component's clip.
A per-cell error leaves earlier field cells painted if the callback
handles the error and returns success.

Use `surface.paintTextEffect` or `paint.paintTextEffect` with a
`TextEffect` for reveal, hide, scramble, flipboard, and trails.
Pass elapsed microseconds as a `bigint`. A synchronous transform
receives owned sample data and a pose. Return a new pose, or return
`undefined` or zero to keep it. A nonzero signed native status
refuses the draw and is returned verbatim on `NativeError`.
Use `sdk.textPoseMotion` and `sdk.textPoseNoise` to compose
offsets and hue. A thrown transform cause is available on
`CallError`. Reduced motion skips transforms and draws the settled
effect. Closing a layout during a transform invalidates later calls
and lets the admitted draw finish.

Use `createTree` for an owned native component tree. Mount a stock box
and its labels, then call `publish` with a cell size. Read individual
cells from `frame`. The frame expires when another publication succeeds
or the tree closes. Node references belong to the tree that issued them.
Use `tree.sameNode(delivery.target, mountedNode)` to compare references from
dispatch, focus, hit tests or geometry with a mounted node. These operations
may return fresh objects for the same native node. The comparison checks the
issuing tree, slot and generation. It returns false if either reference is
absent. Matching references may still refer to a removed node.
Call `close` on the tree and any owned text when finished.

Use `mountComponent` to provide synchronous TypeScript callbacks for
measurement, layout, update, painting, events, and disposal. Advance the
tree with `tick` using an explicit microsecond `bigint`. Callback contexts
expire on return. A thrown cause reaches the originating call in
`CallError`. Focus notification failures can accompany a committed focus
change, so a `CallError` can have an empty native error.

Dispatch normalized input with an explicit time and an optional
reserved-key policy. An admitted callback failure includes its delivery
report in `DispatchError`. The host can read and pop copied intents.
`writeTerminalRows` streams the visible glyphs of a published frame to
a line-oriented host. Use individual frame cells for style-aware output.

Mount lists, radio groups and selects with `tree.mountChoice`. The tree
copies the entries and labels. Omit `selected` for no selection and call
`tree.setChoice(node)` to clear it. Decode a queued stock-control intent
with `sdk.decodeControlIntent`. Selection changes from user input carry
the index as a `bigint`. Programmatic setters update state directly.

Mount a card with `tree.mountNotification` and a nonzero `bigint` ID. The
tree copies the title, message and action labels. `expiresAt` uses the
same absolute microsecond clock as `tree.tick`. Omit expiry and actions
and set `dismissible: false` for a persistent status card. Read
`tree.notification` for status and selection. Decode the owned item from
`tree.peekEventIntent()` with `sdk.decodeNotificationIntent`. Expiry and
`tree.dismissNotification` change state without queuing an intent.

`tree.mountCursor` and `tree.mountWait` accept caller-timed phases in
microseconds. A zero phase leaves the indicator steady. Scheduled ticks
under reduced motion paint phase zero. Read current normalized values with
`tree.scrollbar` and `tree.progress`, and read a held pointer press with
`tree.buttonPressed`.

Use `tree.applyProperties` for atomic property writes and
`tree.createBindingGraph` to combine inputs, pre-commit property reads and
computed values. A compute function receives copied values and returns one
typed property value or a nonzero signed native result. An exception or
asynchronous return fails the application with a `CallError`. Removing a
referenced node makes its graph stale before compute runs. Close the graph
explicitly, even if the tree has closed.

`tree.configureSchedule` sets capacity. `tree.scheduleBatch` copies its
assignments, including text. `tree.nextScheduled` gives the earliest
deadline and `tree.runSchedule` runs due batches in deadline and
registration order up to a caller-supplied budget. Use microsecond
`bigint` timestamps on the same caller clock. If a batch fails, it is
consumed and `ScheduleError.report` identifies it.

In a routed event callback, use `event.emitFields` to stage typed fields or
`event.applyRules` to match an `EventKind` and `EventPhase` and derive an
intent from current properties. Predicates and derivations receive copied
properties and an owned snapshot of the normalized input. They return a
value or a nonzero signed native result synchronously. A failed rule
discards the enclosing callback's staged intents, even if the callback
catches the failure. `tree.peekEventIntent` copies the head of the same
FIFO used by `tree.popIntent`.

Use `tree.subscribeIntent` and `tree.deliverIntents` for explicit delivery
of committed intents. The caller supplies a budget for FIFO items.
Matching receivers run synchronously in registration order. Delivery
ignores an ordinary return value. `IntentDeliveryError.delivered` reports
the number consumed if a receiver throws or returns a promise. Canceling the subscription, removing its owner, or
closing the tree releases the receiver. Other live subscriptions can
receive previously committed intents from a removed source.

Create sheets with `sdk.createStyleSheet` and pass them in declaration
order. Override rules win first, then selector specificity, then later
declarations. Use `bigint` for caller-defined roles, regions, state
masks and cluster IDs. `sdk.resolveStyle` returns an owned property
value when there is a match. `tree.applyStyles` uses the tree's current
ancestry and updates matching targets in one transaction.

Pass sheets to `surface.paintStyledText` or `paint.paintStyledText` to
overlay foreground and background on visible Unicode clusters. Selector
ranges use byte offsets in the original UTF-8 source, and character
intervals use Unicode scalars. A winning color replaces the channel's
procedural paint. Call `close` on each sheet when finished.

Use `tree.defineProperties` to attach typed properties to a node with
no property table. Each descriptor declares its ID, value kind and read
callback, and can request layout or paint invalidation. Omit `finish`
for a read-only property. An optional synchronous `prepare` can return
`{ prepared: value }` to pass that value to `finish`, or return a
nonzero native result to refuse the write. `finish` receives a commit
flag and must complete synchronously. Do all fallible work in
`prepare`. A thrown finish callback is reported on the originating
call through `CallError`, and the native transaction continues.
`tree.properties` lists metadata in declaration order for stock and
custom nodes.

Use `tree.captureState` with an ordered list of writable properties.
`snapshot.bytes` returns a copy of the versioned binary encoding, and
`sdk.importStateSnapshot` validates and copies a `Uint8Array`.
`tree.restoreState` uses a corresponding list of live destinations
with the same property IDs and types. A snapshot can outlive its source
tree. If any destination rejects a value, the restore leaves all
properties as they were. Call `close` on each snapshot when finished.

Use `editor.setHistoryLimit` and `editor.addHistory` to store submitted
history separately from undo. `editor.historyEntry(0)` reads the newest
entry. Stepping forward past it restores the draft and its selection.
`editor.handleKey` uses an explicit map of normalized chords and repeat
policies. Create an owned register bank with `sdk.createEditorRegisters`
to use copy and paste actions. A register read copies UTF-8 text and
returns whether the slot is occupied. Close the editor and bank after use.
Use `NamedKey` and `KeyModifier` for named keys and modifier masks.
A handled chord validates its matched action.

Mount an editor with `tree.mountEditorView`. Set `submitOnEnter` to
emit a submission intent, or pair a read-only editor with `scrollKeys`
for a keyboard reader. `tree.scrollEditorView` requests an offset and
`tree.editorViewOffset` reads the clamped value after publication.
Mount the editor under a panel to frame it.

The view copies keys and styles and retains the editor and optional
register bank from the same SDK instance. Closing a handle makes its
standalone methods stale while the view keeps its native resource until
removal or tree close. Shared resources can be used by one tree operation
at a time. Calls made during a conflicting borrow report `WRONG_PHASE`.
Invalidate each affected view for layout after direct text changes or
for paint after direct selection changes, then republish.

Plot options include sampling mode, per-axis automatic domains, data
limit, sample stride and caller-timed reveal. Bars space entries uniformly
along the x-axis. Use `sdk.projectPlot`
for continuous cell-edge coordinates and `sdk.clipPlotSegment` for
directed clipping. Display signed fixed-point integers as `bigint`
with `tree.mountDigits` and `tree.setDigits`.

Use `sdk.ease` for a normalized fraction and `sdk.sampleKeyframes` for
a track of values. Put the first key at zero and later keys in strictly
increasing order. A key's easing applies to its outgoing segment. Track
time and key times are unsigned microsecond `bigint` values.

`sdk.createPlayback` starts local time at zero on the supplied host
clock. Use `elapsed` to sample a track or spring. Calls to `pause`,
`resume`, `seek` and `cancel` return a new playback snapshot.
`sdk.createSpring` initializes a segment with caller-selected
tolerances. Use `sample(nowUs, true)` for reduced motion and
`retarget` to make a new segment with a velocity policy from
`RetargetPolicy`. Playback snapshots and springs need no cleanup.

`sdk.createTimeline` compiles a flat group of tween clips. Use
`sdk.createTimelineTree` for nested sequence and parallel groups,
per-node delays, and tween, keyframe or seeded random sources.
The compiled timeline owns copies of its keys and timing edits.
Read ascending channel IDs through `channelCount` and `channelAt`.
Close the timeline after use. `sdk.limitTrackRate`, `sdk.mapTrackTime`,
`sdk.sampleRandomTrack` and `sdk.scheduleTrack` also work independently
of a timeline.

Use `sdk.pathLength` and `sdk.samplePath` to move through continuous
coordinates. A sample reports position, travel tangent and the
distance resolved under the path's boundary policy. Use
`sdk.projectPanelPoint` to map a point through anchor-relative
scaling and rotation, then translation. Set scale to `{ x: 1, y: 1 }`
and opacity to one for an identity pose. `sdk.samplePanelPose`
samples a transition on caller-owned microsecond time, or selects
its authored target under the `"reduced"` policy.

Inside a custom paint callback, `paint.projectPanels` projects
frame crops into viewport-local clips. Sources can be published tree
frames or offscreen frames from the same loaded SDK. A layer can
borrow the destination tree's prior publication. The call holds its
sources through completion.
Reentrant access to the borrowed trees or active paint context
reports `WRONG_PHASE`. A layer can supply a fixed viewport-local
`SurfaceMask` and a synchronous `remap` that returns a source point
and opacity for each sample. The call copies the mask bytes before
sampling. A thrown remap cause reaches `CallError` and preserves
the prior paint output, so the component can handle it and continue.
The mounted component owns all contributions.

Use `sdk.createOffscreenFrame` for an owned mutable frame. It exposes
current cells and physical backgrounds, including their separate
owner tokens. It supports resize, grapheme placement and clear.
Close the frame when done.

`sdk.frameFromCells` copies glyph and background descriptors in array order.
Use zero columns, an empty glyph and transparent black foreground for a
background-only cell. `sdk.sampleImage` reads a straight RGBA8 image at
normalized coordinates. `sdk.rasterizeImage` samples onto a physical grid
and returns an owned frame. Supply row stride in pixels, including padding.
Each call reads the current pixel array. Close each resulting frame when done.

`frame.surface()` captures a root view. `surface.child` captures
parent-local bounds and copies any alpha samples. A view clips against
the current frame after resize and keeps its captured bounds. The view
can fill, draw or erase glyphs and compose source layers with
`compose` or `projectPanels`. Layers clear, share or map owner tokens.
Offscreen sources may alias the destination and sample its pre-call
cells. Owners and times use unsigned `bigint` values. A failed owner
map or remap rolls back the batch, and reentry on a borrowed frame
reports `WRONG_PHASE`. Close the owning frame to expire its views.

`sdk.createParticleEmitter` copies a seeded schedule. Use `sample` for
the live particles within its presentation capacity, `particle` to inspect
one birth independently of that capacity, and `trail` for its recent path.
Clocks and IDs are `bigint`. `deadline` reports the next update under the
caller-selected motion policy. Close the emitter when done. Returned
samples are independent copies.
`tree.mountParticleView` copies the authored emitter and one-cell glyph
settings into a clocked leaf. `paint.drawParticles` draws an owned emitter
through a custom component's clip. Reduced motion keeps birth and expiry
timing while drawing stationary particles without trails.

`sdk.createParticleDeposits` owns a bounded store of contacts. Supply
monotonic microsecond times to `add` and `advance`. The copied results
from `piles` and `releases` preserve deposit IDs, mass and times as
`bigint`. Use `deadline` to drive updates and `paint.drawParticleDeposits`
to draw in a custom component. Call `close` when the store is no longer used.

`sdk.sampleVisualBranch` samples seeded, parent-linked segments at an
explicit microsecond time. `VisualBranchNoParent` marks the root.
`sdk.visualBranchDeadline` reports the next update, and
`paint.drawVisualBranch` draws in a custom component using a one-cell glyph.

Run `node examples/signal.mjs` after building. Enter `+` or `-` to
change the level and `q` to quit. The host supplies the input, clock
and output pipe.

## Optional native modules

The WASM build links the optional graph, spatial and simulation targets
that are present in the installed WASI prefix. Inspect
`sdk.availableComponents` before using those modules. Calls to an
unavailable module report `NT_RESULT_UNSUPPORTED_CAPABILITY`.

```ts
const tree = sdk.createTree();
try {
  const parent = tree.mountBox("column");
  tree.mountLabel("Trail connections", { parent });
  if (sdk.availableComponents.includes("graph")) {
    const graph = {
      nodes: [{ id: 1n }, { id: 2n }],
      edges: [{ id: 1n, source: 1n, target: 2n, directed: true }],
    };
    const view = tree.mountGraph(graph, { parent, focusable: true });
    tree.setGraph(view, { ...graph, layout: "radial" });
  }
  tree.publish({ columns: 36, rows: 12 });
} finally {
  tree.close();
}
```

Graph records are copied by the mount and setter. Graph IDs are unsigned
64-bit `bigint` values. Call `tree.remove` to dispose a mounted graph before
closing the tree. Use `sdk.spatial` for vector operations, affine
transforms, 3D paths, cameras, clipping and picking. Start a transform
with `defaultTransformOptions`. Apply it to a point or a direction and
use `compose(outer, inner)` to apply the inner transform first. Convert
world coordinates to view coordinates with `toView`. In view space
depth is minus Z. Projection maps the frustum to screen coordinates
increasing right and up from −1 to +1.

`project`, `clipSegment` and `pick` return `undefined` on a miss.
`clipTriangle` returns an empty array when the clipped polygon is empty.
Clip vertices carry weights for the original inputs, and a hit carries
the original triangle index and unsigned 64-bit `bigint` IDs.
`sdk.spatial.createPath` copies the polyline. Sample signed distance
with the selected `Repeat` boundary and close the path when finished.

Use `sdk.spatial.createMesh` for indexed triangles and `decodeOBJ` for
counted Wavefront OBJ. A mesh's `data()` returns copies.
`sdk.spatial.rasterize` draws ordered instances with optional transforms,
camera and textures. Supply an opaque white tint to use a texture's own
colors. Samples carry geometric coverage, UVs, depth and unsigned
`bigint` object and primitive IDs and owner tokens. The snapshot remains
usable after you close the input meshes.

Use `raster.cellHit` to retrieve the covered sample closest to a terminal
cell center, or `raster.frame` to create an independently owned offscreen
frame. In a mounted paint callback, `paint.paintRaster` draws through the
component's clip with its mount owner. Close the raster and any frames
when finished.

`sdk.spatial.surfacePose` maps time-zero world placement to signed
elapsed seconds. `sampleSurface` returns instantaneous position and
velocity. `sweepPoint` finds the earliest contact with translating
triangles, including coplanar entry, and returns `undefined` on a miss.
Exact time ties retain input order.

`sampleWater` takes caller-owned wave modes, world X/Z and seconds.
`sampleFlow` evaluates serial stages at unsigned `bigint` microseconds
and returns inlet and outlet ports plus retained volume. Use
`flowSectionPoint` to move a dry vertex along a unit depth direction.
`emitSpray` produces a value batch from the offered volume and caller
seed. `sampleSpray` evaluates one drop at caller time and returns
`undefined` before birth or at expiry. IDs, seeds and times retain all
64 bits as `bigint` values.

`stepSurfaceTransport` advances a caller-defined water, foam and heat
network by an explicit `bigint` interval in microseconds. Use the
`"drain"` destination for outlets. Each successful step returns copied
cells and the external budget, so the original cells can be reused.

`createHull` copies a convex boundary in body space. Use `immersePlane`
for infinite water and `immerseWater` for a finite triangle patch. Pass
the immersion to `hullBuoyancy` and apply the returned force and torque
with `stepFloatingBody`. Use `floatingBodyTransform` to place the body
in world space. Close the hull when finished. `exchangeLinearDrag` and
`exchangeAngularDrag` report the updated pair velocities, impulse and
dissipated energy for one interval.

## Native API

Use `sdk.raw` to call the public C functions linked into the installed
WASM module. It reports native signatures, constants, record sizes and
field offsets. The native easing API evaluates normalized progress
through ordinary WASM value slots.

```ts
const sample = sdk.raw.withArena(arena => {
  const easing = arena.value("NtEasing");
  arena.setUnsigned(easing, sdk.raw.size("NtEasing"), sdk.raw.constant("NT_LINEAR")!);
  const progress = arena.value("double");
  arena.setFloat64(progress, 0.25);
  const output = arena.value("double");
  const pointer = arena.value("pointer");
  arena.setPointer(pointer, output);
  const status = arena.call("nt_ease", easing, progress, pointer);
  if (arena.unsigned(status, sdk.raw.size("NtStatus")) !== 0n) {
    throw new Error("Native easing failed");
  }
  return arena.float64(output);
});
```

`tree.withNative` lends the native tree synchronously and adopts stock
nodes returned by a native mount. Keep using managed `publish` and
`close` for a managed tree. `sdk.raw.callbacks` supplies native
function addresses and opaque contexts for C callback contracts.
Close each registration after its native borrowers are destroyed.
Temporary records expire with their arena. Follow the C ownership and
pointer contracts for native resources and borrowed values.
