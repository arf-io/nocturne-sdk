package nocturne

import "testing"

func TestAuthoredContentAndImages(t *testing.T) {
	const owner = ^uint64(0)
	color := Color{255, 10, 20, 255}
	background := Color{3, 4, 5, 255}
	frame, err := FrameFromCells(Size{Columns: 2, Rows: 1}, []ContentCell{
		{Glyph: "界", Columns: 2, Style: Style{Foreground: color}, Owner: owner},
		{Position: Point{X: 1}, Style: Style{Background: background}, Owner: 12},
	})
	mustOK(t, err)
	defer frame.Close()
	cell, err := frame.Frame().Cell(Point{X: 1})
	mustOK(t, err)
	if cell.Lead != (Point{}) || cell.Glyph != "界" || cell.Owner != owner {
		t.Fatalf("wide cell = %+v", cell)
	}
	bg, err := frame.Frame().Background(Point{X: 1})
	mustOK(t, err)
	if bg.Color != background || bg.Owner != 12 {
		t.Fatalf("background = %+v", bg)
	}
	if frame, err := FrameFromCells(Size{Columns: 1, Rows: 1}, []ContentCell{{Glyph: "X"}}); err != InvalidArgument || frame != nil {
		t.Fatalf("background-only cell with glyph = %v, %v", frame, err)
	}
	image := ImageView{Pixels: []Color{color, {}, {255, 255, 0, 255}, color, {}},
		Size: Size{Columns: 2, Rows: 2}, Stride: 3}
	sampling := ImageSampling{Filter: ImageBilinear}
	pixel, err := image.Sample(sampling, .5, .5)
	if must(t, pixel, err) != (Color{255, 10, 20, 128}) {
		t.Fatalf("padded bilinear = %+v", pixel)
	}
	image.Pixels = image.Pixels[:4]
	if _, err := image.Sample(sampling, .5, .5); err != InsufficientBuffer {
		t.Fatalf("short image = %v", err)
	}
	image = ImageView{Pixels: []Color{color}, Size: Size{Columns: 1, Rows: 1}, Stride: 1}
	raster, err := image.Rasterize(ImageSampling{}, Size{Columns: 3, Rows: 5}, SampleCell, owner)
	mustOK(t, err)
	defer raster.Close()
	size, err := raster.Frame().Size()
	if must(t, size, err) != (Size{Columns: 2, Rows: 2}) {
		t.Fatalf("raster dimensions = %+v", size)
	}
	bg, err = raster.Frame().Background(Point{})
	mustOK(t, err)
	if bg.Color != color || bg.Owner != owner {
		t.Fatalf("raster background = %+v", bg)
	}
}
