package nocturne

import (
	"math"
	"testing"
)

func TestPaintColorAndSamples(t *testing.T) {
	black, white, red := Color{Alpha: 255}, Color{255, 255, 255, 255}, Color{Red: 255, Alpha: 255}
	a, b := PaintColorFromRGBA(black), PaintColorFromRGBA(white)
	mixed, err := MixPaintColor(a, b, .5, ColorInterpolation{Space: ColorLinearSRGB})
	rgba, err := must(t, mixed, err).RGBA(GamutClip)
	if got := must(t, rgba, err); got != (Color{188, 188, 188, 255}) {
		t.Fatalf("linear-light midpoint = %+v", got)
	}
	encoded, err := a.Convert(ColorOKLab, GamutClip)
	if got := must(t, encoded, err); got.Space != ColorOKLab || got.Channels != ([4]float64{}) {
		t.Fatalf("converted black = %+v", got)
	}
	palette := Palette{black, white, red, red}
	color, err := palette.Lookup(2)
	if must(t, color, err) != red {
		t.Fatalf("palette lookup = %+v", color)
	}
	nearest, err := palette.Nearest(PaintColorFromRGBA(red), GamutClip)
	if must(t, nearest, err) != 2 {
		t.Fatalf("nearest palette tie = %d", nearest)
	}
	if _, err := palette.Lookup(4); err != OutOfRange {
		t.Fatalf("palette lookup bounds = %v", err)
	}
	ramp := PaletteRamp{Palette: palette[:2], Address: PaintRepeat}
	result, err := ramp.Sample(.75)
	if got := must(t, result, err); got.Channels[0] != .5 {
		t.Fatalf("cyclic last-to-first sample = %+v", got)
	}
	hard := Gradient{Address: PaintReflect,
		Stops: []GradientStop{{0, a, Linear}, {.5, b, Linear}, {.5, PaintColorFromRGBA(red), Linear}, {1, b, Linear}}}
	result, err = hard.Sample(1.5)
	if must(t, result, err) != PaintColorFromRGBA(red) {
		t.Fatalf("equal stops after reflect = %+v", result)
	}
	rotation := PaintField{Scale: ContinuousPoint{4, 4}, Rotation: PaintSpin, RotationPeriodUs: 16}
	coord, err := rotation.Coordinate(PaintSample{Position: ContinuousPoint{0, 4}, ElapsedUs: 4})
	if must(t, coord, err) != 1 {
		t.Fatalf("quarter-turn coordinate = %g", coord)
	}

	paint := PaintSource{Kind: PaintGradient,
		Gradient: Gradient{Stops: []GradientStop{{0, a, Linear}, {1, b, Linear}}},
		Field:    PaintField{Scale: ContinuousPoint{4, 1}, PhaseTrack: &Tween{To: .5, DurationUs: 8}},
		Opacity:  .8, OpacityTrack: &KeyframeTrack{Keys: []Keyframe{{0, .5, Linear}, {8, 1, Linear}}},
	}
	point := PaintSample{Position: ContinuousPoint{2, .5}, ElapsedUs: 4}
	result, err = paint.Sample(point)
	if got := must(t, result, err); got.Channels[0] != .75 || math.Abs(got.Alpha-.6) > 1e-15 {
		t.Fatalf("time-dependent color = %+v", got)
	}
	color, err = paint.SampleRGBA(point, GamutClip)
	if got := must(t, color, err); got != (Color{191, 191, 191, 153}) {
		t.Fatalf("time-dependent RGBA = %+v", got)
	}
	point.Motion = MotionReduced
	color, err = paint.SampleRGBA(point, GamutClip)
	if got := must(t, color, err); got != (Color{128, 128, 128, 102}) {
		t.Fatalf("reduced-motion RGBA = %+v", got)
	}
	paint.Field.Scale.X = 0
	if _, err := paint.Sample(point); err != InvalidArgument {
		t.Fatalf("zero scale = %v", err)
	}
}

func TestSurfaceStyledText(t *testing.T) {
	layout, err := LayoutText("e\u0301\t界 Q", TextLayoutOptions{})
	mustOK(t, err)
	defer layout.Close()
	frame, err := NewOffscreenFrame(Size{12, 3})
	mustOK(t, err)
	defer frame.Close()
	root, err := frame.Surface()
	mustOK(t, err)
	read := frame.Frame()
	const owner = uint64(1<<63) + 19
	fg, bg := Color{240, 220, 160, 255}, Color{60, 40, 20, 255}
	red := Color{Red: 255, Alpha: 255}
	ink := TextPaintStyle{Color: Style{Foreground: fg}, Owner: 7}
	solid := PaintSource{Solid: PaintColorFromRGBA(red), Opacity: 1}
	backdrop := PaintSource{Kind: PaintPalette, Palette: PaletteRamp{
		Palette: Palette{bg, red}, Filter: PaletteStep, Address: PaintPad,
	}, Field: PaintField{Scale: ContinuousPoint{10, 1}}, Opacity: 1}
	options := TextPaintOptions{Base: ink, Spans: []TextPaintSpan{
		{0, 3, ink},
		{0, 1, TextPaintStyle{Color: ink.Color, Owner: owner, Foreground: &solid}},
		{1, 3, ink},
		{3, 4, TextPaintStyle{Color: Style{Foreground: fg, Background: bg}, Owner: 21}},
		{4, 7, TextPaintStyle{Color: ink.Color, Owner: 22, Background: &backdrop}},
	}}
	mustOK(t, root.PaintText(Point{}, layout, options))
	first, err := read.Cell(Point{})
	if got := must(t, first, err); got.Glyph != "e\u0301" || got.Owner != owner || got.Style.Foreground != red {
		t.Fatalf("span precedence = %+v", got)
	}
	for x := int32(1); x < 4; x++ {
		cell, err := read.Cell(Point{x, 0})
		if got := must(t, cell, err); got.Glyph != " " || got.Owner != 21 || got.Style.Background != bg {
			t.Fatalf("tab cell %d = %+v", x, got)
		}
	}
	mustOK(t, root.PaintText(Point{1, 1}, layout, options))
	for _, at := range []Point{{5, 0}, {6, 1}} {
		cell, err := read.Cell(at)
		if got := must(t, cell, err); got.Glyph != "界" || got.Lead != (Point{at.X - 1, at.Y}) ||
			got.Style.Background != bg || got.Owner != 22 {
			t.Fatalf("wide continuation at %+v = %+v", at, got)
		}
	}
	clip, err := root.Child(Rect{Origin: Point{5, 2}, Size: Size{1, 1}}, nil)
	mustOK(t, err)
	mustOK(t, clip.PaintText(Point{-5, 0}, layout, options))
	cell, err := read.Cell(Point{5, 2})
	if got := must(t, cell, err); got.Glyph != "" || got.Style.Background != bg || got.Owner != 22 {
		t.Fatalf("clipped glyph background = %+v", got)
	}
	physical, err := read.Background(Point{5, 2})
	if got := must(t, physical, err); got != (CellBackground{Color: bg, Owner: 22}) {
		t.Fatalf("clipped physical background = %+v", got)
	}
	before := make([]Cell, 12)
	for x := range before {
		cell, err := read.Cell(Point{int32(x), 0})
		before[x] = must(t, cell, err)
	}
	bad := PaintSource{Solid: PaintColorFromRGBA(red), Opacity: 2}
	options.Spans = append(options.Spans, TextPaintSpan{8, 9, TextPaintStyle{Foreground: &bad}})
	if err := root.PaintText(Point{1, 0}, layout, options); err != InvalidArgument {
		t.Fatalf("late paint refuses = %v", err)
	}
	options.Spans[len(options.Spans)-1].End = 100
	if err := root.PaintText(Point{}, layout, options); err != OutOfRange {
		t.Fatalf("bad range refuses = %v", err)
	}
	for x, want := range before {
		cell, err := read.Cell(Point{int32(x), 0})
		if got := must(t, cell, err); got != want {
			t.Fatalf("refused paint modified cell %d = %+v, was %+v", x, got, want)
		}
	}
	layout.Close()
	if err := root.PaintText(Point{}, layout, options); err != StaleReference {
		t.Fatalf("closed layout = %v", err)
	}
}

func TestMountedStyledTextUsesComponentOwner(t *testing.T) {
	layout, err := LayoutText("AB", TextLayoutOptions{})
	mustOK(t, err)
	defer layout.Close()
	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	var escaped *Paint
	const external = uint64(1<<63) + 25
	style := TextPaintStyle{Color: panelInk, Owner: external}
	_, err = tree.Mount(Node{}, MountOptions{Enabled: true}, &panelPaintFixture{draw: func(paint *Paint) error {
		escaped = paint
		if err := paint.Glyph(Point{}, "X", 1, panelInk); err != nil {
			return err
		}
		bad := PaintSource{Opacity: 2}
		if err := paint.PaintText(Point{}, layout, TextPaintOptions{Base: style,
			Spans: []TextPaintSpan{{1, 2, TextPaintStyle{Foreground: &bad}}}}); err != InvalidArgument {
			t.Errorf("failed mounted draw = %v", err)
		}
		return paint.PaintText(Point{1, 0}, layout, TextPaintOptions{Base: style})
	}})
	mustOK(t, err)
	mustOK(t, tree.Publish(Size{3, 1}))
	frame, err := tree.Frame()
	mustOK(t, err)
	for x, glyph := range []string{"X", "A", "B"} {
		cell, err := frame.Cell(Point{int32(x), 0})
		if got := must(t, cell, err); got.Glyph != glyph || got.Owner == 0 || got.Owner == external {
			t.Fatalf("mounted cell %d = %+v", x, got)
		}
	}
	if err := escaped.PaintText(Point{}, layout, TextPaintOptions{Base: style}); err != StaleReference {
		t.Fatalf("escaped context = %v", err)
	}
}
