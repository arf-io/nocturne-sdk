package nocturne

import (
	"math"
	"testing"
)

func TestColorComposableModes(t *testing.T) {
	mode, err := ParseBlendMode("color-dodge")
	if must(t, mode, err) != BlendColorDodge {
		t.Fatalf("blend parse = %v", mode)
	}
	operator, err := ParseCompositeOperator("destination-atop")
	if must(t, operator, err) != CompositeDestinationAtop {
		t.Fatalf("operator parse = %v", operator)
	}
	if _, err := ParseBlendMode("multiply\x00other"); err != InvalidArgument {
		t.Fatalf("NUL in blend name = %v", err)
	}
	if _, err := ParseCompositeOperator("source-under"); err != UnsupportedCapability {
		t.Fatalf("unknown operator = %v", err)
	}
	black, white := Color{Alpha: 255}, Color{255, 255, 255, 128}
	defaults := DefaultCompositeOptions()
	for _, test := range []struct {
		options *CompositeOptions
		gray    uint8
	}{
		{nil, 188}, {&defaults, 188}, {&CompositeOptions{}, 128}, {&CompositeOptions{Space: ColorOKLab}, 100},
	} {
		color, err := CompositeRGBA(black, white, test.options)
		if must(t, color, err) != (Color{test.gray, test.gray, test.gray, 255}) {
			t.Fatalf("composite = %+v", color)
		}
	}
	defaults.Blend = BlendMultiply
	product, err := CompositeRGBA(Color{40, 120, 200, 255}, Color{220, 60, 30, 160}, &defaults)
	if must(t, product, err) != (Color{37, 79, 129, 255}) {
		t.Fatalf("linear multiply = %+v", product)
	}
	backdrop := PaintColor{Space: ColorSRGB, Channels: [4]float64{0, 0, 1}, Alpha: .4}
	source := PaintColor{Space: ColorSRGB, Channels: [4]float64{1, 0, 0}, Alpha: .6}
	blended, err := CompositeColor(backdrop, source, &CompositeOptions{Operation: operator, Space: ColorSRGB})
	mustOK(t, err)
	if math.Abs(blended.Alpha-.6) > 2e-15 || math.Abs(blended.Channels[0]-.6) > 2e-15 ||
		math.Abs(blended.Channels[2]-.4) > 2e-15 {
		t.Fatalf("destination atop = %+v", blended)
	}
}
