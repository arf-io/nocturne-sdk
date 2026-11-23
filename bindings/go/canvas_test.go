package nocturne

import "testing"

func TestOwnedCanvasAndCellReduction(t *testing.T) {
	mode, err := ParseSamplingMode("braille")
	mustOK(t, err)
	ink := CanvasInk{Color: Color{Red: 250, Green: 80, Alpha: 255}, Owner: 1<<63 + 11}
	canvas, err := NewCanvas(Size{Columns: 3, Rows: 5})
	mustOK(t, err)
	defer canvas.Close()
	background := Color{Blue: 60, Alpha: 255}
	mustOK(t, canvas.Clear(background))
	mustOK(t, canvas.Line(Point{X: -1, Y: 0}, Point{X: 1, Y: 0}, ink))
	first, err := canvas.Pixel(Point{})
	mustOK(t, err)
	if !first.Covered || first.Color != ink.Color || first.Owner != ink.Owner {
		t.Fatalf("first pixel = %+v", first)
	}
	blank, err := canvas.Pixel(Point{X: 2, Y: 0})
	mustOK(t, err)
	if blank.Covered || blank.Color != background || blank.Owner != 0 {
		t.Fatalf("untouched pixel = %+v", blank)
	}
	if _, err := canvas.Pixel(Point{X: -1}); err != OutOfRange {
		t.Fatalf("pixel outside the sample grid = %v", err)
	}
	var tile CellSamples
	for i := range tile.Pixels {
		tile.Pixels[i].Color = background
		tile.Background[i] = background
	}
	tile.Pixels[0], tile.Pixels[1] = first, first
	sampled, err := ReduceCellSamples(tile, mode)
	mustOK(t, err)
	if sampled.Glyph != "⠉" || sampled.Style.Foreground != ink.Color || sampled.Owner != ink.Owner {
		t.Fatalf("reduced tile = %+v", sampled)
	}
	frame, err := canvas.Render(mode)
	mustOK(t, err)
	defer frame.Close()
	mustOK(t, canvas.Clear(Color{}))
	canvas.Close()
	size, err := frame.Frame().Size()
	mustOK(t, err)
	if size != (Size{Columns: 2, Rows: 2}) {
		t.Fatalf("frame size = %+v", size)
	}
	rendered, err := frame.Frame().Cell(Point{})
	mustOK(t, err)
	if rendered.Glyph != sampled.Glyph || rendered.Owner != sampled.Owner || rendered.Style != sampled.Style {
		t.Fatalf("rendered cell = %+v, reduced tile = %+v", rendered, sampled)
	}
	if _, err := canvas.Pixel(Point{}); err != StaleReference {
		t.Fatalf("closed canvas = %v", err)
	}
}

func TestCanvasPrimitivesAndCompositeRollback(t *testing.T) {
	canvas, err := NewCanvas(Size{Columns: 4, Rows: 4})
	mustOK(t, err)
	defer canvas.Close()
	size, err := canvas.Size()
	mustOK(t, err)
	if size != (Size{Columns: 4, Rows: 4}) {
		t.Fatalf("canvas size = %+v", size)
	}
	if mode, err := ParseSamplingMode("half-block"); err != nil || mode != SampleHalfBlock {
		t.Fatalf("half-block mode = %v, %v", mode, err)
	}
	if _, err := ParseSamplingMode("cell\x00braille"); err != InvalidArgument {
		t.Fatalf("embedded NUL mode = %v", err)
	}
	ink := CanvasInk{Color: Color{Red: 120, Blue: 220, Alpha: 255}, Owner: ^uint64(0)}
	bounds := Rect{Size: size}
	center := Point{X: 1, Y: 1}
	expectOwner := func(at Point, owner uint64) {
		t.Helper()
		pixel, err := canvas.Pixel(at)
		mustOK(t, err)
		if pixel.Owner != owner || pixel.Covered != (owner != 0) {
			t.Fatalf("pixel at %+v = %+v", at, pixel)
		}
	}
	mustOK(t, canvas.Border(bounds, ink))
	expectOwner(Point{X: 3, Y: 0}, ink.Owner)
	expectOwner(center, 0)
	mustOK(t, canvas.Triangle(Point{}, Point{X: 2}, Point{Y: 2}, ink))
	expectOwner(center, ink.Owner)
	expectOwner(Point{X: 2, Y: 2}, 0)
	mustOK(t, canvas.Fill(Rect{Origin: Point{X: 2, Y: 2}, Size: Size{Columns: 1, Rows: 1}}, ink))
	expectOwner(Point{X: 2, Y: 2}, ink.Owner)

	bad := ink
	bad.Owner = 1
	bad.Composite = &CompositeOptions{Operation: CompositeOperator(999)}
	if err := canvas.Fill(bounds, bad); err != InvalidArgument {
		t.Fatalf("invalid composite = %v", err)
	}
	expectOwner(Point{}, ink.Owner)
	expectOwner(Point{X: 1, Y: 2}, 0)
	ink.Composite = &CompositeOptions{Operation: CompositeClear}
	mustOK(t, canvas.Point(center, ink))
	expectOwner(center, 0)

	for _, mode := range []SamplingMode{SampleCell, SampleHalfBlock, SampleBraille, SampleHybrid} {
		frame, err := canvas.Render(mode)
		mustOK(t, err)
		frame.Close()
	}
	if _, err := canvas.Render(SamplingMode(99)); err != InvalidArgument {
		t.Fatalf("invalid render mode = %v", err)
	}
}
