package nocturne

import (
	"errors"
	"testing"
)

type panelPaintFixture struct {
	ComponentBase
	draw func(*Paint) error
}

func (fixture *panelPaintFixture) Paint(paint *Paint, _ Size) error {
	return fixture.draw(paint)
}

var panelInk = Style{Foreground: Color{Red: 240, Green: 210, Blue: 140, Alpha: 255}}
var panelIdentity = PanelPose{Scale: ContinuousPoint{1, 1}, Opacity: 1}

func projectedPanelSource(t *testing.T) (*Tree, Frame) {
	t.Helper()
	tree, err := NewTree()
	mustOK(t, err)
	t.Cleanup(func() { mustOK(t, tree.Close()) })
	_, err = tree.Mount(Node{}, MountOptions{Enabled: true}, &panelPaintFixture{draw: func(paint *Paint) error {
		if err := paint.Glyph(Point{0, 0}, "界", 2, panelInk); err != nil {
			return err
		}
		if err := paint.Glyph(Point{2, 0}, "B", 1, panelInk); err != nil {
			return err
		}
		return paint.Glyph(Point{3, 0}, "C", 1, panelInk)
	}})
	mustOK(t, err)
	mustOK(t, tree.Publish(Size{4, 1}))
	frame, err := tree.Frame()
	return tree, must(t, frame, err)
}

func TestPanelLayersMaskAndRemap(t *testing.T) {
	source, frame := projectedPanelSource(t)
	otherHandle, err := source.Frame()
	mustOK(t, err)
	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	var samples []PanelSample
	var escaped *Paint
	region := Rect{Size: Size{4, 1}}
	const clock = uint64(1<<63) + 9
	_, err = tree.Mount(Node{}, MountOptions{Enabled: true}, &panelPaintFixture{draw: func(paint *Paint) error {
		escaped = paint
		copiedPaint := *paint
		err := paint.ProjectPanels([]PanelLayer{
			{Source: frame, Region: region, Viewport: region, Pose: panelIdentity,
				Mask: &SurfaceMask{Kind: MaskAlpha, Bounds: region, Alpha: []byte{255, 255, 0, 255}, Stride: 4}},
			{Source: otherHandle, Region: region, Viewport: Rect{Origin: Point{4, 0}, Size: region.Size}, Pose: panelIdentity,
				ElapsedUs: clock, Remap: func(sample PanelSample) (PanelPixel, error) {
					samples = append(samples, sample)
					if err := copiedPaint.Fill(region, Color{}); err != WrongPhase {
						t.Errorf("paint copy reentry: %v", err)
					}
					if err := source.Close(); err != WrongPhase {
						t.Errorf("source close while sampled: %v", err)
					}
					return PanelPixel{Source: ContinuousPoint{4 - sample.Source.X, sample.Source.Y}, Opacity: 1}, nil
				}},
		})
		if err != nil {
			return err
		}
		_, err = frame.Size()
		return err
	}})
	mustOK(t, err)
	mustOK(t, tree.Publish(Size{8, 1}))
	if len(samples) != 4 {
		t.Fatalf("remaps = %d, want 4", len(samples))
	}
	for i, sample := range samples {
		want := ContinuousPoint{float64(i) + .5, .5}
		if sample.Destination != want || sample.Source != want || sample.ElapsedUs != clock ||
			sample.RegionSize != region.Size || sample.ViewportSize != region.Size {
			t.Fatalf("sample %d = %+v", i, sample)
		}
	}
	out, err := tree.Frame()
	mustOK(t, err)
	for x, glyph := range map[int32]string{0: "界", 2: "", 3: "C", 4: "C", 5: "B", 6: "界"} {
		cell, err := out.Cell(Point{x, 0})
		if got := must(t, cell, err); got.Glyph != glyph {
			t.Fatalf("cell %d = %+v, want %q", x, got, glyph)
		}
	}
	for _, x := range []int32{1, 7} {
		cell, err := out.Cell(Point{x, 0})
		if got := must(t, cell, err); got.Lead != (Point{x - 1, 0}) || got.Owner == 0 {
			t.Fatalf("wide continuation %d = %+v", x, got)
		}
	}
	if err := escaped.ProjectPanels(nil); err != StaleReference {
		t.Fatalf("expired paint projection = %v", err)
	}
}

func TestPanelProjectionPriorFrameAndCallbackRollback(t *testing.T) {
	_, external := projectedPanelSource(t)
	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	var prior Frame
	phase := 0
	cause := errors.New("remap refused")
	viewport := Rect{Size: Size{4, 1}}
	node, err := tree.Mount(Node{}, MountOptions{Enabled: true}, &panelPaintFixture{draw: func(paint *Paint) error {
		if err := paint.Glyph(Point{0, 0}, "Q", 1, panelInk); err != nil {
			return err
		}
		switch phase {
		case 1:
			return paint.ProjectPanels([]PanelLayer{{Source: prior, Region: Rect{Size: Size{1, 1}},
				Viewport: Rect{Origin: Point{1, 0}, Size: Size{1, 1}}, Pose: panelIdentity}})
		case 2, 3:
			remap := func(sample PanelSample) (PanelPixel, error) {
				if sample.Destination.X < 2 {
					return PanelPixel{Source: sample.Source, Opacity: 1}, nil
				}
				if phase == 3 {
					panic(cause)
				}
				return PanelPixel{}, cause
			}
			err := paint.ProjectPanels([]PanelLayer{
				{Source: external, Region: viewport, Viewport: viewport, Pose: panelIdentity},
				{Source: external, Region: viewport, Viewport: viewport, Pose: panelIdentity, Remap: remap},
			})
			var failure *CallError
			if !errors.Is(err, CallbackFailed) || !errors.Is(err, cause) || !errors.As(err, &failure) {
				t.Errorf("remap error = %v", err)
				return InvalidArgument
			}
			if _, err := external.Size(); err != nil {
				return err
			}
			if err := paint.ProjectPanels([]PanelLayer{{Source: prior, Region: viewport,
				Viewport: viewport, Pose: panelIdentity}}); err != StaleReference {
				t.Errorf("stale same-tree frame = %v", err)
				return InvalidArgument
			}
			return paint.Glyph(Point{3, 0}, "Z", 1, panelInk)
		default:
			return nil
		}
	}})
	mustOK(t, err)
	mustOK(t, tree.Publish(viewport.Size))
	prior, err = tree.Frame()
	mustOK(t, err)
	for phase = 1; phase <= 3; phase++ {
		mustOK(t, tree.Invalidate(node, InvalidatePaint))
		mustOK(t, tree.Publish(viewport.Size))
		frame, err := tree.Frame()
		mustOK(t, err)
		first, err := frame.Cell(Point{0, 0})
		if must(t, first, err).Glyph != "Q" {
			t.Fatalf("phase %d overwritten first glyph = %+v", phase, first)
		}
		if phase == 1 {
			second, err := frame.Cell(Point{1, 0})
			if must(t, second, err).Glyph != "Q" {
				t.Fatalf("same-tree projection = %+v", second)
			}
		} else {
			second, err := frame.Cell(Point{1, 0})
			if must(t, second, err).Glyph != "" {
				t.Fatalf("failed batch changed cell 1 = %+v", second)
			}
			last, err := frame.Cell(Point{3, 0})
			if must(t, last, err).Glyph != "Z" {
				t.Fatalf("paint after rollback = %+v", last)
			}
		}
	}
}
