# Nocturne language bindings

Nocturne uses the same C component contracts across its language packages.
The native result numbers, Unicode rules, node identities, input admission
and publication behavior are defined in [ABI.md](ABI.md) and
[CONTRACTS.md](CONTRACTS.md). The wrappers keep the clock and terminal I/O
with the application.

## Installed consumers

| Package | Build and install | Signal example |
| --- | --- | --- |
| C and C++ headers | Install the C SDK and use the `Nocturne::nocturne` CMake target | [Standalone installed C project](../examples/signal-c) |
| Rust | Build from the local crate with an installed C SDK and `NOCTURNE_SDK_PREFIX` | [Rust package and example](../bindings/rust/README.md) |
| Python | Install a platform wheel with its CFFI runtime dependency | [Python wheel and example](../bindings/python/README.md) |
| Go | Resolve the installed C SDK with `nocturne-config` and build with cgo | [Go module and example](../bindings/go/README.md) |
| TypeScript | Install the npm tarball and load its exported wasm32-wasi module | [TypeScript tarball and terminal example](../bindings/typescript/README.md) |

To build the C consumer from a clean project, configure its source
directory with the installed SDK prefix.

```sh
cmake -S examples/signal-c -B build/signal-c -DCMAKE_PREFIX_PATH=/path/to/nocturne-install
cmake --build build/signal-c
printf '+\n+\n-\nq\n' | build/signal-c/nocturne_signal
```

Each example mounts a stock label and a custom meter, then dispatches
normalized keys under a caller-driven clock. The five packages render
the same four frames for that input. The fixture is a consumer example
and uses the public package entry points.

On an eligible POSIX terminal, the C example also selects interactive
presentation from the terminal probe. Press **+** or **−** without
Enter, and **q** to quit. The `>` beside the meter identifies keyboard
focus even in monochrome. Redirected input and output retain the
line-oriented demonstration.

## Coverage

| Capability | C | Rust | Python | Go | TypeScript |
| --- | --- | --- | --- | --- | --- |
| Counted UTF-8 and grapheme identities | Public text API | Owned `Text` | Owned `Text` | Owned `Text` | Owned `Text` |
| Line breaks, script runs and bidi paragraph and line analysis | Public text analysis API | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | `LineBreaks`, `ScriptRuns` and owned `Bidi` | `sdk.lineBreaks`, `sdk.scriptRuns` and `sdk.createBidi` |
| Glyph width, standalone terminal text layout and byte caret mapping | Public text and layout APIs | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | `MeasureGlyph` and owned `TextLayout` | `sdk.measureGlyph`, `sdk.layoutText` and `sdk.createTextLayout` |
| Color conversion, interpolation and caller-timed paint sampling | Public color and paint sample APIs | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | `PaintColor`, `Palette`, `Gradient`, `PaletteRamp`, `PaintField` and `PaintSource` | Typed color, palette, gradient, field and paint samplers on `sdk` |
| Blend modes and Porter–Duff compositing | Public blend API | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | `ParseBlendMode`, `ParseCompositeOperator`, `CompositeColor` and `CompositeRGBA` | `sdk.parseBlendMode`, `parseCompositeOperator`, `compositeColor` and `compositeRGBA` |
| Clipped subcell canvas, compositing ink and cell reducers | Public canvas API | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | Owned `Canvas`, `ReduceCellSamples` and `ParseSamplingMode` | `sdk.createCanvas`, `sdk.reduceCellSamples` and `sdk.parseSamplingMode` |
| Optical falloff, ripple and analytic wave fields with mounted backgrounds | Public field API | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | `OpticalFalloff.Sample`, `Field.Sample`, `Field.Color`, `Field.Deadline` and `Paint.DrawField` | `sdk.opticalFalloff`, `sampleField`, `fieldColor`, `fieldDeadline` and `paint.drawField` |
| Text span paint with sampled foreground and background | Public text paint APIs | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | `Surface.PaintText` and `Paint.PaintText` | `surface.paintText` and `paint.paintText` |
| Animated text effects, pose transforms and trails | Public text effect API | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | `Surface.PaintTextEffect`, `Paint.PaintTextEffect` and typed pose helpers | `surface.paintTextEffect`, `paint.paintTextEffect` and typed pose helpers |
| Standalone Unicode editor, selection, undo and submitted history | Public editor API | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | Owned `Editor` | `sdk.createEditor` |
| Standalone editor key maps and UTF-8 register banks | Public editor input API | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | Typed key maps and owned `EditorRegisters` | Typed key maps and `sdk.createEditorRegisters` |
| Mounted Unicode editor and read-only reader | Public editor view API | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | Typed view with retained editor and optional register bank | Typed view with retained editor and optional register bank |
| Native tree, stock box and label, one-cell reads | Public component and frame APIs | Typed wrappers | Typed wrappers | Typed wrappers | Typed wrappers |
| Configurable boxes, rules and clipped callback placement | Public layouts, widgets and paint APIs | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | Typed configuration, mutation and paint | Typed configuration, mutation and paint |
| Displayed editor caret and viewport geometry | Public editor and viewport APIs | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | Typed selection, caret, scroll and anchors | Typed selection, caret, scroll and anchors |
| Panel and clipped viewport with committed scroll | Public widgets and viewport APIs | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | Typed mounts, setters and viewport state | Typed mounts, setters and viewport state |
| Window workspace, placement, animation, focus and panel content providers | Public window API | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | Typed mounts, placement, focus, panel replacement and published state | Typed mounts, placement, focus, panel replacement and published state |
| Notification cards, persistent status and user actions | Public notification API | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | Typed options, state and `DecodeNotificationIntent` | Typed options, state and `sdk.decodeNotificationIntent` |
| Plot projection, clipping, sampling, reveals and fixed-point digits | Public plot API | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | Typed geometry, mounts and setters | Typed geometry, mounts and setters |
| Graph layout snapshots, authored labels, published hits and selection intents | Optional public graph API | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | `Graph.LayoutSnapshot`, `MountGraphView`, `SetGraphView` and typed hit and selection APIs | `sdk.layoutGraph`, `tree.mountGraph`, `setGraph` and typed hit and selection APIs |
| Caller-timed particle recipes and editable radial, sway and glitter motion | Public particle effect API | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | `ParticleEffectRecipe`, `NewParticleEffectEmitter`, `MountAuthoredParticleEffect`, `SetAuthoredParticleEffect` | `sdk.particleEffectRecipe`, `sdk.createParticleEffectEmitter`, `tree.mountParticleEffect`, `tree.setParticleEffect` |
| Seeded particle schedules, trajectories, trails and deadlines | Public particle API | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | Owned `ParticleEmitter` | `sdk.createParticleEmitter` |
| Configurable clocked particle views and mounted emitter paint | Public particle API | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | `MountParticleView`, `SetParticleView` and `Paint.DrawParticles` | `tree.mountParticleView`, `setParticleView` and `paint.drawParticles` |
| Bounded particle contacts, piles, and secondary releases | Public particle deposit API | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | Owned `ParticleDeposits` and `Paint.DrawParticleDeposits` | `sdk.createParticleDeposits` and `paint.drawParticleDeposits` |
| Seeded visual branches with explicit reveal, flicker and clipping | Public visual branch API | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | `VisualBranchOptions` and `Paint.DrawVisualBranch` | `sdk.sampleVisualBranch`, `sdk.visualBranchDeadline` and `paint.drawVisualBranch` |
| Buttons, selectable items, checkboxes, lists, radio groups, selects and progress | Public controls API | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | Typed mounts, state and intent decoding | Typed mounts, state and intent decoding |
| Cursor, wait indicator and scrollbar | Public controls API | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | Typed mounts and setters, scrollbar state and intents | Typed mounts and setters, scrollbar state and intents |
| Custom measure, layout, update, paint, event and dispose | `NtComponentOps` | `Component` trait | `Component` subclass | `Component` interface | `Component` callbacks |
| Host-neutral terminal byte decoding and Escape deadlines | Public input decoder API | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | Owned `InputDecoder` | `sdk.createInputDecoder` |
| Caller time, focus, normalized input and byte intents | Public component and event APIs | Typed wrappers | Typed wrappers | Typed wrappers | Typed wrappers |
| Published node geometry | `nt_tree_geometry` | `Tree::geometry` | `Tree.geometry` | `Tree.Geometry` | `tree.geometry` |
| Scalar tween | `nt_tween_sample` | `sample_tween` | `sample_tween` | `SampleTween` | `sdk.sampleTween` |
| Scalar easing, keyframe tracks, playback clocks and retargetable springs | Public animation API | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | `Ease`, `SampleKeyframes`, `Playback` and `Spring` | `sdk.ease`, `sdk.sampleKeyframes`, `Playback` and `Spring` |
| 2D path movement and panel pose geometry | Public motion and panel transform APIs | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | `Path`, `PanelPose.Project` and `PanelTransition.Sample` | `sdk.pathLength`, `sdk.samplePath`, `sdk.projectPanelPoint` and `sdk.samplePanelPose` |
| Panel projection from published frames in a mounted paint callback | `nt_paint_project_panels` | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | `Paint.ProjectPanels` with masks and remaps | `paint.projectPanels` with masks and remaps |
| Owned frames, nested surface masks, layer composition and owner mapping | Public frame and surface APIs | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | `OffscreenFrame` and `Surface` | `sdk.createOffscreenFrame` and `Surface` |
| Authored frame cells and RGBA image sources | Public content API | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | `FrameFromCells` and `ImageView` sampling and rasterization | `sdk.frameFromCells`, `sampleImage` and `rasterizeImage` |
| Nested timeline groups, source timing, seeded tracks and scheduling | Public timeline API | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | Owned `Timeline`, `NewTimelineTree` and standalone track functions | Owned `Timeline`, `sdk.createTimelineTree` and standalone track functions |
| Stock property reads and writes | Public property API | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | Typed property values | Typed property values |
| Custom properties and mounted metadata | Descriptor and provider APIs | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | `Tree.DefineProperties` and `Tree.Properties` | `tree.defineProperties` and `tree.properties` |
| Atomic property assignments and computed binding graphs | Public property and binding APIs | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | `Tree.ApplyProperties` and owned `BindingGraph` | `tree.applyProperties` and owned `BindingGraph` |
| Scheduled property batches | Public schedule API | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | Configure, queue, cancel, inspect and run with an explicit budget | Configure, queue, cancel, inspect and run with an explicit budget |
| Typed event rules and scheduled intent subscriptions | Public event rules and schedule APIs | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | `Event.ApplyRules`, `Event.EmitFields`, typed FIFO reads and budgeted subscription delivery | `event.applyRules`, `event.emitFields`, typed FIFO reads and budgeted subscription delivery |
| Style sheets, live tree ancestry and Unicode cluster selectors | Public style API | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | Owned `StyleSheet`, `ResolveStyle`, `Tree.ApplyStyles` and `PaintStyledText` | Owned `StyleSheet`, `sdk.resolveStyle`, `tree.applyStyles` and `paintStyledText` |
| Versioned binary state capture, import and atomic restoration | Public state API | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | Owned `StateSnapshot`, `Tree.CaptureState`, `ImportStateSnapshot` and `Tree.RestoreState` | Owned `StateSnapshot`, `tree.captureState`, `sdk.importStateSnapshot` and `tree.restoreState` |
| Spatial vectors, affine transforms, 3D paths, cameras, frustum clipping and picking | Public spatial geometry API | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | `Vec3`, `SpatialTransform`, `SpatialPath`, `SpatialCamera` and `SpatialProjection` | `sdk.spatial` with typed geometry and owned paths |
| Indexed meshes, counted OBJ, textured raster snapshots and mounted raster paint | Public spatial mesh API | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | `SpatialMesh`, `SpatialRaster` and `Paint.PaintRaster` | `sdk.spatial.createMesh`, `decodeOBJ`, `rasterize` and `paint.paintRaster` |
| Surface poses and swept point contact | Public spatial surface API | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | `SpatialSurfaceMotion` and `SweepSpatialPoint` | `sdk.spatial.surfacePose`, `sampleSurface` and `sweepPoint` |
| Serial flow, section thickness and seeded spray | Public simulation API | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | `Flow`, `FlowSectionPoint`, `EmitSpray` and `SprayBatch` | `sdk.sampleFlow`, `flowSectionPoint`, `emitSpray` and `sampleSpray` |
| Surface water, foam and heat transport | Public simulation API | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | `TransportOptions.Step` and `TransportBudget` | `sdk.stepSurfaceTransport` and `TransportBudget` |
| Hull immersion, buoyancy, body advance and drag exchange | Public simulation API | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | `Hull`, `HullImmersion.Buoyancy`, `FloatingBody`, `ExchangeLinearDrag` and `ExchangeAngularDrag` | `sdk.createHull`, `hullBuoyancy`, `stepFloatingBody`, `floatingBodyTransform`, `exchangeLinearDrag` and `exchangeAngularDrag` |
| Other data views | Public C headers | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | C calls via `NativeAPI` | C calls via `sdk.raw` |
| Native ANSI frame encoding and xterm palette | Public frame sink and presenter APIs | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | `Frame.WriteANSI` and `Xterm256Palette` | `sdk.writeAnsi` and `sdk.xterm256Palette` |
| Layers, surfaces and terminal presentation | Public C headers | Installed declarations via `raw` | Installed declarations via `nocturne.raw` | C calls via `NativeAPI` | WASI surfaces, frame sinks, ANSI presentation and `writeTerminalRows` |
| Optional graph, spatial, simulation and host adapters | Selected installed C targets | Cargo feature selection and installed raw declarations | Wheel selection and installed raw declarations | Typed graph, spatial geometry, simulation and POSIX probe wrappers. Raw access to selected modules and native host adapters | Typed graph, spatial geometry and simulation wrappers. Raw access to linked WASI modules. The JavaScript host owns terminal I/O |

The Rust and Python raw APIs carry the native lifetime and argument
contracts. Rust raw `NT_CHOICE_NONE` is generated as a signed sentinel.
Use `as usize` to obtain the native `SIZE_MAX` value. Convert
`NT_BIDI_REMOVED_LEVEL` to `u8` when passing it to an eight-bit field.

Go and TypeScript retain typed helpers for common operations. Their native
interfaces additionally expose the full function inventory from the linked
public C headers. Go selects graph, spatial, simulation and the host adapter
for its OS from the installed C SDK. TypeScript reports graph, spatial and
simulation from the installed WASI SDK. Its declarations use the DOM
library's WebAssembly and BufferSource types.

### Native calls from Go and TypeScript

Use `NativeAPI()` in Go or `sdk.raw` in TypeScript to inspect function
signatures, constants, type sizes and field offsets. Layouts come from
the compiler that built the package. Array counts, pointers, unions,
records returned by value and microsecond timestamps use the corresponding
C representation for the target.

Allocate temporary records in `WithArena` or `withArena`. Store each
argument in its own slot. For a pointer argument, the slot holds the
target address. A call returning a value allocates its return slot in
the same arena. `Result` and `result` translate `NtResult` statuses.
The C headers specify value sizes, optional pointers, borrowed lifetimes
and destruction. Close native objects before freeing any storage they
borrow. Only give native calls live addresses and values that satisfy the
C contract.

Use `Tree.WithNative` in Go or `tree.withNative` in TypeScript to borrow
the managed tree synchronously. Convert a managed parent to a native value,
call a stock mount, and adopt its returned ID into the managed tree.
Adopted nodes work with ordinary removal and can parent managed custom
components when the native mount allows children. Publish and close a
managed tree through its managed API so frame reference lifetimes remain
tracked.

Raw callback registrations provide native function addresses and an
opaque context. Keep the registration live until all native borrowers
are destroyed. Arguments and result slots expire with the synchronous
callback. Use the registration's `TakeErrors` or `takeErrors` after the
originating call to retrieve foreign failures. A Go callback can return
a native `Result` status. A TypeScript callback writes through its
result slot and returns `undefined` synchronously. Returning a promise
reports callback failure to the originating call. Managed custom
components use their typed callback scopes and `CallError` handling.

## Scalar animation on caller time

The five tween entry points evaluate the same native scalar contract.
Supply unsigned local microseconds (`bigint` in TypeScript), finite endpoints,
and the easing and repeat mode. The returned sample owns its value, uneased
progress, leg index, phase and direction. A zero-duration repeating tween
reports `NT_RESULT_INVALID_ARGUMENT` through the binding's native error type.
See [the animation contract](CONTRACTS.md#easing-tween-and-keyframe-sampling)
for timing boundaries and floating-point behavior.

Go and TypeScript also sample easing curves and ordered keyframes,
pause and seek an explicit playback clock, and sample or retarget
critically damped springs. Playback transitions and spring retargets
return a new value. Applications can keep earlier values for future
sampling and feed a playback's elapsed time to a keyframe track or spring.
See [playback](CONTRACTS.md#playback-control) and
[spring semantics](CONTRACTS.md#springs-and-retargeting).

For composed tracks, use `NewTimelineTree` in Go or
`sdk.createTimelineTree` in TypeScript. Both accept nested sequences
and parallel groups with tween, keyframe and seeded random clips.
Track time applies the rational rate, rounds down to the optional clock
quantum, then applies the ordered dwell and skip edits.
[Timeline and track contracts](CONTRACTS.md#timelines-and-track-modifiers)

A track's sampled scalar can drive a path's signed distance. The sampled
position and travel tangent can drive component placement or a custom pose.
Panel pose sampling applies the caller's reduced-motion preference and
keeps the authored target available at every time.
[Path movement](CONTRACTS.md#path-movement)
· [Panel transform contracts](CONTRACTS.md#panel-projection-and-transitions)

Mounted projection borrows managed published frames through the paint
call. It preserves whole glyphs, uses the active component clip, and
assigns each contribution to the mounted component. Remaps receive
copied sample values and run synchronously. Errors preserve the paint
candidate so the component can choose another way to draw.
[Panel projection contract](CONTRACTS.md#panel-projection-and-transitions)

Offscreen compositions use owned frames and captured views. A view
holds its original clip geometry and alpha samples. Batches hold
their source and destination frames through completion, and same-frame
sources sample the pre-call cells. A synchronous owner mapper can
replace each contributing token or fail the batch.
[Offscreen surface contract](CONTRACTS.md#offscreen-surfaces-clipping-and-layers)

The [installed binding replay](../tests/consumer/binding_replay) compares
wide-glyph cell geometry, clock deadlines, keyboard reservation and intents,
publication after callback failures, disposal counts and fine-grained native
tween samples. It checks exact state and integer fields and applies a finite
absolute tolerance to the floating-point samples.

## Ownership and callback failures

- Close owned text and trees using the wrapper's documented teardown
  operation. The Rust wrapper also releases owned values on drop.
- Text cluster spans count UTF-8 bytes. Frame reads return the requested
  cell or grapheme. Frame publications and callback contexts have their
  documented lifetimes.
- Drive time and supply the normalized input. An admitted callback
  failure can carry a delivery report. A failing native focus
  notification can accompany a committed focus change.
- Preserve the notices bundled with the distributable package. The
  TypeScript build takes the selected WASI runtime notices through
  `NOCTURNE_WASI_NOTICE_DIR` for distribution builds.

The [release verification guide](RELEASES.md) describes how to reproduce
the installed package and cross-language checks.
