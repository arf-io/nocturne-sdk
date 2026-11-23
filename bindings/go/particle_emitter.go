package nocturne

/*
#include <nocturne/particle.h>
*/
import "C"

import (
	"sync"
	"unsafe"
)

type ParticleRange struct{ Min, Max ContinuousPoint }

func (span ParticleRange) native() C.NtParticleRange {
	return C.NtParticleRange{min: span.Min.native(), max: span.Max.native()}
}

// ParticleEmitterOptions maps to the native schedule fields. Start with
// DefaultParticleEmitterOptions to retain the periodic and visible defaults.
type ParticleEmitterOptions struct {
	Seed, Stream                 uint64
	StartUs, PeriodUs            uint64
	EmissionLimit                uint64
	BirthsPerTick, Capacity      uint32
	LifetimeMinUs, LifetimeMaxUs uint64
	Origin, Velocity             ParticleRange
	Acceleration                 ContinuousPoint
	BirthColor, EndColor         Color
	ColorEasing                  Easing
}

func DefaultParticleEmitterOptions() ParticleEmitterOptions {
	return particleEmitterOptionsFromNative(C.nt_particle_emitter_options_default())
}

func particleEmitterOptionsFromNative(value C.NtParticleEmitterOptions) ParticleEmitterOptions {
	return ParticleEmitterOptions{Seed: uint64(value.seed), Stream: uint64(value.stream),
		StartUs: uint64(value.start_us), PeriodUs: uint64(value.period_us), EmissionLimit: uint64(value.emission_limit),
		BirthsPerTick: uint32(value.births_per_tick), Capacity: uint32(value.capacity),
		LifetimeMinUs: uint64(value.lifetime_min_us), LifetimeMaxUs: uint64(value.lifetime_max_us),
		Origin:       ParticleRange{plotPointFromNative(value.origin.min), plotPointFromNative(value.origin.max)},
		Velocity:     ParticleRange{plotPointFromNative(value.velocity.min), plotPointFromNative(value.velocity.max)},
		Acceleration: plotPointFromNative(value.acceleration), BirthColor: colorFromNative(value.birth_color),
		EndColor: colorFromNative(value.end_color), ColorEasing: Easing(value.color_easing)}
}

func (options ParticleEmitterOptions) native() C.NtParticleEmitterOptions {
	return C.NtParticleEmitterOptions{seed: C.uint64_t(options.Seed), stream: C.uint64_t(options.Stream),
		start_us: C.NtTimeUs(options.StartUs), period_us: C.NtTimeUs(options.PeriodUs), emission_limit: C.uint64_t(options.EmissionLimit),
		births_per_tick: C.uint32_t(options.BirthsPerTick), capacity: C.uint32_t(options.Capacity),
		lifetime_min_us: C.NtTimeUs(options.LifetimeMinUs), lifetime_max_us: C.NtTimeUs(options.LifetimeMaxUs),
		origin: options.Origin.native(), velocity: options.Velocity.native(), acceleration: options.Acceleration.native(),
		birth_color: options.BirthColor.native(), end_color: options.EndColor.native(), color_easing: C.NtEasing(options.ColorEasing)}
}

type ParticleSample struct {
	ID, BornUs, AgeUs, LifetimeUs uint64
	Progress                      float64
	Position, Velocity            ContinuousPoint
	Color                         Color
}

type ParticleTrailOptions struct {
	SpacingUs uint64
	Samples   uint32
}

func particleFromNative(value C.NtParticleSample) ParticleSample {
	return ParticleSample{ID: uint64(value.id), BornUs: uint64(value.born_us), AgeUs: uint64(value.age_us),
		LifetimeUs: uint64(value.lifetime_us), Progress: float64(value.progress),
		Position: plotPointFromNative(value.position), Velocity: plotPointFromNative(value.velocity), Color: colorFromNative(value.color)}
}

// ParticleEmitter owns an immutable seeded schedule. Copies share its owner.
type ParticleEmitter struct{ state *particleEmitterState }

type particleEmitterState struct {
	mu     sync.RWMutex
	native *C.NtParticleEmitter
}

func NewParticleEmitter(options ParticleEmitterOptions) (*ParticleEmitter, error) {
	if err := ensureABI(); err != nil {
		return nil, err
	}
	state := &particleEmitterState{}
	native := options.native()
	if err := check(C.nt_particle_emitter_create(nil, &native, &state.native)); err != nil {
		return nil, err
	}
	return &ParticleEmitter{state}, nil
}

func (emitter *ParticleEmitter) withNative(call func(*C.NtParticleEmitter) error) error {
	if emitter == nil || emitter.state == nil {
		return StaleReference
	}
	state := emitter.state
	state.mu.RLock()
	defer state.mu.RUnlock()
	if state.native == nil {
		return StaleReference
	}
	return call(state.native)
}

func (emitter *ParticleEmitter) Close() {
	if emitter == nil || emitter.state == nil {
		return
	}
	state := emitter.state
	state.mu.Lock()
	defer state.mu.Unlock()
	C.nt_particle_emitter_destroy(state.native)
	state.native = nil
}

func (emitter *ParticleEmitter) samples(read func(*C.NtParticleEmitter, *C.NtParticleSample, C.size_t, *C.size_t) C.NtResult) ([]ParticleSample, error) {
	var samples []ParticleSample
	err := emitter.withNative(func(native *C.NtParticleEmitter) error {
		var count C.size_t
		if err := check(read(native, nil, 0, &count)); err != nil || count == 0 {
			return err
		}
		values := make([]C.NtParticleSample, int(count))
		if err := check(read(native, unsafe.SliceData(values), count, &count)); err != nil {
			return err
		}
		samples = make([]ParticleSample, int(count))
		for index := range samples {
			samples[index] = particleFromNative(values[index])
		}
		return nil
	})
	return samples, err
}

// Sample copies the live particles admitted by the presentation capacity.
func (emitter *ParticleEmitter) Sample(now uint64) ([]ParticleSample, error) {
	return emitter.samples(func(native *C.NtParticleEmitter, out *C.NtParticleSample, capacity C.size_t, count *C.size_t) C.NtResult {
		return C.nt_particle_emitter_sample(native, C.NtTimeUs(now), out, capacity, count)
	})
}

// Particle samples one birth independently of the presentation capacity.
func (emitter *ParticleEmitter) Particle(id, now uint64) (ParticleSample, bool, error) {
	var value C.NtParticleSample
	var alive C.bool
	err := emitter.withNative(func(native *C.NtParticleEmitter) error {
		return check(C.nt_particle_emitter_particle(native, C.uint64_t(id), C.NtTimeUs(now), &alive, &value))
	})
	return particleFromNative(value), bool(alive), err
}

// Trail returns samples behind a live head, in oldest-first order.
func (emitter *ParticleEmitter) Trail(id, now uint64, trail ParticleTrailOptions) ([]ParticleSample, error) {
	input := C.NtParticleTrailOptions{spacing_us: C.NtTimeUs(trail.SpacingUs), samples: C.uint32_t(trail.Samples)}
	return emitter.samples(func(native *C.NtParticleEmitter, out *C.NtParticleSample, capacity C.size_t, count *C.size_t) C.NtResult {
		return C.nt_particle_emitter_trail(native, C.uint64_t(id), C.NtTimeUs(now), input, out, capacity, count)
	})
}

func (emitter *ParticleEmitter) Deadline(now uint64, motion Motion, frameIntervalUs uint64) (Deadline, error) {
	var value C.NtDeadline
	err := emitter.withNative(func(native *C.NtParticleEmitter) error {
		return check(C.nt_particle_emitter_deadline(native, C.NtTimeUs(now), C.NtMotionPolicy(motion),
			C.NtTimeUs(frameIntervalUs), &value))
	})
	return Deadline{Scheduled: bool(value.scheduled), At: uint64(value.at)}, err
}
