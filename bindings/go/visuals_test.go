package nocturne

import (
	"errors"
	"math"
	"testing"
	"unicode/utf8"
)

func TestTypedPlotProjectionRevealAndDigits(t *testing.T) {
	domain := PlotBounds{X: PlotDomain{0, 2}, Y: PlotDomain{0, 2}}
	view := PlotViewport{Domain: domain, Cells: Rect{Origin: Point{2, 3}, Size: Size{8, 4}}}
	point, err := view.Project(PlotPoint{1, 1})
	if err != nil || point != (PlotPoint{6, 5}) {
		t.Fatalf("projected point = %v, %v", point, err)
	}
	segment := PlotSegment{First: PlotPoint{-1, 1}, Last: PlotPoint{3, 1}}
	clipped, visible, err := domain.ClipSegment(segment)
	if err != nil || !visible || clipped != (PlotSegment{PlotPoint{0, 1}, PlotPoint{2, 1}}) {
		t.Fatalf("clipped segment = %v, %v, %v", clipped, visible, err)
	}

	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	enabled := MountOptions{Enabled: true}
	const start = 1<<53 + 1
	size := Size{12, 4}
	options := PlotOptions{
		Kind: PlotScatter, Sampling: SampleBraille, Domain: &domain, Preferred: size,
		Foreground: testInk.Foreground, DataLimit: 3,
		Data:   []PlotDatum{{.25, .25, Color{}}, {1, 1, Color{}}, {1.75, 1.75, Color{}}},
		Reveal: PlotReveal{StartUs: start, DurationUs: 120, Easing: Smooth},
	}
	plot, err := tree.MountPlot(Node{}, enabled, options)
	mustOK(t, err)
	mustOK(t, tree.Tick(start-1, MotionFull))
	next, err := tree.NextUpdate()
	if err != nil || !next.Scheduled || next.At != start {
		t.Fatalf("reveal deadline = %v, %v", next, err)
	}
	brailleCount := func() int {
		t.Helper()
		mustOK(t, tree.Publish(size))
		frame, err := tree.Frame()
		mustOK(t, err)
		count := 0
		for row := int32(0); row < int32(size.Rows); row++ {
			for column := int32(0); column < int32(size.Columns); column++ {
				cell, err := frame.Cell(Point{column, row})
				mustOK(t, err)
				glyph, _ := utf8.DecodeRuneInString(cell.Glyph)
				if glyph > 0x2800 && glyph <= 0x28ff {
					count++
				}
			}
		}
		return count
	}
	if count := brailleCount(); count != 0 {
		t.Fatalf("before reveal: %d Braille cells", count)
	}
	mustOK(t, tree.Tick(start+120, MotionFull))
	if count := brailleCount(); count != 3 {
		t.Fatalf("after reveal: %d Braille cells", count)
	}
	options.SampleStride = 2
	mustOK(t, tree.SetPlot(plot, options))
	if count := brailleCount(); count != 2 {
		t.Fatalf("with stride: %d Braille cells", count)
	}
	options.DataLimit = 2
	if err := tree.SetPlot(plot, options); !errors.Is(err, OutOfRange) {
		t.Fatalf("replacement beyond data limit = %v", err)
	}
	if count := brailleCount(); count != 2 {
		t.Fatalf("after refused replacement: %d Braille cells", count)
	}

	mustOK(t, tree.Remove(plot))
	digits := DigitsOptions{Value: math.MinInt64, FractionalDigits: 18, ASCII: true, Foreground: testInk.Foreground}
	display, err := tree.MountDigits(Node{}, enabled, digits)
	mustOK(t, err)
	value, err := tree.Property(display, ValueProperty)
	if err != nil || value.Kind != SignedValue || value.Signed != math.MinInt64 {
		t.Fatalf("display property = %v, %v", value, err)
	}
	digits.Value, digits.FractionalDigits = 17, 19
	if err := tree.SetDigits(display, digits); !errors.Is(err, OutOfRange) {
		t.Fatalf("replacement with invalid decimal placement = %v", err)
	}
	digits.FractionalDigits = 2
	mustOK(t, tree.SetDigits(display, digits))
	value, err = tree.Property(display, ValueProperty)
	if err != nil || value.Signed != 17 {
		t.Fatalf("updated display property = %v, %v", value, err)
	}
}

func TestTypedPlotAndParticleRecipes(t *testing.T) {
	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	enabled := MountOptions{Enabled: true}
	root, err := tree.MountBox(Node{}, enabled, Box{Kind: BoxColumn})
	mustOK(t, err)
	plot := PlotOptions{Kind: PlotLine, Data: []PlotDatum{{0, 0, Color{}}, {1, 2, Color{}}},
		Preferred: Size{Columns: 8, Rows: 3}, Foreground: testInk.Foreground, ASCII: true}
	chart, err := tree.MountPlot(root, enabled, plot)
	mustOK(t, err)
	plot.Data[0].Y = math.NaN()
	if err := tree.SetPlot(chart, plot); !errors.Is(err, InvalidArgument) {
		t.Fatalf("replacement containing NaN = %v", err)
	}
	preferred := Size{Columns: 8, Rows: 2}
	effect, err := tree.MountParticleEffect(root, enabled,
		ParticleEffectOptions{Recipe: ParticleSnow, Seed: 9, StartUs: 1000, Preferred: &preferred})
	mustOK(t, err)
	mustOK(t, tree.Tick(1000, MotionFull))
	mustOK(t, tree.Publish(Size{Columns: 8, Rows: 6}))
	for _, node := range []Node{chart, effect} {
		geometry, err := tree.Geometry(node)
		if !must(t, geometry, err).Placed {
			t.Fatal("visual component has no published placement")
		}
	}
	mustOK(t, tree.SetParticleEffect(effect, ParticleEffectOptions{Recipe: ParticleRain, StartUs: 1050, Preferred: &preferred}))
	mustOK(t, tree.Remove(effect))
}
