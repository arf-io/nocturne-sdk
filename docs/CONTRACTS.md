# Public contracts and composition model

## Implemented: animation and movement

The C99 scalar animation and path APIs are available in
`nocturne/animation.h` and `nocturne/motion.h`. This section describes the
allocation-free samplers and caller-owned playback state.

Each call takes explicit inputs, borrows them for the duration of the call, and
writes a result only on success. `NT_INVALID_ARGUMENT` rejects invalid pointers,
enums, nonfinite values and invalid configuration. `NT_OUT_OF_RANGE` rejects
unrepresentable arithmetic. As usual in C, non-null pointers must reference valid,
properly aligned storage, and array counts must describe accessible elements.

The modules do not allocate memory, obtain time, open a terminal, start threads,
register callbacks, or access a global runtime. Callers retain their configuration
and mutable playback state. Read-only configuration may be shared across threads;
mutations of one playback object require caller serialization.

### Easing, tween and keyframe sampling

`nt_ease` accepts progress in `[0,1]` and returns a value in that same interval,
including at floating-point endpoint boundaries. Curves are linear, quadratic in/out/in-out,
smoothstep, smootherstep, sine, step-start and step-end. Step-start returns zero
exactly at zero and one thereafter; step-end changes at one.

`nt_tween_sample` interpolates any finite scalar endpoints. The scalar can be a
position, opacity, color channel, reveal fraction, size or application value. The
consumer applies the result to a property and enforces that property's domain.

Time is unsigned integer microseconds. Delay occurs once before all repeats.
Before delay, the initial value is returned with phase `NT_DELAYED`. At delay,
playback starts. A once-only track ends exactly at its duration and retains its
endpoint. A zero-duration once-only track changes at delay. Repeated tracks must
have positive duration.

- `NT_ONCE`: progress reaches one and phase becomes `NT_FINISHED`.
- `NT_LOOP`: an exact cycle boundary starts the next iteration at zero.
- `NT_PING_PONG`: odd legs run backward; at the first turnaround progress is
  exactly one and direction is -1. Integer division avoids doubling the duration.

`NtAnimationSample.progress` is uneased progress in the forward track coordinate
system; `value` is eased. `direction` describes track time, not the sign of the
value derivative. `iteration` counts legs. Repeated tracks remain `NT_RUNNING`.
An interior integer tick stays below normalized one even when its ratio rounds
to one in floating point. Step-end consequently changes at the exact boundary.

Keyframes start at time zero and have strictly increasing times. A key's easing
controls the segment after it. Reverse playback retraces the same value curve.
One key defines a once-only constant. Arrays have caller-defined lengths, with no
inherited screen-slot limits. Keyframes are validated and searched on each call,
so sampling is O(number of keys); there is no hidden compiled track cache.

Sampling the same input and time is independent of previous render calls. Tests
assert behavioral/numeric properties; bit-identical floating-point results across
all compilers and math libraries are not promised.

### Playback control

`nt_playback_init` starts at local time zero using a host monotonic timestamp.
`nt_playback_elapsed` returns local time. Pause freezes local time, resume excludes
the pause interval, and seek changes local time while preserving playing/paused
state. Cancel freezes the object and prevents resume/seek; initialize it again to
start a new playback. Pausing/resuming an object already in that state is allowed.

All timestamps must be at least the timestamp of the last successful mutation.
Queries do not move that boundary. Arithmetic overflow leaves playback and outputs
untouched. There are no completion callbacks: consumers inspect the sample phase.
Dropping an object requires no cleanup because the SDK retains no reference to it.

### Springs and retargeting

`NtSpring` is a caller-owned, copyable scalar segment in
`nocturne/animation.h`. Initialize it with `nt_spring_init` and serialize
mutations of the same object. Its fields are initialized by the API. The
positive response time in microseconds is the time constant. The positive
position and velocity tolerances are absolute. Velocity is the derivative
with respect to logical seconds, in property units per second.

For elapsed seconds `t` and time constant `tau`, put `u = t / tau`.
With initial position `x0`, initial velocity `v0`, and target `g`,
the running sample follows the critically damped solution
`g + ((x0 - g) * (1 + u) + v0 * t) * exp(-u)`. Its derivative
is `((g - x0) * u / tau + v0 * (1 - u)) * exp(-u)`.
The result can travel past a target if the initial velocity carries it there.
The consumer applies any limits required by the property, such as a
nonnegative width.

Before the start, normal sampling holds the initial position with zero
velocity and phase `NT_DELAYED`. At onset it returns the initialized
position and velocity. An equilibrium starts finished. For an active
segment the SDK computes a fixed settling boundary from conservative
exponential bounds on remaining displacement and speed. At that boundary
it snaps by at most the position and velocity tolerances and returns
the exact target, zero velocity and `NT_FINISHED` at every later tick.
The spring can pass within tolerance earlier and continue moving.
Sampling is pure. Call order has no effect. As with the other scalar
samplers, floating-point trajectories are compared within tolerance.

`nt_spring_retarget` replaces the segment on the same clock. Its timestamp
must be at or after the current segment start.

- `NT_RETARGET_KEEP_VELOCITY` preserves the sampled position and derivative
  at the interruption. The new target changes subsequent acceleration.
- `NT_RETARGET_RESET_VELOCITY` preserves the sampled position and sets
  derivative to zero at the interruption.
- `NT_RETARGET_SETTLE` starts at the new target with zero velocity. This
  applies an immediate state change without sampling the previous segment.

Invalid tuning, nonfinite inputs or an invalid policy are rejected.
A running position or velocity beyond the finite scalar range returns
`NT_OUT_OF_RANGE`, even if rounding would give the largest finite value.
Near that limit the sampler compares outward bounds on the exponential
against the exact clock polynomial before rounding. It refines the bounds
without allocating and returns `NT_OUT_OF_RANGE` if the maximum internal
precision is reached without resolving the comparison.
Spring arithmetic uses nearest-even rounding and gradual underflow and
restores the caller's floating-point environment. An unavailable standard
environment also returns `NT_OUT_OF_RANGE`. Failed samples and retargets
preserve caller storage.

Sample a spring using local time from `NtPlayback` when a consumer needs
pause, seek or cancel. Its velocity then describes the curve on that local
clock. Paused or cancelled playback holds the displayed value by freezing
local time. Seeking within the current segment resamples it. Retargeting
replaces its earlier trajectory, so a consumer that needs past segments
can retain copies explicitly.

Reduced sampling returns the current target and zero velocity without
mutating the segment, regardless of its start time. Use the settle policy
when the reduction is meant to persist in that segment. Apply the same
caller preference when sampling panel poses and text effects so the endpoint
content and model state are independent of motion. The SDK has no
process-wide motion setting.

The [panel and editor consumer](../tests/consumer/panel_editor.c) samples
two springs for position and viewport width, then projects a text effect
inside the viewport while a separate editor retains focus.

### Path movement

`NtPath` is a borrowed array of 2D points. Open paths require two or more points;
closed paths require three or more. Adjacent points, including the implicit closing
segment, must differ. The closing point is not repeated in the array.

`nt_path_length` computes total Euclidean arc length. `nt_path_sample` resolves a
signed distance with clamp, wrap or reflection and returns position, unit tangent,
resolved distance and total length. A screen consumer normally uses x right and
y down; the sampler does not hard-code screen orientation or cell aspect ratio.

The tangent follows increasing requested distance. At a corner, the segment in
the resolved travel direction wins. At the end of an open path, the final segment
supplies the tangent. A consumer traversing with decreasing distance reverses the
reported tangent. Unrepresentable lengths, period arithmetic or coordinate
differences fail without changing output. Each call is O(number of points).

### Composition and custom behavior

Combine a scalar track and a path by passing the sampled scalar as path distance.
Sample several tracks using one playback time to coordinate them, or separate
playback objects to pause/seek them independently. Consumers can use `nt_ease` in
custom algorithms and apply returned samples to their own structures without
registration or a closed set of supported widget types.

`examples/motion.c` demonstrates a consumer-defined `AnimatedLabel`, two
independent movement/brightness pairs, and a keyframed progress value. Its ASCII
drawing helper is example code.

## Timelines and track modifiers

Include `nocturne/timeline.h`. The rate, time, schedule and noise
operations are pure and allocation-free. `nt_timeline_create` builds an
immutable schedule from the supplied description using the selected
allocator. Sampling an existing timeline allocates nothing. All functions
with output pointers publish their result only on success.

### Rate, dwell, skip and quantization

A rate is a nonnegative rational with a positive denominator. The
all-zero rate denotes one, and an explicit zero numerator with a
positive denominator freezes the scaled clock at zero.
`nt_track_rate_limit` clamps a requested rate to inclusive caller bounds
using exact integer comparison. A reversed bound or a nonzero
numerator with a zero denominator returns `NT_RESULT_INVALID_ARGUMENT`.

`nt_track_time` first computes the floor of elapsed time times the
rational rate. With a positive quantum it rounds the scaled clock down
to a multiple of the quantum. A dwell at source time `a` holds that
coordinate for its positive length in clock microseconds. A skip at
`a` jumps to `a + length` when the clock reaches the edit. An earlier
dwell delays when a later skip or dwell is reached. The caller orders
edits by source coordinate. A dwell and then a skip at the same
coordinate first holds, then jumps. After a skip the next edit must be
at or beyond its endpoint. A null timing record
acts as an identity, and the default timing record maps elapsed time
directly.

The time mapper validates the entire edit array before sampling. An
invalid enum, missing array, zero edit length or edit inside a skipped
interval returns `NT_RESULT_INVALID_ARGUMENT`. An unrepresentable
scale, skip endpoint or mapped coordinate returns
`NT_RESULT_OUT_OF_RANGE`.

### Seeded random tracks and scheduling

`nt_random_track_sample` hashes the seed, stream and time bucket into
a scalar between the finite endpoints. The interval must be positive.
Stepped mode holds one value over each half-open interval. Smooth mode
interpolates the same successive values using the smoothstep weight.
The two modes agree at each interval boundary. Both use logical time,
so seeking or sampling in a different order produces the same result
for the same input. The algorithm uses fixed unsigned 64-bit arithmetic.
As with easing, floating-point tolerances apply across toolchains.

`nt_track_schedule` computes a slot from a caller-assigned rank, a
stable item identity and the unvaried span. Groups of `batch_size`
consecutive ranks share a stagger base, with zero meaning one per batch.
The base is the batch index times `stagger_us`. Seeded jitter adds a
deterministic amount from zero through `jitter_us`. Use zero jitter to
preserve exact ties. Duration variation adds or subtracts a bounded
amount independently of the jitter. Shortening clamps at zero.
Multiplication or positive addition that exceeds the time domain
returns `NT_RESULT_OUT_OF_RANGE`. Pass the natural item index as rank
for insertion order, or a caller-computed rank for another order. A
slot's delay and span can feed a timeline clip. Set the source duration
or map source time independently to choose how it fits that slot.

### Timeline composition and playback

A clip schedules a tween, keyframe track or seeded random track on one
caller-chosen 64-bit channel. Its delay and finite span are measured in
timeline microseconds. Delay advances the node's start from its assigned
slot. A sequence assigns each child a slot starting at the previous
child's end. A parallel group assigns children the same slot start and
ends with the latest child. Groups may nest and must contain at least
one child. A root-to-leaf path may contain up to 128 nodes. A clip's
time modifier maps its local coordinate into source time. The source's
own delay, duration and repeat still apply within that coordinate.

A channel can recur in a sequence or in separate parallel branches
at disjoint times. Two positive spans on that channel cannot overlap.
A clip with zero span can fall at a boundary or in a gap. An
instantaneous clip strictly inside a positive span on the same
channel is invalid. At a shared start the positive span wins, or the
last declared instantaneous clip wins if all are instantaneous.
Before a channel's first scheduled start, hold the source start of
the winner at that coordinate. From the end of a clip to the next
start, hold its mapped endpoint. This rule follows the oriented
coordinate in either direction.

Creation copies the descriptors it retains, including all keyframes
and time edits. Destroy the timeline with `nt_timeline_destroy`.
The allocator context remains borrowed until destruction. Channel
enumeration is in ascending numeric ID order. An invalid node, track,
repeat mode or conflicting span returns `NT_RESULT_INVALID_ARGUMENT`.
An unrepresentable count, nesting depth, time span or first leg with
outer delay returns `NT_RESULT_OUT_OF_RANGE`. An allocation failure
releases partial storage and leaves the caller's output untouched.
Sampling by a missing channel or querying a missing index returns
`NT_RESULT_OUT_OF_RANGE`.

An outer delay runs once. By default the timeline plays one forward
leg. `NT_LOOP` starts a new leg at each boundary, and `NT_PING_PONG`
reverses the coordinate on odd legs. The `reverse` option reverses the
initial direction, including the coordinate held during outer delay.
A repeated timeline requires positive duration. The sample provides
the oriented playhead, mapped source time, timeline and clip phases,
zero-based leg, direction and scalar. The clip phase describes the
scheduled span at the oriented coordinate. It is independent of delays
or repeats inside the source sampler.

For a running animation, pass `nt_playback_elapsed` to
`nt_timeline_sample`. Seek, pause, resume and cancel act on that
caller-owned playback. A cancelled playback holds its final sampled
coordinate until the caller initializes it again. The consumer owns
when to stop applying a timeline to its properties. Share a compiled
timeline for read-only sampling while it is alive.

The [installed timeline consumer](../tests/consumer/timeline.c)
composes grouped tracks, uses playback operations and checks timing,
seed order, ownership, and allocator refusal.

## UTF-8, grapheme iteration, and owned text

Include `nocturne/text.h` for the counted UTF-8 API. `nt_utf8_decode` reads one
scalar at the caller's byte offset. On success it writes the scalar and advances
the offset. An offset at or beyond the byte count is out of range. Malformed
encoding returns `NT_RESULT_INVALID_ARGUMENT`. Both outputs stay unchanged on
error. `nt_utf8_validate` checks the full view and can return its scalar count.

The decoder follows RFC 3629 scalar encoding. It rejects incomplete sequences,
invalid starts or continuations, overlong values, surrogates and values above
U+10FFFF. The byte count permits U+0000 inside the input. Noncharacters are
Unicode scalars and decode normally. An empty view may have a null data pointer.

`nt_grapheme_iterator_init` validates the full view before publishing a
caller-owned iterator. Keep the bytes alive and immutable until the iterator is
finished. Copy an iterator to continue independently at the same boundary.
`nt_grapheme_iterator_next` returns a half-open byte span and a boolean to
indicate availability. At end of input the boolean is false and the span stays
unchanged. Each successful span covers exactly one Unicode 17.0.0 extended
grapheme cluster. This includes combining marks, Hangul syllables, Indic
conjuncts, paired regional indicators and joined emoji.

The primitives allocate no memory and require no terminal or rendering state.
Validation scans the input once. Walking all clusters scans the input linearly
in bytes, with a fixed property table and up to one scalar of lookahead per
cluster. `nt_unicode_version` identifies the compiled data release.
[Unicode data provenance](../unicode/README.md)

`nt_text_create` copies the validated source into an owned `NtText`. Read
its counted UTF-8 bytes with `nt_text_bytes`, and use `nt_text_cluster` for
the half-open source span and ID of a cluster in logical order. The byte
view is valid until a content change or destruction. Empty owned text has
an empty null byte view and zero clusters. IDs are nonzero, unique within
one text lifetime, and independent of the current byte offset.

`nt_text_replace` takes byte offsets at Unicode scalar boundaries, including
boundaries inside clusters. An edit with identical replacement bytes keeps the
bytes, view and IDs. For other edits, the SDK resegments the resulting text.
Each cluster wholly in the retained prefix or suffix keeps its ID if the
mapped span forms exactly one cluster after the edit. A split, merge, changed
cluster, or newly inserted copy receives a fresh ID. Segmentation can change
beyond the immediate edit edge, as with a run of regional indicators. The
implementation indexes the full candidate in linear time and publishes only
after validation, allocation and identity assignment succeed. Removed IDs are never reused. Offset errors,
invalid UTF-8 or scalar boundaries, size or ID exhaustion, and allocation
failure preserve the previous bytes, views and IDs. Replacement may reference
the current bytes, including an overlapping range.

Text and frame constructors accept the same allocator contract. A null
allocator uses the C heap. A custom allocator supplies both callbacks, a
live context and storage for the requested byte size and alignment. The
object copies the descriptor. The release callback receives the original
size and alignment for every successful allocation that is released. Failed
constructors preserve the output pointer and release partial allocations.
Callers serialize access to each object and keep callbacks from reentering it.

## Terminal glyph width

`nt_glyph_measure` uses the pinned Unicode 17.0.0 data to measure one extended
grapheme cluster. The caller supplies independent one- or two-column choices
for ambiguous text and emoji presentation. The result is a kind and intrinsic
column count. A multi-cluster input, malformed UTF-8, invalid policy or empty
input returns `NT_RESULT_INVALID_ARGUMENT`. The result stays unchanged on
failure. This operation allocates nothing and is independent of terminal I/O.

- Emoji presentation uses the selected emoji width even for an East Asian wide
  base. It covers default emoji, valid VS16 pairs, regional indicators, valid
  modifier-base pairs, an isolated modifier, keycaps with or without VS16, and
  extended pictographs joined by ZWJ. A registered VS15 requests text for its
  base and disables joined-emoji classification for that cluster. Other bases
  can still request emoji. Registered immediate pairs alone affect variation.
  Pictographic joins and flag widths use structural rules. Glyph support and
  the RGI repertoire depend on the renderer. [UTS #51, Unicode 17](https://www.unicode.org/reports/tr51/tr51-29.html)
- Text uses the largest constituent base width. Fullwidth and wide bases use
  two cells, ambiguous bases use the ambiguous policy, and other bases use one
  cell. Attached Grapheme_Cluster_Break SpacingMarks and non-emoji
  extenders contribute zero. An isolated non-ignorable SpacingMark or
  extender reserves one cell for a visible base such as U+25CC. An emoji modifier attached to an ineligible base adds no
  width. Visible prepended concatenation marks use text widths.
  [UAX #11, Unicode 17](https://www.unicode.org/reports/tr11/tr11-44.html)
  [Unicode 17, section 23.2.3](https://www.unicode.org/versions/Unicode17.0.0/core-spec/chapter-23/)
- CRLF, CR, LF, VT, FF, NEL, LS and PS are line breaks of zero intrinsic
  width. Tab has zero intrinsic width and advances to a tab stop in layout.
  Default-ignorable scalars, including bidi formatting controls, contribute
  no cells. A cluster containing another nonprinting control reserves one
  cell for a visible replacement such as U+FFFD. Its source bytes stay intact.

## Owned cell frames

`nocturne/frame.h` defines a top-left, zero-based grid with signed lookup
coordinates. Either extent may be zero. Each extent may reach
`INT32_MAX + 1`, subject to checked allocation bounds. A frame owns its
glyph bytes, and each glyph occupies an explicitly supplied one- or two-cell
footprint. Use the measured width for terminal text, and set style and owner
with the footprint. Owner zero designates an unowned glyph. A plain space
is a renderable cluster and may carry colors and an owner.

`nt_frame_set_glyph` copies one printable text or emoji cluster before
clearing overlaps, so the input may come from the frame. For control and
mark fallback, supply the renderable glyph instead of the raw cluster.
The operation starts at a lead or empty cell. Writing from a continuation
returns `NT_RESULT_INVALID_ARGUMENT`. A valid write clears all overlapping
footprints and owners, even if the second column overlaps the lead of
another wide glyph. This direct storage operation records the style and owner
supplied by the caller. Layer blending and clip-dependent owner resolution
belong to painting.

`nt_frame_cell` exposes the lead coordinates, glyph bytes, footprint, colors
and owner. Both cells of a wide glyph resolve to the same glyph and foreground.
After surface painting, backgrounds can differ per physical cell.
`nt_frame_background` reads the physical background and its separate owner.
A cell without a glyph reports its own coordinates and an empty glyph.
A cell with neither glyph nor background has zero colors and owner.
A glyph view borrows bytes until a successful mutation or destruction.
`nt_frame_clear_glyph` clears the whole footprint and both color channels
when given either cell. With no glyph it clears the cell's background.
`nt_frame_resize` preserves complete in-bounds glyphs and leaves new cells
empty. A shrink that would keep only the leading cell drops the whole
glyph and any background attached by direct glyph storage. In-bounds
background contributions made independently through a surface survive.
Allocation or bounds failure preserves the previous frame and outstanding
glyph views.

## Offscreen surfaces, clipping and layers

Include `<nocturne/surface.h>`. An ordinary owned frame can be reused as
an offscreen source. `nt_surface_root` borrows a destination frame and
captures its current bounds. `nt_surface_child` places a borrowed view
relative to its parent. Zero extents are empty, rectangles are half-open,
and signed negative origins permit partial visibility. Origin translation
uses wider arithmetic. An unrepresentable translated origin returns
`NT_RESULT_OUT_OF_RANGE` even for an empty rectangle. Query
`nt_surface_clip` for the rectangular intersection in frame coordinates.
The query includes rectangular mask bounds. Use `nt_surface_coverage`
for the actual mask sample at a view-local point. A point outside the
intersection has zero coverage.

Views allocate nothing. Keep each ancestor and its mask bytes live and
stable while using a descendant. The initializer copies a mask descriptor.
It borrows its sample bytes for the life of the view or call. Reinitializing
an ancestor invalidates its descendants. Existing views keep their authored
bounds over frame resizes and additionally clip against the current frame
size. Create a new root to use a larger captured extent. Callers serialize
access to a frame.

| Mask | Sample at the destination cell center |
|---|---|
| Rectangle | Full coverage inside the mask rectangle |
| Ellipse | Full coverage inside the ellipse inscribed in the rectangle |
| Diamond | Full coverage inside the diamond with its vertices at the side centers |
| Rounded rectangle | Full coverage inside the rectangle with circular corners. Clamp its finite, nonnegative radius to half the smaller extent. |
| Alpha | Use the authored byte divided by 255. Arbitrary silhouettes and feathered edges can use this form. |

Shape boundaries are inclusive within the half-open mask rectangle.
Ellipse and rounded-rectangle tests use double precision. An alpha
mask requires `stride >= columns` and enough bytes to read the last
row's used cells. Zero-area masks require no buffer. Missing samples
or a short stride are invalid. Insufficient length and overflowing
stride arithmetic return `NT_RESULT_INSUFFICIENT_BUFFER` and
`NT_RESULT_OUT_OF_RANGE`, respectively. A null mask gives full coverage
within the view. Multiply all ancestor mask samples to obtain local coverage.
A positive product that underflows to zero is reported at the smallest
positive double. Geometric eligibility uses each mask's zero or nonzero
sample independently of numerical underflow.

`nt_surface_fill` paints backgrounds, preserving glyphs. A glyph write
can paint its background independently in each covered cell. Its
foreground uses the minimum mask coverage across the incoming complete
footprint. A clipped wide glyph may contribute only a subset of its
background cells. If an existing overlapping glyph straddles the effective
clip or a zero-coverage mask cell, skip the incoming foreground and preserve
the existing glyph. An eligible replacement clears overlapping glyphs as
whole footprints. `nt_surface_erase_glyph` accepts a lead or continuation and
removes that whole glyph when every cell has positive coverage. Both
operations preserve physical backgrounds outside their new background writes.
Use a painted space when an offscreen source needs a deliberate blank
that replaces a destination glyph.

Surface colors use linear-light source-over for background and foreground
separately. For a replacement, use the existing foreground resolved at the
incoming lead as the foreground backdrop. Multiply each channel's source
alpha by mask coverage and any layer opacity. If the effective source alpha
in byte units is below 0.5, that channel makes no contribution. Otherwise
calculate source-over in linear sRGB and straight alpha, then round the
encoded channels and alpha to bytes with ties up. At an alpha rounding
boundary, compare the exact product of byte ratios and the binary
layer opacity. RGB channel pairs within sRGB's linear segment use their
byte values directly and the same exact midpoint test. Other channels
use the sRGB transfer function at double precision. A new contributing
foreground owns the replacement glyph. An independently painted
background carries its own contributor's token and leaves a visible
glyph's owner in place. Removing the glyph exposes the background owner.
A directly stored transparent foreground with a visible background
also reports that background's owner. A directly stored glyph with both
channels transparent retains its direct storage owner.

`nt_surface_compose` draws an ordered array of caller-retained layers.
Each layer borrows a source frame and a half-open crop within it. Place
the upper-left of that crop in destination-view coordinates. The
destination region and layer mask further limit the affected footprint,
including any glyph that a source would displace. A crop through a source
wide glyph can transfer backgrounds. The whole source footprint must
fit for a glyph to transfer. Within a layer visit cells left to right
and top to bottom. A layer later in the order draws over earlier content.
Retain frames and descriptors between calls to move or recompose layers.

Source token zero bypasses mapping. Select `NT_SURFACE_OWNERS_CLEAR` to
make source contributions unowned, `NT_SURFACE_OWNERS_SHARED` for a common
namespace, or `NT_SURFACE_OWNERS_MAP` to call a resolver for each contributing
nonzero token. The same source token may be resolved more than once,
including separately for foreground and backgrounds. Keep resolvers
deterministic. They may return an error to abort the batch and must avoid
reentering or altering the borrowed frames and descriptors.

Composition stages a copy of the target on the first contribution and
publishes the whole batch once. Only the destination allocator is used
for staging and copied glyphs. Source aliasing is allowed, including
overlapping self-blits. Every aliased source reads the frame as it was
before the batch. Any validation, owner-resolution or allocation failure
preserves destination contents and glyph borrows. A successful call with no
contributions performs no allocation or mutation. The direct glyph writer
allocates any required glyph bytes before changing either channel.

The installed C example moves a Unicode glyph across a clipped viewport.
The C++ example fills through an alpha mask. Native tests cover the nested
wide-glyph lifecycle trace, overlap, mask and owner rules, aliasing,
resizing, overflow, and allocation or mapping failures. These APIs were
informed by the clip, layer and shape-mask roles in the historical
`native/src/area_geometry.c`, `native/src/layers_render.c` and
`native/src/window_transition.c:nrWindowMask`.

### Panel projection and transitions

Include `<nocturne/panel_transform.h>`. A panel layer borrows an offscreen
frame and a half-open crop. Its viewport is a fixed destination clip in
surface-local cells. The pose maps crop-relative coordinates to
viewport-relative coordinates. It scales about a cell-coordinate anchor,
rotates about the same anchor and then adds the offset. Positive turns
rotate clockwise. Scales and anchors may be signed. A zero scale along
either axis or zero pose opacity draws no ink. `nt_panel_project_point`
uses the same geometry and publishes its coordinate on success. The
projector accumulates rounding residuals with separate binary exponents
so small products and cancellation between large values can retain
their contribution. Its inverse rotates and scales relative to the
placed anchor, which maps back to the source anchor at every angle.
Forward and inverse use the same binary rotation coefficients.
Right-angle and diagonal rotations preserve their exact symmetry
when deciding which side of a source-cell edge to sample.
A mapped coordinate outside the finite range returns out of range.
The point projector rounds to the nearest representable coordinate,
including subnormal values, and rounds ties to even.
Geometry calculations use the standard floating-point environment
with nearest-even rounding and gradual underflow. They restore the
caller's mode and exception flags before returning or calling a remapper.
Projected mask sampling and blending use the same isolated environment.
Remappers, owner resolvers and allocator callbacks run in the caller's
environment.
A host that cannot establish that arithmetic
reports `NT_RESULT_UNSUPPORTED_CAPABILITY`.

`nt_panel_pose_at` samples two poses at the supplied elapsed microseconds.
It uses one easing and linearly interpolates each pose field at the eased
fraction. Author turns along the desired rotational path, including
multiple rotations. Before delay it returns the start. At the endpoint or
with reduced motion it returns the target. A zero span settles at onset.
The endpoint needs to fit in `NtTimeUs`. Consumers may also build poses
from timeline channels, with different timing for each property.

`nt_surface_project_panels` processes layers in array order. For each
covered destination cell, it maps the center back through the pose and
optionally calls `NtPanelRemap` with the center, its inverse point,
dimensions and the caller clock. The callback starts with the inverse
point and unit opacity. It can change either field, even when the inverse
point lies outside the source crop. A finite returned point inside the
crop is rounded down to a physical source cell. When rounding the
inverse point for the callback or sampling, keep it on the same side
of a representable integer cell boundary as the accumulated coordinate,
including signed coordinates outside the viewport. A point outside the
crop or procedural opacity zero removes that cell from the layer's
coverage. The fixed viewport mask and ancestor clips still apply. Use
the callback for moving apertures, inverse row or column warps and
per-cell opacity. Use cropped layers and array order to stack pieces
as they move or fold. On reduced motion, the caller can omit any
procedural deformation and use the target pose.

Backgrounds follow individual physical samples. A sampled glyph can
contribute its original bytes and column width if its whole footprint
fits the source crop. A wide glyph needs two adjacent covered destination
cells. If the first cell samples a continuation, the second must sample
the same source glyph. This allows reflected glyphs to occupy the whole
pair. Its foreground uses the lower procedural opacity and the lower
surface-mask weight across those cells. Process left to
right in each row, giving a contributing wide glyph priority over
another foreground at its continuation cell. Rotation moves the
sampled glyph positions while the terminal displays glyphs upright.
Scaling can repeat a source glyph, and a narrowed layer can omit a
glyph whose complete footprint does not fit. A glyph at the
destination that straddles an uncovered cell is preserved in full.
Backgrounds still composite independently.

The surface API uses the same owner mapping and linear-light blending
as `nt_surface_compose`. At an alpha or linear encoding midpoint the
comparison retains pose opacity and procedural opacity as separate
binary factors. `nt_paint_project_panels` uses the mounted
node's owner for each contribution and skips layer owner mapping.
The component lays out its own hit geometry. Projected paint stays
inside the existing component clip.

The remapper runs once per statically covered destination cell in
row-major order for each drawable layer. Keep the source, destination,
descriptors and borrowed masks stable during both callbacks. A source
may alias the destination and samples its pre-call state. Invalid
descriptors or remap results, unrepresentable mapped coordinates,
allocation refusal or callback errors preserve the destination and
its outstanding glyph borrows, including when a mounted caller
handles an error and continues painting. Layers with empty crops,
empty clips, zero scale or zero opacity skip sampling. For other
layers the destination allocator supplies one scratch row and stages
the destination when drawing first contributes.

See [panel transition recipes](PANEL-RECIPES.md) and the
[installed panel consumer](../tests/consumer/panel_transform.c).

## Unicode line breaking, bidi and script runs

`nocturne/text_analysis.h` provides three operations on counted UTF-8.
All offsets refer to bytes in the original logical input. The operations
accept embedded NUL, reject malformed UTF-8, and require no terminal or
renderer. For array-returning functions, a null array with zero capacity
queries the required element count. An undersized array returns
`NT_RESULT_INSUFFICIENT_BUFFER`, sets the required count and preserves all
array elements. Other errors preserve every output. Output objects must
occupy disjoint storage. Scratch storage uses the specified allocator
or the C heap. The analysis APIs have no fixed input length limit.

### Line-break opportunities

`nt_line_breaks` applies default Unicode 17 UAX #14, including default
class resolution for scripts whose line breaking can use a dictionary.
It returns optional and mandatory opportunities in ascending byte order.
The end of input is always a mandatory opportunity, including offset zero
for empty text. CRLF remains a single hard break. This API does not apply
a locale or dictionary, measure a line, or choose a wrap point. Terminal
wrapping intersects the opportunities with extended grapheme boundaries.
For example, default UAX #14 permits a break after a space before its
combining mark, while terminal layout keeps that grapheme together.
[UAX #14, Unicode 17](https://www.unicode.org/reports/tr14/tr14-55.html)

### Bidirectional paragraphs and lines

`nt_bidi_create` resolves Unicode 17 UAX #9 in logical order. The analysis
owns its decoded scalar and source ranges independently of the input
buffer. Paragraph separator scalars with bidi class B end their paragraph.
In the standalone bidi analysis, CR and LF each end a paragraph, even
when adjacent. The line-break and terminal layout APIs treat CRLF as
one hard line break.
The forced direction applies to each paragraph. Auto direction uses the
first strong L, R or AL outside isolates and defaults to LTR.
`nt_bidi_scalar` exposes a source span, scalar, paragraph embedding level,
and resolved level before line-level resets. The paragraph-separator flag
marks B-class scalars, even when two adjacent paragraphs have the same
embedding level or the separator does not cause a hard UAX #14 break.
Rule X9 controls retain
their original source spans and have level `NT_BIDI_REMOVED_LEVEL`.

`nt_bidi_reorder_line` accepts a half-open byte range within a paragraph.
The caller supplies the line divisions. Both range endpoints must be
scalar boundaries, and an empty line is valid at any such boundary. A
range may include the final paragraph separator. Crossing a paragraph
boundary or pointing inside UTF-8 returns `NT_RESULT_INVALID_ARGUMENT`.
Reversed or out-of-bounds ranges return `NT_RESULT_OUT_OF_RANGE`. The
function applies L1–L2 to the line and returns left-to-right visual order,
source spans, logical scalar indices, and line-resolved levels. Scalars
removed by X9 are absent from the visual array. For odd levels, an
available Unicode mirroring partner appears in `display_scalar`. Glyph
shaping, L3 combining mark placement and any font-dependent mirrored
forms belong to the display layer. Terminal layout projects these
results to complete graphemes. It uses UAX #14 to recognize hard line
breaks, for example keeping a CRLF source span together while reordering
only the content before the break. [UAX #9, Unicode 17](https://www.unicode.org/reports/tr9/tr9-51.html)

### Script runs

`nt_script_runs` partitions the source into adjacent, logical, grapheme-safe
runs tagged with four-letter ISO 15924 script codes. Each cluster uses
its first specific Script property if present, including mixed-script
clusters. Common and Inherited members use the intersection of their
explicit Script_Extensions. Members without such extensions allow any
script. For an unresolved cluster, first try the immediately preceding
cluster's resolved script if it is specific. Then try the next cluster
with a specific Script property. A candidate must be compatible with the
cluster's extensions. If neither neighbor supplies a compatible script,
use the first specific extension in ISO tag order or Common (Zyyy).
Common, Inherited and Unknown do not supply a specific script. Unassigned
text without a specific Script keeps Unknown (Zzzz), so in U+0378 followed
by a period and Latin letter, the period joins the Latin run. Neighboring
clusters resolved to the same script are combined. Empty input yields
zero runs.
[UAX #24, Unicode 17](https://www.unicode.org/reports/tr24/tr24-39.html)

## Terminal text layout and caret mapping

`nocturne/text_layout.h` measures and lays out text in cells using the
same Unicode release and width policy as the analysis APIs. It requires
no frame, scene or terminal. The owned constructor copies source bytes
and preserves the cluster IDs from `NtText` at creation. The borrowed
constructor retains immutable caller bytes and assigns nonzero IDs in
logical cluster order. The input bytes for a borrowed layout must remain
valid until destruction. Both layouts own computed geometry and transformed
glyphs. Failed construction releases intermediate allocations and preserves
the output pointer.

### Rows and columns

- Layout reports the maximum used row width and the number of planned rows,
  each raised to the configured minimum. An axis may be bounded or unbounded.
  Finite bounds and minima must fit signed 32-bit caret coordinates. A
  bounded maximum below its minimum and a zero tab stop are invalid.
- CRLF, CR, LF, VT, FF, NEL, LS and PS end a row. A trailing break produces a
  final empty row. Empty input has one logical row. No glyphs or carets are
  emitted if either extent is zero. Rows added solely to meet the minimum
  height have no caret targets.
- Word wrapping intersects default UAX #14 opportunities with extended
  grapheme boundaries. It falls back to a grapheme boundary when a word
  cannot fit. Cluster wrapping always uses grapheme boundaries. An overwide
  grapheme at the start of a wrapping row is consumed whole and omitted
  from that row. With wrapping disabled, a finite width retains the fitting
  logical prefix of each hard row. Spaces are preserved.
- Tabs advance from the logical row origin to the next configured positive
  stop. Soft wrapping recomputes their advance. Alignment and bidi placement
  happen after the width calculation. A visual tab is one space glyph with
  its full column advance. Paint multiple spaces when materializing a tab
  in a cell frame.
- Start and end alignment follow the row's paragraph direction. Center
  alignment leaves any odd spare column on the physical right.
- Ellipsis trims complete clusters from a clipped row where needed to fit
  U+2026. A height limit marks the last planned row when later rows were
  omitted. The marker follows paragraph direction and uses the configured
  ambiguous width. A marker that cannot fit is omitted. A marker has ID zero
  and an empty source span at the first omitted logical byte.

### Bidi projection and presentation

The layout applies UAX #9 line ordering to each retained row segment within
one paragraph. A B-class paragraph separator can split reordering even
when it does not mandate a UAX #14 line break. The segments remain in
source order relative to each other. For each cluster the first scalar
retained by the bidi analysis represents its visual position and level.
Scalars within that cluster keep their logical order. Unicode mirroring
substitutions apply when the resulting drawable cluster preserves the
source cell width under the selected policy. Otherwise the source glyph
and resolved bidi level remain available for a renderer to mirror without
changing its cell footprint. For example, U+29F5 and U+2215 can substitute
at ambiguous width one. At ambiguous width two their cell widths differ,
so layout retains the original glyph. X9-only clusters attach to the
following logical representative, or to the preceding one at a segment
end. They follow that representative's direction. A segment with no
representative uses its paragraph direction.

Zero-width formats affect order or carets and occupy no glyph cells.
An isolated grapheme extender or SpacingMark receives a dotted-circle base.
Spacing characters excluded from SpacingMark by UAX #29, such as U+102B,
retain their own glyph and width. Visible control replacements use U+FFFD. The half-open source offsets continue to
describe the original bytes. The independent script-run API provides
logical script spans that can be joined to the source spans in visual
order. Contextual glyph shaping and font-specific mirror selection
belong to the rendering layer.
[Unicode 17 grapheme boundaries](https://www.unicode.org/reports/tr29/tr29-47.html)
[Unicode 17 bidirectional ordering](https://www.unicode.org/reports/tr9/tr9-51.html)
[Unicode 17 line breaking](https://www.unicode.org/reports/tr14/tr14-55.html)

### Carets and hit testing

`nt_text_layout_cluster` enumerates drawable entries in visual row-major
order. Its glyph view lasts until the layout is destroyed. The stored
bidi level determines the physical direction of the two caret edges.
Leading affinity means the logical start of a cluster and trailing
affinity means its logical end. At a soft wrap they can select different
rows for the same byte offset. A visible hard separator's leading edge
belongs to the previous row and its trailing edge to the following
visible row. Document start and end accept either affinity when visible.

`nt_text_layout_hit` accepts a horizontal grid edge, with x values from
zero through the layout width, and a visual row. It snaps to the nearest
glyph edge, selects the physical right edge for an interior tie, and
selects the glyph to the left at a shared edge. Horizontal padding
snaps to the closest edge. An empty source row snaps to its logical
start when it has a visible caret. Hits on the ellipsis return the
leading-affinity omission boundary. The caret maps back to the edge
where the retained text meets the marker. Shared edges may have several
logical carets, so an edge hit returns a canonical choice. Bytes inside
a grapheme, clipped carets, rows without a visible caret, and out-of-bounds
edges return `NT_RESULT_OUT_OF_RANGE` and preserve the output.

## Editor buffer and grapheme selection

Include `<nocturne/editor.h>` and link `Nocturne::nocturne`. An `NtEditor`
owns a copy of its strict UTF-8 source. `nt_editor_create` starts with
both carets at byte zero in an editable multiline document unless the
caller supplies options. The caller serializes access and provides the
same allocator contract as `NtText`. Invalid arguments and allocation
failure leave the result pointer unchanged.

Single-line editors reject CRLF, CR, LF, VT, FF, NEL, LS and PS on creation
and replacement. These are the same hard breaks used by text layout.
Multiline editors preserve exact input bytes, including original line
endings and counted NULs. Read-only editors accept selection and navigation.
Content mutations return `UNSUPPORTED_CAPABILITY` for a read-only editor.
The application chooses whether to normalize pasted text before editing.

`NtEditorSelection` stores an anchor and an active `NtCaret`. Each uses a
logical UTF-8 byte offset at an extended grapheme boundary. Both offsets
may be at the end of the document, including in an empty document. Each
affinity is retained to distinguish visual edges at a bidi transition or
wrap. The selected byte span is between the two offsets in either order.
Equal offsets define an empty selection. Interior offsets and unknown
affinities return `INVALID_ARGUMENT`. An offset beyond the document
returns `OUT_OF_RANGE`. Changing one endpoint requires supplying both
valid endpoints to `nt_editor_set_selection`.

`nt_editor_move` visits the previous or next logical grapheme, the ends of
the current hard line, or either end of the document. CRLF is one grapheme
and one hard break. A caret before the separator is on the preceding line.
The next line starts after the whole separator. Extending a selection
keeps its anchor. A non-extending previous/next operation collapses a
nonempty selection to its earlier/later endpoint and retains that endpoint's
affinity. A change to the previous offset uses leading affinity and a
change to the next offset uses trailing affinity. If that move stops at
the current active offset, it keeps the active affinity. Edges of a hard
line or document use leading affinity at the start and trailing affinity
at the end.
The editor's logical motion is independent of the layout width.

`nt_editor_replace_selection` inserts at a collapsed caret or replaces
the selected logical range. Pass a normalized `NT_EVENT_TEXT` or
`NT_EVENT_PASTE` payload directly when application policy permits that edit.
`nt_editor_delete` removes the selected range or one adjacent grapheme in
the requested direction. Deleting at the corresponding document edge
has no effect. Replacement may borrow bytes from the same editor.
Existing owned-text segmentation determines the new boundaries and IDs.
After a nonempty edit span or replacement, both carets move to the first
legal boundary at or after the end of the inserted bytes. This includes
joined graphemes that span the edit boundary. The new caret uses trailing
affinity when its offset is positive and leading affinity at byte zero.
A byte-identical replacement preserves the text and its borrows and still
collapses a selected range. An empty replacement of an empty range keeps
the prior carets. Failure preserves the text, identities, selection and
outstanding borrows.

`nt_editor_text` borrows the read-only `NtText` object until editor
destruction. The text bytes and `nt_editor_selected_text` borrow expire
on a successful byte change or destruction. A selection-only move keeps
those borrows valid. An empty selection returns a null, empty view.

Create an owned `NtTextLayout` from `nt_editor_text` for the display. Use
`nt_editor_select_at` to place or extend the selection from a visual grid
edge and `nt_editor_caret_position` to draw the active edge in the current
layout. Both require identical logical source bytes and return
`STALE_REFERENCE` if the content differs. The caller chooses the actual
displayed layout when more than one width or wrap setting exists for the
same bytes. Hit rules, clipping and visible caret bounds follow `NtTextLayout`.
Supply a positive layout width to hit an empty editable row. An owned
layout remains readable after editor mutation or destruction. Apply
selection colors to the sorted logical source span through `NtTextPaintSpan`
to preserve visual order and whole-glyph painting.

The [installed editor consumer](../tests/consumer/editor.c) checks these
operations with joined Unicode, bidi hit and resize, paste, empty and long
documents, and allocator refusal and retry. It uses the public buffer and
layout APIs directly.

Go and TypeScript expose editor selection at a displayed cell and active
caret position through the same text layout snapshot. A layout of earlier
document bytes returns a stale reference and preserves the selection.

## Editor undo and submitted history

Set a positive `nt_editor_set_undo_limit` to retain that many edit records
across undo and redo. The default is zero. Setting zero discards the
journal. A successful change of limit keeps the newest available undo
records first, then the nearest redo records that fit. It preserves the
current text and selection. An allocation failure or capacity overflow
leaves the previous limit and records intact.

Each successful byte-changing edit records the exact removed and inserted
bytes and both carets before and after the edit. An undo or redo restores
those bytes and both caret affinities, even when the edit joined or split
a grapheme. Unaffected text clusters still use the normal `NtText`
identity rules. The journal's byte ranges may be scalar boundaries inside
newly joined clusters. Restored carets are boundaries in the restored
document. A successful byte-changing edit after undo discards redo.
Selection movement and byte-identical replacement preserve the journal.

`nt_editor_undo_state` reports the available records in each direction.
Undo and redo accept an optional `changed` result. It is false if there
is no record and true after replay. A read-only document returns
`UNSUPPORTED_CAPABILITY`. Allocation refusal preserves the current
document, selection, borrowed bytes, journal and any ongoing history
browse. Errors leave the optional result unchanged.

Submitted history is a separate set of UTF-8 documents. The caller sets
its entry limit and explicitly adds entries. The default zero limit
disables adding entries. Add copies the supplied bytes and applies the
editor's line policy, including single-line rejection of hard breaks.
A successful add beyond the limit evicts the oldest entry. Duplicate or
empty entries are permitted. `nt_editor_history_entry` borrows the bytes
of an entry by zero-based index, newest first. Its borrow lasts until
that entry is evicted or the editor is destroyed.

The first successful older step saves a draft with its exact bytes and
selection. Loaded entries place a collapsed caret at their end, including
byte zero for an empty entry. A newer step past the newest entry restores
the draft and both carets. A step with no move succeeds and reports false.
Moving to a different history entry or restoring the draft reports true
even if its bytes match the visible document. Byte-changing steps become
undo records when enabled. A successful ordinary byte edit or undo/redo
ends browsing and makes the displayed text the new draft. Successfully
adding an entry or changing the history limit also ends browsing.
Selection-only operations and failed changes preserve the saved draft
and browsing position. Read-only editors can inspect and record history
and receive `UNSUPPORTED_CAPABILITY` when asked to navigate it.

## Opt-in editor key maps and registers

`nt_editor_input` accepts a normalized event and an optional key map.
`TEXT` and `PASTE` replace the selected span with their counted UTF-8
payload. The caller controls routing and any paste normalization before
the call. A null or empty key map leaves all key commands unhandled.
The editor accepts a caller-owned map of exact key and modifier pairs.
The first matching binding supplies the action. A release is unhandled.
A repeat is handled only if the first matching binding allows repeats.
A successful mapped action reports handled even when it reaches an edge
or has no entry to replay. An unknown action in a matched binding returns
`INVALID_ARGUMENT`. A failed operation leaves `handled` unchanged.

Use the handled result as the target callback's `consume` flag when
forwarding a tree event. The tree reserves host chords before routing any
key or associated text. A consumed component key also withholds its
associated text. An unconsumed printable key can deliver its normalized
text as a separate event. Only the `TEXT` or `PASTE` path inserts that
normalized payload.

An `NtEditorRegisters` bank is an optional application-owned group of
numbered UTF-8 slots. Create it with a positive slot count and serialize
its access as for the editor. A write copies the payload, including
counted NULs. An occupied slot can contain an empty payload. Reads borrow
until the slot changes or the bank is destroyed. Byte-identical writes
preserve a slot and its borrow. A failed write preserves the prior value.
Passing a bank in a key map lets copy save a nonempty selection and paste
replace the selection with an occupied slot. Copy with an empty selection
and paste from a vacant slot leave the document and bank intact. Register
commands require a bank and use the caller's selected slot. Paste applies
the editor's line and read-only policies.

The [installed history consumer](../tests/consumer/editor_history.c)
tests joined-grapheme replay, draft browsing, allocation rollback, register
ownership, and explicit key maps through public tree dispatch.

## Color conversions and interpolation

Include `<nocturne/color.h>`. `NtColor` holds a color-space tag, channels in
the order shown below, and straight alpha in `[0,1]`. Conversions preserve alpha,
including at zero. The fourth channel is used by CMYK and is cleared in other
output spaces.

| Space | Channel domains |
|---|---|
| sRGB and linear sRGB | R, G, B each in `[0,1]` |
| HSL and HSV | Hue in finite degrees, followed by saturation and lightness or value in `[0,1]` |
| OKLab | L in `[0,1]`, followed by finite signed a and b |
| OKLCH | L in `[0,1]`, finite nonnegative chroma, finite hue in degrees |
| CMYK | C, M, Y, K each in `[0,1]` |

Output hue is normalized to `[0,360)`. RGB conversion uses the piecewise sRGB
transfer function. HSL, HSV and CMYK derive from encoded sRGB. Generic CMYK uses
`R=(1−C)(1−K)` and the corresponding rules for G and B. The RGB-to-CMYK
direction selects `K=1−max(R,G,B)`. Pure black gives `(0,0,0,1)`. OKLab
uses the published D65 linear-sRGB transforms, with an exact neutral-axis
path that yields a and b of zero for equal linear RGB channels. Conversion
to OKLCH preserves hue for nonzero chroma.
[CSS Color 4](https://www.w3.org/TR/css-color-4/) · [Oklab transforms](https://bottosson.github.io/posts/oklab/)

| Gamut policy | Conversion from an OK space to an RGB-derived space |
|---|---|
| `NT_GAMUT_CLIP` | Clamp each linear RGB channel to `[0,1]`. |
| `NT_GAMUT_REDUCE_CHROMA` | Preserve OK lightness and hue while reducing chroma. Bisect toward the neutral axis for 40 iterations with strict channel limits. The lightness endpoints map to black and white. |
| `NT_GAMUT_REJECT` | Return `NT_RESULT_OUT_OF_RANGE` when a linear RGB channel exceeds the range tolerance. |

The reject policy admits `2e-7` absolute linear RGB error at the channel
limits to account for matrix round trips and clips that error. OKLab and OKLCH conversions
can preserve coordinates outside the sRGB gamut. Nonfinite intermediate
results from finite inputs give `NT_RESULT_OUT_OF_RANGE`. Use
`nt_color_to_rgba` to convert to encoded sRGB and round each normalized
channel, including alpha, to the nearest byte with ties up. Direct and
linear-light RGBA8 round trips are exact for the byte domain.

`nt_color_mix` validates both inputs and interpolates after conversion into
the selected space. Progress is finite in `[0,1]`. At zero or one, it converts
and returns the selected endpoint, including its hidden color when alpha is
zero. Within the interval, both conversions must succeed. Alpha changes
linearly with progress. Straight interpolation weights non-hue channels by
progress. Premultiplied interpolation weights those channels by progress
and endpoint alpha, then returns straight channels. An interior result
whose premultiplied alpha is zero has zero channels.

Polar hue follows the shorter, longer, increasing, or decreasing arc and
uses the progress directly. A 180-degree tie follows the signed endpoint
difference for shorter and longer. Identical hues make a decreasing full
turn for longer and stay constant for the other three policies. HSL hue is
powerless at zero saturation or at lightness zero or one. HSV hue is
powerless at zero saturation or value. OKLCH hue is powerless at chroma
`<=1e-7`. A fully transparent endpoint in premultiplied mode is also
powerless. A powerless endpoint takes the other endpoint's hue. Two
powerless endpoints yield zero hue in the interior.

A palette is an explicit borrowed array of RGBA8 entries indexed from zero.
`nt_palette_lookup` resolves an index directly. `nt_palette_nearest`
minimizes squared Euclidean distance in premultiplied linear RGB with
alpha as a fourth coordinate. All four coordinates use the common scale
`[0,255]`. Equal computed distances choose the lowest index.
An empty palette is invalid. An index outside the palette or a count whose
byte size would overflow returns `NT_RESULT_OUT_OF_RANGE`.

## Sampling paint

Include `<nocturne/paint_sample.h>`. All sampling uses caller-supplied values,
with borrowed arrays and tracks held only for the call. Sampling allocates
no memory and mutates no descriptor. Invalid descriptors return
`NT_RESULT_INVALID_ARGUMENT`. Numerical overflow returns
`NT_RESULT_OUT_OF_RANGE`. Results are published only on success.

| Address mode | Coordinate mapping |
|---|---|
| `NT_PAINT_PAD` | Clamp to `[0,1]`. |
| `NT_PAINT_REPEAT` | Wrap with period 1 into `[0,1)`. |
| `NT_PAINT_REFLECT` | Wrap with period 2 and fold the second half back toward zero. |

A gradient contains at least one stop in nondecreasing position order.
Every stop's position, color descriptor and easing are validated on each
call. Before the first position sample the first stop, and after the last
sample the last stop. At an exact stop position the last stop at that
position wins. Between distinct positions, the previous stop's easing
maps segment progress and the specified color interpolation mixes its
color with the next stop's. Equal adjacent positions produce a hard edge.

Palette ramps use one RGBA8 entry per equal-width bin for step filtering.
Breakpoints are the ratios of integer positions to the interval count,
rounded to double precision. At an interior boundary the next entry wins.
The coordinate one uses the final bin when addressing leaves one in the domain. Linear filtering
spreads entries evenly including both endpoints for pad and reflect.
Repeating linear filtering includes an equal-length segment from the last
entry back to the first. Entering a linear segment uses the ramp's easing
and color interpolation. A single-entry palette is constant. Palette
ramps limit the entry count to `UINT32_MAX` and the addressable byte size.

The caller supplies an origin, positive axis scales, angle in degrees and
a sample point. Subtract the origin and project onto axes rotated by the
angle. With screen Y increasing downward, positive angles rotate the
field clockwise. Divide the projected coordinates by `scale_x` and
`scale_y`. Unequal scales change aspect ratio. Exact quarter turns keep
integer-aligned grid boundaries. A fixed rotation uses only the base
angle. Spin adds one full turn per nonzero period. Swing adds the
specified amplitude in degrees times the sine of the completed fraction
of the period. Reversing the rotation reverses the time-driven component.
Rotation reduces elapsed time into a quadrant and an integer tick offset.
Spin combines authored and time offsets before division, preserving exact
fractional alignments and nearby samples. Swing preserves the exact zeros
and unit extrema at quarter-cycle swing samples and the exact half
amplitudes at phases 1/12, 5/12, 7/12 and 11/12. Trigonometric evaluation
and remaining arithmetic are subject to double precision.

In the table, `x` and `y` are those local coordinates, `p` is phase,
`a` is wave amplitude, and `S(t)=sin(2πt)`. All rows return the listed
value plus `p` for the gradient or palette address step.

| Pattern | Scalar before the phase offset |
|---|---|
| Linear | `x`. Set angle for diagonal gradients, or spin or swing for animated sweeps. |
| Radial | `hypot(x,y)` |
| Conic | `atan2(y,x)/(2π)` wrapped into `[0,1)`, zero at the origin |
| Wave | `x + a*S(y+p)` |
| Plasma | `(S(x+p)+S(y-p)+S((x+y)/2+p)+S(hypot(x,y)-p))/4` |
| Moiré | `S(x+p)+S(1.03*x+y/2-p)` |
| Aurora | `y/2 + a*S(x/2+p) + S(3*x-p)/5` |
| Interference | `(S(x+p)+S(y-p)+S(x-y+p))/3` |
| Chevrons | `abs(wrap(x,2)-1)+y` |
| Diamond | `abs(x)+abs(y)` |
| Checker | One when the parity of `floor(x)` and `floor(y)` differs, else zero. Use pad or reflect for the two endpoints at zero phase. |
| Uniform | Zero, so the result is driven by phase alone |

`nt_paint_coordinate` exposes the scalar for callers that want their own
mapping. A phase tween adds its sampled value to the authored phase.
`NtPaint` samples a solid directly or uses the field with a gradient or
palette. Static opacity multiplies straight alpha. An optional keyframe
track multiplies it again, with all key values in `[0,1]`. Reduced motion
uses elapsed time zero for rotation, phase and opacity tracks. The same
explicit sample inputs always yield the same result within the platform's
floating-point precision. `nt_paint_sample_rgba` applies its supplied gamut
policy at the final conversion to RGBA8.

Native numeric fixtures assert sRGB transfer and paint coordinates to
`1e-12` absolute error, OKLab primary coordinates to `5e-10` and their
linear RGB round trip to `2e-7`. The chroma-reduction fixture asserts
`3e-6` lightness and `2e-4` degree hue error. Discrete addressing and RGBA8
checks assert exact results. The installed C consumer samples a linear-light
gradient and the installed C++ consumer samples a repeating premultiplied
palette.

The reference color inventory originated in `native/src/color_spaces.c`.
The spatial pattern extraction uses the MIT-licensed
`native/src/source_material_sampler.c` at
`53191655ec77e41e0a3453afbc08f8dccfcdf4a5`. The field formulas use explicit
coordinates and reusable scalar parameters.

## Blend modes and Porter–Duff operators

Include `<nocturne/blend.h>`. `nt_color_composite` returns straight alpha
and color in the selected mixing space. `nt_rgba_composite` returns encoded
sRGB bytes. The blend function takes the encoded sRGB backdrop color `b`
and source color `s`. The operator independently determines the alpha
contribution of each input. Null options select normal source-over,
linear sRGB mixing and gamut clipping. A zero-initialized `NtCompositeOptions`
uses encoded sRGB. Go's `CompositeOptions` struct maps to the C fields.
Go's `DefaultCompositeOptions()` and TypeScript's omitted options fields
use the null-options defaults. Both input colors must convert to sRGB
under the stated gamut policy. Errors preserve outputs.
[Compositing and Blending Level 1](https://www.w3.org/TR/compositing-1/#generalformula)

| Blend name | Encoded sRGB rule |
|---|---|
| `normal`, `none` | Source channel `s` |
| `multiply` | `b*s` |
| `screen` | `b+s-b*s` |
| `overlay` | Hard light with the input roles reversed |
| `darken`, `lighten` | Channel minimum and maximum |
| `color-dodge` | Zero for black backdrop, one for white source, otherwise clamp `b/(1-s)` |
| `color-burn` | One for white backdrop, zero for black source, otherwise `1-clamp((1-b)/s)` |
| `hard-light` | `2*b*s` when `s<=0.5`, otherwise `1-2*(1-b)*(1-s)` |
| `soft-light` | For `s<=0.5`, darken by `(1-2*s)*b*(1-b)`. Otherwise add `(2*s-1)*(D-b)`, where `D=((16*b-12)*b+4)*b` for `b<=0.25` and `D=sqrt(b)` otherwise. |
| `difference`, `exclusion` | `abs(b-s)` and `b+s-2*b*s` |
| `hue` | Source hue with backdrop saturation and luminosity |
| `saturation` | Backdrop hue and luminosity with source saturation |
| `color` | Source color with backdrop luminosity |
| `luminosity` | Backdrop color with source luminosity |
| `add`, `subtract` | Clamp `b+s` or `b-s` to the unit interval |
| `divide` | Clamp `b/s`. Zero source gives one. |
| `linear-burn`, `linear-light` | Clamp `b+s-1` or `b+2*s-1` |
| `vivid-light` | Color burn with source `2*s` below 0.5, color dodge with `2*s-1` otherwise |
| `pin-light` | `min(b,2*s)` below source 0.5, `max(b,2*s-1)` otherwise |
| `hard-mix` | One at and above the exact threshold `b+s=1` and zero below it. For byte inputs the sum threshold is 255. |
| `darker-color`, `lighter-color` | Return the entire input with lower or higher luminosity. Ties select the backdrop. |
| `dissolve` | Use normal blend and replace source alpha with zero or one from a deterministic sample. |

The non-separable modes use saturation `max−min` and luminosity
`0.30R+0.59G+0.11B`. They rescale saturation linearly and adjust luminosity
while keeping channels in range. Dissolve applies the SplitMix64 finalizer
to `seed + (sample_index+1)*0x9e3779b97f4a7c15` with unsigned 64-bit wrap.
Its high 53 bits form a uniform sample in `[0,1)`. The source survives
when that sample is strictly below source alpha. Byte inputs use an
integer comparison against the exact alpha ratio. The parsers accept the
case-sensitive names in these tables. Unknown names return
`NT_RESULT_UNSUPPORTED_CAPABILITY` without changing the output.

With backdrop alpha `Ab` and source alpha `As`, the blend replaces source
color with `s + Ab*(blend(b,s)-s)`. The operator then multiplies the input
alphas by its factors. The sums of those contributions yield output alpha.

| Operator name | Source factor | Backdrop factor |
|---|---|---|
| `source-over` | `1` | `1-As` |
| `source` | `1` | `0` |
| `destination` | `0` | `1` |
| `clear` | `0` | `0` |
| `source-in` | `Ab` | `0` |
| `source-out` | `1-Ab` | `0` |
| `source-atop` | `Ab` | `1-As` |
| `destination-over` | `1-Ab` | `1` |
| `destination-in` | `0` | `As` |
| `destination-out` | `0` | `1-As` |
| `destination-atop` | `1-Ab` | `As` |
| `xor` | `1-Ab` | `1-As` |
| `lighter` | `1` | `1` |

For a nonempty result, normalize the contributions by their sum and mix
in sRGB, linear sRGB, OKLab, or OKLCH. OKLCH follows the specified hue arc.
Lighter adds premultiplied encoded sRGB energy, divides by capped alpha,
and clamps the resulting channels before converting the result. The atop
operators preserve their shared source or backdrop alpha and cancel it
before deriving the normalized color weights, including at subnormal alpha. Output
with zero alpha is transparent black. Byte output quantizes once with
nearest rounding and ties up. It uses exact integer alpha factors and
exact comparisons for rational encoded blend formulas, including the
non-separable modes. The same comparisons apply to lighter and when both
effective colors lie in the linear segment of linear-sRGB mixing. The
square-root branch of soft light and the other color-space conversions
use double precision. Whole-color luminosity ties use an exact comparison
for both byte and normalized input.

Native fixtures compare normalized color and alpha to `1e-12` absolute error
for representative values in every mode and operator. Gamma fixtures
assert exact byte results. For example, opaque black plus white with
alpha 128 gives byte 188 in linear sRGB, 128 in encoded sRGB, and 100 in
OKLab or OKLCH. The installed C++ consumer parses a mode and composes a
colored screen blend.

## Drawing and subcell sampling

Include `<nocturne/canvas.h>`. A canvas owns a grid of RGBA8 samples and
their geometric coverage and semantic owners. Its dimensions are sample
counts, so either axis may be zero. The current clear color is the
background beneath geometric fragments. Clearing resets the samples,
owner tokens and coverage. Set the clear color explicitly or use the
initial transparent background.

An ink supplies a color, owner and optional composite options. Drawing
updates the clipped samples individually. A surviving source takes its
ink owner and marks that sample geometrically covered. If only the
destination survives, retain its coverage and owner. Zero byte alpha
clears geometric coverage and owner. Dissolve adds the sample's
row-major index to the supplied base index with unsigned 64-bit wrap,
so splitting or reordering equivalent pixel writes preserves the
sampling pattern. An operation that rejects a gamut conversion leaves
the entire canvas unchanged.

| Drawing operation | Geometry |
|---|---|
| Point | A single signed sample coordinate |
| Line | Both endpoints and a nearest minor coordinate for each integer major coordinate. The major axis has the greater extent, with x selected on ties. Traverse from the lower major endpoint. A minor-coordinate tie steps toward the other endpoint. |
| Triangle | Integer sample centers inside the triangle or on its edges. Either winding works. A collinear triangle draws the line between its extremes once. |
| Fill | A half-open rectangle |
| Border | A one-sample rectangular perimeter, with corners and one-sample-wide extents visited once |

The viewport clips geometry to the canvas extent. A clipped border
preserves the original edges. A clipped line preserves its original
rounding phase. Large signed coordinates use widened arithmetic. An
out-of-bounds point or zero-area shape makes no writes. The pixel query
reports an out-of-range coordinate as an error.

`nt_cell_sample` reduces a caller-supplied 2×4 tile. Supply final pixel
colors and separate background colors, with coordinates in row-major
order. Color reduction averages premultiplied linear light, then
unpremultiplies and encodes once. Alpha is the rounded arithmetic mean.
Averages wholly within the linear sRGB segment preserve exact byte ties.
The first geometrically covered sample with nonzero alpha and a nonzero
owner supplies the cell owner.

| Sampling mode | Output |
|---|---|
| `cell` | Space glyph and the average of all eight final pixels as background |
| `half-block` | U+2580 with top-half and bottom-half averages as foreground and background |
| `braille` | U+2800 plus the physical dot bits for geometric coverage. Average covered final pixels for foreground and all eight underlying backgrounds for background. |
| `hybrid` | Both braille paths, followed by half-block fallback |

A full geometric mask stays U+28FF. For braille or hybrid with no
geometry, compute encoded luminosity using the blend luminosity weights
and alpha scaling. If the sum of deviations from its mean exceeds 12
byte units, set dots for samples more than one byte unit above the mean.
The selected and unselected samples supply the two colors. If no
contrast mask exists, braille uses the `cell` reduction and hybrid uses
`half-block`. Mode parsing rejects unsupported names explicitly.

`nt_canvas_render` creates an owned frame. Divide sample dimensions by
2 and 4 respectively, rounding up, and use the clear color for edge
padding. Each nonempty result occupies one frame cell. Consumers select
a sampling mode supported by their host. Allocation failure preserves the
output pointer and canvas. Use the frame as a source in
`nt_surface_compose` to place, clip and layer the drawing. The installed
C consumer draws a line and checks its braille glyph and owner.

In Go, use `NewCanvas` or call `ReduceCellSamples` with a `CellSamples`
tile. In TypeScript, use `sdk.createCanvas` or call
`sdk.reduceCellSamples` with two arrays of exactly eight entries.
Canvas coordinates and dimensions are in samples. Both bindings accept
64-bit owner tokens and the same optional composite settings as their
color APIs. A rendered offscreen frame owns its cells independently
and can be composed after the canvas is closed. Close both objects
when finished. TypeScript's parser accepts the native name
`"half-block"` and returns the `"halfBlock"` mode used by the typed APIs.

## Painting styled text

Include `<nocturne/text_paint.h>`. A layout retains its immutable logical
source, which callers can borrow through `nt_text_layout_source`. The
painter takes the completed layout and a placement in surface-view
coordinates. It preserves visual positions, bidi order, glyph bytes and
logical source associations determined by the layout.

A paint style supplies straight foreground and background colors and a
semantic owner. Borrow optional `NtPaint` overrides for either channel.
The painter samples at the center of the glyph's leading cell in layout
coordinates using the supplied elapsed time, motion and gamut policy.
Placement changes the destination geometry. The same text-local paint
coordinates move with the layout. A tab uses one sample at its leading
cell and paints one space in each visible column of its logical footprint.

Spans are half-open byte ranges inside the logical source. An empty range
matches nothing. Overlapping ranges use the last span containing the
cluster's first source byte. A range that starts within a cluster applies
according to that leading-byte rule and keeps the glyph intact. Ellipsis
has no source cluster identity and uses the base style. Style supplies
the owner regardless of logical order or visual position.

The painter validates range bounds, motion and gamut options. It evaluates
paint overrides for geometric candidates in the visible area. A wide
glyph clipped through either side can contribute its covered backgrounds
while preserving whole foreground footprints. A successfully staged batch
publishes once, and errors preserve the prior frame and its outstanding
glyph borrows. The caller controls the visibility policy through the
surface view. This is the M3 paint path for TEXT-06. Style rule and
selector composition follow in M8.

## Animated text effects

Include `<nocturne/text_effect.h>`. Use `nt_text_effect_paint` on a surface
or `nt_paint_text_effect` in a mounted component's paint callback. Both
sample the existing immutable layout at `NtTextPaintOptions.elapsed_us`.
Layout, effect, spans, paints, callbacks and the bytes they return are
borrowed through the call. Keep the destination and borrowed inputs stable
while a transform runs. Use the supplied sample and read-only application
parameters to reproduce a frame at the same clock value.

Each drawable layout entry has a visual index and a logical index ordered
by source byte position with visual tie-breaking. Select either order or its
reverse to assign ranks. The SDK feeds each rank and the cluster ID to
`nt_track_schedule`. Synthetic layout entries such as ellipses use a
deterministic layout-local ID. The cluster clock starts at `delay_us` plus
its slot delay and runs for the slot span, including any scheduled variation
of `duration_us`. Every onset and endpoint must fit in `NtTimeUs`. Zero
span settles at onset. Opacity eases from zero to one for reveal and from
one to zero for hide. `NT_TEXT_VISIBLE` starts each pose with opacity one.

During an active interval, scramble selects a seeded glyph from the
alphabet at each interval boundary. Flipboard advances the ordered
alphabet from a seeded start at a fixed interval, or uses quantized
progress to distribute the configured rotations and the forward distance
to the original glyph. The rotating mode rounds local microseconds down
to the interval and advances by the integer floor of that tick times the
total number of glyph steps divided by the slot span. If the original is
absent, it uses a full cycle in place of the forward distance. Both modes
restore the original layout glyph at settlement. The alphabet must contain individual
renderable graphemes under the layout's width policy. A substitution uses
only glyphs of the original cell width. A tab or an empty width cohort
keeps its original glyph.

During the same active interval, a transform may update the pose after
substitution. It receives the original entry, indices, row extent, rank,
seed, identity and the current evaluation time. `progress` increases
from zero toward one. `travel` measures distance from the target pose
for reveal and visible effects and increases with progress for a hide.
Both values preserve exact clock endpoints for step easing, including
individual microseconds next to the edges of very long intervals.
`nt_text_pose_motion` adds translation, elliptical displacement and hue
weighted by eased travel. Its phase in turns adds authored phase,
cycles times progress, rank times rank phase and optional seeded phase.
`nt_text_pose_noise` adds independent signed, stepped or smooth offsets
from the cluster's local clock, scaled by travel and the supplied extents.

A pose offset is rounded to whole cells and must fit in signed 32-bit
coordinates after rounding. The layout glyph may be replaced by one
grapheme of the same width. Authored spans still select paint by the
original source position. Paint is sampled at the original layout cell
with the current sample clock. Hue rotates the resulting foreground in
HSL. Pose opacity in zero through one scales both channels. The
destination clip and the whole-wide-glyph rule apply after displacement.
The surface painter uses the selected style owner. The component painter
uses the mounted node's owner.

Trail layer `n` draws at an earlier time by `n * spacing_us`, oldest first
with the current pose last. It contributes only for active cluster
intervals and scales opacity by `decay` raised to `n`. Trails can remain
visible briefly past a cluster endpoint. Draw each scene from its current
base frame to avoid accumulating prior frames. Reduced motion draws the
settled layout immediately for reveal or visible effects and adds no ink
for hide. It skips trail and transform evaluation.

Invalid options or poses, unrepresentable schedules or offsets, allocation
refusals and errors returned by a transform or paint sample preserve the
destination and its outstanding glyph borrows. The SDK publishes all
contributions together. [Text effect recipes](TEXT-RECIPES.md) show how to
compose these operations.

## Consumer cell content and image textures

Include `<nocturne/content.h>`. `nt_frame_from_cells` copies ordered
descriptors and their glyph bytes into an owned frame. A descriptor
with columns one or two uses the direct frame glyph write contract.
With columns zero, supply an empty glyph and zero foreground. The cell
then paints only its physical background, preserving any glyph there.
All authored positions must be in bounds. Errors leave the caller's
result pointer unchanged. The resulting frame can become an offscreen
source in any layer stack.

An image view is a borrowed, nonempty, row-major rectangle of straight
RGBA8 pixels. Specify row stride and buffer length in pixels. Row
padding is allowed. The sample API checks the addressable footprint
before reading. Missing storage, short stride, a nonfinite coordinate
or an invalid sampling mode is invalid. A short buffer returns
`NT_RESULT_INSUFFICIENT_BUFFER`, and arithmetic overflow returns
`NT_RESULT_OUT_OF_RANGE`.
The addressable footprint depends on target `size_t` width. A large
stride can overflow on wasm32 before the corresponding 64-bit build
reaches the short-buffer check.

Use normalized coordinates and independent pad, repeat or reflect
addressing on each axis. Nearest divides the normalized interval into
equal bins, with the boundary rounded to double precision. The next
texel wins at an interior boundary. Pad and reflect use the final
texel at one. Bilinear uses texel centers at `(i+0.5)/extent`, extends
the outer sample for pad and reflect, and interpolates across the seam
for repeat. A coordinate along an axis with one selected texel cannot
change the interpolation on the other axis. Blend in premultiplied
linear light, unpremultiply, encode
once and round to RGBA8. An alpha that rounds to zero produces
transparent black for bilinear. Nearest copies the selected pixel
exactly, including hidden channels. Native fixtures assert exact bytes
at representative centers, seams, padding, alpha edges and halfway
values. General coordinate and transfer calculations use double precision.

`nt_image_rasterize` samples at the centers of the caller's requested
physical sample grid, then uses the canvas tile reducer to construct an
owned frame. A nonzero sample alpha supplies geometric coverage and the
authored owner. Partial outer tiles pad with transparent samples. Select
cell or half-block for dense color fields, or a braille-based mode when
coverage is the principal shape. The API supplies general texture
sampling for PAINT-10. Panel-specific content providers follow in M9.

## Frame sinks and ANSI presentation

Include `<nocturne/present.h>`. `nt_frame_present` passes the frame
and an explicit timestamp to the caller-supplied sink. The callback
borrows the frame for the call. `nt_ansi_present` is a ready-made sink
callback that writes to a caller-supplied byte writer. The caller owns
transport, terminal acquisition, negotiated capabilities and clock.

Go writes a frame through `Frame.WriteANSI` with an `io.Writer`.
TypeScript uses `sdk.writeAnsi` with a synchronous byte callback. Both
use the native encoder and hold a frame lease during the call. They
copy each byte chunk before passing it to the writer and propagate
writer failures. Go reports a short successful write as
`io.ErrShortWrite`. Use `Xterm256Palette` or `sdk.xterm256Palette`
for the reference indexed palette.

Interactive mode emits an attribute reset and clear display, addresses
the start of each row, emits changes in foreground and background, and
resets attributes at the end. It assumes an exclusive display region
large enough for the frame. The presenter makes no terminal mode
changes. Noninteractive mode emits plain rows with LF terminators and
no ANSI control bytes, including when the selected color mode is
truecolor or indexed. An empty axis produces no rows.

Interactive truecolor writes the flattened RGB channels. Indexed
output uses the nearest opaque entry in the caller-provided terminal
palette, with 1–256 entries in index order. `nt_xterm256_palette` fills
the first 256 entries in caller-owned storage with the reference xterm
colors. A short buffer remains unchanged. Supply your own palette when
the terminal uses a different theme or index order. Monochrome emits glyphs
under the reset attributes. Add a glyph or text cue in the composition
for state displayed through colors alone, including stock control focus
and the editor's background-only caret. For RGB and indexed presentation,
composite the background over the matte RGB as an opaque color and then composite
the foreground over that background in linear light. A complete wide
glyph uses its lead cell's flattened background for the terminal
footprint. Physical backgrounds remain available in the frame itself.

The width policy applies to the presented glyph and preserves frame
columns. Disabled UTF-8, braille or half-block capabilities and width
mismatches use one question mark per occupied column. The same fallback
applies to two-column lone regional indicators and medial or final
Hangul Jamo without a visible Hangul base. A glyph that would
join the previous emitted glyph under Unicode grapheme rules uses the
fallback, so independently authored frame cells retain their positions.
These rules apply to plain rows and interactive output. A transparent
foreground presents as spaces over the chosen background. Reject
control scalars from glyph output and generate ANSI sequences only from
trusted commands and numeric style/position values. A write callback
must consume the entire byte slice before reporting success. A failure
is returned immediately and may leave partial transport output.
Interactive calls start with a reset and repaint, so a later call can
recover the display after transport returns.

The installed example renders authored Unicode with span colors into a
retained offscreen frame, clips that frame in two positions, and layers a
braille drawing below it. The presentation gate builds from a relocated
installation, checks plain output, and runs the same executable in an
isolated tmux display. It inspects visible rows and style attributes at
both positions of the retained layer. [Installed presentation gate](../tools/check_presentation.py)

## Component tree and publication

Include `<nocturne/component.h>`. Each tree owns mounted instances and one
published cell frame. Mount supplies measure, layout and paint callbacks.
Update, event and dispose are optional. The tree copies the descriptor and
keeps the state pointer. With a disposer, a successful mount transfers one
disposal obligation. A caller lending state keeps it live until detach.
Zero-initialize the parent reference to mount the single root. A different
tree's reference is invalid. A retired reference from this tree is stale.
Removal retires the entire working subtree before calling any of its
disposers in child-first insertion order. Reparenting preserves the
reference and state, and appends the node at the new parent's insertion
position. Reparenting to the same parent preserves the existing order.

`nt_tree_tick` visits due updates in working-tree insertion preorder. A new
mount's first update is due immediately. Its elapsed time is zero. Later
updates receive time since the last successful update. The tree records a
valid tick's timestamp before callbacks and preserves it through callback
failure. Same-time retry is allowed. `nt_tree_next_update` reports the next
deadline or an unscheduled result. `nt_tree_pending` reports the greatest
pending invalidation. Layout invalidation includes paint. An explicit
publish rebuilds the candidate even when no invalidation is pending.

The root measures under zero minimum and the requested frame size as its
maximum. It receives the full requested allocation at layout. A parent
enumerates and measures direct children with phase-specific contexts.
A successful child measure replaces its earlier measurement in that
invocation. Layout reads measurements produced by that parent's last
successful measure and places each participating child once. A parent
may omit children. A failed child measurement fails the enclosing pass.
Synchronous measure can nest at most 128 child calls below the root
measure. Deeper measurement returns `NT_RESULT_OUT_OF_RANGE` and keeps
the last committed snapshot. The tree can mount and remove deeper graphs.
Bounds are parent-relative, with signed origins and checked translation
to frame coordinates. A child mask is a union of child-local horizontal
spans. The placement call copies its visible coverage. A nonnull
zero-span mask has no coverage.

Paint runs parent first, then child subtrees by ascending z-index and
insertion order on ties. A paint context borrows its component's clipped
local surface, and expires at callback return. Paint uses the mounted
reference's owner token in that tree's namespace. `nt_paint_clip` returns
a local rectangular envelope. It can contain mask holes. It returns
an empty rectangle when the envelope's origin has no representable local
coordinate. Whole-glyph paint and erase follow the surface footprint rules.

Publish stages geometry, ancestor paths, masks, glyphs and frame size
together. Failed allocation or callback status leaves the frame, geometry,
focus, pending invalidation and outstanding frame borrows intact. A
successful publish replaces the frame borrow, including after resize.
A move changes working child lists immediately and appears in the committed
path at the next successful publish. Removing an old committed ancestor
makes the previous path ineligible immediately. The previous frame can
still be presented until replaced. `nt_tree_geometry` reports whether a
live reference has a valid committed placement. `nt_tree_hit` searches
reverse paint order and checks enabled ancestors, clipping, masks and
the target's hit-test option. No hit returns the zero reference.

Programmatic focus requires a live, focusable, enabled component with
visible committed bounds. A zero reference clears focus. The tree updates
focus before target-only loss/gain notifications. Publication and removal
clear focus when the current route becomes ineligible. Loss precedes
dispose. Notifications use the admitted tree watermark as their timestamp.
Notification failure discards its staged intents and invalidation. The
focus or capture transition remains committed and the enclosing native
operation keeps its own result. Intents copy counted payloads, including
embedded NUL, and retain historical source references until popped.
Queries inside a callback see the committed snapshot. Structural
mutations during callbacks return `NT_RESULT_WRONG_PHASE`. Reentrant
destroy and intent pop have no effect.

The [component and host protocol](COMPONENT-PROTOCOL.md) specifies
routing, gesture capture and terminal ownership. The [C ABI guide](ABI.md)
specifies the adapter rules for callbacks, object lifetimes and borrowed views. The
[Installed C99 and C++11 consumers](../tests/consumer) exercise these interfaces.
The [catalog](component-contracts.json) records implementation status.

## Box layouts

Include `<nocturne/layout.h>`. Mount a row, column, or stack with
`nt_box_layout_mount` and mount children beneath its returned reference.
Children can use any component descriptor, including another box layout.
A box lays out its direct children and leaves painting to them. The tree
owns a copy of the configuration and the optional item array. Array entries
apply in the current direct-child insertion order. Additional children use
the fallback. Moving a node can change which indexed override applies.
`nt_box_layout_set` replaces the complete configuration and invalidates
layout. Invalid bounds and failed allocations preserve the old configuration.
The existing frame remains available until the next successful publication.

Go and TypeScript expose the complete box configuration, including
bounded child extents, per-axis growth, alignment, and owned item
overrides. Their setters retain existing child mounts. Both bindings
also expose rule mounting and replacement with native orientation,
ASCII fallback, and cell style. Go uses the supplied `RuleOptions.Style`.
A zero Go style has transparent ink and leaves the glyph invisible.
TypeScript uses its default ink when the rule's `style` is omitted.


Each axis measures a child within its item range and the parent's available
extent after padding. The item minimum takes precedence when the parent
offers less space. A bounded range with equal endpoints fixes the size.
The preferred box extent is the sum of child preferences and gaps along a
row or column, and the largest preference on the other axis. A stack uses
the largest preference on both axes and ignores gaps. Padding is included
in the measured box size, which is clamped to the parent's constraints.

At layout, rows and columns reserve the configured gaps first. Children
shrink equally toward their minimums when needed. Remaining space can grow
items that opt in, up to their maximums. Indivisible cells of growth or
shrink go to earlier eligible children first. The group is aligned in unused
main-axis space. In a stack and on the cross axis of a row or column, alignment
is per child. A growing child fills that axis up to its maximum. Centered odd
slack uses the lower offset. Layout uses the measured preferences from the
same frame, and the child receives its final assigned size in its layout and
paint callbacks.

When the minimums or gaps exceed the available space, the group starts at
the leading edge. Bounds retain the child sizes and paint and hits are
clipped to the padded content rectangle, then to ancestor clips. A child
starting at or beyond either trailing edge is omitted. Empty content omits
all placements. A stack retains insertion paint order. Use
`nt_layout_place_clipped` for this same rectangular clip in a custom
container. Go and TypeScript callbacks expose this placement and the
resulting component-local paint clip. Their callback paint contexts
can erase a glyph at its lead cell. A custom component can walk children in insertion order with
`nt_measure_child_after` or `nt_layout_child_after`. Pass a zero previous
reference to start and stop when the returned reference is zero.

The installed consumer mounts the same animated child in a custom center
container and in a standard stack. It checks resize, parent-relative
placement, retained publication after a callback error, and disposal in
both configurations. [Installed example](../tests/consumer/component.c)

## Clipped viewports, scrolling and anchors

Go and TypeScript expose the scroll clamp and anchor calculation
independently of mounted viewports. They use the same integer rounding,
including oversized children whose anchors can have negative origins.

Include `<nocturne/viewport.h>`. A viewport clips one direct child to its
assigned rectangle and inherits ancestor clips. An empty viewport is valid.
Use a row, column or stack as its single child to display multiple components.
Mounting additional direct children is a tree operation. Measurement with more
than one direct child fails the pending publication and preserves the previous
snapshot.

A viewport measures content without a maximum on each enabled scroll axis.
It clamps its own preference to the incoming constraints. In layout the
content fills the assigned viewport on disabled axes. An enabled axis uses
the larger of the assigned extent and the child's measured preference.
`nt_scroll_resolve` caps each requested cell offset at the excess content
extent and produces zero on disabled axes. The viewport places the child
at the negative effective offset. An origin outside the representable
coordinate range fails publication. An assigned rectangle with an empty
axis omits the child.

`nt_viewport_set` copies pending options and invalidates layout. A resize
clamps effective scroll again using the new allocation. `nt_viewport_view`
reads committed geometry and scroll, including from a component callback.
Pending options and a failed publication leave that view intact. A viewport
without a valid committed placement returns a zero-initialized view. With
no live content placement, it reports its own extent with a zero child,
content extent and offset. A child removed after publication immediately
becomes ineligible for hit testing and for the viewport's live content view.

`nt_anchor_rect` calculates a parent-local placement for a child with an
explicit size. It aligns each axis at the start, center or end before adding
the signed offset. An odd center difference rounds toward the leading
edge. End and center alignment can place oversized children at negative
coordinates. Custom layout callbacks can pass the rect to
`nt_layout_place` or `nt_layout_place_clipped`, which apply the ancestor
clip. The [installed viewport consumer](../tests/consumer/viewport.c)
checks nested clips, resize, retained scroll on failure and anchor bounds.

## Stock labels, boxes, panels and rules

Include `<nocturne/widgets.h>`. Stock components mount in any public
container and participate in the same callbacks and snapshot as custom
components. They take their storage allocator from the tree. Configure
them with the matching setter between tree operations. A successful setter
keeps the mount identity, copies the new input and requests layout. An
invalid option or failed copy preserves the prior configuration. Text is
counted UTF-8. The copy owns its bytes and the panel owns any mask spans.

A label is a leaf with the text layout policies for width, wrapping,
alignment, direction, tabs and ellipsis. Supply both width choices and a
nonzero tab stop. It measures text under its parent constraints. It lays the
text out again against its assigned size for painting. Wide glyphs, tabs,
bidirectional ordering and clipping follow the text and paint contracts.
Text paints with the mounted owner's token.

A panel fills its assigned rectangle and takes zero or one direct child.
Use an unframed panel as a fill-only box. A frame reserves one cell on each
edge and draws single-cell corners and strokes. Set the ASCII option to use
`+`, `-` and `|`. Additional padding reduces the body rectangle. A
framed panel can draw a counted title in the top edge between its corners.
The title uses the label's text policies in a single row with wrapping
disabled. An optional body mask describes a union of child-local row spans.
It clips the body and its descendants and affects their hits. An empty
nonnull mask has zero body coverage. An empty body rectangle omits the
child.

A rule is a leaf with a one-cell intrinsic extent. It fills its assigned
length on the horizontal or vertical axis and draws on the leading row or
column. The ASCII option uses `-` or `|`. Paint runs within the published
candidate and applies the same ancestor clips as other components.

The [installed composition](../tests/consumer/composition.c) combines these
components with a [separately compiled indicator](../tests/consumer/indicator.c).
The consumer changes layout and reuses the same mounted components while
the indicator samples an explicitly timed tween. It checks ownership,
allocation failure, focus loss before disposal and stale generation
rejection.

## Basic controls and typed intents

Use `<nocturne/controls.h>` with the component tree. Controls are leaves.
Mount with `enabled` to allow interaction, `focusable` to
receive keyboard focus, and `hit_testable` for pointer interaction. Pointer
presses on interactive controls request focus when the mount allows it.
Use ordinary box layout, panels and clipping to position controls. The
[controls consumer](../tests/consumer/controls.c) exercises each mount and
uses public headers in both the native and installed builds.

Mounts preserve the caller's output on failure. Getters leave their output
untouched on error. A foreign reference or wrong control kind returns
`INVALID_ARGUMENT`, a retired reference returns `STALE_REFERENCE`, and
a setter or mount during a tree callback returns `WRONG_PHASE`. Memory
comes from the tree allocator. Failed copies and invalid options leave prior
control state intact. Programmatic setters change state without emitting
intents. Getters read live state except for `nt_editor_view_offset`, which
reads the last successful publication.

Applications drain the tree's intent queue and call `nt_control_event_decode`
for entries of kind `NT_INTENT_CONTROL`. The intent's source identifies the
control. Consume the borrowed payload before popping or destroying the queue.
Decode rejects a wrong intent kind, payload size, discriminant or out-of-range
value for the fixed-value variants with `INVALID_ARGUMENT`. Use the typed
value returned by decode.

| Event kind | Value and timing |
|---|---|
| `NT_CONTROL_ACTIVATED` | Zero. One per accepted button activation. |
| `NT_CONTROL_ITEM_CHANGED`, `NT_CONTROL_CHECKBOX_CHANGED` | New Boolean state encoded as zero or one. |
| `NT_CONTROL_CHOICE_CHANGED` | Zero-based selected entry index. One per changed user selection. |
| `NT_CONTROL_SCROLL_CHANGED` | New scrollbar offset, in cells. One per changed user offset. |
| `NT_CONTROL_EDITOR_CHANGED` | Zero. One per successful normalized event that changes document bytes. |
| `NT_CONTROL_EDITOR_SUBMITTED` | Zero. One per accepted submission key press. The consumer can read the borrowed editor's live text. |

The event callbacks reserve an intent before a corresponding mutation. If
allocation fails, that callback leaves the value or document unchanged.
Focus moves, select expansion, pointer selection and indicator ticks do
not emit application intents.

### Button, item and checkbox

`nt_button_mount`, `nt_item_mount` and `nt_checkbox_mount` copy counted
UTF-8 and use the first visual row with no within-line wrap. Empty labels
are accepted. The item and checkbox use explicit initial selection values.
`NULL` style uses `nt_control_style_default`, with independent normal,
focused and engaged colors, ambiguous width one and emoji width two.
Custom width choices must be one or two. Focus colors take precedence
over the selected palette. A held press inside uses the engaged palette.

Enter and Space activate on press with any lock bits. Primary button down
captures one pointer, and up inside the topmost committed hit target
activates. Moving out disarms the pressed visual, and returning rearms it.
Release outside, focus loss, cancellation and removal end the gesture
without activation. The button's pressed query reads the current capture
and last inside state. Item and checkbox activation toggles the respective
Boolean. Their getters read live values and their setters invalidate paint
on a changed value.

### List, radio and select

Use `nt_choice_mount` with `NT_CHOICE_LIST`, `NT_CHOICE_RADIO` or
`NT_CHOICE_SELECT` for a single selection. Mount and configure copy the
entire array, its UTF-8 labels and style. Zero entries with
`NT_CHOICE_NONE` is valid. Nonzero counts require an array, and counts
above the supported range return `OUT_OF_RANGE`. Disabled entries are
visible and cannot be selected. An index outside the array returns
`OUT_OF_RANGE`, and selecting a disabled index returns `INVALID_ARGUMENT`.
`nt_choice_set` accepts `NT_CHOICE_NONE` to clear selection.
`nt_choice_configure` replaces the model in place and collapses a select.

Lists and radio groups display a window anchored at the start when
possible and scrolled far enough to keep the selected row visible.
`visible_rows=0` uses all available rows subject to assigned space. Up
and Left step backward, Down and Right step forward, and Home and End
select the first or last enabled row. Arrow and edge keys accept repeats.
A vertical wheel event steps once in its signed direction. Navigation
skips disabled entries and stops at the ends. Primary pointer activation
uses the rows displayed in the committed frame. A configure operation
invalidates the preceding rows and gestures started on that model.

A select starts collapsed and shows one header row with the current label.
Enter, Space or a primary click on the header toggles expansion. Escape
or focus loss collapses it. Expand reserves one header row plus up to
`visible_rows` entries within the assigned rectangle. Clicking an enabled
entry selects it and collapses the list. Arrows also change selection in the
collapsed state. The component stays in the parent's layout and clip.

### Cursor, wait, progress and scrollbar

| Control | State and behavior |
|---|---|
| Cursor | `visible` enables the one-cell marker. A positive `phase_us` alternates visibility at caller clock boundaries. |
| Wait | `ascii` selects ASCII or braille glyphs. A positive `phase_us` advances through four frames. |
| Progress | `0 <= value <= maximum` and `maximum > 0`. Invalid values return `OUT_OF_RANGE`. Filled length is the floor of the assigned axis length times the ratio. |
| Scrollbar | `content` and `viewport` are cell counts. Mount and setter clamp `offset` to `max(content - viewport, 0)`. A scrollable thumb is at least one cell long and reaches both ends of the track. |

Cursor and wait start at phase zero. At each scheduled tick, phase is
derived from the caller's clock divided by `phase_us`. Zero leaves the
phase steady. Reduced motion paints phase zero at scheduled ticks.
A hidden cursor has no repeating deadline. Updating either configuration
resets its phase and requests a new update. Progress and scrollbar use
a horizontal track unless `vertical` is set. The caller supplies styles
by value. Cursor, wait and progress are display-only.

A focused scrollbar accepts axis arrows, Page Up/Down by `viewport`,
Home/End, and repeats. A wheel changes offset by its signed axis delta.
The horizontal orientation uses the vertical wheel delta when there is no
horizontal delta. Primary press and captured drag map the pointer's position
across the committed track to the whole range. Release ends the gesture
at its last press or drag value. Pointer movement beyond either end
clamps. A changed range or orientation takes effect for pointer mapping
at publication, including during a held drag. A one-cell track can still be adjusted
by key or wheel. The view and scrollbar are separate control instances.
Connect them through the typed offset intent and the appropriate scroll
setter, and update the scrollbar range when the content or viewport changes.

### Mounted editor and reader

`nt_editor_view_mount` borrows the editor and optional register bank until
the view is removed or the tree is destroyed. Key bindings and styles are
copied. Invalid binding keys, actions, motion commands or missing register
banks return `INVALID_ARGUMENT`. A register index outside the bank returns
`OUT_OF_RANGE`. Omitting a key map enables no editing commands. Text and
paste are still available for editable documents. The buffer's existing
read-only and single-line policies apply to all changes. Rejected edit
commands due to editor policy remain unhandled. A present key binding has
priority over optional reader scrolling for that chord.

Measure yields the document's natural width and wrapped height under
constraints. For growing input, place the view in an intrinsic row with
a bounded height. Unwrapped content can scroll horizontally. A stock
panel frames the view without changing the editor or its key map. For a
reader, create a read-only editor and enable `scroll_keys` to accept arrows,
Page Up/Down and Home/End while focused. Wheel scrolls by cell deltas.
`nt_editor_view_scroll` requests a new column and row offset. The next
successful paint clamps it against the document and assigned viewport.
`nt_editor_view_offset` returns `WRONG_PHASE` until the view has a
nonempty published placement.

`follow_caret` minimally scrolls the viewport toward the active caret.
Explicit scrolling suspends following until a handled editing command or
a successful pointer selection. `show_caret` highlights the active
edge while focused. At a full wrapped row's right edge it highlights the
last cell. The unwrapped caret may use an extra cell for the end
edge. Pointer down places the caret from the published visual layout,
Shift extends, and captured drag and release extend the selection.
The view skips pointer placement while document bytes differ from the
published layout and requests a new layout. Failed publication preserves
the previous offsets, pointer mapping and frame. Selections use logical
grapheme boundaries, including in bidirectional content.

`submit_on_enter` gives an unmodified Enter press precedence over the
key map and emits a submission intent. Enter repeats are consumed without
repeated submission. The consumer owns history, clearing and every
application action. When callers change the borrowed editor directly,
call `nt_tree_invalidate` with `NT_INVALIDATE_LAYOUT` for changes to bytes
or `NT_INVALIDATE_PAINT` for selection changes before republishing. Caller
changes emit no control intent.

## Typed properties and explicit bindings

Include `nocturne/property.h` for direct property access and
`nocturne/binding.h` to compile a binding graph. A value is a Boolean,
a signed or unsigned 64-bit integer, a finite real, counted UTF-8 text,
or encoded RGBA. Types match exactly. Validate a standalone value with
`nt_value_validate`. An invalid type or malformed UTF-8 returns
`NT_RESULT_INVALID_ARGUMENT`. A nonfinite real returns
`NT_RESULT_OUT_OF_RANGE`. A text value may contain an embedded zero.

### Mounted properties

A property reference pairs a live node generation with a nonzero ID local
to that node. `nt_tree_property_count` and `nt_tree_property_at` enumerate
the exposed properties in declaration order. Each entry describes the type,
whether it is writable, and the invalidation a successful write requests.
Enumeration has no callbacks. A missing ID or an attempt to write a
read-only property returns `NT_RESULT_UNSUPPORTED_CAPABILITY`. A removed
reference returns `NT_RESULT_STALE_REFERENCE`. A foreign tree or a zero
property ID is invalid.

Use `nt_tree_define_properties` to copy a descriptor table onto a live mount
without a property table. This includes stock boxes, which declare no
properties. IDs must be unique. Registration failure preserves the mount.
A read callback borrows the mounted state and returns a typed
value. Text returned from a direct get borrows the mount's storage until
its state changes or it is disposed. A direct set borrows its value for
the duration of the call. A text writer copies or owns its committed text.

Use `nt_tree_define_property_provider` when one provider handles several
property IDs. The same registration rule applies. The tree copies the
property metadata and provider callbacks.
Each read, prepare and finish callback receives the provider context and
property ID. Read calls `nt_property_output_set` once with the declared
type. The output is scoped to that callback, and the tree copies counted
text during the call to the setter. A successful get borrows that copy
until a later read replaces it or the mount is disposed. The provider's
optional release callback runs once before component disposal after a
successful registration. The caller owns the context on registration
failure.

A writer can prepare resources and validate its domain against the previous
state. Successful preparation receives one finish callback with the same
value and preparation pointer. Commit changes only the descriptor's own
property and cannot fail. Discard releases preparation resources while
preserving component state. A failed prepare releases its own resources.
An infallible writer can omit prepare and receives a null preparation.
Read and prepare preserve observable component state. If a domain spans
multiple fields, expose their update through a single property or use
the component's explicit configuration API. Separate descriptors validate
against the same prior state.

Stock mounts expose the following properties through this interface:

| Component | Property | Type and domain |
| --- | --- | --- |
| Label | `NT_PROPERTY_TEXT` | Counted UTF-8, replaced with layout invalidation |
| Label | `NT_PROPERTY_FOREGROUND`, `NT_PROPERTY_BACKGROUND` | Encoded RGBA colors with paint invalidation |
| Progress | `NT_PROPERTY_VALUE` | Unsigned value up to the current maximum |
| Scrollbar | `NT_PROPERTY_OFFSET` | Unsigned offset up to the current scroll limit |
| Checkbox | `NT_PROPERTY_CHECKED` | Boolean checked state |
| Item | `NT_PROPERTY_SELECTED` | Boolean selected state |
| Choice | `NT_PROPERTY_SELECTED` | An enabled entry index, or `UINT64_MAX` for no selection |

The typed scrollbar setter rejects an offset outside its domain. Its
imperative configuration setter continues to clamp. Programmatic property
writes update the same state as stock getters and emit no input intents.

### Atomic property batches

`nt_tree_properties_apply` accepts a counted list of property assignments.
Each live, writable property may appear once. The tree validates all
references and value types, then copies all text inputs before preparing
any write. An assignment may borrow text from a property that the same
batch changes. Preparations run in list order against prior state.
A failed preparation discards earlier preparations in reverse order.
After all preparations succeed, commits run in list order and accumulate
invalidation for the next publication. The published frame remains
borrowable until the next successful publication.

An empty batch succeeds. The normal tree mutation guard covers allocator,
preparation and finish callbacks. The [state update consumer](../tests/consumer/state_update.c)
exercises a swap of borrowed label strings alongside a bounded progress
value in the same batch.

### Binding graphs

`nt_binding_graph_create` copies a graph of input, property-read and computed
values and a list of writable targets. At least one value and one target
are required. Dependency indices can refer forward. A dependency outside
the graph returns `NT_RESULT_OUT_OF_RANGE`. Cycles, including self edges,
type mismatches and duplicate targets return `NT_RESULT_INVALID_ARGUMENT`.
Graph creation checks target types and permissions without calling component
or compute callbacks. The cycle walk uses explicit storage and evaluates
each node once, including shared dependencies.

`nt_binding_graph_apply` takes values for the input nodes in declaration
order. It validates the graph's live endpoints and input types, copies
input text, then reads all property sources before running any computation.
Computed arguments follow the declared dependency order. Each computed
callback returns one typed value and may borrow its output text through
the immediate SDK copy. Its context remains owned by the caller. Computed
callbacks preserve component state and have no externally visible side
effects. Reading a property that is also a target uses the prior value
in that call. A later explicit apply can read the newly committed value.

Only after all evaluations succeed does the graph prepare its target
writes in declaration order. Any failure discards successful preparations
in reverse order and leaves the prior property values, invalidation and
published frame intact. Once every preparation succeeds, finish commits
in declaration order and the tree accumulates their invalidations. The
caller publishes the next frame through the normal tree API. The binding
graph does not tick, publish, route events or apply itself. Repeated calls
read current property state and may reuse the same graph with new inputs.

Tree and graph entry points are serialized by the caller. Read, compute
and property preparations and finishes run with the tree mutation guard
held. Reentry into callback-bearing property or graph calls returns
`NT_RESULT_WRONG_PHASE`. Endpoint checks reject stale graphs before
running callbacks. Removing or replacing any referenced mount makes the
graph stale, and destroying the tree detaches all graphs without running
graph callbacks. The caller destroys graphs explicitly. The tree allocator
context and all borrowed compute contexts must outlive the graph.

The [binding consumer](../tests/consumer/binding.c) binds one set of
explicit inputs to stock controls and a custom property and verifies
allocation rollback, stale references and computed dependency order.

## Event rules and typed intents

Include `nocturne/event_rules.h`. A component event callback can call
`nt_event_apply_rules` with an ordered, borrowed rule array. Each rule
names one normalized event kind and one routing phase. Both must match
the event and phase of the current callback. Ordinary dispatch controls
routing and callback invocation, including reserved keys and eligibility.
Focus and cancellation notifications can use the helper during their
ordinary callback, including a loss notification before disposal.

The call validates rule shapes and the property references of all
matching rules before any property read, predicate or derivation. Each
matching rule reads its listed live properties in declaration order,
copying text immediately after each read. All its predicate and field
callbacks receive the same typed values, or a null array for zero
properties. Properties reflect working state at the time of the call.
The caller can pass rule-owned context with additional state for the
predicate or derivation to read.

A null predicate allows the rule. A false predicate skips its derived
fields and emits nothing. A true predicate runs each field derivation
once in increasing field ID order and stages exactly one typed intent.
A rule can emit an empty field list. Derived values must match their
declared types and satisfy `nt_value_validate`. Derived text may borrow
storage through its immediate SDK copy. Predicates and derivations
preserve component and application state and have no externally visible
side effects. The normal tree mutation guard stays held. Attempts to
emit or apply rules from a predicate or derivation return
`NT_RESULT_WRONG_PHASE` without changing the enclosing result.

Use `nt_event_emit_fields` for already-computed typed fields, or
`nt_event_emit` for counted bytes. Field IDs within an intent must be
strictly increasing and nonzero. The queue copies typed values, counted
UTF-8 and raw bytes, including embedded NULs. Intent kinds and field IDs
are assigned by the application or component. The helper's optional
output count reports how many intents it staged and remains unchanged on
failure. Staged intents become visible only when the enclosing event
callback succeeds with a valid reply. An evaluation, validation or
allocation failure is retained on the event context even when its caller
ignores the return value. The tree discards all output staged by that
callback and preserves intents committed by earlier callbacks. Updates to
the component's own working state remain the callback's responsibility.
Focus and cancellation notifications apply the same capture and
invalidation enum checks before committing output. Their cleanup is
already in effect when the notification runs.

Each successful matching rule emits once per explicit invocation.
Repeated calls, distinct rules, routed callbacks and distinct normalized
events can each emit. `nt_tree_peek_event_intent` inspects the head of the
same FIFO as `nt_tree_peek_intent` and `nt_tree_pop_intent`. Its payload
kind distinguishes bytes from typed fields, including either empty form.
The legacy view exposes a typed intent's source and kind with an empty
byte payload. Field arrays, text and byte views borrow queue storage
until pop or tree destruction. An empty queue leaves the peek output
unchanged. Removing a source preserves its committed intents as history.
Consumers drain outside callbacks and own any application effects,
acknowledgment, retry or external I/O.

## Scheduled batches and scoped subscriptions

Include `nocturne/schedule.h`. The caller sets active batch and subscription
capacities with `nt_tree_schedule_configure`. Reducing a capacity below
its current use returns `NT_RESULT_OUT_OF_RANGE`. A configured zero
capacity permits no entries of that kind. Each successful registration
returns a nonzero tree-local ID. An exhausted ID sequence returns
`NT_RESULT_OUT_OF_RANGE`. A cancellation removes one live registration.
Canceling an already removed, completed or canceled registration returns
`NT_RESULT_STALE_REFERENCE`.

`nt_tree_schedule_batch` copies a nonempty property assignment list and
its text. It validates references and value types on admission. The batch
also names a live owner. Removing the owner or any assignment target
retires the whole batch before disposal. Work uses live generations, so
slot reuse leaves retired work retired. At execution the batch checks
current domains through the same atomic property transaction as direct
updates. A failed batch is removed, leaves its own targets and invalidation
intact, and reports its ID. Earlier successful batches stay committed.

`nt_tree_schedule_next` returns the earliest queued deadline. Call
`nt_tree_schedule_run` with a caller-supplied monotonic timestamp and a
maximum number of batches to attempt. Due batches run in deadline order,
with registration order breaking ties. The time watermark advances on an
admitted run, including one with a zero budget or a failed batch. An
earlier timestamp returns `NT_RESULT_OUT_OF_RANGE` and preserves the run
report. The admitted report counts attempted batches, including a failure.
The full `NtTimeUs` range is valid. Application clocks and frame
publication remain explicit. The scheduler and ordinary component updates
have separate entry points and deadlines.

`nt_tree_schedule_subscribe` associates a receiver with a live owner and
an exact intent kind. The tree borrows the receiver context until
cancellation, owner removal or tree destruction. Call
`nt_tree_schedule_deliver` to consume up to the requested number of queued
intents. Each item goes once to each matching subscription, in registration
order. An item with no receivers is still consumed. A zero budget leaves
the queue intact. Receivers borrow the payload for the call and observe the
committed queue head during delivery. Tree mutations, recursive delivery,
and cancellation from a receiver are guarded as callback operations.
Ordinary peek and pop remain available between calls. Removing a mount
retires its subscriptions before disposal. Other live subscribers may
still receive historical intents emitted by the removed source.

## Plot and numeric components

Include `nocturne/plot.h`. `nt_plot_project` maps a finite point in an
inclusive data domain into continuous cell coordinates. X increases to
the right and increasing data Y projects upward. The extreme values map
to the corresponding outside cell edges. A constant axis maps its single
value to the center. A point outside the declared domain returns
`NT_RESULT_OUT_OF_RANGE`, leaving the output untouched. A viewport has
positive cell extents and a finite nonnegative cell aspect. Zero aspect
selects a physical cell width of half its height.

When equal units are requested and both axes vary, the projected rectangle
fits the full data range inside the viewport and centers the unused space.
It uses the authored physical cell aspect so one unit has the same physical
scale in both directions. A constant axis retains center placement. The
segment clipper reports visibility in the inclusive data box and returns
the surviving directed endpoints. A segment touching a box at one point
can produce two equal endpoints. A fully hidden segment leaves its output
endpoints unchanged. Segment visibility is decided from exact binary-input
parameter comparisons. The endpoints are rounded after that decision.
The public geometry queries preserve the caller's floating-point environment
and use nearest rounding with gradual underflow. An unavailable standard
environment returns `NT_RESULT_UNSUPPORTED_CAPABILITY`.

`nt_plot_mount` and `nt_plot_set` copy the options and supplied data. A
failed replacement preserves the prior data, measure and published frame.
The component's assigned rectangle governs the transform. A parent clip
restricts which samples are painted, and a parent mask restricts final
glyphs and fills. Chart backgrounds and all data contributions have the
mounted owner. Empty data paints only the optional background. Zero
preferred extents select 16 columns and six rows before constraints.

| Kind | Data and geometry |
| --- | --- |
| Bars | Ordered equal bands across the assigned width. Each bar occupies the center 80% of its band and extends from the authored baseline to its Y value. Both ends are clipped to the Y domain. An automatic Y domain includes the baseline. |
| Pie and donut | Nonnegative Y values are weights. Sectors start at the top and sweep clockwise in array order. Zero weights have no area. The physical circle fits within the assigned viewport. A donut selects its inner radius as a fraction of the outer radius, with 0.5 selected by a zero input. |
| Scatter | Each selected point inside the domain marks one sample of the 2-by-4 cell grid. |
| Line | Consecutive selected points form a directed polyline. Each segment clips to the data domain before projecting and always includes both raster endpoints. A single point is supported. |
| Edges | Consecutive endpoint pairs form independent segments. The first endpoint supplies the pair's color when per-sample colors are enabled. |

Automatic X and Y ranges cover the finite source values before sampling.
An empty automatic range is `[0,1]`. A line uses the destination sample
color for each segment when per-sample colors are enabled. Otherwise, a
chart uses its foreground color. A data limit of zero accepts the full
authored array. A positive limit rejects larger arrays before copying.
For scatter and line, a stride greater than one takes the first point,
every stride-th point, and the final point. For edges it strides whole
pairs. Bars and sectors use their entire data. Consumers can supply a
moving data window to bound retained and rendering costs. Cartesian
raster storage is proportional to the rectangular visible clip. The
2-by-4 grid requires assigned columns no greater than `INT32_MAX/2`
and assigned rows no greater than `INT32_MAX/4` for a visible chart.

The chart reduces covered samples with the shared cell, half-block,
braille or hybrid reducer. ASCII mode uses one `#` for any covered tile
with its reduced foreground color. Chart foreground and background are
writable typed properties. `nt_tree_tick` samples the caller's time for
an optional reveal. Cartesian charts expose samples from left to right,
and radial charts sweep from the top. Zero duration presents the complete
chart. A reduced-motion update completes the reveal and clears its
deadline. A reconfiguration rearms the update callback. The tree keeps
publication explicit.

`nt_digits_mount` and `nt_digits_set` display a signed 64-bit integer
using three-column, five-row seven-segment digits, a minus sign, and an
optional decimal point. Fractional digits from zero to 18 place the
decimal in the integer's exact decimal spelling. Leading place zeros are
filled as needed. `INT64_MIN` is supported. Each character has a one-cell
gap before the next character, and a decimal point uses a single column.
The display paints into its local clip with either full blocks or `#`.
The integer value is writable through `NT_PROPERTY_VALUE`, and its colors
are available through the foreground and background properties.

The [plot consumer](../tests/consumer/plot.c) exercises domains, aspect,
negative bars, both sector forms, clipping, sampling, fixed-point digits
and timed reveal in mounted trees.

## Versioned property snapshots

Include `nocturne/state.h`. Capture takes an ordered list of unique live,
writable property references. Stock component values eligible for capture
are listed under [typed properties](#typed-properties-and-explicit-bindings).
Applications can include custom properties that meet the same descriptor
contract. Capture copies each validated value and its counted text. A
snapshot owns its bytes and can outlive the original tree. The application
owns storage, destination identity mapping and external I/O. Tree topology,
component configurations, clocks and lifecycle state come from the
application's ordinary mount and configuration calls.

Restore takes the destination property list in snapshot order. The number
of entries, property IDs and value kinds must agree. Each destination must
be writable on the current tree and may appear once. Restore prepares and
commits the entire list as one atomic property batch. A domain failure or
stale generation preserves the prior values, invalidation and published
frame. Applications can restore to corresponding mounts on a fresh tree.

The byte format is version 1. The 16-byte header contains ASCII `NTST`, a
little-endian unsigned 32-bit version, and a little-endian unsigned 64-bit
entry count. Each entry starts with a little-endian unsigned 32-bit
property ID, a one-byte value tag and a little-endian unsigned 64-bit
payload length. The payload follows immediately. The version and tag
assignments are part of the format.

| Tag | Value | Payload |
| --- | --- | --- |
| 1 | Boolean | One byte, 0 or 1 |
| 2 | Signed integer | Eight little-endian two's-complement bytes |
| 3 | Unsigned integer | Eight little-endian bytes |
| 4 | Real | Eight little-endian IEEE 754 binary64 bits, finite values |
| 5 | Text | Counted UTF-8, including embedded NULs |
| 6 | Color | Four straight-alpha encoded sRGB bytes in RGBA order |

Import validates exact sizes, tags, property IDs and payloads before
returning an owned copy. The application owns integrity verification and
storage policy. A recognized snapshot header with an unknown
version returns `NT_RESULT_UNSUPPORTED_CAPABILITY`. Required compatibility
covers this documented schema. Real entries require compatible C `double`
and `uint64_t` binary64 representation. The borrowed byte view lasts until
snapshot destruction. An empty snapshot is valid and restores an empty list.

## Stylesheets, states and text regions

Include `nocturne/style.h`. A stylesheet owns an immutable copy of its
rules and text values. Rules match a subject and declare one typed property
value. A sheet may be empty. A caller can share a sheet across consumers
and supplies the order of sheets at each resolution. Destroy a sheet
after uses of the sheet and any borrowed result have ended.

### Selectors and precedence

A component element carries caller-defined role and region tags and state
bits. A zero selector tag is a wildcard. All required state bits must be
set and every forbidden bit must be clear. Required and forbidden masks
must be disjoint. A selector can additionally require its immediate
parent or any ancestor to match a second condition. The immediate parent
comes first in an explicit path. Subject and ancestor conditions must both
match when a relationship is selected.

Text predicates match the first logical byte within a selected half-open
byte interval, the label of a region that covers that byte, an exact
cluster ID, or any Unicode scalar in an inclusive scalar interval. They
combine by intersection. Overlapping regions can match independently.
An empty span represents a component or synthetic glyph and matches no
text predicate. The pure resolver validates the supplied text slice as
UTF-8. Region bounds lie within the original source and need not coincide
with grapheme boundaries. Text rendering uses the layout's logical source
and cluster IDs even when visual order differs.

Resolution is per property. Among matching rules, the override tier wins
over the ordinary tier. Within a tier the highest specificity wins. Count
one for each specified role, region, required or forbidden state bit, text
region, cluster identity, byte interval and scalar interval. Count one for
an ancestor relationship plus its condition specificity. A direct parent
relationship adds one more. Equal specificity favors the later sheet, then the later rule within
that sheet. `nt_style_resolve` writes
a match flag and a value only for a match. In a successful no-match result
it writes a false flag and preserves the input value. A returned text
value borrows from the winning sheet.

### Applying styles to a tree

`nt_tree_apply_styles` uses current structural ancestry, including a
successful reparent before the next frame is published. The caller
supplies unique live mount annotations and a list of unique writable
property references. An omitted annotation has zero role, region and
state masks. The same sheet can target stock and custom descriptors.
Text predicates do not match this component-only subject. A property
with no matching rule retains its current value. A matched property
must use its descriptor's declared type and domain.

The tree guards the operation and prepares all matching targets before
committing any. Invalid or stale targets, ambiguous duplicate inputs,
allocation failure, type mismatch or domain failure preserve prior
properties, invalidation and the published frame. Only the listed
properties can be written. The caller can supply a different state and
apply again. Apply uses the current property value when a declaration is
absent, so a consumer that needs a baseline restored can provide an
ordinary default rule. Styles do not publish frames or infer application
state.

### Styled text painting

`nt_style_text_paint` and the mounted `nt_paint_styled_text` resolve
foreground and background separately for each visible cluster. They start
from the existing base or last applicable text paint span. A winning
channel rule must be RGBA and replaces that channel's color and procedural
paint. Other property IDs are ignored. The rest of the style remains
available, including the original owner on a surface. Mounted painting
uses the mount owner. Synthetic ellipsis can still match component
conditions. Wide and combining glyphs paint as complete clusters.
On failure the destination is preserved. Scope paths, rule sheets,
regions, layout and paint options are borrowed through the call.

The [style consumer](../tests/consumer/style.c) verifies nested
precedence across stock and custom properties, reparenting, rollback,
separate foreground and background matches, and original source regions
in a bidi layout.

## Terminal input decoding

Include `<nocturne/input.h>` for a host-neutral decoder. The caller owns the
transport and supplies nondecreasing read times in monotonic microseconds.
`nt_input_read` accepts a counted byte span and returns at most one record.
Advance the span by `consumed` and continue until it is empty. Pending
records buffer a fragment. A discarded record has no event. A ready Escape
or discarded prefix can consume zero bytes while leaving the next byte
available for another read. Text fields borrow decoder storage until the
next read, cancellation, or destruction.

Go offers an owned `InputDecoder` through `NewInputDecoder`. TypeScript
uses `sdk.createInputDecoder` and accepts byte arrays so incomplete
UTF-8 can span calls. Both copy text in the returned records. A ready
key includes its associated text in `Input.Text` or `input.text`.
The ready record carries the event's exact `uint64` or `bigint` timestamp.
A Go read may return an accepted prefix together with
`AllocationFailed`. In TypeScript, `InputReadError.partial` carries the
accepted prefix for the same condition. Resume at that prefix and
keep the next byte available for retry. Close the decoder when the
stream ends. Go's options use literal zero values. Begin with
`DefaultInputDecoderOptions` to change one setting while retaining
the default 25 ms Escape delay and 1 MiB paste limit.

A lone Escape has a deadline from `nt_input_deadline`. Pass an empty read
at that time to deliver the Escape with its original timestamp. A following
byte can extend the Escape before the deadline. Once a UTF-8 scalar, CSI,
SS3 or control string has started, it can span input reads. A completed
record uses the completion read's time. The caller can discard partial
input and the terminal button state with `nt_input_cancel`, which emits a
host cancellation with the reason supplied by the caller.

Legacy printable UTF-8 yields a key plus associated text. Named keys cover
Escape, Enter, Tab, Backspace, arrows, Home, End, page keys, insertion,
deletion and F1–F12. ASCII capital letters normalize to a lowercase key
with Shift and retain their original text. Legacy control bytes use their
usual named key or Control chord. Escape-prefixed printable input uses Alt.
CSI and SS3 accept the supported navigation and function-key encodings.
Select the keyboard protocol when constructing the decoder. XTerm is the
default and applies XTerm's modifier encoding to CSI and SS3 and its
modified character reports. In XTerm mode a supported CSI R key encoding
means F3. A host issuing a cursor-position query uses
`nt_input_expect_cursor_report` to give the next reply precedence and
clears the expectation on timeout. A position reply has two positive
parameters and is discarded as transport metadata.

Kitty mode uses its modifier encoding throughout the enhanced key
formats. CSI u can carry Unicode keys, optional modifiers and press,
repeat or release state, plus explicit scalar text. A key-only Kitty
report has no associated text. A report with only text yields
`NT_EVENT_TEXT`. Kitty's F3 applies to its SS3 or numbered key encoding.
Associated `key_text` is independent of the key event. A host with a
reserved chord withholds both together before any component receives input.

Bracketed paste emits one validated UTF-8 payload and preserves exact bytes,
including NUL, line endings and escape bytes within the payload. A length
limit applies to the payload alone. An oversized or invalid paste discards
the complete paste through the closing marker. Allocation failures report
the accepted prefix and leave the next byte pending for retry. A CSI or SS3
body is bounded to 64 bytes. An unsupported or overlong sequence discards
through its final byte or resumes at a new Escape. Unsupported control
strings discard through their terminator.

SGR mouse reports use pointer ID zero and convert one-based cell coordinates
to zero-based signed cells. The decoder tracks pressed buttons across
press, motion and release. Wheel steps use signed cell deltas. Positive x
means right and positive y means down. The SGR Meta/Alt bit maps to Alt.
Focus-out produces a host cancellation and clears button state. An outstanding
cursor query survives an in-band focus change. Explicit decoder cancellation
also clears the cursor reply expectation. A supported character-size report
yields a resize. Hosts can also construct resize,
pointer cancellation or other normalized records directly and check them
with `nt_event_validate`. Terminal modes and event routing are separate
host and tree operations.

The [installed input consumer](../tests/consumer/input.c) checks stream splits,
timestamps, pointer transitions, malformed and out-of-range records, and
paste allocation retry. Encoding references include
[XTerm control sequences](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html)
and the [Kitty keyboard protocol](https://sw.kovidgoyal.net/kitty/keyboard-protocol/).

## Focus and input routing

`nt_tree_dispatch` accepts a normalized event and the optional associated
UTF-8 text of a key press or repeat. The caller can supply exact reserved
key and modifier pairs. A reserved key and its associated text are withheld
before callbacks and default actions. An ordinary key's associated text
goes to the current focus after a successful unconsumed key route. Text,
paste and the key events themselves remain distinct callbacks.

Pointer and wheel coordinates refer to the frame. A callback can query a
component-local position when the input has a position and the translated
coordinates fit. Each route uses the committed ancestor path and calls
preview, target and bubble in that order. Consumption stops the route
and its default action. Only a target can request focus or capture.
Requests commit after a successful route. Earlier successful callback
intents and invalidations stay committed if a later callback fails.
External resize events address the published root. The caller supplies
the new frame size on publication. Focus gain and loss are tree
notifications.

Use `nt_tree_focus_advance` for forward or reverse traversal, or enable
the Tab and Shift+Tab default in the dispatch policy. Traversal follows
published insertion preorder and wraps among eligible mounts. A release
does not traverse. Programmatic focus and both traversal paths require
visible committed geometry.

The tree admits consistent pointer button transitions before opening a
route. Captured gestures continue outside the owner's visible geometry.
Capture ends on the final release. If a release skips the owner or its
callback fails, capture ends immediately even when other buttons remain
pressed. Loss suppresses further input from that gesture until all its
buttons release.
A pointer cancellation clears its own gesture. Host cancellation clears
all gestures and focus. On removal, visibility loss or host cancellation,
all affected input state clears before notifications. The tree delivers
pointer cancellations in ID order, followed by any focus loss.

Invalid input and failure to allocate a new gesture leave the dispatch
report unchanged. Once a pointer transition is admitted, physical button
state advances even when delivery fails. The report then identifies the
selected target and whether the route consumed the event. It reports
associated text as delivered when the target's text callback succeeds.
The [installed routing consumer](../tests/consumer/routing.c) checks these
transitions across layout rollback, reparenting, and removal.
The [component protocol](COMPONENT-PROTOCOL.md#input-focus-and-capture)
describes the callback and ownership semantics.

## Terminal capability report and output fallback

`nt_posix_terminal_probe` uses descriptors to report whether the input and
output are tty endpoints and whether they form a usable adapter pair. Pass
`-1` for absent input. A redirected input can still use the interactive
frame sink on tty output. A redirected output always selects plain rows
and monochrome. The query borrows descriptors for the call and changes no
settings. Null output pointers and invalid descriptor arguments return
`INVALID_ARGUMENT`. A descriptor that cannot be inspected returns
`TRANSPORT_FAILED`. In either case the report stays unchanged.

The report suggests capabilities from the caller's `NtPosixTerminalHints`.
With a null hints pointer, use `TERM`, `COLORTERM`, the first nonempty of
`LC_ALL`, `LC_CTYPE` and `LANG`, and the presence of a nonempty `NO_COLOR`.
An explicit hints record replaces these defaults. The query reads the
environment without changing process locale. Keep the environment stable
during such a call.

Recognized ANSI terminal families enable interactive output on a writable
tty. The suggestions recognize xterm, tmux, screen, rxvt, alacritty, foot,
kitty, wezterm, konsole, st and putty for extended graphics and input, and
`ansi`, `vt100`, `vt102`, `vt220`, `linux` and `cygwin` for basic output.
Each family name also matches a suffix beginning with a hyphen. Missing,
`dumb` and unrecognized names select plain output. Interactive output
selects truecolor for `COLORTERM=truecolor`, `COLORTERM=24bit` or an advertised
`-direct` or `-truecolor` suffix, indexed color for `-256color`, and
monochrome otherwise. A nonempty `NO_COLOR` selects monochrome and retains
cursor movement in interactive mode. [NO_COLOR convention](https://no-color.org/)

An advertised UTF-8 codeset leaves Unicode available. Locale modifiers after
`@` do not affect the codeset. Plain output preserves
UTF-8 graphics when the locale permits them. In interactive mode, the extended
families suggest braille and half-blocks with UTF-8 and suggest pointer and
paste modes for a usable tty pair. Basic terminals use cell-width replacements
for those graphics. Width defaults to one cell for ambiguous characters and
two for emoji. These are local suggestions. The application can override them
with its own negotiation, font limits or user preferences. The caller sets
the `NtMotionPolicy` independently and passes it to the tree or POSIX host.

When `host_available` is true, pass the selected capabilities to
`nt_posix_host_open`. Otherwise a host that owns the output can use
`NtAnsiPresenter` directly on a finished frame. For indexed output,
supply an opaque palette in index order. An application using the
reference palette can fill it with `nt_xterm256_palette` before opening
the host or presenting its first frame. [XTerm color resources](https://invisible-island.net/xterm/manpage/xterm.html)

The [installed profile consumer](../tests/consumer/terminal_profile.c) uses
the same tree in full and reduced motion, through tty, output-only tty and
pipe paths. Its [driver](../tests/consumer/terminal_profile.py) checks the
reported settings and the raw and rendered output for each mode.

## POSIX terminal host

Link `Nocturne::posix_host` and include `<nocturne/posix_host.h>` for the
terminal adapter. The caller lends a live tree, input and output descriptors
for the same tty device, and any indexed palette storage until close. The
adapter copies the options and decoder configuration. The caller serializes
adapter operations, reserves the tty for this adapter, and keeps the descriptors
open throughout the session. Use a foreground tty or the slave side of a
privately driven PTY. A `/dev/tty` descriptor and a direct descriptor to
that controlling tty can be used together.
Open accepts one live adapter per process. A second open returns `WRONG_PHASE`.
Noninteractive capabilities, pipes, PTY masters and mismatched devices
return `UNSUPPORTED_CAPABILITY` before ownership changes.

Open uses raw input and temporarily sets `O_NONBLOCK` on the output open-file
description. Aliases of that description share the flag. The application
supplies its terminal capabilities, including independent motion and input
settings. Pointer reporting enables button tracking with SGR coordinates.
Bracketed paste and focus reports use their respective terminal modes.
Alternate screen and cursor hiding are opt-in. For each requested mode, the
application starts from its normal terminal setting. The keyboard protocol
option tells the decoder which reports to interpret. The adapter sends no
keyboard negotiation or cursor-position queries.

Pass nondecreasing monotonic microseconds to `nt_posix_host_open` and
`nt_posix_host_step`. Each step updates the tree with the selected motion
policy, reads the tty's current cell size, and publishes a new size when it
changes. The first step establishes the size. Resize callbacks address the
newly committed root. A size-report sequence from the input stream is ignored
because the kernel tty size is authoritative. Each step checks for input
without waiting and reads at most 4,096 bytes. Fragmented input can span steps.
Call `nt_posix_host_input_deadline` to schedule lone Escape handling even if no
new bytes arrive. Tree updates use `nt_tree_next_update`. The caller chooses
when to step, including when watching descriptors or window-size notifications.

The adapter dispatches normalized events, observes pointer and paste
capabilities, and preserves the tree's exact shortcut reservation. An optional
reserved-key callback receives the key and its associated text for the call.
The reserved key and its text stay out of the component route. The callback
can apply application policy via the public tree API between event routes.
The optional `NtPosixHostOptions.after_input` callback runs after each
successful decoded dispatch and any reserved-key handler, before the next
input event or presentation. Its dispatch report is borrowed for the call.
The host borrows `input_context` for the session. Use the callback to drain
intents and apply actions through the public tree before later events can
change the state they depend on. For example, submit and clear an editor's
draft before another key in the same read inserts text. A callback error
stops the batch and closes the adapter through the ordinary failure path.
The triggering event counts in the step report. Filtered input, kernel
resize and cleanup notifications skip this callback.

Use `nt_posix_host_set_after_resize` to apply application policy to the
first published size and later size changes. Its callback receives the
committed cell size after the root's resize event and before terminal input
is read. Aurora Survey uses it to restore the editor or graph focus in
time for input in the same step. The setter requires an open, idle host.
The caller lends its context until replacement, clearing with a null
callback or close. A callback error stops the step before reading input
and follows the ordinary host cleanup path.

Keep host lifecycle calls outside the callbacks. The application can also
mount, invalidate and change focus between steps. Publishing a dirty tree
and presenting the resulting frame occur in the same step.
Output writes are synchronous and may wait up to the configured timeout per
blocked write. If output fails, some bytes may already have reached the tty.

`NtPosixStep` reports the latest committed size, whether the step published a
new size and presented it, and how many decoded events completed tree dispatch.
A reserved event counts when the tree accepts it, even if its application
callback fails. Null arguments, time reversal and calls on a closed or busy
adapter leave the report untouched. Once admitted, a step that fails closes the
adapter and reports any earlier progress and the cleanup result. On ordinary
failure the returned result is the processing failure. When the step observes
a managed signal, it returns the cleanup result and reports the signal number.

Close is idempotent. It attempts to reverse the modes in reverse acquisition
order, restore the saved tty settings and output status flags, and deliver
normalized host cancellation to clear focus and pointer gestures. Destroy
also closes and frees the adapter. A transport failure during restoration
returns `TRANSPORT_FAILED`. For example, a disconnected tty may reject the
mode reversals and termios calls. The adapter still attempts every restoration
operation. The kernel can reprocess pending typeahead when restoring canonical
input. The adapter preserves the queue for its next owner.

With `catch_signals`, open saves and replaces the process dispositions for
`SIGINT`, `SIGTERM`, `SIGHUP` and `SIGQUIT`. The handler only records a signal.
The next step observing it closes the adapter and restores the saved
dispositions. Drive the step after an interrupted caller wait. The caller
chooses any subsequent exit code or signal handling. If the caller manages
signals itself, leave `catch_signals` off and close explicitly. Close before
handing over the tty or suspending the application. Signal cleanup depends on
the application running another step. Changes to the terminal that cannot be
restored through unavailable I/O are reported as transport errors.

The [PTY consumer](../tests/consumer/posix_host.c) and its
[driver](../tests/consumer/posix_host.py) exercise the public API through
real terminal input and output. Other hosts can use the core decoder,
event, tree and frame-sink APIs directly.

## Source references for the implemented slice

- `native/src/math.c:nrEase/nrTrack` identifies easing, explicit time and scalar tracks.
- `bindings/chromatic/chromatic-motion.js` identifies independent channels,
  keyframes and repeat policies.
- `native/src/path.c:nrPathSample` identifies arc-length movement, wrap/reverse,
  and position/tangent results; `nrPathActorsTick` demonstrates the old
  camera/emitter/window/node restriction that the new sampler does not require.
- The new playback API makes pause, seek and cancellation explicit for consumers.

Implementation here is new C code informed by those capabilities. The old global
state, JSON command dispatcher, numeric screen IDs and source-profile paint
branches are not linked into it.

## Graph layout and mounted graph view

Include `<nocturne/graph.h>` and link `Nocturne::graph`. Installed CMake
projects can declare the dependency with `find_package(Nocturne REQUIRED
COMPONENTS graph)`. Source builds can omit the extension by configuring
`NOCTURNE_BUILD_GRAPH=OFF`.

`nt_graph_layout_create` copies a consumer's nodes and edges into an owned
snapshot. IDs are nonzero and unique within each of the two namespaces.
An edge keeps its ID, source, target and direction, including parallel
edges and self-loops. Dangling endpoints and duplicate IDs are invalid.
Output records are sorted by ID. Inputs with the same IDs and attributes
yield the same positions within a build regardless of input order.

- Layered layout ranks directed edges left to right with longer paths
  advancing the rank. Undirected edges do not constrain ranks. If a cycle
  leaves no ready node, the lowest remaining ID starts the next rank
  calculation and incoming links from unplaced nodes become back edges.
  In each column, nodes follow increasing ID from top to bottom.
- Radial layout starts at the top and spaces IDs clockwise around the
  center. A single unpinned node is centered.
- Force-directed layout starts from radial positions and takes a fixed
  number of bounded steps with pair repulsion, edge attraction and a
  centering force. Parallel edges each contribute attraction. Direction
  does not change the attraction. The result is a bounded approximation
  and does not guarantee convergence or separation in dense graphs.
- Pinned nodes keep their exact normalized positions in all three
  layouts. A pin must be finite and within the inclusive `[0,1]` range
  on both axes. The tree and layout require caller serialization for
  mutation and destruction.

The extension accepts up to 512 nodes, 4,096 edges and 64 force steps.
The default force run takes 24 steps. The force algorithm takes
`O(steps × (nodes² + edges))` time. Submit a bounded working set for a
larger dataset using application policy such as filtering, paging through
neighborhoods or displaying groups as nodes. Capacity failures and
allocation failures preserve the prior output. The shared integer line
raster used by the view and canvas preserves placement under clipping.

`nt_graph_view_mount` creates a leaf compatible with panels, layouts and
viewports. Start with `nt_graph_view_options_default()` and set the data,
labels and optional colors. Counted UTF-8 labels are keyed by node ID.
Each label is at most 256 bytes, and the SDK lays out at most 24 columns
on one row. Invalid UTF-8 or duplicate and unknown label keys reject the
whole configuration. The caller may release all input arrays and label
bytes on return. Layout positions are mapped to the assigned rectangle
with x increasing right and y increasing down. Either extent can be
zero. Extents above `INT32_MAX + 1` cannot be represented by the view's
signed local cell coordinates.

The view paints one-cell edges before markers and labels. An available
edge cell next to a destination carries a direction arrow. A marker,
label or later edge can cover that cell. A self-loop uses an adjacent
turn where space permits, with the arrow pointing back to its marker
for a directed loop. At a shared edge cell or shared marker cell,
the higher ID wins. Labels start two columns right of their marker and
are considered in increasing ID order. A label can cover an edge and
skips a whole cluster if it would cover a marker, a preceding label or
a clipped cell. The view allocates its cell map only for the intersected
clip. Edges visit the visible major-axis interval rather than walking
the entire assigned extent.

`nt_graph_view_hit` takes frame coordinates and resolves colored glyph
cells that survive clipping and still belong to the graph in the last
published frame. The hit is a kind and stable ID. Both halves of a wide
label map to its node. No hit is `{NT_GRAPH_HIT_NONE, 0}`. A successful
set can change the input data before publication. The last frame and hit
map stay coherent until the next successful publication, so a hit may
still describe a removed edge during that interval. Dispatch ignores
such an edge when it is no longer in the current configuration.

Primary pointer down on a visible item selects it. This also requests
focus if the mount is focusable. On a focused graph, unmodified arrows
cycle through visible markers by ID with wraparound. A changed user
selection emits `NT_INTENT_GRAPH_SELECTION` with typed kind and ID
fields. Decode it with `nt_graph_event_decode`. The selection getter
reflects current state. The silent setter accepts a live node or edge
ID, or the no-hit value to clear selection. Data replacement retains the
selected kind and ID when it still exists. Removal invalidates the graph
mount handle. The [station map](../tests/consumer/graph_view.c) and
[publication workflow](../tests/consumer/graph_workflow.c) demonstrate
pointer and keyboard use from installed SDKs.

Go consumers with the `nocturne_graph` build tag can copy a
`Graph.LayoutSnapshot` or use `MountGraphView` and `SetGraphView` for
authored labels, widths and colors. The `MountGraph` and `SetGraph`
shortcuts apply default view styling. TypeScript uses `sdk.layoutGraph`
for a snapshot and accepts the optional view settings through
`tree.mountGraph` and `tree.setGraph`. A WASI package without the graph
module reports `UNSUPPORTED_CAPABILITY` from these operations.

Both bindings copy layout records in ID order. Query the published hit
map with Go `GraphHitAt` or TypeScript `graphHitAt` in frame coordinates.
Use `GraphSelection` or `graphSelection` for the current selection and
`SelectGraph` or `selectGraph` for a silent change. User changes emit a
selection intent that Go `DecodeGraphIntent` or TypeScript
`sdk.decodeGraphIntent` can read. Leave the mount hit-testable for pointer
selection. Node and edge IDs use `uint64` in Go and `bigint` in TypeScript.
Both bindings may report an oversized label before another invalid graph argument.


## Window workspace and panel content providers

Include `<nocturne/window.h>` and link the portable `Nocturne::nocturne`
target. A workspace accepts up to 64 direct window mounts. Each window owns
a copy of its panel title and optional mask. It has one body child. Mount a
container as that child to compose several controls, a document or a graph.
Removing the workspace removes its windows and their bodies in postorder.

A window's normal rectangle uses signed workspace-local cell coordinates
and nonzero extents up to `INT32_MAX + 1`. Dock left, right, top or bottom
with an authored positive extent. In workspace insertion order, each normal
dock consumes a strip, clamped to the available size. Other windows use the
remaining region. A maximized window fills it. A floating window keeps its
full rectangle, then clips to that region. Fully offscreen windows are
unplaced. Minimizing a window unplaces its subtree and preserves its body,
normal rectangle and dock settings. Its descendant updates keep their normal
caller-driven schedule until removal. Use `nt_window_set` to restore normal
placement and `nt_window_set_panel` to replace the chrome and body mask
without remounting the window or child.

`nt_window_get` returns requested placement and last published geometry.
Painting, including caller-owned content, must succeed before a new geometry
and hit snapshot is published. Raise a visible window by calling
`nt_window_focus` with its handle, optionally naming a focusable descendant.
Pointer down also raises a window in the committed route. A primary press
on a draggable, framed floating title starts a move. The lower-right corner
handles resize when enabled. The gesture begins from committed geometry,
captures that pointer and uses frame-coordinate deltas until release or
cancellation. Signed coordinates and extents are clamped to their representable
ranges. A placement setter supersedes an active drag and cancels rectangle
animation. Removal or publication of minimized geometry clears ineligible
focus and capture through the standard tree lifecycle. Choose a successor
with the same focus API or ordinary focus traversal.

`nt_window_animate` runs from the current requested rectangle to a new
rectangle at an explicit tree time. A future start schedules the first
update. Linear motion rounds the exact integer-clock interpolation to the
nearest cell with ties away from zero. Other easing curves use the SDK's
binary64 easing sample before rounding to cells. Full motion asks for another tick
within 16 milliseconds or at the endpoint. At or after the start, reduced
motion selects the endpoint immediately. A zero duration also selects the
endpoint at the start. Only normal floating windows can animate. Starting a
chrome gesture interrupts animation from the published rectangle. A new
animation may retarget a running one from its latest requested rectangle.
Application time and the frame publication loop remain explicit.

`nt_panel_content_mount` adapts a caller's paint, optional update and optional
event callbacks to a leaf with a preferred size. Use it as a panel body or a
child of an ordinary layout. Its cookie is borrowed for the life of the mount
when no disposer is supplied. A successful owning mount calls the disposer
once at removal. A failed mount leaves the cookie with the caller. The
provider draws in body-local cells through the supplied paint context and
inherits clipping, mask and tree lifetime. A paint error preserves the prior
published frame. Use the update reply to schedule follow-up work and
`nt_tree_invalidate` for application-driven changes outside callbacks.
Go and TypeScript expose this leaf through `MountPanelContent` and
`tree.mountPanelContent`. Give the provider a preferred size and a paint
callback. Update, event and dispose callbacks are optional. Both
bindings retain the callbacks on success, release them on removal,
and report callback failures through the originating tree operation.
The window APIs `SetWindowPanel` and `tree.setWindowPanel` replace
chrome and padding while retaining the body. Call `FocusWindow` or
`tree.focusWindow` to raise a published window and focus a descendant.
Use the zero Go node or omit the TypeScript target to focus the window
itself.

See the [window consumer](../tests/consumer/window.c).

## Notifications and persistent status cards

Include `<nocturne/notification.h>` and link `Nocturne::nocturne`. A notification
is an ordinary leaf. It owns the authored UTF-8 title, wrapped message,
action labels and optional dismiss label. Use
`nt_notification_options_default()` and set a nonzero notification ID.
Action IDs must be nonzero and unique within the card. A card can display up
to eight actions and eight message rows. A persistent status can omit both
the actions and dismiss control and use a zero expiry. The title, when
present, takes one row ahead of the configured message rows. Each action and
the optional dismiss control take one row after the message. Smaller layouts
clip these rows in order.

A primary click on an enabled action or the dismiss row captures that
pointer. Activation requires release on the same row of the same published
configuration. Movement and resize can reposition the row during the gesture.
On a focused card, Up and Down cycle through enabled action rows in its
published clip. Enter or Space activates the current visible action, or the
first enabled visible action if no selection exists. Escape invokes a
dismissible card's dismiss action. Keyboard and pointer actions emit
`NT_INTENT_NOTIFICATION` on the standard typed intent queue with event kind,
notification ID and action ID. A dismissal event uses action ID zero.
Applications handle domain actions outside dispatch. With
`dismiss_on_action`, a successful action also dismisses the card.

Expiry is an absolute tree-clock deadline. The card expires at or after it
when updated or when it receives an event. It becomes silent immediately,
and the next successful publication hides it and prunes ineligible focus
and capture. `nt_notification_dismiss` changes state without emitting a user
intent. Removing the mount releases its text and disables its pending updates.
`nt_notification_set` atomically replaces its contents, resets the selection
and recalculates expiry. A prior published configuration stays visible until
publication succeeds. Its action rows do not activate the replacement.
Use `nt_notification_get` for requested state. See the [installed contract
consumer](../tests/consumer/notification.c) and the [live terminal
consumer](../tests/consumer/window_terminal.py).

## Seeded particle emission, motion and trails

Include `<nocturne/particle.h>` and link `Nocturne::nocturne`.
`nt_particle_emitter_create` copies an emitter configuration into an
immutable object. The caller supplies the seed, stream, initial birth time,
batch size, period and optional total birth limit. A zero period produces
one batch at the start. A zero limit permits all representable scheduled
births. A nonzero limit can end partway through a batch. Each ID is a
zero-based birth ordinal. Birth times and retirement use integer
microseconds. A particle is active at birth and inactive at its exact
lifetime endpoint, including when an endpoint exceeds the representable
clock and needs to be compared by age.

The two axes of spawn position and initial velocity sample from authored
finite ranges. A fixed range takes the authored endpoint. The seed,
stream, ID and property identify the draws for position, velocity and
lifetime. Lifetime is a positive integer in the inclusive configured
range. The SDK samples position as initial position plus velocity times
age plus half the constant acceleration times age squared. It reports
velocity at the same age. Coordinates use caller units, speed uses units
per second and acceleration uses units per second squared. Motion uses
the exact age in seconds given by integer microseconds divided by one million,
then rounds the final position and speed to nearest binary64. A position
or speed outside the finite binary64 range returns `OUT_OF_RANGE`.
Validation, sampling and the particle-specific alpha and cell calculations
preserve the caller's floating-point environment. A failure to establish
that environment returns `UNSUPPORTED_CAPABILITY`. Once a cell is prepared,
the surface glyph operation inherits its usual compositing and allocator
behavior, including their effects on floating-point flags. Birth
and end colors mix through a caller-selected easing curve in
premultiplied linear sRGB. The sample reports straight RGBA8. Progress
reserves one for the endpoint and preserves the step-end boundary.

`nt_particle_emitter_sample` visits at most the requested capacity of
newest scheduled births and returns the live subset in increasing ID
order. Both capacity and batch size are limited to 4,096. New births
displace the oldest IDs from the presentation budget. When a newer
particle expires, a displaced ID stays out of the budget. No background
simulation or accumulated work follows a dropped birth. Sampling directly
at a time, moving backward to revisit it, or sampling intervening frames
has the same result under the same numeric environment. Query the count
with a null buffer and zero capacity. An insufficient buffer reports the
necessary count and leaves its elements unchanged. A numeric failure
leaves the outputs unchanged.

`nt_particle_emitter_particle` provides a trajectory sample for an authored
ID independently of presentation. It can inspect an ID displaced from
the budget. A valid call with an unborn, expired or unscheduled ID
reports `alive=false` and leaves the sample untouched.
`nt_particle_emitter_trail` reconstructs at most 16 points behind a live
head at fixed time spacing, oldest first. It excludes the head and
returns only the requested points at or after birth. Both the head and
the trail use explicit query time. No history buffer accumulates when
the application skips a frame or removes a mount.

Draw from a custom component with `nt_particle_emitter_draw` or use
`nt_particle_view_mount` as a leaf in an ordinary layout. Drawing floors
the exact local position before clipping and takes the
surface's normal masks and compositing rules. Choose a one-cell
grapheme, such as a Unicode dot or ASCII period. The mounted view owns
the glyph bytes and emitter settings. For each retained particle the
painter visits its trail from oldest to newest with attenuated opacity
and paints the current head last. It visits the particle IDs in increasing
order. A zero trail opacity suppresses trail painting. A paint error
returned from the component callback preserves the prior published frame.

The leaf samples at explicit tree updates, starting from the tree's
current clock on mount. Its deadline is the earliest representable birth,
expiry or full-motion sample. A zero frame interval selects 16 ms.
Reduced motion takes effect on a due update, preserves birth and expiry,
and paints stationary birth positions and colors without trails. With
no scheduled births or active particles, the view leaves no update
deadline. During a long idle interval it samples the new clock directly
and never catches up by replaying missed births. Set validates a complete
replacement before altering the view. Removing the view drops its
deadline and owned configuration. The [installed consumer](../tests/consumer/particles.c)
covers exact boundaries, time-order independence, resource refusal,
reduced motion and removal.

### Deposits, piles and re-emission

Include `<nocturne/particle_deposits.h>`. The caller supplies the exact
contact time, local cell, positive mass and color. A contact can come
from a sampled emitter, a surface intersection or another application
event. The caller decides when an originating particle stops drawing.
`nt_particle_deposits_create` copies its options and reserves one block
for up to 256 contacts. The store starts at clock zero. The caller
serializes access and owns destruction. Add, advance, sample and deadline
operations allocate nothing. Drawing uses normal surface allocations.

`nt_particle_deposits_add` assigns a monotonically increasing ID from
zero. The ID output is optional. Add and advance accept equal or later
times. Each accepted mutation advances the store clock and reclaims
contacts that have completed. A full store rejects a new contact with
`OUT_OF_RANGE` unless a slot can be reclaimed at its contact time.
Rejection preserves the existing entries, clock and next ID, and leaves
the output untouched. IDs stay unique until the representable ID budget
is exhausted. A mass of zero or a time earlier than the stored clock
returns `INVALID_ARGUMENT`.

A contact contributes to its cell from birth through its half-open
hold interval. `nt_particle_deposits_piles` combines each occupied cell
into a contact count, exact integer mass, and independent mass-weighted
RGBA8 channel means with half ties rounded up. Piles follow the oldest
currently contributing contact in each cell. Sample calls borrow data
at any time at or after the committed clock and preserve the state.
Queries at an identical time agree regardless of intervening queries.
Between calls to add, advancing the clock more often gives the same
subsequent results as a single advance to the same time.

At the hold endpoint the deposit leaves its pile and can produce a
one-shot cohort of up to eight particles. Each particle starts at the
cell center with the original deposit's color. The store seed, stream
and assigned deposit ID select a deterministic release stream. The
emitter applies authored velocity, acceleration, lifetime and color
easing. Each result identifies the deposit and cohort particle ID.
Contacts reserve their slots until their last released particle's
exclusive expiry. A zero release count expires the contact at hold
end and ignores the remaining release options. An endpoint outside
the caller clock schedules no deadline. The count-query, short-buffer,
and numeric error rules match the standalone emitter.

`nt_particle_deposits_deadline` returns the earliest representable
hold endpoint, release expiry or full-motion sample time. The standard
emitter frame interval applies to active releases. Reduced motion
keeps hold and release deadlines and paints released particles at the
cell center without trails. `nt_particle_deposits_draw` paints held
piles first and releases in deposit and particle order using the
ordinary clip, glyph and compositing rules. It borrows appearance only
for the call. A custom component can own the store and use its update
and dispose callbacks to manage deadlines and release all storage.
The [standalone contract test](../tests/consumer/particle_deposits.c) and
[terminal consumer](../tests/consumer/visual_terminal.c) show both forms.

## Visual particle recipes and branches

Include `<nocturne/particle_effect.h>` and link `Nocturne::nocturne`.
`nt_particle_effect_recipe` writes editable settings for a radial burst,
nova, glitter, rain, snow, embers, leaves, sparks or a firework. The
recipe is pure. Its starting time and seed default to zero and its
placement is authored for a 32-by-9 view. Set the start time, seed, spawn
range, velocities, capacity, trail and glyph to fit the consumer. Recipe
glyphs view immutable static bytes. A mounted effect copies its options
and glyph. A standalone effect copies its emitter and motion settings.
Use the existing emitter operations for standalone sampling, trails,
deadlines and painting. Both mounted setters address the same leaf type.
`nt_particle_view_set` installs the ordinary motion law, and
`nt_particle_effect_set` installs the supplied effect law.

In Go, call `ParticleEffectRecipe` for independent settings and edit
the returned `ParticleEffect` fields. Use `NewParticleEffectEmitter` for a
standalone schedule, or `MountAuthoredParticleEffect` and
`SetAuthoredParticleEffect` for a leaf. In TypeScript, use
`sdk.particleEffectRecipe` and object spreads to author settings, then
`sdk.createParticleEffectEmitter` or the tree's `mountParticleEffect`
and `setParticleEffect` methods. When editing a nested TypeScript field,
spread its existing object before changing the field. Omitted fields
in a replacement object use the generic particle defaults.
Both languages also accept the recipe shortcut for mounted effects. Standalone construction reads only the
emitter and motion settings. Times, seeds and streams use Go `uint64`
and TypeScript `bigint`.

In radial mode the initial velocity uses an angle on a seeded arc and an
independent seeded speed between the authored nonnegative limits. A
direction in `[0,1)` turns starts from the positive x axis toward the
positive y axis. A sweep in `[0,1]` travels forward from the direction,
wrapping after one turn. Zero sweep fixes the direction. The reduced
angle is converted to radians before binary64 rounding. The supplied
Cartesian range remains subject to emitter validation. Spawn ranges,
constant acceleration, admission, lifetimes and colors use the base emitter.

A nonzero sway period adds a sinusoidal displacement to both axes,
scaled by the authored signed amplitudes. Each birth has a fixed seeded
phase. The displacement subtracts the value at birth, so the spawn
position stays exact, and the reported velocity includes the derivative.
The period uses the signed integer age remainder. Argument reduction
preserves the small phase change at either end of a long cycle.
Trigonometric coefficients are binary64 approximations within the
platform's math library. The analytic ballistic and sway
contributions share an exact sum before rounding position and speed.
The derivative retains the integer period through the range check. Numeric
overflow and buffer errors follow the base emitter rules.

A nonzero glitter period selects a seeded opacity multiplier per birth
and per whole elapsed glitter interval. The multiplier runs from
`glitter_min_alpha` to 255, scales the eased particle alpha, and is held
through that interval. Full-motion deadlines include the next glitter
boundary, including when it precedes the configured frame interval.
Reduced motion draws the spawn pose and first opacity sample, with no
sway, travel or trails. Birth and expiry remain scheduled. The mounted
view inherits the base leaf's update, publication and cleanup rules.

`nt_visual_branch_sample` returns a trunk followed by a bounded number
of attached branch paths. All coordinates and topology are in a local
cell grid. Each path interpolates integer vertices with half steps
rounding in its direction of travel. The seed and stream select interior
jitter along the trunk or branch's minor axis. Each side path attaches
at a seeded interior trunk vertex and extends to a seeded side by the
configured reach, moving forward along the trunk's major axis by half
that reach rounded down. A branch may share an attachment with another
branch. Branches need at least two trunk segments and a positive number
of segments and reach. The first segment has parent ID `UINT32_MAX`.
Every later segment starts at the end of its earlier parent. Segment IDs
are contiguous from zero and stable across queries. The total is limited
to 256. A visible coordinate outside the signed cell grid returns
`OUT_OF_RANGE` with neither output modified. The count-query and
insufficient-buffer rules follow the particle sampler. The API borrows
the options during each call and retains no geometry or schedule.

The branch starts at `start_us` and expires at the end of its positive
lifetime. In full motion the first segment appears at birth and one
more appears every `reveal_us`, with a zero interval showing all at
birth. The trunk is shown first, followed by each path in order. Full
motion fades alpha linearly over the lifetime, with half ties rounded
up. A configured flicker interval multiplies the fading alpha by a
seeded per-segment value. Reduced motion reveals the complete geometry
at birth with the authored alpha and keeps the same expiry. Deadlines
consider birth, expiry, reveal, flicker and full-motion frame interval,
using 16 ms for a zero frame interval. Deadlines fit the caller clock.

`nt_visual_branch_draw` borrows a one-cell glyph for the call and rasterizes
the sampled segments through the supplied paint context. It clips its raster work to
the local view and lets the surface apply masks and normal composition.
A visible parent owns its shared endpoint with its child. Other path
crossings follow trunk-first paint order. An application can combine a
branch with other leaves by drawing in a custom component and using
`nt_visual_branch_deadline` in its update callback. See the
[installed particle effect consumer](../tests/consumer/particle_effects.c).

## Optical fields, ripple trains and analytic waves

Include `<nocturne/field.h>` and link `Nocturne::nocturne`.
`nt_field_sample` is an allocation-free, stateless query of a point,
caller clock and motion policy. The descriptor is borrowed for each call.
A ripple returns its strength in `[0,1]` as both value and coverage.
An active wave returns a signed value in `[-1,1]` and coverage of one.
Before a field begins and after all ripple rings expire, coverage is
zero. Applications can use the scalar to drive component motion,
sample a palette, or build another field.

Call `nt_optical_falloff` with a finite nonnegative normalized distance.
Linear falloff yields `max(0, 1-d)`. Gaussian falloff yields
`exp(-d*d/2)`. Inverse-quadratic falloff yields `1/(1+d*d)`.
All three return one at zero. The Gaussian and inverse-quadratic tails
can underflow to zero at wide distances. The profiles are also used
inside ripple fields.

A ripple has from one to 32 rings. The first begins at `start_us` and
successive rings begin at `spacing_us` intervals. Zero spacing permits
simultaneous rings. Births beyond the caller clock are omitted. Each
ring starts at `first_radius`, moves linearly to `last_radius` and expires
at its positive `lifetime_us` endpoint. Both radii are finite and
nonnegative and can be equal or decrease. The point in aspect-adjusted
space is `(position - origin) * aspect` with a positive finite multiplier
on each axis. The radial band samples the chosen optical profile at
`abs(distance - radius) / width` with a positive finite width. Each ring
multiplies that response by `peak` and the easing of its remaining
lifetime fraction. Overlapping rings use the maximum strength. The ring
compares squared radii and linear band edges before rounding so that
narrow edges remain distinct even when the radius itself would round to
the sample distance. Reduced motion retains birth and expiry and samples
each active ring at its birth radius and opacity.
The inverse-quadratic profile applies peak and fade before rounding
and resolves rounding boundaries against the squared geometry.
Sine and ease-out fades preserve positive strength
at the last active tick when the remaining fraction is very small.

A wave begins at `start_us` and samples the sine of the specified phase
in turns plus the dot product of `(position - origin)` and
`cycles_per_unit`, minus elapsed turns from the positive period. A zero
period holds the authored time phase. Reduced motion similarly holds
the initial time phase while preserving spatial variation and the start
clock. Spatial products and clock phases reduce around a quadrant before
rounding the angle to radians. Huge whole turns remain periodic and do
not erase small fractional terms. Trigonometric functions use the
platform's binary64 math library.

`nt_field_color` samples an optional borrowed palette ramp at the ripple
strength or at `(wave_value + 1) / 2`. A null ramp selects the authored
solid RGBA. Coverage and the caller's opacity in `[0,1]` multiply the
straight alpha before final RGBA8 conversion. `nt_field_draw` fills local
cell backgrounds, samples each visible cell at its center, and skips
fully transparent samples. Zero coverage or zero ink opacity returns
transparent black after palette descriptor validation. Samples with
visible coverage apply the ramp's authored gamut policy. Drawing
intersects bounds with the component clip and visits cells in row order.
A per-cell error, such as a gamut rejection, leaves the earlier field
cells painted if the callback handles the error and returns success.
Normal surface masks and compositing apply, and the glyph layer remains
independent. A failed paint callback preserves the prior published tree frame.

Sampling, color conversion and validation use the standard floating-point
environment and restore the caller's environment before returning or
invoking surface paint. As with other paint operations, the surface and
its allocator retain their normal floating-point behavior. Result-pointer
operations leave their outputs untouched on error. Bad descriptors,
motions and nonfinite coordinates return `INVALID_ARGUMENT`. A ring
count above the supported cap returns `OUT_OF_RANGE`.

`nt_field_deadline` returns the next representable birth, expiry, or
full-motion sampling deadline. Zero frame interval selects 16 ms.
A periodic wave continues sampling in full motion, while a static wave
has only a possible start deadline. Reduced motion uses activation and
expiry only. The caller owns all scheduling, paint state and layering.
An active field reports an unscheduled deadline when its next time lies
beyond the unsigned clock range.
See the [installed field consumer](../tests/consumer/fields.c) for a
palette sampled into a clipped background behind a glyph.

## Spatial transforms, paths and cameras

Include `<nocturne/spatial.h>` and link `Nocturne::spatial`. World and view
coordinates use right-handed Cartesian axes. Points and directions use
`NtVec3`, with application-defined distance units. Angles are radians and
positive rotations follow the right-hand rule. Front-facing view points
have negative view Z. The projection and picking operations in the
following sections consume those view coordinates.

Vector operations accept finite values and return the result through a
caller-owned output. The zero vector has length zero and has no unit direction.
Transform matrices store three row-major linear rows and a translation.
They multiply column vectors. Applying a transform to a direction uses only
the linear rows. `nt_spatial_transform_create` scales along the local axes,
rotates about the supplied pivot and adds the requested translation. Zero axis
is allowed when the angle is zero. Reflection and zero scale are valid
for transformation. An inverse requires a nonsingular linear matrix.
`nt_spatial_transform_compose(outer, inner, out)` applies the inner transform
first. Transform and camera operations stage their results before writing
the output, so callers can compose, invert or fly in place.

Products in vector dot and cross operations, affine composition,
inversion and world-to-view conversion retain exponent range through
cancellation. Vector length rounds from the exact squared norm. Rotation
coefficients and unit directions use binary64 trigonometric and norm
operations. Allow for the rounding of published values when chaining operations.
The [spatial consumer](../tests/consumer/spatial.c) checks ordinary fixture
components with absolute error at most `2e-13 * max(1, abs(expected))` and
uses exact comparisons for authored powers of two. Callers with
ill-conditioned geometry should control the conditioning and tolerance of
their own round trips.

### Distance-sampled paths

`nt_spatial_path_create` copies the vertices and builds an immutable
piecewise-linear path in one allocator-backed block. An open path needs at
least two vertices. A closed path needs at least three and adds a segment
from the last vertex to the first. Adjacent vertices must differ, including
the closing pair. Each segment adds its Euclidean length to the previous
stored cumulative distance before rounding the sum once to binary64.
Initialization fails with `OUT_OF_RANGE` if segment
length or allocation size exceeds its representable range or a segment
is too short to advance the cumulative distance. Storage is proportional
to the number of segments. Destruction releases it with the same allocator.
Length queries are constant time and distance queries use a binary search.
Neither query allocates or changes the path.

`NT_ONCE` clamps, `NT_LOOP` wraps signed distance, and `NT_PING_PONG`
reflects alternate legs, including at negative distances. At an exact
interior join the tangent belongs to the next segment in the resolved
direction. Ping-pong reverses at the far endpoint and starts forward at the
origin. The final clamped endpoint uses the final segment. An open loop
jumps from its end to its start at an exact period multiple. A negative
offset just before the wrap can round to the final endpoint. Samples report
position, unit tangent in the direction of increasing requested distance,
resolved distance and full length. Position uses the two stored cumulative
distances and the resolved distance without first rounding segment progress
to a binary64 fraction. The caller chooses how time maps to distance.

### Camera poses

`nt_spatial_camera_look_at` builds a camera at an eye position aimed at a
distinct target. The up hint must have a component perpendicular to the
line of sight. The camera's right, up and backward axes form a right-handed
orthonormal frame. `nt_spatial_camera_orbit` places the eye at the supplied
positive radius around a target. Zero azimuth and elevation put the eye on
world +Z. Positive azimuth moves the eye toward +X, positive elevation
raises it, and elevation can include either pole at the binary64 value of
π/2. At a pole, azimuth still controls the camera orientation.

`nt_spatial_camera_fly` moves in the starting camera frame, then rotates
its orientation by `Ry(yaw) * Rx(pitch) * Rz(roll)` expressed in that
frame. Negative local Z moves forward. Supply distances and angles
directly, for example by integrating controller input in the caller's
update. `nt_spatial_camera_to_view` projects the eye-to-point displacement
onto the three camera axes. The view and flight functions validate the
basis lengths, mutual orthogonality and handedness with a tolerance of
64 binary64 epsilons in the dot products.

Invalid arguments, including nonfinite inputs, report
`INVALID_ARGUMENT`. Unrepresentable computed results report
`OUT_OF_RANGE`. Constructors report `ALLOCATION_FAILED` when their
allocator declines storage. Failed operations preserve outputs. Math uses
the standard nearest-even floating-point environment and restores the
caller's environment before returning.

### Projection and clipping

`NtSpatialProjection` specifies the near and far depths and the horizontal
and vertical half extents. The four values are finite and positive, and
far exceeds near. Depth is minus view Z. Perspective half extents are
view-space slopes at depth one. At depth `d` the side limits are
`±half_width*d` and `±half_height*d`. Orthographic limits are the supplied
half extents at every depth. Callers can derive the slopes from field of
view and aspect or author them directly.

`nt_spatial_project` reports visibility in the closed frustum. Visible
points divide their view X and Y by the corresponding half extent at depth
and report that depth in view units. Screen coordinates run from −1 to +1,
increasing right and up. A miss succeeds, sets visibility false and
preserves the projected value. `nt_spatial_unproject` evaluates a screen
ray at any supplied in-range depth. Two depths give two points on that ray.
For a viewport of width `w` and height `h`, the center of terminal cell
`(column,row)` has screen coordinates `2*(column+.5)/w-1` and
`1-2*(row+.5)/h`. The application chooses the viewport and cell aspect.

`nt_spatial_clip_segment` intersects the input segment with all six planes.
It keeps the first-to-last order and reports one weight per endpoint in X and
Y, with Z zero. A segment touching the frustum at one point is visible
with coincident endpoints. An empty intersection succeeds, sets visibility
false and preserves the segment output.

`nt_spatial_clip_triangle` returns an ordered convex polygon with at most
nine vertices. Each vertex carries three weights for the original triangle
and preserves its winding. An empty intersection, or one confined to a line
or point in the triangle's parameter domain, returns a zero count. The
input triangle may have zero spatial area. Consumers that fill polygons
can test the area in their own fill space. Corner coordinates and weights
are rounded independently. Each corner also carries its projection computed
from the unrounded intersection, including exact screen edges at the clip
boundary. Two distinct clipped points can have the same rounded parameter.

### Screen picking

`nt_spatial_pick` borrows an array of view-space triangles with caller-supplied
object and primitive IDs. It returns the exact nearest intersection of
the screen ray within the closed near-to-far interval. Both windings are
eligible. Edges and vertices count as hits. Parallel, coplanar and degenerate
triangles contribute no hit. Equal exact depths retain input order, even
when the caller's identifiers differ. The result includes the input index,
view point, depth and original-vertex weights.

A finite screen coordinate outside the normalized rectangle is a successful
miss. The complete triangle array must have finite vertex coordinates
regardless of visibility. A zero triangle count accepts a null array. A
successful miss writes only the hit flag. Projection, clipping and picking
allocate no storage and borrow inputs through the call. Exact dyadic
predicates resolve frustum, intersection and depth ordering before
rounding the published binary64 ratios. Unprojection rejects out-of-frustum
arguments and returns `OUT_OF_RANGE` if a view coordinate exceeds the
finite range. Failed operations preserve all outputs and numeric work
restores the caller's floating-point environment.

### Meshes and polygonal models

Include `<nocturne/spatial_mesh.h>` and link `Nocturne::spatial`.
`nt_spatial_mesh_create` copies finite positions, UVs and indexed triangle
records. Each triangle carries a caller-defined primitive ID. Vertices
use zero-based indices. A draw supplies an object ID and a frame owner
token independently. `nt_spatial_mesh_data` returns arrays borrowed from
the immutable mesh until destruction. Degenerate authored triangles can
be stored and simply produce no ray hit.

`nt_spatial_mesh_decode_obj` reads a length-delimited polygonal Wavefront
OBJ input. It accepts three-coordinate `v` and `vn` records, one to three
coordinates on `vt`, and three to 256 corners per face. Coordinates use
ASCII decimals with optional signs and decimal exponents. Each numeric
field has a 128-byte limit. Decimal conversion rounds to nearest-even
binary64 independently of process locale and rejects values that round
to infinity. Missing texture coordinates default to
zero, and supplied U and V values are preserved. Arrange image rows to
match the authored V convention. Normals and optional third texture
coordinates are validated and the resulting mesh stores positions and
two-component texture coordinates.

Face references use `v`, `v/vt`, `v//vn` or `v/vt/vn`. Positive
indices are one-based and negative indices refer back from the number
of records defined so far. Each corner in the result owns its position
and UV so seams with distinct UV indices are preserved. Simple planar
polygons, including concave ones, triangulate in deterministic order
with their original winding. The generated triangles use zero-based
emission order as their primitive IDs. Degenerate, nonplanar and
self-intersecting faces are invalid. Comments, CRLF, object and group
labels, and material and smoothing directives are accepted. The caller
supplies material and texture choices. Decoding reads only the supplied
bytes. Unsupported geometry records report `UNSUPPORTED_CAPABILITY`.

### Spatial sample rasterization

`nt_spatial_rasterize` borrows a list of visible mesh instances, projection
parameters, an optional world camera and a sample grid. Each instance
can transform its mesh before the camera conversion. An absent transform
selects identity. With an absent camera the transformed values are already
in view space. Each conversion publishes a binary64 position before the
next conversion. The rasterizer uses the same closed triangle and exact
depth order as screen picking. The sample grid uses one ray per exact
sample center, using `(2*column+1-width)/width` and
`(height-2*row-1)/height` as ratios in the intersection predicates.
X increases right and Y down in the grid. Normalized screen
coordinates increase right and up. Depth ties retain instance order,
then face order.

The front triangle's UV is interpolated with its original view-space hit
weights, including under perspective. A surface can use a solid tint or
an `NtImageView` with the shared nearest and bilinear texture sampling
options. Texture channels multiply the tint channels in RGBA8. The
nearest sample composites over the supplied background in linear sRGB.
Coverage is geometric, so transparent texels still retain hit identity.
`nt_spatial_raster_sample` reads the covered flag, composited color,
owner, depth, UV and object and primitive IDs. A miss carries the
background color and zeroed hit fields.

The renderer copies the resulting samples into an immutable snapshot.
The input instances, meshes, transforms and textures can be released
after rendering. Temporary geometry storage is proportional to the
input triangle count and the snapshot to the sample count. The input
budget fields bound those two counts before allocation. Zero selects
the maximum representable count. Either sample extent can be zero.
`nt_spatial_raster_frame` produces an independent ordinary frame using
the shared 2-by-4 cell sample reducer and the caller's sampling mode.
Partial edge tiles are padded with the raster background.

### Mounted spatial paint and cell input

`nt_spatial_raster_cell_size` gives the raster's terminal extent in
2-by-4 sample tiles. `nt_spatial_raster_paint` draws these tiles at a
component-local origin through the supplied paint context. It visits
the intersecting rectangular clip and uses the ordinary paint operation
for masks, compositing and mount ownership. Its sampling mode and visual
output use the same reducer as the owned terminal frame. The raster
remains immutable. A paint callback propagates any painting error to
retain the prior committed tree snapshot.

`nt_spatial_raster_cell_hit` accepts a raster-local cell. A cell can
identify the object and primitive whose covered physical sample is
closest to that cell's center. At equal distances it chooses the smaller
sample row and then column. The result carries the physical sample
position and all the corresponding `NtSpatialRasterSample` fields.
The geometric coverage rule includes transparent samples and ignores
padded samples. A valid cell with no covered samples sets `found` false
and preserves the hit value. An out-of-bounds cell reports
`OUT_OF_RANGE` and preserves both outputs. Null required pointers, or an
invalid sampling mode in paint, report `INVALID_ARGUMENT`.

For routed pointer input, translate the component-local pointer by the
raster's painted origin and query the raster associated with the latest
committed frame. Application code owns that association and decides
when to change camera or geometry, how to choose input bindings and
which semantic intents to emit. The [Model Bench consumer](../examples/model-bench/README.md)
uses a stock editor alongside a raster-painted leaf, keyboard camera
and geometry controls, and typed object and primitive selection.

Constructors publish their opaque output pointer only after success.
Malformed input reports `INVALID_ARGUMENT`. Size or numeric exhaustion
reports `OUT_OF_RANGE`, and allocator refusal reports
`ALLOCATION_FAILED`. Failed calls preserve outputs and reclaim all
temporary or partially built storage through the allocator that supplied
it. Numeric work uses the standard nearest-even environment and restores
the caller's environment. The [installed mesh consumer](../tests/consumer/spatial_mesh.c)
exercises decoding, camera and instance transforms, textures, ownership
and allocation failure.

### Surface poses and point motion

Include `<nocturne/spatial_surface.h>` and link `Nocturne::spatial`.
`NtSpatialSurfaceMotion` describes a time-zero world pivot, a constant
world linear velocity in units per second and a constant world angular
velocity in radians per second. Its direction is the rotation axis and
its length is the angular speed. Positive rotation follows the right-hand
rule. Supply a finite signed elapsed time in seconds to evaluate the
motion. All fields are supplied by value or borrowed through the call.

Let `p` be the initial world pivot, `v` the linear velocity, `w` the
angular velocity and `R(t)` the rotation for axis `w` and angle
`length(w)*t`. At zero angular velocity `R(t)` is the identity.
`nt_spatial_surface_pose` returns the affine transform from time-zero
world placement to `p + v*t + R(t)*(point-p)`. To move a mesh authored
in local coordinates, compose the surface pose outside its initial
local-to-world transform. To recover its initial placement, use the
standard affine inverse. `nt_spatial_surface_sample` takes an initial
world point and returns its current position and instantaneous world
velocity `v + cross(w,R(t)*(point-p))`.

The angle is rounded to binary64 after evaluating the size of the full
angular displacement. Rotation coefficients use the same trigonometric
evaluation as `nt_spatial_transform_create`. The direct sample keeps
intermediate point offsets and products until combining them, including
when the translation column of the equivalent affine transform is out
of range. Pose composition and direct sampling are subject to rounding
at the API boundaries. The installed surface fixture allows `1e-14`
absolute error for its ordinary-scale rotation and velocity values.

### Swept point contact

`nt_spatial_sweep_point` finds the first intersection of a world point
moving linearly from `first` to `last` with caller-authored triangles over
one closed normalized interval `[0,1]`. Each triangle supplies three
initial world vertices and a single displacement shared by its vertices.
At fraction `f` each vertex has moved by `f * displacement`. Stationary
points and triangles are supported. The translating triangle retains its
initial winding and shape over the interval. To use this query with a
pose sampler, evaluate the starting geometry and supply the translation
for the interval being swept. The sweep model is linear translation.

Edges, vertices, and coplanar overlap count. A coplanar path uses its
first entry into the closed triangle, including fraction zero for an
initial overlap. Zero-area triangles contribute no contact. Both
windings are eligible. A hit publishes the selected object and primitive
IDs, the input triangle index, the time fraction, the world contact
point, the three vertex weights in input order, and a unit winding normal
from `cross(b-a,c-a)`. The normal keeps its orientation for back-side
contacts. Equal exact contact fractions keep input order.

Exact dyadic geometry resolves plane crossing, coplanar entry, triangle
coverage and time order before the fraction, point and weights round
independently to binary64. Normalization uses scaled binary64 components.
The query borrows its triangle array through the call, allocates no
storage and checks every supplied triangle for finite coordinates. A
zero count accepts a null array. A successful miss sets `found` false
and preserves the contact value. A finite displacement can be used even
when the mathematical final triangle would exceed the coordinate range.

Surface queries preserve outputs on failure and restore the caller's
floating-point environment. Null required pointers and nonfinite inputs
return `INVALID_ARGUMENT`. Unrepresentable results, an angular
displacement that rounds to infinity, and an unrepresentable sweep array
count report `OUT_OF_RANGE`. The [installed surface consumer](../tests/consumer/spatial_surface.c)
checks pose conventions, velocity, first contact, coplanar entry, exact
time ties, large coordinates and failure rollback.

## Water waves, channel flow and spray

Include `<nocturne/simulation.h>` and link `Nocturne::simulation`.
These operations borrow their inputs through the call and allocate no
storage. The caller owns schedules, buffers, time, and the way sampled
values drive plot, paint, pose or spatial geometry. Result structures
are written only on success. Numeric operations use nearest-even
arithmetic and restore the caller's floating-point environment.
A host that cannot establish or restore it returns `UNSUPPORTED_CAPABILITY`.

### Water surface

`nt_water_wave_sample` adds up to 32 traveling cosine modes above the
local x–z plane. Each mode contributes
`amplitude * cos(2*pi*(kx*x + kz*z - frequency*seconds + phase))`.
The wavenumbers use cycles per world length unit, frequency uses cycles
per second, and phase uses turns. The sample contains the vertical
displacement, both horizontal derivatives, the upward unit normal
proportional to `(-dx,1,-dz)`, vertical velocity and the horizontal
Laplacian. Coordinates, signed seconds and mode values must be finite
and within ±1,000,000. Zero modes give the flat surface with an upward
normal. The exact dyadic phase sum is reduced to a quarter turn and a
small angle before evaluation. After evaluating sine and cosine in
binary64, the sampler forms exact products and sums the contributions
before rounding each output. This preserves small contributions when
larger ones cancel and makes the sample independent of mode order.
Applications choose how to transfer a displacement or velocity to a
mesh or body.

The single-mode fixtures compare each output with its analytic
reference within `3e-13 * max(1, abs(reference))`. In the multi-mode
reference cases, the height and derivative error allowance is `3e-13`
times the greater of one and the sum of the magnitudes of the mode
contributions for that output. The normal must agree with the
normalization of the returned gradients within `3e-13` per component.
When comparing to the analytic normal for multiple modes, the absolute
allowance per component is `3e-13 + 2*hypot(Bx,Bz)/L`, where `Bx` and
`Bz` are the two derivative error allowances and `L` is the length of
the analytic unnormalized normal `(-dx,1,-dz)`.

### Flow and thickness

`nt_flow_sample` takes up to eight serial stages and 64 sorted,
nonoverlapping half-open inlet segments. Each segment supplies a
constant rate in cubic world units per second between two timestamps
in microseconds. Rate is in [0, 1,000,000]. Outside the segments the
inlet supplies zero. The sample has a port for the inlet and for each
successive outlet. Port volume is cumulative through the query time.
A port's rate is instantaneous. At a step discontinuity on an
unfiltered port it is the value on the right.

Each stage first delays the incoming signal by its transit time, then
filters it through an initially empty linear reservoir with residence
time τ. In continuous time its stored volume V satisfies
`dV/dt = incoming - V/tau`, and the outlet supplies `V/tau`.
Zero residence time bypasses the reservoir. Each time parameter ranges
from zero to one day in integer microseconds. Delays add along the
cascade. Each port integrates the authored positive intervals and
reports how much has reached that point. The linear chain uses a
probability transition with one absorbing outflow state, a bounded
positive series on a scaled interval, and at most 66 squaring steps.
It samples directly at the requested time, so the result is independent
of rendering frequency. Repeated residence times are valid.

Adjacent port volumes are ordered and their difference gives combined
transit and reservoir storage at that stage. Total admitted volume
equals total storage plus final discharged volume to floating-point
roundoff. When the stored volume is small compared with total throughput,
this difference may round to zero. Subnormal rates or outputs can
underflow. The contract tests use an absolute error bound of
`3e-13 * max(1, abs(reference))` for the analytic channel fixtures.
The channel has no evaporation or back pressure.

`nt_flow_section_sample` divides a rate by width × speed to get depth,
capped at the configured maximum. Width and speed must be in
[0.000001, 1,000,000] in world units and world units per second. The
depth cap is in [0, 1,000,000] world units. It rounds the depth,
carried rate and excess rate independently from their exact values,
so the depth can remain positive when the carried rate underflows. The caller decides whether to route excess to another
section, retain it, or allocate a portion to spray.
`nt_flow_section_point` displaces a caller-supplied dry point by
`depth * weight * depth_direction`, for mesh vertices or ordinary
spatial positions. The caller supplies a direction of unit length within
1e-12 and a weight between zero and one. A zero weight leaves a dry vertex
in place as a numeric value. Each coordinate rounds once after displacement.
Dry coordinates and depth follow the same 1,000,000 unit bounds.

### Spray

`nt_spray_emit` copies the authored spray parameters into a value batch
at a caller-supplied birth time. It selects up to the supplied capacity
(at most 4,096) of whole drops from the available volume. Per-drop
volume is in [1e-18, 1e18] and available volume is in [0, 1e18] in
cubic world units. Droplet IDs are consecutive from the provided ID.
An unrepresentable ID range fails. Emitted and remaining volume are
reported separately. Within rounding, they sum to the offered volume.
Capacity zero preserves all offered volume. The caller can retain the
remaining volume for another batch.

`nt_spray_sample` returns a drop's position, velocity, volume and
normalized age from its ID and explicit timestamp. A drop is active
from its birth through the last microsecond before its lifetime expires.
Birth time and sample time share one unsigned, non-wrapping clock.
When the mathematical expiry exceeds `UINT64_MAX`, the drop stays
active through the last representable timestamp. A lower timestamp
than the birth time lies before birth.
The lifetime ranges from one microsecond to one day. A valid drop outside
that interval writes only `active=false`. Initial velocity uses a
deterministic per-axis draw in the supplied spread around nominal
velocity and then follows constant acceleration. Coordinates, velocity,
spread and acceleration are finite within ±1,000,000 in their respective
world units. Spread is nonnegative. Callers own batch retention,
cancellation and the fate of expired or intercepted liquid.
Sampling a drop at the same time returns the same result regardless of
earlier samples. A consumer may pass its sampled point to a spatial
contact query to decide when to deposit the drop.

Oversized counts report `OUT_OF_RANGE`, as do invalid droplet indices
or ID ranges. Other malformed inputs, including unordered inlet
intervals, nonfinite values and invalid parameter ranges, report
`INVALID_ARGUMENT`. The [installed surface-flow consumer](../tests/consumer/surface_flow.c)
composes these public APIs.

Discovery references at `53191655ec77e41e0a3453afbc08f8dccfcdf4a5` are
`native/src/wave_field.c`, `native/src/flow.c` and
`native/src/flow_thickness.c`. This implementation uses new typed C
interfaces and its own positive reservoir solver.

## Surface water, foam and heat transport

Include `<nocturne/simulation.h>` and link `Nocturne::simulation`.
`nt_surface_transport_step` advances caller-owned cells by an explicit
interval of zero to 1,000,000 microseconds. Each cell represents a
region chosen by the application, such as a mesh face or reservoir.
The topology has up to 128 cells and 512 edges. Edges address cells by
array index. `NT_TRANSPORT_DRAIN` sends the edge flow out of the network.
Self-edges and nonzero conductance to a drain are invalid. Parallel
edges are independent. A zero-length call validates the descriptors,
preserves the cell values and returns a zero budget.

Inputs and outputs use disjoint storage. The function borrows topology
and options only for the call. A nonempty step with positive elapsed
time allocates bounded temporary routing storage. If that allocation
fails, the result is `ALLOCATION_FAILED`. Restore
a snapshot by copying the cell values and supplying the same topology
and interval. Remove cells or edges by passing a new consistent array.
Applications supply a new step when desired and choose whether to
subdivide the interval to model faster changes in forcing or transport.
Failure preserves the entire cell array and optional budget.

### Units and state

Use a coherent system of volume, temperature, time and energy units.
A cell stores water volume, foam amount and nonnegative thermal energy
on an absolute temperature scale. Foam uses an independent amount unit.
The wet and substrate volumes are well mixed and share a temperature.
For water volume V, heat capacity is `C = substrate_heat_capacity +
V * water_heat_capacity` and temperature is `E / C`. Water heat capacity
is a volumetric value for the whole network. The substrate capacity
must be positive even when a cell is dry.

Water and foam capacities, inlet rate and absolute temperature, foam
generation and decay rates, evaporation rate, volumetric heat capacity,
ambient temperature and conductance, and edge rates and conductance
use finite values in [0, 1,000,000]. Substrate capacity uses
[0.000001, 1,000,000]. Wet heat capacity computed at maximum water
volume must be at most 1e12. Cell and isolated-store energies are in
[0, 1e24]. Latent heat in energy per unit of volume uses [0, 1e12].
Power uses ±1e18 energy units per second. A cell starts at or below its
water and foam capacities. A visible water or foam surface can be
derived from its inventory without assigning it a specific mesh.

### Step order and balances

1. Propose simultaneous edge transfers from the pre-step cell values.
   For each donor, send the lesser of its stored water or its total
   authored outlet rate times elapsed seconds. Each water rate is in
   volume per second. Apportion the total among its edges by their
   relative rates. Foam uses separate first-order edge rates per second.
   Send the donor foam times the sum of those rates times elapsed
   seconds, with the transferred fraction capped at one. Apportion
   foam separately. Water transfers carry sensible heat using the
   donor's pre-step temperature. Sum parallel rates for each routing
   destination, then accumulate contributions from multiple donors before
   rounding the receiver's inventory. At ambiguous water and heat
   rounding boundaries, compare the sum of the routed fractions exactly.
   Water and foam arriving during the
   step first become eligible for outgoing transfers on the next step.
2. Add the local water inlet at its authored temperature and foam
   generation in amount per second. Compare water with capacity before
   rounding the total inventory. Spill the excess with the mixed
   sensible heat at that point in the step. Evaporate a
   fraction `1-exp(-evaporation*seconds)` of the remaining water,
   removing its sensible heat and the specified latent heat. When
   latent heat is positive, cap evaporation by available energy.
   Foam decays exponentially at its local rate before the foam
   capacity is applied. Report the discarded excess.
3. Solve local heat exchange with a constant ambient reservoir and
   constant applied power at the resulting heat capacity. The exact
   local model is `dE/dt = power + ambient_conductance*(ambient_T - E/C)`.
   Split the observed energy budget into applied energy and signed
   exchange with the ambient reservoir. A negative computed local
   energy fails the step.
4. Apply each conductive edge in input order to the two current
   stores. Edge conductance is symmetric and measured in energy per
   second per unit of temperature difference. The analytic isolated
   pair conserves energy and relaxes its temperature difference by
   `exp(-conductance*(1/Ca + 1/Cb)*seconds)`. This ordered split is
   deterministic for the same inputs. Changing the edge order or
   subdividing time can change results when several effects interact.

The budget contains external transfers for this step. Total water
changes by inlet less drain, spill and evaporation. Foam changes by
generation less drain, spill and decay. Heat changes by inlet,
applied and ambient energy less drained, spilled, sensible evaporated
and latent heat. Internal transfers cancel to rounding error. Balance
fixtures compare with a tolerance of `5e-12 * max(1, scale)`, where
scale is the initial inventory plus the magnitudes of budget terms.
The step returns binary64 values in nearest-even arithmetic. Thermal
contact and local cooling keep small differences through the final
energy calculation. The internal exponential approximation and split
steps still have finite precision, so very small changes can round away. A nonfinite, negative or over-limit final
energy reports `OUT_OF_RANGE`.

`nt_thermal_exchange` also exposes the isolated pair on its own. It
borrows two value stores with constant capacities in [0.000001, 1e12]
and copies the result. The result can alias the input. This operation
can couple a material or body with a liquid or substrate using authored
capacities and conductance. A zero duration or conductance returns the
same stores. The two APIs report `INVALID_ARGUMENT` for malformed
descriptors, `OUT_OF_RANGE` for excess counts, time or computed
energy, and `UNSUPPORTED_CAPABILITY` when the floating-point
environment cannot be established or restored.

The [installed transport fixture](../tests/consumer/surface_transport.c)
exercises fan-out, open and closed balances, warm inlet, evaporation
with latent heat, ambient response, isolated conduction, snapshot replay,
cell removal and failure rollback. Discovery references at
`53191655ec77e41e0a3453afbc08f8dccfcdf4a5` are
`native/src/foam_transport.c`, `native/src/geometry.c` (foam generation
and decay), `native/src/flow.c` (surface water), `native/src/thermal.c`
and `native/src/material_heat.c`. The typed transport implementation
uses its own bounded step and energy budgets.

### Convex hulls, immersion and buoyancy

`nt_hull_create` copies a closed, outward-wound convex boundary in
right-handed body coordinates. Supply 4 to 128 distinct vertices and
4 to 252 nondegenerate triangles. Every vertex must be used and each
edge must occur once in each direction with matching indices. Coplanar
triangles may share edges or vertices and must have disjoint interiors.
Convexity and topology apply to the supplied binary64 coordinates.
Concave shapes can be represented by several nonoverlapping convex hulls
and composed by the caller. The hull is immutable and may be queried
concurrently with a thread-safe allocator. Destroy it with
`nt_hull_destroy`, which accepts null. The hull retains its allocator.
Queries borrow their inputs. Immersion uses bounded temporary clipping
storage from the C allocator. It uses the hull allocator for additional
exact storage when an error interval straddles a volume rounding
midpoint. Either allocation can return `ALLOCATION_FAILED`. All query
storage is released before the call returns.

The caller owns an `NtFloatingBody` snapshot. Model coordinates are
relative to the center of mass. The snapshot gives its world position,
body-to-world axis and angle, mass, body-axis principal moments of
inertia and world-axis linear and angular velocities. Position, axis,
radians and each velocity component allow finite magnitudes up to 1e6.
A nonzero rotation requires a nonzero axis. Valid mass is in (0, 1e6]
and each principal moment is in (0, 1e24]. Use coherent units such as
meters, kilograms, seconds and radians. `nt_floating_body_transform`
produces a rigid transform usable by the optional spatial mesh,
surface and contact APIs. The immersion routines evaluate the supplied
pose directly and leave the body snapshot unchanged.

`nt_hull_immerse_plane` clips the hull against the half-space below
an oriented world plane. Use a normal with a positive Y component.
Scaling the normal by a positive factor preserves the half-space
when the supplied coefficients retain their proportions. A
water-wave sample can supply the point and tangent normal.

`nt_hull_immerse_water` clips the hull against the finite columns
below world-space water triangles. Either winding is valid. Each
triangle must have a nonzero area in XZ, and projected interiors
must be disjoint. The triangles can meet at edges and vertices
and can give different heights on either side. The covered footprint
is exactly their union, with dry space outside it. Up to 64 triangles
are supported. An empty patch is dry. Hull vertices, world plane
points, plane-normal components and water vertices allow magnitudes
up to 1e6 per component.

Both queries return full volume, displaced volume and the center
of the displaced volume. They also return the center displacement
relative to the body's center of mass, resolved independently so a
small lever arm can survive a large world translation. At zero
displaced volume both center vectors are zero. Face, edge and point
touches with no enclosed submerged volume return zero. Each side test
uses the exact rational vertex derived from the supplied binary64
geometry and the resolved pose. Later cuts retain those predicates.
Volume and first moments use exact homogeneous tetrahedral determinants
and coordinate sums. The solver converts the resulting rational
terms to scaled four-component values for summation. This preserves
narrow hull dimensions and grazing wedges at large coordinate scales.
For ambiguous volume rounding, the solver sums the fractions exactly
and compares against the midpoint before selecting the nearest-even output.
An output volume that rounds to zero uses the dry center convention.
The fixture compares cases within `3e-12 * max(1, |reference|)`.
A volume excess or negative residual within `1e-12 * full_volume`
is clamped to the physical interval.
Larger inconsistencies or a positive full volume that rounds to zero
report `OUT_OF_RANGE`.

`nt_hull_buoyancy` takes a sample from the same body placement and
authored density and gravity magnitude in [0, 1e6]. It returns lift,
weight, their sum, and the moment of lift about the center of mass.
Lift equals displaced volume times density times gravity along +Y.
Weight acts through the center of mass along -Y. Torque is the
cross product of center displacement and lift. Dry bodies still
have weight. The force solve leaves velocity and application time
under caller control.

Output objects retain their previous values on failure. Malformed
descriptors and invalid geometry report `INVALID_ARGUMENT`.
Counts beyond the documented bounds report `OUT_OF_RANGE`. Allocation
failure for a hull or temporary exact storage reports `ALLOCATION_FAILED`.
If the standard
floating-point environment cannot be established or restored, the
operation reports `UNSUPPORTED_CAPABILITY`.

The [installed hull fixture](../tests/consumer/hull_immersion.c)
exercises dry, partial, full, oblique and nonplanar water surfaces,
owned storage, failure rollback and the maximum hull size. The
discovery references at `53191655ec77e41e0a3453afbc08f8dccfcdf4a5` are
`native/src/float_hulls.c`, `native/src/hull_immersion.c`,
`native/src/hull_mesh_immersion.c` and `native/src/hull_buoyancy.c`.

### Isolated linear and angular exchange

`nt_linear_drag_exchange` accepts two mass and velocity stores and a
nonnegative conductance. `nt_angular_drag_exchange` accepts two sets of
diagonal moments of inertia, their angular velocities and one
conductance per axis. All angular quantities use the same caller-chosen
principal frame. The angular operation applies independent exchanges
along those axes with the authored diagonal inertias held constant.

Use consistent dimensions. Linear conductance has units of mass per
second and the published impulse has units of momentum. Angular
conductance has units of moment of inertia per second and the published
impulse has units of angular momentum. Velocity units are distance per
second or radians per second. The impulse is signed into the second
store. The first store receives its negative. Dissipated energy is
nonnegative.

For a scalar axis with masses or inertias `a` and `b`, conductance
`g`, and elapsed seconds `t`, define `x = g*t*(1/a + 1/b)`.
The velocity difference decays by `exp(-x)`. The impulse into the
second store is `(u-v)*a*b/(a+b)*(1-exp(-x))`. Total momentum is
conserved up to binary64 rounding. Dissipated energy equals the
initial relative kinetic energy times `1-exp(-2*x)`, summed across
axes. The solver keeps small terms scaled until it forms the reported
endpoints and budgets. Conservation tests use
`5e-12 * max(1, scale)`, with scale drawn from the sum of the
magnitudes of the compared terms. Repeated caller steps accumulate
rounding error in the stored state.

Each mass or moment is in [1e-6, 1e12], each conductance in [0, 1e6],
and each velocity component is finite with magnitude at most 1e6.
Elapsed time is an explicit integer in microseconds and at most
1,000,000. At zero duration or conductance the corresponding store
velocities are copied and the impulse and loss are zero. The
operations borrow their inputs for the call. The caller owns the
new stores and any transfer into another simulation budget.
Failure leaves the result unchanged.

### Floating-body steps

`nt_floating_body_step` advances one caller-owned body snapshot
under a constant supplied world-frame force and torque about its
center of mass. It accepts loads with finite components of magnitude
at most 1e38 and an elapsed time up to 1,000,000 microseconds.
A zero-duration step copies a valid snapshot. Input and output may
be the same object.

Translation uses the constant-acceleration equations at the supplied
mass. The solver checks exact rational position and velocity endpoints
against the body coordinate bounds before rounding. The rotation step
transforms angular velocity and applied torque into the current body
axes, evaluates Euler's gyroscopic term
`omega × (I*omega)`, and holds the resulting world angular acceleration
constant for one step. It updates angular velocity and composes the
world rotation increment from the average angular velocity with the
starting orientation. The product is reported as an axis and
an angle in [0, π] when the step changes the orientation. A zero
increment preserves the original axis and angle.

The angular approximation is a local integration rule. The initial
and terminal angular speeds, multiplied by step duration, must each
be at most 0.25 radians. The solver compares the exact squared
travel limits before rounding the velocity endpoints. Quaternion
composition carries the common denominator of tiny rotation increments
through cancellation before resolving the output angle and direction.
Collinear turns within the principal-angle range combine before
rounding, so opposing turns preserve a representable residual. Near
opposition, the solver composes that residual with transverse turns.
The solver subtracts whole turns from the starting angle with a
384-bit fractional period before rounding the reduced angle for
trigonometric evaluation. The collinear and near-opposite calculations
retain the wider remainder through cancellation.
When both input rotations are smaller than `2^-1000` radians, the
solver retains the original axis norm through quaternion-vector
cancellation. It rounds the squared-angle approximation with the
fourth-order correction for nonparallel axes.
A caller can subdivide time and resample
loads as the body turns or meets a surface. The step applies no
automatic constraint or collision response. It can consume the
world force and torque from `nt_hull_buoyancy` and provide starting
and ending points for `nt_spatial_sweep_point`. Applications choose
how to exchange velocities, apply forces, solve contacts and retain
state. Momentum and dissipation guarantees apply to the isolated
exchange operations.

Malformed descriptors report `INVALID_ARGUMENT`. Excess duration,
angular step or computed state range reports `OUT_OF_RANGE`. If
the standard floating-point environment cannot be used or restored,
the operation reports `UNSUPPORTED_CAPABILITY`. Failure leaves the
output snapshot unchanged.

The [installed coupling fixture](../tests/consumer/fluid_coupling.c)
checks momentum and energy balance, subnormal impulses and strong
relaxation tails, snapshot replay, composed rotation, bounded
motion, and a buoyancy result that drives an explicit swept contact.
Discovery references at `53191655ec77e41e0a3453afbc08f8dccfcdf4a5` are
`native/src/fluid_drag_exchange.c`, `native/src/angular_drag_exchange.c`
and `native/src/surface_fluid.c`.
