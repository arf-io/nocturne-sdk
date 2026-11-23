package nocturne

/*
#include <nocturne/text_effect.h>

extern NtResult nocturneGoTextTransform(uintptr_t handle, NtTextEffectSample *sample, NtTextPose *pose);

static NtResult go_text_transform(void *context, const NtTextEffectSample *sample, NtTextPose *pose)
{
    return nocturneGoTextTransform((uintptr_t)context, (NtTextEffectSample *)sample, pose);
}

static void go_text_set_transform(NtTextEffect *effect, uintptr_t handle)
{
    effect->transform = go_text_transform;
    effect->context = (void *)handle;
}
*/
import "C"

import (
	"runtime/cgo"
	"unsafe"
)

type TextEffectOrder uint32

const (
	TextLogical        TextEffectOrder = C.NT_TEXT_EFFECT_LOGICAL
	TextReverseLogical TextEffectOrder = C.NT_TEXT_EFFECT_REVERSE_LOGICAL
	TextVisual         TextEffectOrder = C.NT_TEXT_EFFECT_VISUAL
	TextReverseVisual  TextEffectOrder = C.NT_TEXT_EFFECT_REVERSE_VISUAL
)

type TextVisibility uint32

const (
	TextReveal  TextVisibility = C.NT_TEXT_REVEAL
	TextHide    TextVisibility = C.NT_TEXT_HIDE
	TextVisible TextVisibility = C.NT_TEXT_VISIBLE
)

type TextGlyphEffectKind uint32

const (
	TextOriginalGlyphs TextGlyphEffectKind = C.NT_TEXT_ORIGINAL_GLYPHS
	TextScramble       TextGlyphEffectKind = C.NT_TEXT_SCRAMBLE
	TextFlipboard      TextGlyphEffectKind = C.NT_TEXT_FLIPBOARD
)

type TextGlyphEffect struct {
	Kind       TextGlyphEffectKind
	Alphabet   []string
	IntervalUs uint64
	Rotations  uint32
}

type TextTrails struct {
	Count     uint32
	SpacingUs uint64
	Decay     float64
}

type TextPose struct {
	Glyph      string
	Offset     ContinuousPoint
	Opacity    float64
	HueDegrees float64
}

// TextEffectSample is a copy of a running cluster and its local clock.
// The sample and its glyph can outlive the callback.
type TextEffectSample struct {
	Cluster                        LayoutCluster
	VisualIndex, LogicalIndex      uint
	Rank, Count                    uint
	LineBegin, LineEnd             int32
	ElapsedUs, LocalUs, DurationUs uint64
	Progress, Travel               float64
	Seed, Identity                 uint64
}

type TextMotion struct {
	Travel, Orbit                             ContinuousPoint
	PhaseTurns, Cycles, RankTurns, HueDegrees float64
	Easing                                    Easing
	SeededPhase                               bool
}

func (sample TextEffectSample) poseClock() C.NtTextEffectSample {
	return C.NtTextEffectSample{rank: C.size_t(sample.Rank), local_us: C.NtTimeUs(sample.LocalUs),
		progress: C.double(sample.Progress), travel: C.double(sample.Travel),
		seed: C.uint64_t(sample.Seed), identity: C.uint64_t(sample.Identity)}
}

func (pose TextPose) nativePlacement() C.NtTextPose {
	return C.NtTextPose{offset: pose.Offset.native(), opacity: C.double(pose.Opacity), hue_degrees: C.double(pose.HueDegrees)}
}

func textPoseFromNative(glyph string, pose C.NtTextPose) TextPose {
	return TextPose{Glyph: glyph, Offset: ContinuousPoint{float64(pose.offset.x), float64(pose.offset.y)},
		Opacity: float64(pose.opacity), HueDegrees: float64(pose.hue_degrees)}
}

// Motion adds a travel-weighted translation, orbit, and hue rotation.
func (sample TextEffectSample) Motion(pose TextPose, motion TextMotion) (TextPose, error) {
	if err := ensureABI(); err != nil {
		return TextPose{}, err
	}
	clock, out := sample.poseClock(), pose.nativePlacement()
	native := C.NtTextMotion{travel: motion.Travel.native(), orbit: motion.Orbit.native(),
		phase_turns: C.double(motion.PhaseTurns), cycles: C.double(motion.Cycles),
		rank_turns: C.double(motion.RankTurns), hue_degrees: C.double(motion.HueDegrees),
		easing: C.NtEasing(motion.Easing), seeded_phase: C.bool(motion.SeededPhase)}
	if err := check(C.nt_text_pose_motion(&clock, &native, &out)); err != nil {
		return TextPose{}, err
	}
	return textPoseFromNative(pose.Glyph, out), nil
}

// Noise adds seeded, travel-weighted offsets sampled from the cluster clock.
func (sample TextEffectSample) Noise(pose TextPose, extent ContinuousPoint, intervalUs uint64, smooth bool) (TextPose, error) {
	if err := ensureABI(); err != nil {
		return TextPose{}, err
	}
	clock, out := sample.poseClock(), pose.nativePlacement()
	if err := check(C.nt_text_pose_noise(&clock, extent.native(), C.NtTimeUs(intervalUs), C.bool(smooth), &out)); err != nil {
		return TextPose{}, err
	}
	return textPoseFromNative(pose.Glyph, out), nil
}

// TextTransform updates the pose for a running cluster, including trail
// samples. Returned glyph bytes are copied for the rest of the draw.
type TextTransform func(sample TextEffectSample, pose TextPose) (TextPose, error)

type TextEffect struct {
	DelayUs, DurationUs uint64
	Schedule            TrackSchedule
	Order               TextEffectOrder
	Visibility          TextVisibility
	OpacityEasing       Easing
	Glyphs              TextGlyphEffect
	Seed                uint64
	Trails              TextTrails
	Transform           TextTransform
}

func (effect TextEffect) native(arena *RawArena) (C.NtTextEffect, error) {
	glyphs := effect.Glyphs
	alphabet, err := nativeSlice[C.NtByteView](arena, len(glyphs.Alphabet))
	if err != nil {
		return C.NtTextEffect{}, err
	}
	for index, glyph := range glyphs.Alphabet {
		bytes, err := arenaByteView(arena, glyph)
		if err != nil {
			return C.NtTextEffect{}, err
		}
		alphabet[index] = bytes
	}
	return C.NtTextEffect{
		delay_us: C.NtTimeUs(effect.DelayUs), duration_us: C.NtTimeUs(effect.DurationUs),
		schedule: effect.Schedule.native(), order: C.NtTextEffectOrder(effect.Order),
		visibility: C.NtTextVisibility(effect.Visibility), opacity_easing: C.NtEasing(effect.OpacityEasing),
		glyphs: C.NtTextGlyphEffect{kind: C.NtTextGlyphEffectKind(glyphs.Kind), alphabet: unsafe.SliceData(alphabet),
			count: C.size_t(len(alphabet)), interval_us: C.NtTimeUs(glyphs.IntervalUs), rotations: C.uint32_t(glyphs.Rotations)},
		seed: C.uint64_t(effect.Seed), trails: C.NtTextTrails{count: C.uint32_t(effect.Trails.Count),
			spacing_us: C.NtTimeUs(effect.Trails.SpacingUs), decay: C.double(effect.Trails.Decay)},
	}, nil
}

type textTransformCall struct {
	arena     *RawArena
	transform TextTransform
	cause     error
}

//export nocturneGoTextTransform
func nocturneGoTextTransform(handle C.uintptr_t, sample *C.NtTextEffectSample, out *C.NtTextPose) (status C.NtResult) {
	call := cgo.Handle(handle).Value().(*textTransformCall)
	defer func() {
		if cause := recover(); cause != nil {
			call.cause = &PanicError{Value: cause}
			status = C.NT_RESULT_CALLBACK_FAILED
		}
	}()
	cluster, err := layoutClusterFromNative(sample.cluster)
	if err != nil {
		return callbackResult(err)
	}
	glyph, err := copyBytes(out.glyph)
	if err != nil {
		return callbackResult(err)
	}
	pose, err := call.transform(TextEffectSample{
		Cluster: cluster, VisualIndex: uint(sample.visual_index), LogicalIndex: uint(sample.logical_index),
		Rank: uint(sample.rank), Count: uint(sample.count),
		LineBegin: int32(sample.line_begin), LineEnd: int32(sample.line_end),
		ElapsedUs: uint64(sample.elapsed_us), LocalUs: uint64(sample.local_us), DurationUs: uint64(sample.duration_us),
		Progress: float64(sample.progress), Travel: float64(sample.travel),
		Seed: uint64(sample.seed), Identity: uint64(sample.identity),
	}, textPoseFromNative(glyph, *out))
	if err != nil {
		call.cause = err
		return callbackResult(err)
	}
	native := pose.nativePlacement()
	native.glyph = out.glyph
	if pose.Glyph != glyph {
		native.glyph, err = arenaByteView(call.arena, pose.Glyph)
		if err != nil {
			return callbackResult(err)
		}
	}
	*out = native
	return C.NT_RESULT_OK
}

func drawTextEffect(arena *RawArena, layout *TextLayout, options TextPaintOptions, effect TextEffect,
	draw func(*C.NtTextLayout, *C.NtTextPaintOptions, *C.NtTextEffect) C.NtResult) error {
	text, release, err := layout.borrow()
	if err != nil {
		return err
	}
	defer release()
	paint, err := options.native(arena)
	if err != nil {
		return err
	}
	native, err := effect.native(arena)
	if err != nil {
		return err
	}
	if effect.Transform == nil {
		return check(draw(text, &paint, &native))
	}
	call := textTransformCall{arena: arena, transform: effect.Transform}
	handle := cgo.NewHandle(&call)
	defer handle.Delete()
	C.go_text_set_transform(&native, C.uintptr_t(handle))
	err = check(draw(text, &paint, &native))
	if call.cause != nil {
		return &CallError{Native: err, Callbacks: []CallbackFailure{{Phase: "paint", Cause: call.cause}}}
	}
	return err
}

// PaintTextEffect evaluates and paints at the given caller time. Any error
// preserves the destination, including errors from a transform.
func (surface Surface) PaintTextEffect(at Point, layout *TextLayout, options TextPaintOptions, effect TextEffect) error {
	return surface.withView(func(arena *RawArena, view *C.NtSurfaceView) error {
		return drawTextEffect(arena, layout, options, effect, func(text *C.NtTextLayout, paint *C.NtTextPaintOptions, native *C.NtTextEffect) C.NtResult {
			return C.nt_text_effect_paint(view, at.native(), text, paint, native)
		})
	})
}

// PaintTextEffect draws in the component clip and uses the mounted owner.
func (context *Paint) PaintTextEffect(at Point, layout *TextLayout, options TextPaintOptions, effect TextEffect) error {
	if err := context.check(); err != nil {
		return err
	}
	context.scope.drawing = true
	defer func() { context.scope.drawing = false }()
	var arena RawArena
	defer arena.close()
	return drawTextEffect(&arena, layout, options, effect, func(text *C.NtTextLayout, paint *C.NtTextPaintOptions, native *C.NtTextEffect) C.NtResult {
		return C.nt_paint_text_effect(context.native, at.native(), text, paint, native)
	})
}
