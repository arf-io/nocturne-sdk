package nocturne

/*
#include <nocturne/blend.h>
*/
import "C"

import (
	"runtime"
	"strings"
	"unsafe"
)

type BlendMode uint32

const (
	BlendNormal       BlendMode = C.NT_BLEND_NORMAL
	BlendMultiply     BlendMode = C.NT_BLEND_MULTIPLY
	BlendScreen       BlendMode = C.NT_BLEND_SCREEN
	BlendOverlay      BlendMode = C.NT_BLEND_OVERLAY
	BlendDarken       BlendMode = C.NT_BLEND_DARKEN
	BlendLighten      BlendMode = C.NT_BLEND_LIGHTEN
	BlendColorDodge   BlendMode = C.NT_BLEND_COLOR_DODGE
	BlendColorBurn    BlendMode = C.NT_BLEND_COLOR_BURN
	BlendHardLight    BlendMode = C.NT_BLEND_HARD_LIGHT
	BlendSoftLight    BlendMode = C.NT_BLEND_SOFT_LIGHT
	BlendDifference   BlendMode = C.NT_BLEND_DIFFERENCE
	BlendExclusion    BlendMode = C.NT_BLEND_EXCLUSION
	BlendHue          BlendMode = C.NT_BLEND_HUE
	BlendSaturation   BlendMode = C.NT_BLEND_SATURATION
	BlendColor        BlendMode = C.NT_BLEND_COLOR
	BlendLuminosity   BlendMode = C.NT_BLEND_LUMINOSITY
	BlendAdd          BlendMode = C.NT_BLEND_ADD
	BlendSubtract     BlendMode = C.NT_BLEND_SUBTRACT
	BlendDivide       BlendMode = C.NT_BLEND_DIVIDE
	BlendLinearBurn   BlendMode = C.NT_BLEND_LINEAR_BURN
	BlendLinearLight  BlendMode = C.NT_BLEND_LINEAR_LIGHT
	BlendVividLight   BlendMode = C.NT_BLEND_VIVID_LIGHT
	BlendPinLight     BlendMode = C.NT_BLEND_PIN_LIGHT
	BlendHardMix      BlendMode = C.NT_BLEND_HARD_MIX
	BlendDarkerColor  BlendMode = C.NT_BLEND_DARKER_COLOR
	BlendLighterColor BlendMode = C.NT_BLEND_LIGHTER_COLOR
	BlendDissolve     BlendMode = C.NT_BLEND_DISSOLVE
)

type CompositeOperator uint32

const (
	CompositeSourceOver      CompositeOperator = C.NT_COMPOSITE_SOURCE_OVER
	CompositeSource          CompositeOperator = C.NT_COMPOSITE_SOURCE
	CompositeDestination     CompositeOperator = C.NT_COMPOSITE_DESTINATION
	CompositeClear           CompositeOperator = C.NT_COMPOSITE_CLEAR
	CompositeSourceIn        CompositeOperator = C.NT_COMPOSITE_SOURCE_IN
	CompositeSourceOut       CompositeOperator = C.NT_COMPOSITE_SOURCE_OUT
	CompositeSourceAtop      CompositeOperator = C.NT_COMPOSITE_SOURCE_ATOP
	CompositeDestinationOver CompositeOperator = C.NT_COMPOSITE_DESTINATION_OVER
	CompositeDestinationIn   CompositeOperator = C.NT_COMPOSITE_DESTINATION_IN
	CompositeDestinationOut  CompositeOperator = C.NT_COMPOSITE_DESTINATION_OUT
	CompositeDestinationAtop CompositeOperator = C.NT_COMPOSITE_DESTINATION_ATOP
	CompositeXOR             CompositeOperator = C.NT_COMPOSITE_XOR
	CompositeLighter         CompositeOperator = C.NT_COMPOSITE_LIGHTER
)

// CompositeOptions maps directly to the native options. Its zero value uses
// encoded sRGB. Start with DefaultCompositeOptions to customize the linear
// sRGB defaults shared by nil options and TypeScript's omitted fields.
type CompositeOptions struct {
	Blend             BlendMode
	Operation         CompositeOperator
	Space             ColorSpace
	Gamut             GamutPolicy
	Hue               HueInterpolation
	Seed, SampleIndex uint64
}

// DefaultCompositeOptions returns the options used by a nil options pointer.
func DefaultCompositeOptions() CompositeOptions {
	return CompositeOptions{Space: ColorLinearSRGB}
}

func (options *CompositeOptions) native() *C.NtCompositeOptions {
	if options == nil {
		return nil
	}
	return &C.NtCompositeOptions{blend: C.NtBlendMode(options.Blend), operation: C.NtCompositeOperator(options.Operation),
		space: C.NtColorSpace(options.Space), gamut: C.NtGamutPolicy(options.Gamut), hue: C.NtHueInterpolation(options.Hue),
		seed: C.uint64_t(options.Seed), sample_index: C.uint64_t(options.SampleIndex)}
}

// ParseBlendMode resolves a canonical lowercase blend name.
func ParseBlendMode(name string) (BlendMode, error) {
	if err := ensureABI(); err != nil {
		return 0, err
	}
	if strings.IndexByte(name, 0) >= 0 {
		return 0, InvalidArgument
	}
	terminated := name + "\x00"
	var out C.NtBlendMode
	status := C.nt_blend_mode_parse((*C.char)(unsafe.Pointer(unsafe.StringData(terminated))), &out)
	runtime.KeepAlive(terminated)
	return BlendMode(out), check(status)
}

// ParseCompositeOperator resolves a canonical lowercase compositing operator.
func ParseCompositeOperator(name string) (CompositeOperator, error) {
	if err := ensureABI(); err != nil {
		return 0, err
	}
	if strings.IndexByte(name, 0) >= 0 {
		return 0, InvalidArgument
	}
	terminated := name + "\x00"
	var out C.NtCompositeOperator
	status := C.nt_composite_operator_parse((*C.char)(unsafe.Pointer(unsafe.StringData(terminated))), &out)
	runtime.KeepAlive(terminated)
	return CompositeOperator(out), check(status)
}

// CompositeColor returns an unquantized color. Nil options select normal
// source-over with linear-sRGB interpolation and gamut clipping.
func CompositeColor(backdrop, source PaintColor, options *CompositeOptions) (PaintColor, error) {
	if err := ensureABI(); err != nil {
		return PaintColor{}, err
	}
	var out C.NtColor
	status := C.nt_color_composite(backdrop.native(), source.native(), options.native(), &out)
	return paintColorFromNative(out, status)
}

// CompositeRGBA returns straight encoded sRGB channels quantized once.
func CompositeRGBA(backdrop, source Color, options *CompositeOptions) (Color, error) {
	if err := ensureABI(); err != nil {
		return Color{}, err
	}
	var out C.NtRgba
	err := check(C.nt_rgba_composite(backdrop.native(), source.native(), options.native(), &out))
	return colorFromNative(out), err
}
