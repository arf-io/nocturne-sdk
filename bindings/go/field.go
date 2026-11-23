package nocturne

/*
#include <nocturne/field.h>

static void go_field_ripple(NtField *field, NtRippleField ripple)
{
    field->kind = NT_FIELD_RIPPLE;
    field->as.ripple = ripple;
}

static void go_field_wave(NtField *field, NtWaveField wave)
{
    field->kind = NT_FIELD_WAVE;
    field->as.wave = wave;
}
*/
import "C"

type OpticalFalloff uint32

const (
	OpticalLinear           OpticalFalloff = C.NT_OPTICAL_LINEAR
	OpticalGaussian         OpticalFalloff = C.NT_OPTICAL_GAUSSIAN
	OpticalInverseQuadratic OpticalFalloff = C.NT_OPTICAL_INVERSE_QUADRATIC
)

func (falloff OpticalFalloff) Sample(distance float64) (float64, error) {
	var out C.double
	err := check(C.nt_optical_falloff(C.NtOpticalFalloff(falloff), C.double(distance), &out))
	return float64(out), err
}

type RippleField struct {
	Origin, Aspect                       ContinuousPoint
	FirstRadius, LastRadius, Width, Peak float64
	StartUs, LifetimeUs, SpacingUs       uint64
	Rings                                uint32
	Falloff                              OpticalFalloff
	Fade                                 Easing
}

func DefaultRippleField() RippleField {
	ripple := C.nt_ripple_field_default()
	return RippleField{
		Origin: plotPointFromNative(ripple.origin), Aspect: plotPointFromNative(ripple.aspect),
		FirstRadius: float64(ripple.first_radius), LastRadius: float64(ripple.last_radius),
		Width: float64(ripple.width), Peak: float64(ripple.peak), StartUs: uint64(ripple.start_us),
		LifetimeUs: uint64(ripple.lifetime_us), SpacingUs: uint64(ripple.spacing_us), Rings: uint32(ripple.rings),
		Falloff: OpticalFalloff(ripple.falloff), Fade: Easing(ripple.fade),
	}
}

func (ripple RippleField) native() C.NtRippleField {
	return C.NtRippleField{origin: ripple.Origin.native(), aspect: ripple.Aspect.native(),
		first_radius: C.double(ripple.FirstRadius), last_radius: C.double(ripple.LastRadius),
		width: C.double(ripple.Width), peak: C.double(ripple.Peak),
		start_us: C.NtTimeUs(ripple.StartUs), lifetime_us: C.NtTimeUs(ripple.LifetimeUs),
		spacing_us: C.NtTimeUs(ripple.SpacingUs), rings: C.uint32_t(ripple.Rings),
		falloff: C.NtOpticalFalloff(ripple.Falloff), fade: C.NtEasing(ripple.Fade)}
}

type WaveField struct {
	Origin, CyclesPerUnit ContinuousPoint
	PhaseTurns            float64
	StartUs, PeriodUs     uint64
}

func DefaultWaveField() WaveField {
	wave := C.nt_wave_field_default()
	return WaveField{Origin: plotPointFromNative(wave.origin), CyclesPerUnit: plotPointFromNative(wave.cycles_per_unit),
		PhaseTurns: float64(wave.phase_turns), StartUs: uint64(wave.start_us), PeriodUs: uint64(wave.period_us)}
}

func (wave WaveField) native() C.NtWaveField {
	return C.NtWaveField{origin: wave.Origin.native(), cycles_per_unit: wave.CyclesPerUnit.native(),
		phase_turns: C.double(wave.PhaseTurns), start_us: C.NtTimeUs(wave.StartUs), period_us: C.NtTimeUs(wave.PeriodUs)}
}

// Field selects one caller-owned optical ripple or analytic wave descriptor.
type Field struct {
	Ripple *RippleField
	Wave   *WaveField
}

func (field Field) native() (C.NtField, error) {
	var out C.NtField
	switch {
	case field.Ripple != nil && field.Wave == nil:
		C.go_field_ripple(&out, field.Ripple.native())
	case field.Wave != nil && field.Ripple == nil:
		C.go_field_wave(&out, field.Wave.native())
	default:
		return out, InvalidArgument
	}
	return out, nil
}

type FieldQuery struct {
	Position ContinuousPoint
	Now      uint64
	Motion   Motion
}

func (query FieldQuery) native() C.NtFieldQuery {
	return C.NtFieldQuery{position: query.Position.native(), now: C.NtTimeUs(query.Now), motion: C.NtMotionPolicy(query.Motion)}
}

type FieldSample struct {
	Value, Coverage float64
}

func (field Field) Sample(query FieldQuery) (FieldSample, error) {
	native, err := field.native()
	if err != nil {
		return FieldSample{}, err
	}
	var out C.NtFieldSample
	err = check(C.nt_field_sample(&native, query.native(), &out))
	return FieldSample{Value: float64(out.value), Coverage: float64(out.coverage)}, err
}

type FieldInk struct {
	Color   Color
	Palette *PaletteRamp
	Opacity float64
}

func (ink FieldInk) native(arena *RawArena) (C.NtFieldInk, error) {
	out := C.NtFieldInk{color: ink.Color.native(), opacity: C.double(ink.Opacity)}
	if ink.Palette == nil {
		return out, nil
	}
	storage, err := nativeSlice[C.NtPaletteRamp](arena, 1)
	if err != nil {
		return out, err
	}
	storage[0], err = ink.Palette.native(arena)
	out.palette = &storage[0]
	return out, err
}

func (field Field) Color(ink FieldInk, query FieldQuery) (Color, error) {
	nativeField, err := field.native()
	if err != nil {
		return Color{}, err
	}
	var arena RawArena
	defer arena.close()
	nativeInk, err := ink.native(&arena)
	if err != nil {
		return Color{}, err
	}
	var out C.NtRgba
	err = check(C.nt_field_color(&nativeField, &nativeInk, query.native(), &out))
	return colorFromNative(out), err
}

// Deadline returns the next caller-visible time for a field boundary or active sample.
func (field Field) Deadline(now uint64, motion Motion, frameIntervalUs uint64) (Deadline, error) {
	native, err := field.native()
	if err != nil {
		return Deadline{}, err
	}
	var out C.NtDeadline
	err = check(C.nt_field_deadline(&native, C.NtTimeUs(now), C.NtMotionPolicy(motion), C.NtTimeUs(frameIntervalUs), &out))
	return Deadline{Scheduled: bool(out.scheduled), At: uint64(out.at)}, err
}

// DrawField fills local backgrounds within the current mounted paint clip.
func (context *Paint) DrawField(bounds Rect, field Field, ink FieldInk, now uint64, motion Motion) error {
	if err := context.check(); err != nil {
		return err
	}
	nativeField, err := field.native()
	if err != nil {
		return err
	}
	var arena RawArena
	defer arena.close()
	nativeInk, err := ink.native(&arena)
	if err != nil {
		return err
	}
	return check(C.nt_field_draw(&nativeField, &nativeInk, context.native, bounds.native(),
		C.NtTimeUs(now), C.NtMotionPolicy(motion)))
}
