package nocturne

import (
	"math"
	"testing"
)

func TestOpticalFieldSamplingAndClock(t *testing.T) {
	value, err := OpticalInverseQuadratic.Sample(0x1p530)
	if must(t, value, err) != 0x1p-1060 {
		t.Fatalf("optical tail = %g", value)
	}
	ripple := DefaultRippleField()
	ripple.Origin = ContinuousPoint{10, 10}
	ripple.Aspect = ContinuousPoint{2, 1}
	ripple.StartUs, ripple.LifetimeUs, ripple.SpacingUs, ripple.Rings = 100, 100, 25, 3
	ripple.LastRadius, ripple.Falloff, ripple.Fade = 8, OpticalLinear, Linear
	field := Field{Ripple: &ripple}
	query := FieldQuery{Position: ContinuousPoint{12, 10}, Now: 150}
	sample, err := field.Sample(query)
	if must(t, sample, err) != (FieldSample{.5, .5}) {
		t.Fatalf("ripple sample = %+v", sample)
	}
	ink := FieldInk{Color: Color{Red: 200, Alpha: 255}, Opacity: 1}
	color, err := field.Color(ink, query)
	if must(t, color, err) != (Color{Red: 200, Alpha: 128}) {
		t.Fatalf("ripple color = %+v", color)
	}
	deadline, err := field.Deadline(150, MotionReduced, 19)
	if must(t, deadline, err) != (Deadline{true, 200}) {
		t.Fatalf("reduced-motion deadline = %+v", deadline)
	}
	ripple.StartUs, ripple.SpacingUs = math.MaxUint64-10, 10
	deadline, err = field.Deadline(math.MaxUint64-1, MotionReduced, 1)
	if must(t, deadline, err) != (Deadline{true, math.MaxUint64}) {
		t.Fatalf("ceiling deadline = %+v", deadline)
	}
	ripple.Rings = 33
	if _, err := field.Sample(query); err != OutOfRange {
		t.Fatalf("oversized ring count = %v", err)
	}
	if _, err := (Field{}).Sample(query); err != InvalidArgument {
		t.Fatalf("empty field = %v", err)
	}
}

func TestMountedOpticalFieldPaint(t *testing.T) {
	wave := DefaultWaveField()
	wave.PeriodUs = 0
	wave.CyclesPerUnit = ContinuousPoint{}
	wave.PhaseTurns = .25
	field := Field{Wave: &wave}
	ink := FieldInk{Color: Color{Alpha: 255}, Opacity: 1, Palette: &PaletteRamp{
		Palette: Palette{{Alpha: 255}, {Red: 200, Alpha: 255}}, Filter: PaletteStep,
	}}
	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	var saved *Paint
	bounds := Rect{Origin: Point{X: 1}, Size: Size{Columns: 8, Rows: 1}}
	_, err = tree.Mount(Node{}, MountOptions{Enabled: true}, &panelPaintFixture{draw: func(paint *Paint) error {
		saved = paint
		return paint.DrawField(bounds, field, ink, 0, MotionFull)
	}})
	mustOK(t, err)
	mustOK(t, tree.Publish(Size{3, 1}))
	frame, err := tree.Frame()
	mustOK(t, err)
	outside, err := frame.Background(Point{})
	if must(t, outside, err).Owner != 0 {
		t.Fatalf("field covered outside bounds: %+v", outside)
	}
	for _, x := range []int32{1, 2} {
		inside, err := frame.Background(Point{X: x})
		if must(t, inside, err).Color != (Color{Red: 200, Alpha: 255}) || inside.Owner == 0 {
			t.Fatalf("field background = %+v", inside)
		}
	}
	if err := saved.DrawField(bounds, field, ink, 0, MotionFull); err != StaleReference {
		t.Fatalf("escaped paint = %v", err)
	}
}
