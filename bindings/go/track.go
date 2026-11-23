package nocturne

/*
#include <nocturne/timeline.h>
*/
import "C"

import "unsafe"

// TrackRate is a nonnegative rational multiplier. The zero value means 1:1.
type TrackRate struct{ Numerator, Denominator uint32 }

func (rate TrackRate) native() C.NtTrackRate {
	return C.NtTrackRate{numerator: C.uint32_t(rate.Numerator), denominator: C.uint32_t(rate.Denominator)}
}

func LimitTrackRate(requested, minimum, maximum TrackRate) (TrackRate, error) {
	if err := ensureABI(); err != nil {
		return TrackRate{}, err
	}
	var out C.NtTrackRate
	status := C.nt_track_rate_limit(requested.native(), minimum.native(), maximum.native(), &out)
	return TrackRate{uint32(out.numerator), uint32(out.denominator)}, check(status)
}

type TrackEditKind uint32

const (
	TrackDwell TrackEditKind = C.NT_TRACK_DWELL
	TrackSkip  TrackEditKind = C.NT_TRACK_SKIP
)

type TrackEdit struct {
	Kind           TrackEditKind
	AtUs, LengthUs uint64
}

// TrackTime scales time, rounds down to the quantum and applies ordered edits.
type TrackTime struct {
	Rate      TrackRate
	Edits     []TrackEdit
	QuantumUs uint64
}

func (timing TrackTime) native(arena *RawArena) (C.NtTrackTime, error) {
	edits, err := nativeSlice[C.NtTrackEdit](arena, len(timing.Edits))
	if err != nil {
		return C.NtTrackTime{}, err
	}
	for i, edit := range timing.Edits {
		edits[i] = C.NtTrackEdit{kind: C.NtTrackEditKind(edit.Kind), at_us: C.NtTimeUs(edit.AtUs),
			length_us: C.NtTimeUs(edit.LengthUs)}
	}
	return C.NtTrackTime{rate: timing.Rate.native(), edits: unsafe.SliceData(edits),
		count: C.size_t(len(edits)), quantum_us: C.NtTimeUs(timing.QuantumUs)}, nil
}

func MapTrackTime(timing TrackTime, elapsedUs uint64) (uint64, error) {
	if err := ensureABI(); err != nil {
		return 0, err
	}
	var arena RawArena
	defer arena.close()
	native, err := timing.native(&arena)
	if err != nil {
		return 0, err
	}
	var out C.NtTimeUs
	status := C.nt_track_time(&native, C.NtTimeUs(elapsedUs), &out)
	return uint64(out), check(status)
}

// RandomTrack describes a deterministic, seeded source sampled in local time.
type RandomTrack struct {
	From, To                 float64
	IntervalUs, Seed, Stream uint64
	Smooth                   bool
}

func (track RandomTrack) native() C.NtRandomTrack {
	return C.NtRandomTrack{from: C.double(track.From), to: C.double(track.To),
		interval_us: C.NtTimeUs(track.IntervalUs), seed: C.uint64_t(track.Seed),
		stream: C.uint64_t(track.Stream), smooth: C.bool(track.Smooth)}
}

func SampleRandomTrack(track RandomTrack, atUs uint64) (float64, error) {
	if err := ensureABI(); err != nil {
		return 0, err
	}
	native := track.native()
	var out C.double
	status := C.nt_random_track_sample(&native, C.NtTimeUs(atUs), &out)
	return float64(out), check(status)
}

type TrackSchedule struct {
	StaggerUs, JitterUs, DurationVariationUs uint64
	BatchSize, Seed                          uint64
}

type TrackSlot struct{ DelayUs, SpanUs uint64 }

func (schedule TrackSchedule) native() C.NtTrackSchedule {
	return C.NtTrackSchedule{stagger_us: C.NtTimeUs(schedule.StaggerUs),
		jitter_us: C.NtTimeUs(schedule.JitterUs), duration_variation_us: C.NtTimeUs(schedule.DurationVariationUs),
		batch_size: C.uint64_t(schedule.BatchSize), seed: C.uint64_t(schedule.Seed)}
}

// Slot derives a slot from the caller's rank and stable item identity.
func (schedule TrackSchedule) Slot(rank, item, spanUs uint64) (TrackSlot, error) {
	if err := ensureABI(); err != nil {
		return TrackSlot{}, err
	}
	native := schedule.native()
	var out C.NtTrackSlot
	status := C.nt_track_schedule(&native, C.uint64_t(rank), C.uint64_t(item), C.NtTimeUs(spanUs), &out)
	return TrackSlot{uint64(out.delay_us), uint64(out.span_us)}, check(status)
}
