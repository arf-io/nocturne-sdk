package nocturne

/*
#include <nocturne/particle_effect.h>
*/
import "C"

import "unsafe"

// VisualBranchNoParent is the parent ID of the first trunk segment.
const VisualBranchNoParent = ^uint32(0)

// VisualBranchOptions describes a seeded visual branch in local cell coordinates.
type VisualBranchOptions struct {
	Seed, Stream                               uint64
	Start, End                                 Point
	TrunkSegments, BranchCount, BranchSegments uint32
	JitterCells, BranchReachCells              uint32
	StartUs, LifetimeUs, RevealUs, FlickerUs   uint64
	Color                                      Color
	FlickerMinAlpha                            uint8
}

func DefaultVisualBranchOptions() VisualBranchOptions {
	value := C.nt_visual_branch_options_default()
	return VisualBranchOptions{
		Seed: uint64(value.seed), Stream: uint64(value.stream),
		Start: pointFromNative(value.start), End: pointFromNative(value.end),
		TrunkSegments: uint32(value.trunk_segments), BranchCount: uint32(value.branch_count),
		BranchSegments: uint32(value.branch_segments), JitterCells: uint32(value.jitter_cells),
		BranchReachCells: uint32(value.branch_reach_cells), StartUs: uint64(value.start_us),
		LifetimeUs: uint64(value.lifetime_us), RevealUs: uint64(value.reveal_us), FlickerUs: uint64(value.flicker_us),
		Color: colorFromNative(value.color), FlickerMinAlpha: uint8(value.flicker_min_alpha),
	}
}

func (options VisualBranchOptions) native() C.NtVisualBranchOptions {
	return C.NtVisualBranchOptions{
		seed: C.uint64_t(options.Seed), stream: C.uint64_t(options.Stream),
		start: options.Start.native(), end: options.End.native(),
		trunk_segments: C.uint32_t(options.TrunkSegments), branch_count: C.uint32_t(options.BranchCount),
		branch_segments: C.uint32_t(options.BranchSegments), jitter_cells: C.uint32_t(options.JitterCells),
		branch_reach_cells: C.uint32_t(options.BranchReachCells), start_us: C.NtTimeUs(options.StartUs),
		lifetime_us: C.NtTimeUs(options.LifetimeUs), reveal_us: C.NtTimeUs(options.RevealUs),
		flicker_us: C.NtTimeUs(options.FlickerUs), color: options.Color.native(),
		flicker_min_alpha: C.uint8_t(options.FlickerMinAlpha),
	}
}

type VisualBranchSegment struct {
	ID, ParentID uint32
	Start, End   Point
	Color        Color
}

// Sample returns copied segments in trunk-first, branch-major order.
func (options VisualBranchOptions) Sample(now uint64, motion Motion) ([]VisualBranchSegment, error) {
	if err := ensureABI(); err != nil {
		return nil, err
	}
	spec := options.native()
	var count C.size_t
	if err := check(C.nt_visual_branch_sample(&spec, C.NtTimeUs(now), C.NtMotionPolicy(motion), nil, 0, &count)); err != nil || count == 0 {
		return nil, err
	}
	rows := make([]C.NtVisualBranchSegment, int(count))
	if err := check(C.nt_visual_branch_sample(&spec, C.NtTimeUs(now), C.NtMotionPolicy(motion),
		unsafe.SliceData(rows), count, &count)); err != nil {
		return nil, err
	}
	segments := make([]VisualBranchSegment, int(count))
	for i := range segments {
		row := rows[i]
		segments[i] = VisualBranchSegment{ID: uint32(row.id), ParentID: uint32(row.parent_id),
			Start: pointFromNative(row.start), End: pointFromNative(row.end), Color: colorFromNative(row.color)}
	}
	return segments, nil
}

func (options VisualBranchOptions) Deadline(now uint64, motion Motion, frameIntervalUs uint64) (Deadline, error) {
	if err := ensureABI(); err != nil {
		return Deadline{}, err
	}
	spec := options.native()
	var value C.NtDeadline
	err := check(C.nt_visual_branch_deadline(&spec, C.NtTimeUs(now), C.NtMotionPolicy(motion),
		C.NtTimeUs(frameIntervalUs), &value))
	return Deadline{Scheduled: bool(value.scheduled), At: uint64(value.at)}, err
}

// DrawVisualBranch paints the sampled segments through the component's clip.
func (context *Paint) DrawVisualBranch(options VisualBranchOptions, glyph string, width WidthPolicy, now uint64, motion Motion) error {
	if err := context.check(); err != nil {
		return err
	}
	var arena RawArena
	defer arena.close()
	ink, err := arenaByteView(&arena, glyph)
	if err != nil {
		return err
	}
	spec := options.native()
	return check(C.nt_visual_branch_draw(&spec, context.native, C.NtTimeUs(now), C.NtMotionPolicy(motion), ink, width.native()))
}
