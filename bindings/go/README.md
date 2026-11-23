# Nocturne for Go

Build with Go 1.23 or newer, cgo, CMake 3.20 or newer, a C11 compiler
and a matching installed Nocturne C SDK. The configuration command
resolves installed CMake targets, selects archives in dependency order
and includes an SDK content hash in the Go build settings.

Run these commands in a POSIX shell from the directory containing the
SDK source checkout. On Windows, use a POSIX-compatible shell with a
Windows GNU-targeting C compiler for cgo and a matching C SDK.

```sh
cd bindings/go
sdk_profile=$(mktemp)
go run ./cmd/nocturne-config -prefix /path/to/nocturne-install > "$sdk_profile"
. "$sdk_profile"
rm "$sdk_profile"
go test -tags "$NOCTURNE_GO_TAGS" ./...
go run -tags "$NOCTURNE_GO_TAGS" ./examples/signal
```

Enter `+` or `-` to change the Signal meter level and `q` to quit.
The example accepts line-oriented input and leaves terminal control with
its host. A clean consumer can import
`github.com/arf-io/nocturne-sdk/bindings/go` using the built local module.

The configuration command selects the core by default. Use
`-components posix_host,graph,spatial,simulation` to request optional
modules from a full POSIX SDK. Select `windows_host` for a native
Windows SDK. Simulation also selects spatial. A required
component that is absent causes configuration to fail.
`AvailableComponents()` reports the modules compiled into the package.

`Text` owns a counted UTF-8 value. Its cluster spans use byte offsets.
`Tree` owns native components and publishes frames at caller-selected
sizes. Call `Close` on owned text and trees. Mount a `Component` for Go
behavior or use the stock box and label. Dispatch and tick with your
clock and policy. Read published cells individually. Concurrent and
reentrant tree operations report `WrongPhase`.

Use `LineBreaks` and `ScriptRuns` on counted UTF-8 to get byte offsets
and ISO 15924 script codes. `NewBidi` resolves paragraph direction and
owns the scalar analysis. Inspect logical scalars with `Scalar`, then
call `ReorderLine` on a half-open byte range within one paragraph to
get visual order and mirrored scalars. Call `Close` when finished.
`UnicodeVersion` reports the native data version.

Call `MeasureGlyph` for the intrinsic width and display class of one
grapheme. `LayoutText` lays out a UTF-8 string. `NewTextLayout`
snapshots an existing `Text` with its cluster IDs. Layouts own their
source and remain readable after the text is edited or closed. Use
`Cluster` to read the display glyph, logical byte range, and cell
position. `Hit` maps a visual grid edge to a logical caret and
`CaretPosition` maps it back. Close the layout when done.

For example, `LayoutText("ab cd", TextLayoutOptions{Wrap: WrapWord,
Bounds: Constraints{Columns: Extent{Max: 4, Bounded: true}}})` wraps at
byte offset 3. That offset's leading caret is on the second row and
its trailing caret is on the first.

Use `Surface.PaintText` or the mounted `Paint.PaintText` to draw
that layout. Set a base `TextPaintStyle` and optional byte spans.
The last span containing a cluster's first byte wins. A span can
override either channel with a solid, gradient or palette
`PaintSource`. Use opacity one for full opacity. Sampled colors
move with the layout, and the same elapsed time and motion policy
can be used with `Sample` or `SampleRGBA` to inspect the paint alone.
A failed draw preserves the destination. Mounted draws use the
component owner.

Use `CompositeColor` for continuous straight-alpha channels or
`CompositeRGBA` for quantized output. Pass nil options for normal
source-over in linear sRGB. Call `DefaultCompositeOptions()` before setting
individual fields to retain those defaults. A `CompositeOptions` struct
literal with `Space` omitted selects encoded sRGB. Options select the blend
mode, Porter–Duff operator, color space and gamut policy. Dissolve takes a caller-owned
64-bit seed and sample index. The parse helpers accept native canonical
lowercase names.

`DefaultRippleField` and `DefaultWaveField` provide authored defaults
for a `Field`. Select one descriptor, then use `Sample` or `Color` at
an explicit position and microsecond clock. `Field.Deadline` reports
when to sample again under the chosen motion policy. In a mounted
paint callback, `DrawField` samples solid or palette backgrounds at
cell centers inside the component's clip.
A per-cell error leaves earlier field cells painted if the callback
handles the error and returns success.

Use `Surface.PaintTextEffect` or `Paint.PaintTextEffect` with a
`TextEffect` for reveal, hide, scramble, flipboard, and trails.
Supply elapsed microseconds in `TextPaintOptions`. A transform
receives a copied sample and a pose, and can use the sample's
`Motion` and `Noise` helpers before returning the pose. Return
a nil error on success. A nonzero native `Result` in the error
is forwarded as its signed status. Other nonnil errors, including
`OK` stored as an error, and recovered panics return `CallbackFailed`.
A failed draw preserves the frame.
Reduced motion settles the effect and skips transforms. Closing a
layout during a transform invalidates later calls and lets the
admitted draw finish.

Each successfully mounted component receives one disposal callback.
A retained callback context expires on return. Context operations on a
different goroutine fail while the callback is active. Native status
values and Go causes, including recovered panics, are available through
`CallError`. A panic returns `CallbackFailed` to the native callback
and preserves the panic value for Go. An error from a focus notification can accompany a
committed focus change. An admitted dispatch returns its delivery
report even on failure.

Use `MountChoice` for a list, radio group or select and `MountItem`
for an independent selectable item. Choice entries and labels are copied.
The default selected index is zero. The initial selection must be enabled.
Pass `ChoiceNone` to mount or clear a choice without a selection.
Keyboard changes produce stock-control intents. Pass an intent from
`PeekIntent` to `DecodeControlIntent` to read its event kind and value.
Programmatic setters update state directly.

Start with `DefaultNotificationOptions`, set a nonzero `ID`, then call
`MountNotification`. The tree copies the title, message and action labels.
`ExpiresAt` uses the same absolute microsecond clock as `Tick`. Use zero
expiry, no actions and `Dismissible: false` for a persistent status card.
Read `Notification` for status and selection. Decode the owned item from
`PeekEventIntent` with `DecodeNotificationIntent` to handle a user action or
dismissal. Expiry and `DismissNotification` change state without queuing an
intent.

Mount a cursor or wait indicator with a caller-timed phase in microseconds.
A zero phase leaves the indicator steady. Scheduled ticks under reduced
motion paint phase zero. `Scrollbar` reports the clamped offset, and user
movement emits a scroll-changed intent. `Progress` reads the current meter
configuration. `ButtonPressed` reports an active pointer press.

Use `Editor.SetHistoryLimit` and `AddHistory` to keep submitted entries
separate from the undo journal. `HistoryEntry(0)` reads the newest entry.
`StepHistory` restores the draft and selection when it moves forward past
the newest entry. `HandleKey` applies an explicit `EditorKeyMap` by exact
normalized chord. Bindings opt in to repeats. A map can use an owned
`EditorRegisters` bank for copy and paste. Register reads copy the UTF-8
bytes and report occupancy separately. Close both the editor and bank
after use.
Use constants such as `KeyF12` and `ModControl` to name keys and
combine modifier bits. A handled chord validates its matched action.

Use `Tree.MountEditorView` with an editor, an optional key map, and the
view options. An editable view can emit editor-changed and submission
intents. A read-only editor with `ScrollKeys` gives a keyboard reader.
`ScrollEditorView` requests an offset and `EditorViewOffset` reads the
clamped offset from a successful publication. A stock panel can frame
either view.

The tree copies keys and styles and retains the editor and optional bank.
Closing either handle makes its standalone methods stale. Mounted views
keep their native resources until removal or tree close. Sharing the
editor or bank across trees is supported. Concurrent or reentrant
operations on resources borrowed by a tree return `WrongPhase`.
After changing an editor directly, invalidate each affected view for
layout on text changes or paint on selection changes before republishing.

Plots support per-axis domains, sampling, dataset limits and
caller-timed reveal. Bars space entries uniformly along the x-axis.
`PlotViewport.Project` returns continuous cell-edge
coordinates, and `PlotBounds.ClipSegment` returns directed endpoints
and visibility. `MountDigits` displays a signed fixed-point integer
with up to 18 fractional digits.

Use `Tree.ApplyProperties` for atomic property writes and
`Tree.NewBindingGraph` to combine inputs, pre-commit property reads and
computed values. A compute function receives copied values, including text,
and runs synchronously under the owner tree's mutation guard. Return a
nonzero `Result` to preserve its native code. Returned errors and recovered
panics are available in `CallError`. Removing a referenced node makes its
graph stale before compute runs. Close the graph explicitly, even if the
tree has closed. If a concurrent operation causes `Close` to return
`WrongPhase`, call it after that operation finishes.

`ConfigureSchedule` sets capacity. `ScheduleBatch` copies its assignments,
including text. `NextScheduled` gives the earliest deadline and
`RunSchedule` runs due batches in deadline and registration order up to a
caller-supplied budget. A failed batch is consumed. The returned
`ScheduleRun` identifies it alongside the error. Keep deadlines and run
times in microseconds on the same caller clock.

In a routed event callback, use `Event.EmitFields` to stage typed fields or
`Event.ApplyRules` to match an event kind and routing phase and derive an
intent from current properties. A predicate or derivation receives copied
property values and an owned `RuleEvent`. It runs synchronously and cannot
change the event reply or stage more output. A failed rule discards all
intents staged by that event callback, even when the callback handles the
error. `Tree.PeekEventIntent` copies the head of the same FIFO used by
`Tree.PopIntent`, including counted bytes or typed fields.

Use `SubscribeIntent` and `DeliverIntents` for explicit dispatch of committed
intents. Delivery consumes up to the caller's budget and invokes matching
receivers in registration order. The count follows FIFO items, and
receiver failures accompany it in `CallError`. `CancelSchedule`, removal of
the subscription owner, and tree close release the registration. Other
live subscriptions can still receive intents from a removed source.

Create an immutable `StyleSheet` with `NewStyleSheet`. Pass sheets in
declaration order. Override rules win first, then more specific selectors,
then later declarations. `ResolveStyle` uses an explicit subject path and
copies the winning property value. Use `Tree.ApplyStyles` to annotate
mounted nodes with roles, regions and state masks and update the listed
properties together. The tree uses its current parent links.

Use `Surface.PaintStyledText` or `Paint.PaintStyledText` to overlay sheet
colors on Unicode clusters. Text selectors use counted UTF-8 byte
offsets, cluster IDs, named regions, or Unicode scalar intervals.
A winning color replaces that channel's procedural paint. Close
sheets when finished. A call already using a sheet keeps it live
through the operation.

Use `Tree.DefineProperties` to attach typed properties to a node with
no property table. Each `PropertyDescriptor` declares its ID, value kind,
invalidation and read callback. Omit `Finish` for a read-only property.
An optional `Prepare` validates the write and returns a value for
`Finish`. A successful preparation is finished once, with `commit`
indicating whether to apply the write or discard the preparation.
Update only that property during commit. Do all fallible work in
`Prepare`. The originating call reports a panic in `Finish` through
`CallError`, and the native transaction continues. `Tree.Properties`
returns the metadata in declaration order for stock and custom nodes.

Use `Tree.CaptureState` to save an ordered list of writable properties.
`StateSnapshot.Bytes` returns a copy of the versioned binary encoding.
`ImportStateSnapshot` validates and copies that encoding. Restore with
`Tree.RestoreState` and an ordered list of live destinations that match
the captured property IDs and types. The destination nodes may belong
to a different tree. An invalid destination or value leaves all
properties as they were. Close snapshots when finished.

Use `Ease` for a normalized fraction and `SampleKeyframes` for a
track of values on local microsecond time. Put the first key at zero
and later keys in strictly increasing order. A key's easing applies
to its outgoing segment, including when a ping-pong leg runs backward.

`NewPlayback` starts at local time zero on the supplied host clock.
Use `Elapsed` to feed a track or spring. `Pause`, `Resume`, `Seek` and
`Cancel` return a new playback value. Keep it to continue the transition,
or keep the earlier value to branch. `NewSpring` takes a response time,
position and velocity tolerances, and an initial segment.
`Spring.Retarget` returns a new segment using the selected velocity
policy. `Spring.Sample` accepts reduced motion and reports velocity in
property units per second. Playback and spring values need no cleanup.

`NewTimeline` compiles a flat group of tween clips. Use
`NewTimelineTree` for nested sequence and parallel groups, group
delays, and tween, keyframe or seeded random sources. Set exactly
one field of a clip's `TimelineSource`. The compiled timeline owns
its keys and timing edits. `ChannelCount` and `ChannelAt` enumerate
stable IDs in ascending order. Close the timeline after use.
`LimitTrackRate`, `MapTrackTime`, `SampleRandomTrack` and
`TrackSchedule.Slot` also work independently of a timeline.

Use `Path.Length` and `Path.Sample` to move through continuous
coordinates. A sample returns the position, travel tangent and
resolved distance after clamping, wrapping or reflection. Use
`PanelPose.Project` to map a point through the pose's anchor,
scale, rotation and translation. For an identity pose, set scale
to `ContinuousPoint{X: 1, Y: 1}` and opacity to one.
`PanelTransition.Sample` takes caller-owned microseconds and
returns the authored target under `MotionReduced`.

In a custom paint callback, use `Paint.ProjectPanels` to project
frame crops into viewport-local clips. Sources can come from an
offscreen frame, another tree or the destination tree's prior publication.
The call borrows each source until completion. Reentrant access to
the borrowed trees or the active paint context reports `WrongPhase`.
A `SurfaceMask` defines fixed viewport-local coverage. An optional
`PanelRemap` receives a copied sample in viewport and region
coordinates and returns its source coordinate and opacity.
A remapper error or panic is reported through `CallError` and
leaves the paint batch untouched. The callback can handle the error
and continue painting. The mounted component owns all contributions.

Use `NewOffscreenFrame` for a mutable frame with explicit size,
grapheme placement, resize and clear operations. `Frame()` returns a
readable reference that follows the current cells. `Frame.Background`
reads the physical background and its separate owner. Close the owned
frame when done.

`FrameFromCells` copies authored glyphs and backgrounds in slice order.
Use zero columns, an empty glyph and zero foreground for a background-only
cell. `ImageView.Sample` reads a straight RGBA8 image at normalized
coordinates. `ImageView.Rasterize` samples it onto a physical grid and
returns a frame you own. Set `Stride` in pixels, including row padding.
The image input is read for each call. Close each resulting frame when done.

`OffscreenFrame.Surface` captures a root view. Child views capture
parent-local bounds and copy any alpha samples. Existing views retain
their captured bounds and clip against the current size after resize.
Use `Fill`, `Glyph`, `EraseGlyph`, `Compose` and `ProjectPanels` on a
view. Composition preserves whole glyphs and supports clear, shared
or mapped owner tokens. Set a layer's `Opacity` to 1 for full opacity.
The Go zero value is fully transparent. A layer may read the
destination's pre-call cells. A failed owner map or remap rolls back
the entire batch.
Operations on its borrowed frames and surfaces report `WrongPhase`
during the callback.

Start with `DefaultParticleEmitterOptions` to configure a seeded emitter.
`Sample` returns live particles within the presentation capacity. `Particle`
inspects one birth independently of that capacity, and `Trail` samples its
recent path in time order. All clocks and IDs are `uint64`. `Deadline`
reports the next update under the caller's motion policy. Close the emitter
to release its native state. The returned samples are copies.
Use `DefaultParticleViewOptions` and `MountParticleView` for a leaf that
copies its emitter and one-cell glyph settings. `Paint.DrawParticles`
draws an owned emitter in an existing component's clip. Reduced motion
keeps birth and expiry timing while drawing stationary particles without
trails.

Start with `DefaultParticleDepositsOptions` to configure a bounded contact
store. `Add` and `Advance` use the caller's monotonic microsecond clock.
`Piles` and `Releases` return owned snapshots without advancing that clock.
A full store rejects the new contact. Use `Paint.DrawParticleDeposits` in
a custom component and `Deadline` to drive updates. Close the owned store.

Use `DefaultVisualBranchOptions` for a seeded visual branch.
`Sample` returns a copied segment list in trunk-first order, with
`VisualBranchNoParent` on its root. `Deadline` uses the caller's clock
and motion policy. Use `Paint.DrawVisualBranch` to draw through the
component clip using a one-cell glyph.

With `nocturne_spatial` enabled, use `Vec3` for vector operations and
`DefaultSpatialTransformOptions` to start an affine transform.
`SpatialTransform.Point` applies translation and `Direction` uses the
linear matrix. `Compose` applies the inner transform first.
`SpatialLookAt` and `SpatialOrbit` construct camera poses.
`SpatialCamera.ToView` converts world points to view coordinates.

`SpatialProjection` projects negative view Z into positive depth. Its
screen coordinates increase right and up over −1 to +1.
`ClipSegment` preserves endpoint order and `ClipTriangle` preserves
triangle winding. Each returns weights for the original vertices.
`Pick` returns the nearest hit with its
original index and two unsigned 64-bit IDs. Use the Boolean from
`Project`, `ClipSegment` and `Pick` to distinguish a miss.
`NewSpatialPath` copies a 3D polyline. Sample signed distance using
`Once`, `Loop` or `PingPong`, and close the path when finished.

Use `NewSpatialMesh` with indexed triangles or `DecodeSpatialOBJ` with
counted Wavefront OBJ bytes. `SpatialMesh.Data` returns copies.
`RasterizeSpatial` draws ordered instances with optional transforms,
camera and textures. Supply an opaque white tint to use a texture's own
colors. Each snapshot owns its rendered samples, including geometric
coverage, interpolated UVs, object and primitive IDs, and a separate
frame owner. Close the input meshes when they are no longer needed.
Closing a mesh refuses new operations and lets admitted draws finish.

Use `CellSize` and `CellHit` on a `SpatialRaster` to map 2-by-4 sample
tiles to terminal cells and retrieve the covered sample closest to the
cell center. `Frame` returns an independent offscreen frame. In a mounted
paint callback, `PaintRaster` draws through the component's clip with
its mount owner. Close the raster and any frames when finished.

Use `SpatialSurfaceMotion.Pose` to map time-zero world coordinates to
signed elapsed seconds or `Sample` to get instantaneous point motion.
`SweepSpatialPoint` returns the first contact of a point segment with
linearly translating triangles over a closed interval. Coplanar entries
count and exact time ties retain input order.

With `nocturne_simulation` linked, `Flow.Sample` evaluates serial delay
and reservoir stages at caller-supplied microseconds. Its ports begin
with the inlet and continue through each stage, with retained volume
reported per stage. `FlowSectionPoint` displaces a dry point along a unit
depth direction. `EmitSpray` turns available volume into a copyable
batch. Sample a drop by index and time. A drop is active from birth
until its lifetime expires. The application owns the batch and clock.

Use `TransportOptions.Step` to route water, foam and heat for an explicit
interval. Select `TransportDrain` for edges that leave the network. A
successful step returns independent cells and the external budget. The
initial cells remain available for inspection or retry.

`NewHull` copies the vertices and faces of a convex body boundary. Use
`ImmersePlane` for an infinite plane or `ImmerseWater` for a finite patch
of water triangles. Pass the resulting immersion to `HullImmersion.Buoyancy`
and use the force and torque to advance a `FloatingBody`. Close the hull
when all queries are finished. Use `ExchangeLinearDrag` and
`ExchangeAngularDrag` for pairwise velocity exchange and dissipated energy.

Use `NativeAPI` for additional stock controls, editing, effects,
composition and numeric operations. `Signature`, `Size`, `Field`
and `Constant` describe the linked C API. `WithArena` owns temporary
native storage while `Call` invokes functions using their C signatures.
`Tree.WithNative` lends the native tree for stock mounting and
configuration. Adopt a mount to use its node in the managed tree.
Use `NewCallbacks` for C contracts that take a function and a context.
Destroy native objects and close callback registrations in their
documented ownership order.
