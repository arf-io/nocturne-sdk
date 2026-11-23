package nocturne

import "testing"

func TestAuthoredParticleEffect(t *testing.T) {
	const start uint64 = 1<<63 + 17
	effect, err := ParticleEffectRecipe(ParticleSparks)
	mustOK(t, err)
	if !effect.Motion.Radial || effect.Motion.DirectionTurns != .625 || effect.View.Paint.Trail.Samples != 3 {
		t.Fatalf("sparks defaults = %+v", effect)
	}
	snow, err := ParticleEffectRecipe(ParticleSnow)
	mustOK(t, err)
	if snow.Motion.SwayPeriodUs != 1600000 || snow.Motion.SwayAmplitude.X != 1.3 || snow.View.Paint.Glyph != "*" {
		t.Fatalf("snow defaults = %+v", snow)
	}
	if _, err := ParticleEffectRecipe(ParticleRecipe(99)); err != InvalidArgument {
		t.Fatalf("unknown recipe = %v", err)
	}
	emission := &effect.View.Emitter
	emission.StartUs, emission.Seed, emission.Stream = start, ^uint64(0), 1<<54+3
	emission.BirthsPerTick = 1
	emission.Origin = ParticleRange{ContinuousPoint{X: 2, Y: 2}, ContinuousPoint{X: 2, Y: 2}}
	emission.Acceleration = ContinuousPoint{}
	emission.LifetimeMinUs, emission.LifetimeMaxUs = 1000000, 1000000
	effect.View.Paint.Glyph = "#"
	effect.View.Paint.Trail = ParticleTrailOptions{}
	effect.Motion.DirectionTurns, effect.Motion.SweepTurns = .25, 0
	effect.Motion.SpeedMin, effect.Motion.SpeedMax = 4, 4
	effect.Motion.GlitterPeriodUs, effect.Motion.GlitterMinAlpha = 50000, 120

	emitter, err := NewParticleEffectEmitter(effect)
	mustOK(t, err)
	defer emitter.Close()
	now := start + 250000
	samples, err := emitter.Sample(now)
	mustOK(t, err)
	if len(samples) != 1 || samples[0].BornUs != start || samples[0].Position != (ContinuousPoint{X: 2, Y: 3}) {
		t.Fatalf("authored radial samples = %+v", samples)
	}
	deadline, err := emitter.Deadline(now, MotionFull, 200000)
	mustOK(t, err)
	if !deadline.Scheduled || deadline.At != now+50000 {
		t.Fatalf("glitter deadline = %+v", deadline)
	}

	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	node, err := tree.MountAuthoredParticleEffect(Node{}, MountOptions{Enabled: true}, effect)
	mustOK(t, err)
	size := Size{Columns: 8, Rows: 6}
	assertGlyph := func(y int32, glyph string) {
		t.Helper()
		mustOK(t, tree.Publish(size))
		frame, err := tree.Frame()
		mustOK(t, err)
		cell, err := frame.Cell(Point{X: 2, Y: y})
		mustOK(t, err)
		if cell.Glyph != glyph || cell.Owner == 0 {
			t.Fatalf("cell at y=%d = %+v", y, cell)
		}
	}
	mustOK(t, tree.Tick(now, MotionFull))
	assertGlyph(3, "#")
	bad := effect
	bad.Motion.SweepTurns = 2
	if err := tree.SetAuthoredParticleEffect(node, bad); err != InvalidArgument {
		t.Fatalf("bad motion replacement = %v", err)
	}
	bad = effect
	bad.View.Paint.Glyph = "wide"
	standalone, err := NewParticleEffectEmitter(bad)
	mustOK(t, err)
	standalone.Close()
	if err := tree.SetAuthoredParticleEffect(node, bad); err != InvalidArgument {
		t.Fatalf("bad glyph replacement = %v", err)
	}
	mustOK(t, tree.Tick(now+50000, MotionReduced))
	assertGlyph(2, "#")
	effect.View.Paint.Glyph = "+"
	mustOK(t, tree.SetAuthoredParticleEffect(node, effect))
	assertGlyph(2, "+")
}
