package nocturne

import "testing"

func TestParticleViewHonorsMotionAndOwnership(t *testing.T) {
	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	options := DefaultParticleViewOptions()
	options.Emitter.PeriodUs = 0
	options.Emitter.LifetimeMinUs, options.Emitter.LifetimeMaxUs = 1000000, 1000000
	options.Emitter.Origin = ParticleRange{Min: ContinuousPoint{X: 1, Y: 1}, Max: ContinuousPoint{X: 1, Y: 1}}
	options.Emitter.Velocity = ParticleRange{Min: ContinuousPoint{X: 4}, Max: ContinuousPoint{X: 4}}
	options.Paint.Glyph = "+"
	options.Paint.Trail = ParticleTrailOptions{SpacingUs: 250000, Samples: 3}
	size := Size{Columns: 10, Rows: 3}
	node, err := tree.MountParticleView(Node{}, MountOptions{Enabled: true}, options)
	mustOK(t, err)
	mustOK(t, tree.Tick(500000, MotionFull))
	mustOK(t, tree.Publish(size))
	cell := func(x int32) Cell {
		t.Helper()
		frame, err := tree.Frame()
		mustOK(t, err)
		value, err := frame.Cell(Point{X: x, Y: 1})
		mustOK(t, err)
		return value
	}
	owner := cell(3).Owner
	if cell(3).Glyph != "+" || owner == 0 || cell(2).Owner != owner {
		t.Fatal("full motion did not render the head and trail")
	}
	bad := options
	bad.Paint.Glyph = "wide"
	if err := tree.SetParticleView(node, bad); err != InvalidArgument {
		t.Fatalf("invalid replacement = %v", err)
	}
	mustOK(t, tree.Tick(600000, MotionReduced))
	mustOK(t, tree.Publish(size))
	if cell(1).Glyph != "+" || cell(2).Owner != 0 {
		t.Fatal("reduced motion did not keep only the birth point")
	}
	mustOK(t, tree.Remove(node))
	emitter, err := NewParticleEmitter(options.Emitter)
	mustOK(t, err)
	defer emitter.Close()
	component := &panelPaintFixture{draw: func(paint *Paint) error {
		return paint.DrawParticles(emitter, options.Paint, 500000, MotionReduced)
	}}
	_, err = tree.Mount(Node{}, MountOptions{Enabled: true}, component)
	mustOK(t, err)
	mustOK(t, tree.Publish(size))
	if cell(1).Glyph != "+" || cell(1).Owner == 0 || cell(1).Owner == owner {
		t.Fatal("draw did not use the custom component owner")
	}
}
