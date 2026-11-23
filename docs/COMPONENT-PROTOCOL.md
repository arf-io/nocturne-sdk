# Component and host contracts

This contract governs the M2–M5 implementation and later consumers. The public
C and C++ declarations were exercised as interface sketches during M1. The
[installed lifecycle and publication contracts](CONTRACTS.md#component-tree-and-publication)
describe the implemented M4 work and the remaining M5 routing responsibilities.
The [component catalog](component-contracts.json) lists public component
families, operations and capability IDs.

## Identity, ownership, and errors

A tree holds one root and an ordered list of children per node. The application
creates the tree and serializes all operations on it. Independent trees may run
on different threads. There is no implicit current tree, context, terminal, or
allocator.

A mounted reference names its live tree, slot and nonzero generation. A null
reference denotes no parent, focus, or owner as applicable. Passing a reference
from another live tree is an error. A detached slot's old references are stale,
including when the slot is reused. A generation never wraps into an issued
reference. Retire exhausted slots and report capacity failure if no new slot
can be allocated. All references cease to be usable when their tree is destroyed.
An application model ID is separate and may link different mounted instances
over time.

Mount validates the callback descriptor, state/lifetime policy, prospective
parent, capacity, and remaining identities before publishing the node. The tree
copies the descriptor. A successful mount takes one lifetime obligation for the
state pointer. With a dispose callback, the tree calls it exactly once after
detach. Its implementation releases owned resources and may free the state.
A null dispose callback means the caller lends the state and keeps it live
through detach or tree destruction. The caller retains full responsibility on
failure, including for any derived component or configure-time resources.
A node has exactly one parent while mounted. Move/reparent validates membership
and rejects cycles before changing the working child lists. A move preserves
identity and state. The new parent path becomes available for routing with
the frame that publishes the corresponding geometry.

Allocating objects take a caller-supplied allocator or a documented default.
An allocator has paired allocation/free calls, caller context, and a documented
alignment guarantee. It remains live until the last object using it is released.
Check counts, size arithmetic and alignment first. Distinguish invalid input,
out-of-range arithmetic, allocation failure, insufficient caller buffer, stale
reference, wrong phase and unavailable capability. Keep the animation/motion
status values and validation behavior stable as the common status API grows.
C callers must provide accessible, correctly aligned records and arrays. Use
lengths for text and buffers, including empty buffers.

A failed operation leaves caller output parameters and committed SDK state
unchanged unless the operation explicitly reports a required size or committed
progress. For example, a transport failure can occur after some bytes were
sent. The terminal adapter reports failure, invalidates its previous-frame
cache and performs cleanup through its ownership path. Callback phases below
define what the tree can commit and what consumer code owns.

## Coordinate and text model

- The origin is top-left, x increases right and y increases down. Points are
  signed 32-bit cell coordinates. Widths and heights are unsigned 32-bit cell
  counts. Rectangles are half-open. A zero extent has empty coverage. Negative
  origins are valid. Compute rectangle edges and changes of origin with checked
  wider arithmetic. An operation requiring an unrepresentable origin returns
  out-of-range before publication.
- Constraints are independent min/max cell counts per axis with an explicit
  unbounded maximum. A finite max below min is invalid. Measure returns
  preferred dimensions within the constraint and supports empty constraints.
  A child may need clipping when the parent arranges it below its intrinsic
  size. Containers own distribution and shrinking policy.
- Layout publishes child rectangles relative to the parent. Paint coordinates
  are local to the mounted node. The effective clip intersects the frame,
  ancestor clips and the node's paint bounds, in root coordinates. A mask
  narrows the coverage further. Offset and scroll change child placement
  without moving the parent's viewport. Pointer location is in signed root
  cells, with the local position derived from the target's committed layout.
- UTF-8 storage validates scalar encoding and rejects malformed input before
  mutating owned text. Borrowed text is valid for its declared borrow. Text
  layout tracks logical byte spans and stable cluster IDs. In unchanged
  clusters outside the resegmented edit neighborhood, IDs survive insertion
  and deletion. IDs are invalidated if cluster content or boundaries change.
  Visual order is recorded separately from source identity. Round-trip hit
  results use cluster boundaries and a leading/trailing affinity, including
  for RTL clusters.
- Measurement uses a version-pinned Unicode data set and an explicit terminal
  policy. Ambiguous-width text and emoji presentation each select a width of
  one or two. Resolve emoji presentation for the entire cluster first.
  For text presentation, East Asian fullwidth/wide text uses two cells, and
  ambiguous text follows the selected ambiguity policy. Other printable
  text uses one cell. Unicode presentation selectors and joiners act at
  cluster level. An isolated mark
  renders with a visible one-cell fallback base. Marks joined to a base do
  not add cells. CRLF, CR, LF, VT, FF, NEL, LS and PS break lines. Tabs advance
  to the next positive configured column stop. Default-ignorable scalars
  contribute zero cells. Bidi formatting controls still participate in layout.
  Other nonprinting controls use a one-cell visible replacement. The
  [terminal glyph width contract](CONTRACTS.md#terminal-glyph-width) records
  the Unicode version and exact policy.
- Wrapping and ellipsis keep whole glyph clusters. An empty area emits no
  cells. If a line can fit zero copies of a cluster or ellipsis marker, emit no
  portion of it. A tab may cover several cells and has one logical source span.
  A wide glyph's leading cell owns the cluster. Its continuation supplies the
  same owner and leading-cell coordinates to hit lookup. A borrowed cell or
  glyph view expires on frame mutation or destruction. Copy it to keep it.

A frame contains the SDK-owned cell values and the immutable glyph storage
those values reference. Setting a wide glyph, clearing it, or replacing it
changes the two-cell glyph footprint together. If a write to a visible
continuation would require altering a lead outside the effective clip, skip
the glyph mutation. Direct frame setting starts at a lead or empty cell.
Clearing can start at a continuation and removes the whole glyph.
When surface composition would clip an incoming wide glyph, it may blend
covered background channels. Preserve any existing complete destination glyph
whose footprint cannot be changed inside the clip. Shrinking a frame removes
glyphs with an incomplete footprint, then safely releases their cluster
references. Allocation failure preserves the old frame and borrowed views.

The **semantic owner** identifies the visible contribution at a cell within a
frame's owner namespace. A zero token means no owner. A fully transparent
source cannot replace the previous owner. Background-only paint keeps an
existing visible glyph's owner. An explicit glyph erase or replacement resolves
ownership with the corresponding visible coverage operation. An opaque blank
may own its background when no visible glyph remains. Compositing an
independent surface requires mapping its owner tokens into the destination
namespace before publication. Pointer routing uses a separate hit policy over
the same ordered, clipped geometry. An interactive blank control can receive
input, and a decorative glyph may decline it.

## Component callbacks and frame publication

Built-ins and extension components use the same callback descriptor and public
context functions. A callback may use its state pointer and the context for
that invocation. Borrowed contexts, input strings, scratch buffers and paint
views expire when the callback returns. Reserve private storage before
publishing changes if an operation can fail. Consumer code owns its external
side effects. The tree cannot roll back mutations of caller memory or
application I/O.

| Phase | Inputs and result | Allowed effects |
|---|---|---|
| Measure | Constraints in local cells, preferred size as output | Inspect state and measure direct children via the context. Pure with respect to geometry and time. |
| Layout | Assigned local size and already measured child sizes | Submit one placement per participating direct child. A failed pass preserves the previous published geometry. |
| Update | Host-supplied time and elapsed microseconds | Mutate component state, request invalidation and specify or clear the next update deadline. |
| Paint | Published candidate geometry, read-only state, bounded paint context | Draw through the context. The tree stages the resulting frame. Painting never advances time or dispatches input. |
| Event | Normalized input, phase and local coordinates where applicable | Update local gesture or control state. Return routing decisions, invalidation, and explicit focus/capture requests. |
| Dispose | The mounted instance state after it has been detached | Release owned state. The tree delivers no further callbacks for this mount. |

Mount requires measure, layout and paint callbacks. A paint-free component may
return success without drawing, and a leaf may use an empty successful layout.
Update, event and dispose are optional. Callback success results that exceed
the phase bounds cause a contract error. The application defers structural commands
until the outer SDK call has returned. Tree mount, move, remove, dispatch,
tick and frame publication invoked reentrantly return wrong-phase.
Phase-specific child measurement/placement and paint are allowed through their
respective contexts.

A parent measures and places each child it elects to arrange. Children are
painted in insertion order within equal z-order. Layout can specify a different
z-order without changing focus traversal. A child omitted by its parent is
unplaced for that frame, receives no paint or hit test, and becomes ineligible
for focus and capture on publication. A viewport derives its effective scroll
offset from its requested offset and candidate content/viewport extents. The
effective offset publishes with its child placements. A frame consists of a
coherent geometry, ancestor paths and cell snapshot. Measure, layout, and
paint errors keep that whole snapshot and preserve pending invalidations.
If there is no previous frame, the host has nothing to present. Removal and
explicit cancellation can proceed when frame publication fails. A snapshot
is eligible for routing only when every reference on the selected path is
still live. If removal retires an old-path ancestor of a moved node, that
node stays mounted at its new working parent and its old route becomes
ineligible immediately.

The host advances time using unsigned integer monotonic microseconds.
Admit a tick only if its time is at least the tree's last admitted timestamp.
Once the tick passes validation, record its timestamp before running any
callbacks. That watermark remains advanced even if an update fails. The
first update for a mount receives zero elapsed time. Later callbacks receive
the time since that node's last successful update. A retry at the watermark
can resume a failed component without rewinding the tree clock. Each tick
calls a due component at most once, skipping
removed or stale generations. A returned deadline no later than this tick
becomes eligible on the next tick, preventing a self-rescheduling loop in
one call. An update failure preserves that node's previous deadline and last
successful time. Earlier successful callbacks in the same tick remain
committed. The host sees the earliest eligible deadline or an explicit
absence when none remain. The tree never sleeps.

Invalidation is paint or layout. Layout invalidation also requests a repaint
and may propagate size changes through ancestors. A late event may dirty the
tree for the next frame. A successful frame clears the invalidations it
processed. The host supplies new terminal bounds at resize. Host time and
motion policy are explicit; reduced motion lets the component settle a
transition and stop requesting ticks.

## Input, focus and capture

A normalized event carries a kind and caller-supplied timestamp in monotonic
microseconds. Queued input may carry older timestamps and dispatch never
advances playback time. Key chords include key identity, modifiers,
press/repeat state and no implicit text
insertion. Text and bracketed paste carry validated UTF-8 independently.
Pointer events carry a pointer ID, location when known, button change and
current button state. Wheel carries signed deltas with declared cell units.
Resize supplies the new cell dimensions to host layout, and cancellation
includes a reason. An invalid event is rejected before opening a route.

Hit testing walks the committed, visible geometry front to back in reverse
paint order, applying clip, mask, interactivity and enabled state.
Key, text and paste events route to the current focus. There is no component
target for these events if focus is empty. A captured pointer routes to its
owner even outside that owner's clip. Capture is tracked independently per
pointer. Focus and capture require a live, enabled, placed node with nonempty
effective bounds at publication and a live committed ancestor path.
Focus additionally requires a focusable node. A focus change sends loss to
the previous owner and then target-only focus gain to the newly focused node
after the new focus has been set.

Routing snapshots the path before callbacks. It calls ancestor preview in
root-to-parent order, the target once, and ancestor bubble in parent-to-root
order. A consume response stops this route and suppresses its default action.
This is independent of whether a callback requested focus or capture.
Only the target can return a request to take focus or to take/release capture
for the current pointer. The application can also set focus between SDK calls.
Apply valid requests after a successful dispatch, even if its target consumed
the event. A later failing callback cancels staged focus/capture requests.
Previously successful invalidations and consumer state changes remain in
effect. Focus traversal searches eligible nodes in tree preorder using each
sibling list's insertion order. It wraps on an unconsumed traversal chord if
the host enabled traversal.
The host handles reserved shortcuts before dispatch. A reserved chord is
never supplied to component callbacks or consumed by SDK defaults. The host
also withholds any text it derives from the same reserved chord.

Starting capture requires an active gesture. An owner can renew its own
capture. An attempt to replace an active owner fails and leaves it intact.
A pointer release uses the capture and path at dispatch start. A final
release always ends capture. If the owner was skipped or failed its
release callback, clear ownership and send it a target-only cancellation
with `NT_CANCEL_ROUTE_INTERRUPTED`, even when other buttons remain down.
A later bubble error after a successful owner callback still ends capture
and needs no extra cancellation. A final release cannot acquire capture.
The dispatch status still reports callback failures after this cleanup.

Explicit early release, focus/capture loss or removal cancels the current
captured gesture. Suppress its remaining drag/button events until all
buttons are released. A subsequent fresh gesture can hit any eligible
component. A host pointer cancellation terminates only that pointer's
gesture and capture. Host-session cancellation terminates all the attached
tree's pointer gestures and clears keyboard focus. Both cancellations run
through terminal cleanup before any ordinary route.

Consumers can drain application intents outside callbacks. An intent carries
a source reference, kind and either counted bytes or typed fields. The SDK
owns a copy of enqueued payloads. A removed source remains a historical identifier.
Application handlers validate a reference before addressing a live component.
The event context stages a callback's successful intent output together with
that callback's result. If the context cannot reserve the required space, it
reports failure and publishes none of that callback's staged intents.
See [event rules and typed intents](CONTRACTS.md#event-rules-and-typed-intents)
for phase matching, typed derivation, and queue borrowing.

The component tree owns focus, capture and dispatch eligibility records so
it can retire them in the same operation that publishes layout or removes
a subtree. M5 routing supplies selection policy and reads the tree's
committed path and eligibility services. Tree publication and removal use a
common input-state transition and emit typed loss events through the M4
component callback protocol. They require only the shared event types.

On focus loss, loss of route eligibility or subtree removal, first clear the
affected focus and capture records. An ineligible path cannot receive new
routes. Emit one target-only pointer cancellation
per affected active capture, in ascending pointer-ID order, then target-only
focus loss. These notifications cannot request another route or block
teardown. Removal also clears deadlines and invalidates public lookup of the
subtree before notifications. After notifications, dispose descendants before
ancestors, visiting siblings in insertion order. A replacement is a new
mount and receives a new generation. Events already delivered are never
automatically replayed.

## Terminal ownership and fallback

The application selects a frame sink and supplies capabilities, or explicitly
opens a platform adapter that acquires terminal input/output modes. Exactly
one owner controls an interactive terminal session. The adapter uses an
acquisition and restoration path that knows which modes were changed. Restore
them on normal exit, input/output error and explicit host cancellation.
Platform adapters specify the signals they can safely catch and how the main
loop finishes cleanup. The component tree has no terminal handles.

Capabilities identify color mode (truecolor, indexed, monochrome), available
glyph modes (including ASCII), input modes, and whether the sink is
interactive. Width policy and reduced motion are separate negotiated or
caller-chosen values. Enabling a requested unavailable mode reports an explicit
unsupported-capability result. When a fallback has been selected, substitute
configured glyph/paint modes deterministically while preserving semantic
ownership and layout policy. A width-policy change invalidates text layout.
Noninteractive output emits a logical frame with no terminal mode changes or
cursor movement. Text and application payloads cannot enter the terminal
control-output path. Trusted adapter operations generate control bytes.

The POSIX adapter offers a descriptor check and capability suggestions that
the application can override. It reports output capability separately from
eligibility to open an interactive input session. Applications embedding
Nocturne can present directly through the frame sink. [Terminal capability contract](CONTRACTS.md#terminal-capability-report-and-output-fallback)

See the [package graph](PACKAGE-DEPENDENCIES.md) and the
[machine-readable component contract](component-contract.schema.json).
