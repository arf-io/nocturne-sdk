package nocturne

import "testing"

func TestParticleDepositCapacityAndClock(t *testing.T) {
	options := DefaultParticleDepositsOptions()
	options.HoldUs, options.Capacity = 10, 2
	options.Release.Count = 1
	options.Release.LifetimeMinUs, options.Release.LifetimeMaxUs = 20, 20
	deposits, err := NewParticleDeposits(options)
	mustOK(t, err)
	defer deposits.Close()
	at := Point{X: 2, Y: 3}
	const end = ^uint64(0)
	red, blue := Color{255, 0, 0, 255}, Color{0, 0, 255, 255}
	id, err := deposits.Add(ParticleContact{Cell: at, At: end - 10, Mass: 1, Color: red})
	if must(t, id, err) != 0 {
		t.Fatalf("first ID = %d", id)
	}
	id, err = deposits.Add(ParticleContact{Cell: at, At: end - 9, Mass: 3, Color: blue})
	if must(t, id, err) != 1 {
		t.Fatalf("second ID = %d", id)
	}
	if _, err := deposits.Add(ParticleContact{Cell: at, At: end - 8, Mass: 1, Color: red}); err != OutOfRange {
		t.Fatalf("full store = %v", err)
	}
	piles, err := deposits.Piles(end - 9)
	mustOK(t, err)
	if len(piles) != 1 || piles[0] != (ParticlePile{Cell: at, Contacts: 2, Mass: 4, Color: Color{64, 0, 191, 255}}) {
		t.Fatalf("combined pile = %+v", piles)
	}
	deadline, err := deposits.Deadline(end-1, MotionReduced, 0)
	if must(t, deadline, err) != (Deadline{Scheduled: true, At: end}) {
		t.Fatalf("deadline = %+v", deadline)
	}
	releases, err := deposits.Releases(end)
	mustOK(t, err)
	if len(releases) != 1 || releases[0].DepositID != 0 || releases[0].Particle.BornUs != end ||
		releases[0].Particle.Position != (ContinuousPoint{X: 2.5, Y: 3.5}) {
		t.Fatalf("released particle = %+v", releases)
	}
	mustOK(t, deposits.Advance(end))
	if _, err := deposits.Piles(end - 1); err != InvalidArgument {
		t.Fatalf("query before committed clock = %v", err)
	}
	deposits.Close()
	if _, err := deposits.Piles(end); err != StaleReference {
		t.Fatalf("query after close = %v", err)
	}
	if piles[0].Mass != 4 || releases[0].Particle.BornUs != end {
		t.Fatal("copied outputs changed")
	}
}

func TestMountedParticleDeposits(t *testing.T) {
	options := DefaultParticleDepositsOptions()
	options.HoldUs = 10
	options.Release.Count = 1
	store, err := NewParticleDeposits(options)
	mustOK(t, err)
	defer store.Close()
	cell := Point{X: 1, Y: 1}
	_, err = store.Add(ParticleContact{Cell: cell, Mass: 2, Color: Color{Red: 255, Alpha: 255}})
	mustOK(t, err)

	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	style := DefaultParticleDepositPaint()
	style.PileGlyph, style.Release.Glyph = "+", "*"
	var now uint64
	var saved *Paint
	node, err := tree.Mount(Node{}, MountOptions{Enabled: true}, &panelPaintFixture{draw: func(paint *Paint) error {
		saved = paint
		return paint.DrawParticleDeposits(store, style, now, MotionReduced)
	}})
	mustOK(t, err)
	for _, step := range []struct {
		at    uint64
		glyph string
	}{{9, "+"}, {10, "*"}} {
		now = step.at
		mustOK(t, tree.Invalidate(node, InvalidatePaint))
		mustOK(t, tree.Publish(Size{4, 3}))
		frame, err := tree.Frame()
		mustOK(t, err)
		cellView, err := frame.Cell(cell)
		mustOK(t, err)
		if cellView.Owner == 0 || cellView.Glyph != step.glyph {
			t.Fatalf("draw at %d = %+v", now, cellView)
		}
	}
	if err := saved.DrawParticleDeposits(store, style, now, MotionReduced); err != StaleReference {
		t.Fatalf("escaped paint = %v", err)
	}
}
