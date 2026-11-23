package nocturne

/*
#include <nocturne/paint_sample.h>

static void go_paint_solid(NtPaint *paint, NtColor color) { paint->content.solid = color; }
static void go_paint_gradient(NtPaint *paint, NtGradient gradient) { paint->content.gradient = gradient; }
static void go_paint_palette(NtPaint *paint, NtPaletteRamp palette) { paint->content.palette = palette; }
*/
import "C"

import "unsafe"

type ColorSpace uint32

const (
	ColorSRGB       ColorSpace = C.NT_COLOR_SRGB
	ColorLinearSRGB ColorSpace = C.NT_COLOR_LINEAR_SRGB
	ColorHSL        ColorSpace = C.NT_COLOR_HSL
	ColorHSV        ColorSpace = C.NT_COLOR_HSV
	ColorOKLab      ColorSpace = C.NT_COLOR_OKLAB
	ColorOKLCH      ColorSpace = C.NT_COLOR_OKLCH
	ColorCMYK       ColorSpace = C.NT_COLOR_CMYK
)

type GamutPolicy uint32

const (
	GamutClip         GamutPolicy = C.NT_GAMUT_CLIP
	GamutReduceChroma GamutPolicy = C.NT_GAMUT_REDUCE_CHROMA
	GamutReject       GamutPolicy = C.NT_GAMUT_REJECT
)

type HueInterpolation uint32

const (
	HueShorter    HueInterpolation = C.NT_HUE_SHORTER
	HueLonger     HueInterpolation = C.NT_HUE_LONGER
	HueIncreasing HueInterpolation = C.NT_HUE_INCREASING
	HueDecreasing HueInterpolation = C.NT_HUE_DECREASING
)

type AlphaInterpolation uint32

const (
	AlphaPremultiplied AlphaInterpolation = C.NT_ALPHA_PREMULTIPLIED
	AlphaStraight      AlphaInterpolation = C.NT_ALPHA_STRAIGHT
)

// PaintColor is an unquantized color with straight alpha and a channel order
// determined by its color space. Only CMYK uses the fourth channel.
type PaintColor struct {
	Space    ColorSpace
	Channels [4]float64
	Alpha    float64
}

func (color PaintColor) native() C.NtColor {
	value := C.NtColor{space: C.NtColorSpace(color.Space), alpha: C.double(color.Alpha)}
	for i, channel := range color.Channels {
		value.channel[i] = C.double(channel)
	}
	return value
}

func paintColorFromNative(value C.NtColor, status C.NtResult) (PaintColor, error) {
	if err := check(status); err != nil {
		return PaintColor{}, err
	}
	color := PaintColor{Space: ColorSpace(value.space), Alpha: float64(value.alpha)}
	for i, channel := range value.channel {
		color.Channels[i] = float64(channel)
	}
	return color, nil
}

// PaintColorFromRGBA converts encoded sRGB bytes to normalized channels.
func PaintColorFromRGBA(color Color) PaintColor {
	value, _ := paintColorFromNative(C.nt_color_from_rgba(color.native()), C.NT_RESULT_OK)
	return value
}

func (color PaintColor) Convert(space ColorSpace, gamut GamutPolicy) (PaintColor, error) {
	if err := ensureABI(); err != nil {
		return PaintColor{}, err
	}
	var out C.NtColor
	status := C.nt_color_convert(color.native(), C.NtColorSpace(space), C.NtGamutPolicy(gamut), &out)
	return paintColorFromNative(out, status)
}

func (color PaintColor) RGBA(gamut GamutPolicy) (Color, error) {
	if err := ensureABI(); err != nil {
		return Color{}, err
	}
	var out C.NtRgba
	status := C.nt_color_to_rgba(color.native(), C.NtGamutPolicy(gamut), &out)
	return colorFromNative(out), check(status)
}

type ColorInterpolation struct {
	Space ColorSpace
	Gamut GamutPolicy
	Hue   HueInterpolation
	Alpha AlphaInterpolation
}

func (value ColorInterpolation) native() C.NtColorInterpolation {
	return C.NtColorInterpolation{space: C.NtColorSpace(value.Space), gamut: C.NtGamutPolicy(value.Gamut),
		hue: C.NtHueInterpolation(value.Hue), alpha: C.NtAlphaInterpolation(value.Alpha)}
}

func MixPaintColor(a, b PaintColor, progress float64, interpolation ColorInterpolation) (PaintColor, error) {
	if err := ensureABI(); err != nil {
		return PaintColor{}, err
	}
	var out C.NtColor
	status := C.nt_color_mix(a.native(), b.native(), C.double(progress), interpolation.native(), &out)
	return paintColorFromNative(out, status)
}

type PaintAddress uint32

const (
	PaintPad     PaintAddress = C.NT_PAINT_PAD
	PaintRepeat  PaintAddress = C.NT_PAINT_REPEAT
	PaintReflect PaintAddress = C.NT_PAINT_REFLECT
)

type GradientStop struct {
	Position float64
	Color    PaintColor
	Easing   Easing
}

type Gradient struct {
	Stops         []GradientStop
	Address       PaintAddress
	Interpolation ColorInterpolation
}

func (gradient Gradient) native(arena *RawArena) (C.NtGradient, error) {
	stops, err := nativeSlice[C.NtGradientStop](arena, len(gradient.Stops))
	if err != nil {
		return C.NtGradient{}, err
	}
	for i, stop := range gradient.Stops {
		stops[i] = C.NtGradientStop{position: C.double(stop.Position), color: stop.Color.native(),
			from_here: C.NtEasing(stop.Easing)}
	}
	return C.NtGradient{stops: unsafe.SliceData(stops), count: C.size_t(len(stops)),
		address: C.NtPaintAddress(gradient.Address), interpolation: gradient.Interpolation.native()}, nil
}

func (gradient Gradient) Sample(coordinate float64) (PaintColor, error) {
	if err := ensureABI(); err != nil {
		return PaintColor{}, err
	}
	var arena RawArena
	defer arena.close()
	native, err := gradient.native(&arena)
	if err != nil {
		return PaintColor{}, err
	}
	var out C.NtColor
	status := C.nt_gradient_sample(&native, C.double(coordinate), &out)
	return paintColorFromNative(out, status)
}

type PaletteFilter uint32

const (
	PaletteLinear PaletteFilter = C.NT_PALETTE_LINEAR
	PaletteStep   PaletteFilter = C.NT_PALETTE_STEP
)

type Palette []Color

func (palette Palette) native(arena *RawArena) (C.NtPaletteView, error) {
	colors, err := nativeSlice[C.NtRgba](arena, len(palette))
	if err != nil {
		return C.NtPaletteView{}, err
	}
	for i, color := range palette {
		colors[i] = color.native()
	}
	return C.NtPaletteView{colors: unsafe.SliceData(colors), count: C.size_t(len(colors))}, nil
}

func (palette Palette) Lookup(index uint) (Color, error) {
	if err := ensureABI(); err != nil {
		return Color{}, err
	}
	var arena RawArena
	defer arena.close()
	native, err := palette.native(&arena)
	if err != nil {
		return Color{}, err
	}
	var out C.NtRgba
	status := C.nt_palette_lookup(native, C.size_t(index), &out)
	return colorFromNative(out), check(status)
}

func (palette Palette) Nearest(color PaintColor, gamut GamutPolicy) (uint, error) {
	if err := ensureABI(); err != nil {
		return 0, err
	}
	var arena RawArena
	defer arena.close()
	native, err := palette.native(&arena)
	if err != nil {
		return 0, err
	}
	var out C.size_t
	status := C.nt_palette_nearest(native, color.native(), C.NtGamutPolicy(gamut), &out)
	return uint(out), check(status)
}

type PaletteRamp struct {
	Palette       Palette
	Filter        PaletteFilter
	Address       PaintAddress
	Interpolation ColorInterpolation
	Easing        Easing
}

func (ramp PaletteRamp) native(arena *RawArena) (C.NtPaletteRamp, error) {
	palette, err := ramp.Palette.native(arena)
	if err != nil {
		return C.NtPaletteRamp{}, err
	}
	return C.NtPaletteRamp{palette: palette, filter: C.NtPaletteFilter(ramp.Filter),
		address: C.NtPaintAddress(ramp.Address), interpolation: ramp.Interpolation.native(),
		easing: C.NtEasing(ramp.Easing)}, nil
}

func (ramp PaletteRamp) Sample(coordinate float64) (PaintColor, error) {
	if err := ensureABI(); err != nil {
		return PaintColor{}, err
	}
	var arena RawArena
	defer arena.close()
	native, err := ramp.native(&arena)
	if err != nil {
		return PaintColor{}, err
	}
	var out C.NtColor
	status := C.nt_palette_sample(&native, C.double(coordinate), &out)
	return paintColorFromNative(out, status)
}

type PaintPattern uint32

const (
	PaintLinear       PaintPattern = C.NT_PAINT_LINEAR
	PaintRadial       PaintPattern = C.NT_PAINT_RADIAL
	PaintConic        PaintPattern = C.NT_PAINT_CONIC
	PaintWave         PaintPattern = C.NT_PAINT_WAVE
	PaintPlasma       PaintPattern = C.NT_PAINT_PLASMA
	PaintMoire        PaintPattern = C.NT_PAINT_MOIRE
	PaintAurora       PaintPattern = C.NT_PAINT_AURORA
	PaintInterference PaintPattern = C.NT_PAINT_INTERFERENCE
	PaintChevrons     PaintPattern = C.NT_PAINT_CHEVRONS
	PaintDiamond      PaintPattern = C.NT_PAINT_DIAMOND
	PaintChecker      PaintPattern = C.NT_PAINT_CHECKER
	PaintUniform      PaintPattern = C.NT_PAINT_UNIFORM
)

type PaintRotation uint32

const (
	PaintFixed PaintRotation = C.NT_PAINT_FIXED
	PaintSpin  PaintRotation = C.NT_PAINT_SPIN
	PaintSwing PaintRotation = C.NT_PAINT_SWING
)

// PaintField defines the coordinate and clock of a procedural sampler.
// Both scale axes must be positive, including for a uniform field.
type PaintField struct {
	Pattern          PaintPattern
	Origin, Scale    ContinuousPoint
	AngleDegrees     float64
	Rotation         PaintRotation
	RotationPeriodUs uint64
	SwingDegrees     float64
	ReverseRotation  bool
	Phase            float64
	WaveAmplitude    float64
	PhaseTrack       *Tween
}

func (field PaintField) native(arena *RawArena) (C.NtPaintField, error) {
	result := C.NtPaintField{pattern: C.NtPaintPattern(field.Pattern),
		origin_x: C.double(field.Origin.X), origin_y: C.double(field.Origin.Y),
		scale_x: C.double(field.Scale.X), scale_y: C.double(field.Scale.Y),
		angle_degrees: C.double(field.AngleDegrees), rotation: C.NtPaintRotation(field.Rotation),
		rotation_period_us: C.NtTimeUs(field.RotationPeriodUs), swing_degrees: C.double(field.SwingDegrees),
		reverse_rotation: C.bool(field.ReverseRotation), phase: C.double(field.Phase),
		wave_amplitude: C.double(field.WaveAmplitude)}
	if field.PhaseTrack != nil {
		track, err := nativeSlice[C.NtTween](arena, 1)
		if err != nil {
			return C.NtPaintField{}, err
		}
		track[0] = field.PhaseTrack.native()
		result.phase_track = &track[0]
	}
	return result, nil
}

type PaintSample struct {
	Position  ContinuousPoint
	ElapsedUs uint64
	Motion    Motion
}

func (sample PaintSample) native() C.NtPaintSample {
	return C.NtPaintSample{x: C.double(sample.Position.X), y: C.double(sample.Position.Y),
		elapsed_us: C.NtTimeUs(sample.ElapsedUs), motion: C.NtMotionPolicy(sample.Motion)}
}

func (field PaintField) Coordinate(sample PaintSample) (float64, error) {
	if err := ensureABI(); err != nil {
		return 0, err
	}
	var arena RawArena
	defer arena.close()
	native, err := field.native(&arena)
	if err != nil {
		return 0, err
	}
	var out C.double
	status := C.nt_paint_coordinate(&native, sample.native(), &out)
	return float64(out), check(status)
}

type PaintKind uint32

const (
	PaintSolid    PaintKind = C.NT_PAINT_SOLID
	PaintGradient PaintKind = C.NT_PAINT_GRADIENT
	PaintPalette  PaintKind = C.NT_PAINT_PALETTE
)

// PaintSource describes a sampled color. Opacity is a straight-alpha
// multiplier in [0,1]. One selects full opacity.
type PaintSource struct {
	Kind         PaintKind
	Solid        PaintColor
	Gradient     Gradient
	Palette      PaletteRamp
	Field        PaintField
	Opacity      float64
	OpacityTrack *KeyframeTrack
}

func (paint *PaintSource) native(arena *RawArena) (*C.NtPaint, error) {
	if paint == nil {
		return nil, nil
	}
	source, err := nativeSlice[C.NtPaint](arena, 1)
	if err != nil {
		return nil, err
	}
	native := &source[0]
	native.kind = C.NtPaintKind(paint.Kind)
	native.opacity = C.double(paint.Opacity)
	switch paint.Kind {
	case PaintSolid:
		C.go_paint_solid(native, paint.Solid.native())
	case PaintGradient:
		gradient, err := paint.Gradient.native(arena)
		if err != nil {
			return nil, err
		}
		C.go_paint_gradient(native, gradient)
	case PaintPalette:
		ramp, err := paint.Palette.native(arena)
		if err != nil {
			return nil, err
		}
		C.go_paint_palette(native, ramp)
	}
	if paint.Kind == PaintGradient || paint.Kind == PaintPalette {
		native.field, err = paint.Field.native(arena)
		if err != nil {
			return nil, err
		}
	}
	if track := paint.OpacityTrack; track != nil {
		keys, err := nativeSlice[C.NtKeyframe](arena, len(track.Keys))
		if err != nil {
			return nil, err
		}
		for i, key := range track.Keys {
			keys[i] = key.native()
		}
		record, err := nativeSlice[C.NtKeyframeTrack](arena, 1)
		if err != nil {
			return nil, err
		}
		record[0] = C.NtKeyframeTrack{keys: unsafe.SliceData(keys), count: C.size_t(len(keys)),
			delay_us: C.NtTimeUs(track.DelayUs), repeat: C.NtRepeat(track.Repeat)}
		native.opacity_track = &record[0]
	}
	return native, nil
}

func (paint PaintSource) Sample(sample PaintSample) (PaintColor, error) {
	if err := ensureABI(); err != nil {
		return PaintColor{}, err
	}
	var arena RawArena
	defer arena.close()
	native, err := paint.native(&arena)
	if err != nil {
		return PaintColor{}, err
	}
	var out C.NtColor
	status := C.nt_paint_sample(native, sample.native(), &out)
	return paintColorFromNative(out, status)
}

func (paint PaintSource) SampleRGBA(sample PaintSample, gamut GamutPolicy) (Color, error) {
	if err := ensureABI(); err != nil {
		return Color{}, err
	}
	var arena RawArena
	defer arena.close()
	native, err := paint.native(&arena)
	if err != nil {
		return Color{}, err
	}
	var out C.NtRgba
	status := C.nt_paint_sample_rgba(native, sample.native(), C.NtGamutPolicy(gamut), &out)
	return colorFromNative(out), check(status)
}
