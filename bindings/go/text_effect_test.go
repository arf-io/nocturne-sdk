package nocturne

import (
	"errors"
	"testing"
)

func TestTextEffectClockAndCopiedGlyphs(t *testing.T) {
	layout, err := LayoutText("e\u0301👩‍🚀", TextLayoutOptions{})
	mustOK(t, err)
	defer layout.Close()
	frame := offscreenFixture(t, 5)
	root, err := frame.Surface()
	mustOK(t, err)
	const delay = uint64(1 << 60)
	const seed = uint64(1<<63) + 61
	var saved TextEffectSample
	calls := 0
	effect := TextEffect{DelayUs: delay, DurationUs: 100, Seed: seed, Visibility: TextVisible,
		Glyphs: TextGlyphEffect{Kind: TextScramble, Alphabet: []string{"*", "界"}, IntervalUs: 3},
		Trails: TextTrails{Count: 2, SpacingUs: 1, Decay: .5},
		Transform: func(sample TextEffectSample, pose TextPose) (TextPose, error) {
			calls++
			saved = sample
			if sample.ElapsedUs < delay+2 || sample.ElapsedUs > delay+4 || sample.LocalUs != sample.ElapsedUs-delay ||
				sample.Seed != seed || sample.DurationUs != 100 || sample.Count != 2 ||
				sample.LineBegin != 0 || sample.LineEnd != 3 || sample.Identity != sample.Cluster.ID {
				t.Errorf("sample = %+v", sample)
			}
			if _, err := frame.Frame().Size(); err != WrongPhase {
				t.Errorf("borrowed frame read = %v", err)
			}
			if sample.Cluster.Columns == 1 && pose.Glyph == "*" {
				pose.Glyph = "+"
			} else if sample.Cluster.Columns == 2 && pose.Glyph == "界" {
				pose.Glyph = "層"
			} else {
				t.Errorf("unexpected substitution = %+v for %+v", pose, sample)
			}
			return sample.Motion(pose, TextMotion{})
		}}
	base := TextPaintStyle{Color: panelInk, Owner: seed + 1}
	mustOK(t, root.PaintTextEffect(Point{}, layout, TextPaintOptions{Base: base, ElapsedUs: delay + 4}, effect))
	if calls != 6 {
		t.Fatalf("transform ran %d times, want 6", calls)
	}
	for x, glyph := range []string{"+", "層", "層"} {
		cell, err := frame.Frame().Cell(Point{int32(x), 0})
		if got := must(t, cell, err); got.Glyph != glyph || got.Owner != base.Owner {
			t.Fatalf("animated cell %d = %+v", x, got)
		}
	}
	if saved.Cluster.Glyph != "👩‍🚀" {
		t.Fatalf("retained sample = %+v", saved)
	}
	pose := TextPose{Glyph: saved.Cluster.Glyph, Opacity: 1}
	noise, err := saved.Noise(pose, ContinuousPoint{2, 2}, 1, true)
	mustOK(t, err)
	again, err := saved.Noise(pose, ContinuousPoint{2, 2}, 1, true)
	if got := must(t, again, err); got != noise || noise.Glyph != pose.Glyph || noise.Offset == (ContinuousPoint{}) {
		t.Fatalf("noise = %+v, %+v", noise, again)
	}
	mustOK(t, root.PaintTextEffect(Point{}, layout, TextPaintOptions{Base: base, Motion: MotionReduced}, effect))
	cell, err := frame.Frame().Cell(Point{1, 0})
	if got := must(t, cell, err); got.Glyph != "👩‍🚀" || calls != 6 {
		t.Fatalf("reduced draw = %+v with %d transforms", got, calls)
	}
}

func TestTextEffectFailureAndClosingLease(t *testing.T) {
	layout, err := LayoutText("AB", TextLayoutOptions{})
	mustOK(t, err)
	defer layout.Close()
	frame := offscreenFixture(t, 4)
	root, err := frame.Surface()
	mustOK(t, err)
	mustOK(t, frame.SetGlyph(Point{}, "X", 1, panelInk, 17))
	before, err := frame.Frame().Cell(Point{})
	mustOK(t, err)
	options := TextPaintOptions{Base: TextPaintStyle{Color: panelInk, Owner: 70}, ElapsedUs: 5}
	effect := TextEffect{DurationUs: 10, Visibility: TextVisible}
	effect.Transform = func(sample TextEffectSample, pose TextPose) (TextPose, error) {
		if sample.LogicalIndex == 1 {
			return pose, OutOfRange
		}
		return pose, nil
	}
	err = root.PaintTextEffect(Point{}, layout, options, effect)
	var failure *CallError
	if !errors.Is(err, OutOfRange) || !errors.As(err, &failure) || failure.Callbacks[0].Cause != OutOfRange {
		t.Fatalf("explicit transform result = %v", err)
	}
	effect.Transform = func(sample TextEffectSample, pose TextPose) (TextPose, error) {
		if sample.LogicalIndex == 1 {
			panic("transform error")
		}
		return pose, nil
	}
	err = root.PaintTextEffect(Point{}, layout, options, effect)
	var panicCause *PanicError
	if !errors.Is(err, CallbackFailed) || !errors.As(err, &panicCause) || panicCause.Value != "transform error" {
		t.Fatalf("transform panic = %v", err)
	}
	cell, err := frame.Frame().Cell(Point{})
	if got := must(t, cell, err); got != before {
		t.Fatalf("refused draws changed cell = %+v", got)
	}
	effect.Transform = func(sample TextEffectSample, pose TextPose) (TextPose, error) {
		if sample.LogicalIndex == 0 {
			layout.Close()
			if _, err := layout.Len(); err != StaleReference {
				t.Errorf("closed layout = %v", err)
			}
			if err := root.PaintTextEffect(Point{}, layout, options, effect); err != WrongPhase {
				t.Errorf("reentrant effect = %v", err)
			}
		} else if pose.Glyph != "B" {
			t.Errorf("pending layout glyph = %q", pose.Glyph)
		}
		return pose, nil
	}
	mustOK(t, root.PaintTextEffect(Point{}, layout, options, effect))
	cell, err = frame.Frame().Cell(Point{1, 0})
	if got := must(t, cell, err); got.Glyph != "B" || got.Owner != options.Base.Owner {
		t.Fatalf("admitted draw after layout close = %+v", got)
	}
	if err := root.PaintTextEffect(Point{}, layout, options, effect); err != StaleReference {
		t.Fatalf("later draw using closed layout = %v", err)
	}
}

func TestMountedTextEffectClipOwnerAndReentry(t *testing.T) {
	layout, err := LayoutText("AB", TextLayoutOptions{})
	mustOK(t, err)
	defer layout.Close()
	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	const external = uint64(1<<63) + 19
	options := TextPaintOptions{Base: TextPaintStyle{Color: panelInk, Owner: external}, ElapsedUs: 5}
	var escaped *Paint
	_, err = tree.Mount(Node{}, MountOptions{Enabled: true}, &panelPaintFixture{draw: func(paint *Paint) error {
		escaped = paint
		return paint.PaintTextEffect(Point{}, layout, options, TextEffect{DurationUs: 10, Visibility: TextVisible,
			Transform: func(_ TextEffectSample, pose TextPose) (TextPose, error) {
				copy := *paint
				if err := copy.Fill(offscreenRect(0, 2), Color{}); err != WrongPhase {
					t.Errorf("paint copy reentry = %v", err)
				}
				return pose, nil
			}})
	}})
	mustOK(t, err)
	mustOK(t, tree.Publish(Size{2, 1}))
	frame, err := tree.Frame()
	mustOK(t, err)
	cell, err := frame.Cell(Point{})
	if got := must(t, cell, err); got.Glyph != "A" || got.Owner == 0 || got.Owner == external {
		t.Fatalf("mounted glyph = %+v", got)
	}
	if err := escaped.PaintTextEffect(Point{}, layout, options, TextEffect{}); err != StaleReference {
		t.Fatalf("escaped paint effect = %v", err)
	}
}
