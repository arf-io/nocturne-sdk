package nocturne

import "testing"

func TestParticleEmitterClockAndOwnership(t *testing.T) {
	options := DefaultParticleEmitterOptions()
	options.PeriodUs, options.Capacity = 1, 2
	options.LifetimeMinUs, options.LifetimeMaxUs = 2, 2
	options.Seed, options.Stream = ^uint64(0), uint64(1)<<63+5
	emitter, err := NewParticleEmitter(options)
	mustOK(t, err)
	defer emitter.Close()
	options.PeriodUs = 0
	const now = ^uint64(0)
	samples, err := emitter.Sample(now)
	mustOK(t, err)
	if len(samples) != 2 || samples[0].ID != now-1 || samples[0].AgeUs != 1 ||
		samples[1].ID != now || samples[1].BornUs != now || samples[1].AgeUs != 0 {
		t.Fatalf("samples at clock limit = %+v", samples)
	}
	particle, alive, err := emitter.Particle(now-1, now)
	mustOK(t, err)
	if !alive || particle != samples[0] {
		t.Fatalf("random access = %+v, alive = %t", particle, alive)
	}
	_, alive, err = emitter.Particle(now-2, now)
	mustOK(t, err)
	if alive {
		t.Fatal("particle is alive at exact expiry")
	}
	trail, err := emitter.Trail(now-1, now, ParticleTrailOptions{SpacingUs: 1, Samples: 4})
	mustOK(t, err)
	if len(trail) != 1 || trail[0].ID != now-1 || trail[0].AgeUs != 0 {
		t.Fatalf("trail = %+v", trail)
	}
	deadline, err := emitter.Deadline(now, MotionFull, 0)
	mustOK(t, err)
	if deadline.Scheduled {
		t.Fatalf("clock limit deadline = %+v", deadline)
	}
	copy := *emitter
	emitter.Close()
	if _, err := copy.Sample(now); err != StaleReference {
		t.Fatalf("closed emitter copy = %v", err)
	}
	if samples[1].ID != now {
		t.Fatal("samples did not survive close")
	}
}
