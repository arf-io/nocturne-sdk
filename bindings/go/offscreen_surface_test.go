package nocturne

import (
	"errors"
	"testing"
)

func offscreenFixture(t *testing.T, columns uint32) *OffscreenFrame {
	t.Helper()
	frame, err := NewOffscreenFrame(Size{columns, 1})
	mustOK(t, err)
	t.Cleanup(func() { mustOK(t, frame.Close()) })
	return frame
}

func offscreenRect(x int32, columns uint32) Rect {
	return Rect{Origin: Point{x, 0}, Size: Size{columns, 1}}
}

func TestOffscreenSurfaceRetainsGeometryAndMasks(t *testing.T) {
	frame := offscreenFixture(t, 4)
	read := frame.Frame()
	root, err := frame.Surface()
	mustOK(t, err)
	blue := Color{Blue: 255, Alpha: 255}
	const wideOwner = uint64(1<<63) + 3
	mustOK(t, root.Fill(offscreenRect(0, 2), blue, 55))
	mustOK(t, root.Glyph(Point{}, "界", 2, panelInk, wideOwner))
	alpha := []byte{255, 0, 128}
	child, err := root.Child(offscreenRect(1, 3), &SurfaceMask{Kind: MaskAlpha,
		Bounds: offscreenRect(0, 3), Stride: 3, Alpha: alpha})
	mustOK(t, err)
	alpha[2] = 0
	coverage, err := child.Coverage(Point{2, 0})
	if must(t, coverage, err) != 128.0/255 {
		t.Fatalf("captured alpha = %g", coverage)
	}
	mustOK(t, child.EraseGlyph(Point{}))
	cell, err := read.Cell(Point{1, 0})
	if got := must(t, cell, err); got.Glyph != "界" || got.Lead != (Point{}) || got.Owner != wideOwner {
		t.Fatalf("partially clipped erase = %+v", got)
	}
	mustOK(t, child.Glyph(Point{2, 0}, "P", 1, panelInk, 74))
	cell, err = read.Cell(Point{3, 0})
	if got := must(t, cell, err); got.Glyph != "P" || got.Style.Foreground.Alpha != 128 || got.Owner != 74 {
		t.Fatalf("masked glyph = %+v", got)
	}
	background, err := read.Background(Point{1, 0})
	if must(t, background, err) != (CellBackground{Color: blue, Owner: 55}) {
		t.Fatalf("physical background = %+v", background)
	}
	mustOK(t, frame.Resize(Size{1, 1}))
	cell, err = read.Cell(Point{})
	if got := must(t, cell, err); got.Glyph != "" || got.Style.Background != blue || got.Owner != 55 {
		t.Fatalf("shrink result = %+v", got)
	}
	mustOK(t, frame.Resize(Size{8, 1}))
	clip, err := child.Clip()
	if must(t, clip, err) != offscreenRect(1, 3) {
		t.Fatalf("child grew past captured bounds: %+v", clip)
	}
	newRoot, err := frame.Surface()
	mustOK(t, err)
	clip, err = newRoot.Clip()
	if must(t, clip, err) != offscreenRect(0, 8) {
		t.Fatalf("new root clip = %+v", clip)
	}
	mustOK(t, frame.SetGlyph(Point{4, 0}, "界", 2, panelInk, wideOwner))
	mustOK(t, frame.ClearGlyph(Point{5, 0}))
	cell, err = read.Cell(Point{4, 0})
	if must(t, cell, err).Glyph != "" {
		t.Fatalf("continuation clear = %+v", cell)
	}
	mustOK(t, frame.Close())
	if _, err := child.Clip(); err != StaleReference {
		t.Fatalf("closed child = %v", err)
	}
	if _, err := read.Size(); err != StaleReference {
		t.Fatalf("closed readable frame = %v", err)
	}
}

func TestOffscreenComposeAliasesAndRollsBackOwners(t *testing.T) {
	frame := offscreenFixture(t, 6)
	read := frame.Frame()
	const baseOwner = uint64(1<<63) + 17
	for i, glyph := range []string{"A", "B", "C", "D"} {
		mustOK(t, frame.SetGlyph(Point{int32(i), 0}, glyph, 1, panelInk, baseOwner+uint64(i)))
	}
	root, err := frame.Surface()
	mustOK(t, err)
	owners := SurfaceOwners{Mode: OwnersMap, Map: func(owner uint64) (uint64, error) {
		if _, err := read.Size(); err != WrongPhase {
			t.Errorf("aliased frame read during owner map: %v", err)
		}
		return owner + 100, nil
	}}
	mustOK(t, root.Compose([]SurfaceLayer{
		{Source: read, Region: offscreenRect(0, 2), Position: Point{2, 0}, Opacity: 1},
		{Source: read, Region: offscreenRect(2, 2), Position: Point{4, 0}, Opacity: 1, Owners: owners},
	}))
	var before [6]Cell
	for x, glyph := range []string{"A", "B", "A", "B", "C", "D"} {
		cell, err := read.Cell(Point{int32(x), 0})
		before[x] = must(t, cell, err)
		if before[x].Glyph != glyph {
			t.Fatalf("cell %d uses staged source: %+v", x, before[x])
		}
	}
	if before[2].Owner != 0 || before[4].Owner != baseOwner+102 || before[5].Owner != baseOwner+103 {
		t.Fatalf("owner policy results: %+v", before)
	}

	external := offscreenFixture(t, 2)
	mustOK(t, external.SetGlyph(Point{}, "X", 1, panelInk, baseOwner))
	mustOK(t, external.SetGlyph(Point{1, 0}, "Y", 1, panelInk, baseOwner+1))
	fail := errors.New("owner rejected")
	failingOwners := SurfaceOwners{Mode: OwnersMap, Map: func(owner uint64) (uint64, error) {
		if err := external.Close(); err != WrongPhase {
			t.Errorf("source close during owner map: %v", err)
		}
		if owner == baseOwner+1 {
			return 0, fail
		}
		return owner, nil
	}}
	err = root.Compose([]SurfaceLayer{
		{Source: external.Frame(), Region: offscreenRect(0, 2), Position: Point{2, 0}, Opacity: 1},
		{Source: external.Frame(), Region: offscreenRect(0, 2), Opacity: 1, Owners: failingOwners},
	})
	if !errors.Is(err, fail) || !errors.Is(err, CallbackFailed) {
		t.Fatalf("owner failure = %v", err)
	}
	for x, want := range before {
		cell, err := read.Cell(Point{int32(x), 0})
		if got := must(t, cell, err); got != want {
			t.Fatalf("cell %d changed after refusal: %+v", x, got)
		}
	}
	_, err = external.Frame().Size()
	mustOK(t, err)
	mustOK(t, root.ProjectPanels([]SurfacePanelLayer{{
		PanelLayer: PanelLayer{Source: external.Frame(), Region: offscreenRect(0, 2),
			Viewport: offscreenRect(0, 2), Pose: panelIdentity, Remap: func(sample PanelSample) (PanelPixel, error) {
				return PanelPixel{Source: ContinuousPoint{2 - sample.Source.X, sample.Source.Y}, Opacity: 1}, nil
			}},
		Owners: SurfaceOwners{Mode: OwnersShared},
	}}))
	projected, err := read.Cell(Point{})
	if got := must(t, projected, err); got.Glyph != "Y" || got.Owner != baseOwner+1 {
		t.Fatalf("offscreen panel = %+v", got)
	}

	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	_, err = tree.Mount(Node{}, MountOptions{Enabled: true}, &panelPaintFixture{draw: func(paint *Paint) error {
		return paint.ProjectPanels([]PanelLayer{{Source: external.Frame(), Region: offscreenRect(0, 2),
			Viewport: offscreenRect(0, 2), Pose: panelIdentity}})
	}})
	mustOK(t, err)
	mustOK(t, tree.Publish(Size{2, 1}))
	mounted, err := tree.Frame()
	mustOK(t, err)
	cell, err := mounted.Cell(Point{})
	if must(t, cell, err).Glyph != "X" {
		t.Fatalf("mounted offscreen source = %+v", cell)
	}
}
