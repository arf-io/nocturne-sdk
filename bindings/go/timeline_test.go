package nocturne

import (
	"errors"
	"math"
	"testing"
)

func TestTypedTimelineClips(t *testing.T) {
	const channel = uint64(1<<63) + 17
	timeline, err := NewTimeline([]TimelineClip{
		{Channel: channel, SpanUs: 100, Tween: Tween{To: 20, DurationUs: 100}},
		{Channel: channel, SpanUs: 100, Tween: Tween{From: 20, To: 40, DurationUs: 100}},
	}, TimelineOptions{})
	mustOK(t, err)
	defer timeline.Close()
	duration, err := timeline.Duration()
	if must(t, duration, err) != 200 {
		t.Fatalf("duration = %d", duration)
	}
	sample, err := timeline.Sample(channel, 125)
	if sample := must(t, sample, err); sample.Value != 25 || sample.SourceUs != 25 || sample.PlayheadUs != 125 {
		t.Fatalf("timeline sample = %+v", sample)
	}
	timeline.Close()
	if _, err := timeline.Sample(channel, 125); !errors.Is(err, StaleReference) {
		t.Fatalf("closed timeline = %v", err)
	}
}

func TestNestedTimelineCopiesSourcesAndReportsChannels(t *testing.T) {
	const channel = uint64(1<<63) + 17
	keys := KeyframeTrack{Keys: []Keyframe{{Value: 0}, {AtUs: 200, Value: 40}}}
	timing := TrackTime{Rate: TrackRate{2, 1}, QuantumUs: 10, Edits: []TrackEdit{
		{Kind: TrackDwell, AtUs: 50, LengthUs: 20},
		{Kind: TrackSkip, AtUs: 90, LengthUs: 30},
	}}
	random := RandomTrack{To: 1, IntervalUs: 8, Seed: math.MaxUint64, Stream: channel, Smooth: true}
	randomValue, err := SampleRandomTrack(random, 16)
	mustOK(t, err)
	root := TimelineNode{Kind: TimelineSequence, DelayUs: 5, Children: []TimelineNode{
		{Kind: TimelineParallel, DelayUs: 10, Children: []TimelineNode{
			{Track: TimelineTrack{Channel: channel, SpanUs: 100, Timing: timing, Source: TimelineSource{Keyframes: &keys}}},
			{DelayUs: 5, Track: TimelineTrack{Channel: 7, SpanUs: 65, Source: TimelineSource{Random: &random}}},
		}},
		{DelayUs: 3, Track: TimelineTrack{Channel: channel, SpanUs: 40,
			Source: TimelineSource{Tween: &Tween{From: 40, To: 80, DurationUs: 40}}}},
	}}
	timeline, err := NewTimelineTree(root, TimelineOptions{DelayUs: 9, Repeat: PingPong})
	mustOK(t, err)
	defer timeline.Close()

	keys.Keys[1].Value = -1
	timing.Edits[0].LengthUs = 1
	random.IntervalUs = 0
	root.Children[0].Children = nil

	count, err := timeline.ChannelCount()
	if must(t, count, err) != 2 {
		t.Fatalf("channel count = %d", count)
	}
	for i, want := range []uint64{7, channel} {
		id, err := timeline.ChannelAt(i)
		if must(t, id, err) != want {
			t.Fatalf("channel %d = %d, want %d", i, id, want)
		}
	}
	if _, err := timeline.ChannelAt(2); err != OutOfRange {
		t.Fatalf("channel past the end = %v", err)
	}
	duration, err := timeline.Duration()
	if must(t, duration, err) != 158 {
		t.Fatalf("duration = %d", duration)
	}
	sample, err := timeline.Sample(channel, 84)
	if got := must(t, sample, err); got.SourceUs != 130 || got.Value != 26 || got.PlayheadUs != 75 {
		t.Fatalf("mapped keyframe = %+v", got)
	}
	sample, err = timeline.Sample(7, 45)
	if got := must(t, sample, err); got.SourceUs != 16 || got.Value != randomValue {
		t.Fatalf("random source = %+v, want %g", got, randomValue)
	}
	sample, err = timeline.Sample(channel, 167)
	if got := must(t, sample, err); got.Direction != -1 || got.Iteration != 1 || got.Value != 80 || got.ClipPhase != Finished {
		t.Fatalf("ping-pong turnaround = %+v", got)
	}
	timeline.Close()
	if _, err := timeline.ChannelCount(); err != StaleReference {
		t.Fatalf("closed count = %v", err)
	}
}

func TestTrackTimingScheduleAndNestedAdmission(t *testing.T) {
	rate, err := LimitTrackRate(TrackRate{5, 3}, TrackRate{}, TrackRate{3, 2})
	if must(t, rate, err) != (TrackRate{3, 2}) {
		t.Fatalf("limited rate = %+v", rate)
	}
	timing := TrackTime{Rate: TrackRate{3, 2}, QuantumUs: 5, Edits: []TrackEdit{
		{Kind: TrackDwell, AtUs: 10, LengthUs: 8},
		{Kind: TrackSkip, AtUs: 20, LengthUs: 5},
	}}
	for i, pair := range [][2]uint64{{0, 0}, {10, 10}, {15, 12}, {20, 27}} {
		out, err := MapTrackTime(timing, pair[0])
		if must(t, out, err) != pair[1] {
			t.Fatalf("case %d mapped to %d, want %d", i, out, pair[1])
		}
	}
	if _, err := MapTrackTime(timing, math.MaxUint64); err != OutOfRange {
		t.Fatalf("overflowing time = %v", err)
	}
	schedule := TrackSchedule{StaggerUs: 10, BatchSize: 3, Seed: math.MaxUint64}
	slot, err := schedule.Slot(8, math.MaxUint64, 17)
	if must(t, slot, err) != (TrackSlot{DelayUs: 20, SpanUs: 17}) {
		t.Fatalf("slot = %+v", slot)
	}
	if _, err := schedule.Slot(math.MaxUint64, 0, 0); err != OutOfRange {
		t.Fatalf("overflowing slot = %v", err)
	}

	clip := TimelineNode{Track: TimelineTrack{SpanUs: 1, Source: TimelineSource{Tween: &Tween{DurationUs: 1}}}}
	if _, err := NewTimelineTree(TimelineNode{Kind: TimelineParallel, Children: []TimelineNode{clip, clip}},
		TimelineOptions{}); err != InvalidArgument {
		t.Fatalf("overlapping clips = %v", err)
	}
	if _, err := NewTimelineTree(TimelineNode{Kind: TimelineSequence}, TimelineOptions{}); err != InvalidArgument {
		t.Fatalf("empty group = %v", err)
	}
	ambiguous := clip
	ambiguous.Track.Source.Random = &RandomTrack{IntervalUs: 1}
	if _, err := NewTimelineTree(ambiguous, TimelineOptions{}); err != InvalidArgument {
		t.Fatalf("ambiguous source = %v", err)
	}
	root := clip
	for i := 0; i < 127; i++ {
		root = TimelineNode{Kind: TimelineSequence, Children: []TimelineNode{root}}
	}
	deep, err := NewTimelineTree(root, TimelineOptions{})
	mustOK(t, err)
	deep.Close()
	if _, err := NewTimelineTree(TimelineNode{Kind: TimelineSequence, Children: []TimelineNode{root}},
		TimelineOptions{}); err != OutOfRange {
		t.Fatalf("excess nesting = %v", err)
	}
	cycle := []TimelineNode{{Kind: TimelineParallel}}
	cycle[0].Children = cycle
	if _, err := NewTimelineTree(cycle[0], TimelineOptions{}); err != OutOfRange {
		t.Fatalf("cycle = %v", err)
	}
}
