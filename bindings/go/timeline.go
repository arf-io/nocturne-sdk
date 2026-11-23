package nocturne

/*
#include <nocturne/timeline.h>

static void go_tween_clip(NtTimelineNode *node, uint64_t channel, NtTimeUs delay,
                          NtTimeUs span, NtTween tween)
{
    *node = (NtTimelineNode){
        .kind = NT_TIMELINE_CLIP, .delay_us = delay, .data.clip = {
            .channel = channel, .span_us = span, .kind = NT_TRACK_TWEEN, .source.tween = tween,
        },
    };
}

static NtResult go_timeline_create(const NtTimelineNode *clips, size_t count,
                                   bool parallel, NtTimelineOptions options, NtTimeline **out)
{
    const NtTimelineNode root = {
        .kind = parallel ? NT_TIMELINE_PARALLEL : NT_TIMELINE_SEQUENCE,
        .data.group = {clips, count},
    };
    return nt_timeline_create(NULL, &root, &options, out);
}

static void go_timeline_group(NtTimelineNode *node, NtTimelineKind kind, NtTimeUs delay,
                               const NtTimelineNode *children, size_t count)
{
    *node = (NtTimelineNode){.kind = kind, .delay_us = delay, .data.group = {children, count}};
}

static NtTimelineClip *go_timeline_clip(NtTimelineNode *node, NtTimeUs delay,
                                        uint64_t channel, NtTimeUs span, NtTrackTime timing)
{
    *node = (NtTimelineNode){.kind = NT_TIMELINE_CLIP, .delay_us = delay,
        .data.clip = {.channel = channel, .span_us = span, .timing = timing}};
    return &node->data.clip;
}

static void go_timeline_tween(NtTimelineClip *clip, NtTween tween)
{
    clip->kind = NT_TRACK_TWEEN;
    clip->source.tween = tween;
}

static void go_timeline_keyframes(NtTimelineClip *clip, const NtKeyframe *keys, size_t count,
                                  NtTimeUs delay, NtRepeat repeat)
{
    clip->kind = NT_TRACK_KEYFRAMES;
    clip->source.keyframes = (NtKeyframeTrack){keys, count, delay, repeat};
}

static void go_timeline_random(NtTimelineClip *clip, NtRandomTrack random)
{
    clip->kind = NT_TRACK_RANDOM;
    clip->source.random = random;
}
*/
import "C"

import (
	"runtime"
	"sync"
	"unsafe"
)

type TimelineClip struct {
	Channel         uint64
	DelayUs, SpanUs uint64
	Tween           Tween
}

type TimelineOptions struct {
	DelayUs uint64
	Repeat  Repeat
	Reverse bool
	// Parallel selects the root group for NewTimeline.
	Parallel bool
}

func (options TimelineOptions) native() C.NtTimelineOptions {
	return C.NtTimelineOptions{delay_us: C.NtTimeUs(options.DelayUs), repeat: C.NtRepeat(options.Repeat),
		reverse: C.bool(options.Reverse)}
}

// TimelineSource selects exactly one source for a clip.
type TimelineSource struct {
	Tween     *Tween
	Keyframes *KeyframeTrack
	Random    *RandomTrack
}

type TimelineTrack struct {
	Channel, SpanUs uint64
	Timing          TrackTime
	Source          TimelineSource
}

type TimelineKind uint32

const (
	TimelineClipNode TimelineKind = C.NT_TIMELINE_CLIP
	TimelineSequence TimelineKind = C.NT_TIMELINE_SEQUENCE
	TimelineParallel TimelineKind = C.NT_TIMELINE_PARALLEL
)

// TimelineNode is a clip or a group of children. DelayUs offsets either kind.
// The selected Kind determines whether the node reads Track or Children.
type TimelineNode struct {
	Kind     TimelineKind
	DelayUs  uint64
	Track    TimelineTrack
	Children []TimelineNode
}

type TimelineSample struct {
	Value                           float64
	PlayheadUs, SourceUs, Iteration uint64
	Phase, ClipPhase                AnimationPhase
	Direction                       int32
}

// Timeline owns an immutable compiled group or clip.
type Timeline struct{ state *timelineState }

type timelineState struct {
	mu     sync.Mutex
	native *C.NtTimeline
}

func NewTimeline(clips []TimelineClip, options TimelineOptions) (*Timeline, error) {
	if err := ensureABI(); err != nil {
		return nil, err
	}
	native := make([]C.NtTimelineNode, len(clips))
	for i, clip := range clips {
		C.go_tween_clip(&native[i], C.uint64_t(clip.Channel), C.NtTimeUs(clip.DelayUs),
			C.NtTimeUs(clip.SpanUs), clip.Tween.native())
	}
	var timeline *C.NtTimeline
	status := C.go_timeline_create(unsafe.SliceData(native), C.size_t(len(native)), C.bool(options.Parallel),
		options.native(), &timeline)
	return ownTimeline(timeline, status)
}

// NewTimelineTree compiles an owned copy of a nested description.
func NewTimelineTree(root TimelineNode, options TimelineOptions) (*Timeline, error) {
	if err := ensureABI(); err != nil {
		return nil, err
	}
	var arena RawArena
	defer arena.close()
	node, err := nativeSlice[C.NtTimelineNode](&arena, 1)
	if err != nil {
		return nil, err
	}
	if err := root.writeNative(&arena, &node[0], 0); err != nil {
		return nil, err
	}
	settings := options.native()
	var timeline *C.NtTimeline
	status := C.nt_timeline_create(nil, &node[0], &settings, &timeline)
	return ownTimeline(timeline, status)
}

func (node TimelineNode) writeNative(arena *RawArena, native *C.NtTimelineNode, depth int) error {
	// Match the native limit while marshalling, including cyclic descriptions.
	if depth == 128 {
		return OutOfRange
	}
	delay := C.NtTimeUs(node.DelayUs)
	switch node.Kind {
	case TimelineClipNode:
		timing, err := node.Track.Timing.native(arena)
		if err != nil {
			return err
		}
		track := C.go_timeline_clip(native, delay, C.uint64_t(node.Track.Channel),
			C.NtTimeUs(node.Track.SpanUs), timing)
		return node.Track.Source.writeNative(arena, track)
	case TimelineSequence, TimelineParallel:
		children, err := nativeSlice[C.NtTimelineNode](arena, len(node.Children))
		if err != nil {
			return err
		}
		C.go_timeline_group(native, C.NtTimelineKind(node.Kind), delay, unsafe.SliceData(children), C.size_t(len(children)))
		for i, child := range node.Children {
			if err := child.writeNative(arena, &children[i], depth+1); err != nil {
				return err
			}
		}
		return nil
	default:
		return InvalidArgument
	}
}

func (source TimelineSource) writeNative(arena *RawArena, track *C.NtTimelineClip) error {
	switch {
	case source.Tween != nil && source.Keyframes == nil && source.Random == nil:
		C.go_timeline_tween(track, source.Tween.native())
	case source.Keyframes != nil && source.Tween == nil && source.Random == nil:
		spec := source.Keyframes
		keys, err := nativeSlice[C.NtKeyframe](arena, len(spec.Keys))
		if err != nil {
			return err
		}
		for i, key := range spec.Keys {
			keys[i] = key.native()
		}
		C.go_timeline_keyframes(track, unsafe.SliceData(keys), C.size_t(len(keys)),
			C.NtTimeUs(spec.DelayUs), C.NtRepeat(spec.Repeat))
	case source.Random != nil && source.Tween == nil && source.Keyframes == nil:
		C.go_timeline_random(track, source.Random.native())
	default:
		return InvalidArgument
	}
	return nil
}

func ownTimeline(timeline *C.NtTimeline, status C.NtResult) (*Timeline, error) {
	if err := check(status); err != nil {
		return nil, err
	}
	state := &timelineState{native: timeline}
	runtime.SetFinalizer(state, (*timelineState).close)
	return &Timeline{state: state}, nil
}

func (timeline *Timeline) borrow() (*timelineState, error) {
	if timeline == nil || timeline.state == nil {
		return nil, StaleReference
	}
	state := timeline.state
	state.mu.Lock()
	if state.native == nil {
		state.mu.Unlock()
		return nil, StaleReference
	}
	return state, nil
}

func (timeline *Timeline) Duration() (uint64, error) {
	state, err := timeline.borrow()
	if err != nil {
		return 0, err
	}
	defer state.mu.Unlock()
	return uint64(C.nt_timeline_duration(state.native)), nil
}

func (timeline *Timeline) ChannelCount() (int, error) {
	state, err := timeline.borrow()
	if err != nil {
		return 0, err
	}
	defer state.mu.Unlock()
	return int(C.nt_timeline_channel_count(state.native)), nil
}

func (timeline *Timeline) ChannelAt(index int) (uint64, error) {
	state, err := timeline.borrow()
	if err != nil {
		return 0, err
	}
	defer state.mu.Unlock()
	if index < 0 {
		return 0, OutOfRange
	}
	var out C.uint64_t
	status := C.nt_timeline_channel_at(state.native, C.size_t(index), &out)
	return uint64(out), check(status)
}

func (timeline *Timeline) Sample(channel, elapsedUs uint64) (TimelineSample, error) {
	state, err := timeline.borrow()
	if err != nil {
		return TimelineSample{}, err
	}
	defer state.mu.Unlock()
	var out C.NtTimelineSample
	status := C.nt_timeline_sample(state.native, C.uint64_t(channel), C.NtTimeUs(elapsedUs), &out)
	return TimelineSample{Value: float64(out.value), PlayheadUs: uint64(out.playhead_us),
		SourceUs: uint64(out.source_us), Iteration: uint64(out.iteration), Phase: AnimationPhase(out.phase),
		ClipPhase: AnimationPhase(out.clip_phase), Direction: int32(out.direction)}, check(status)
}

func (state *timelineState) close() {
	state.mu.Lock()
	defer state.mu.Unlock()
	if state.native != nil {
		C.nt_timeline_destroy(state.native)
		state.native = nil
	}
}

func (timeline *Timeline) Close() {
	if timeline != nil && timeline.state != nil {
		timeline.state.close()
	}
}
