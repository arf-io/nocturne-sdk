package nocturne

/*
#include <nocturne/particle_deposits.h>
*/
import "C"

import (
	"sync"
	"unsafe"
)

type ParticleReleaseOptions struct {
	Count                        uint32
	LifetimeMinUs, LifetimeMaxUs uint64
	Velocity                     ParticleRange
	Acceleration                 ContinuousPoint
	EndColor                     Color
	ColorEasing                  Easing
}

// ParticleDepositsOptions configures an owned contact store. Start with
// DefaultParticleDepositsOptions for a valid hold duration and capacity.
type ParticleDepositsOptions struct {
	Seed, Stream uint64
	Capacity     uint32
	HoldUs       uint64
	Release      ParticleReleaseOptions
}

func DefaultParticleDepositsOptions() ParticleDepositsOptions {
	value := C.nt_particle_deposits_options_default()
	release := value.release
	return ParticleDepositsOptions{Seed: uint64(value.seed), Stream: uint64(value.stream),
		Capacity: uint32(value.capacity), HoldUs: uint64(value.hold_us),
		Release: ParticleReleaseOptions{
			Count: uint32(release.count), LifetimeMinUs: uint64(release.lifetime_min_us), LifetimeMaxUs: uint64(release.lifetime_max_us),
			Velocity:     ParticleRange{Min: plotPointFromNative(release.velocity.min), Max: plotPointFromNative(release.velocity.max)},
			Acceleration: plotPointFromNative(release.acceleration), EndColor: colorFromNative(release.end_color),
			ColorEasing: Easing(release.color_easing),
		}}
}

func (options ParticleDepositsOptions) native() C.NtParticleDepositsOptions {
	release := options.Release
	return C.NtParticleDepositsOptions{seed: C.uint64_t(options.Seed), stream: C.uint64_t(options.Stream),
		capacity: C.uint32_t(options.Capacity), hold_us: C.NtTimeUs(options.HoldUs),
		release: C.NtParticleReleaseOptions{
			count: C.uint32_t(release.Count), lifetime_min_us: C.NtTimeUs(release.LifetimeMinUs), lifetime_max_us: C.NtTimeUs(release.LifetimeMaxUs),
			velocity: release.Velocity.native(), acceleration: release.Acceleration.native(), end_color: release.EndColor.native(),
			color_easing: C.NtEasing(release.ColorEasing),
		}}
}

type ParticleContact struct {
	Cell  Point
	At    uint64
	Mass  uint32
	Color Color
}

type ParticlePile struct {
	Cell     Point
	Contacts uint32
	Mass     uint64
	Color    Color
}

type ParticleRelease struct {
	DepositID uint64
	Particle  ParticleSample
}

// ParticleDeposits owns a bounded store of contacts and their releases.
// Copies share its owner. Close releases the store.
type ParticleDeposits struct{ state *particleDepositsState }

type particleDepositsState struct {
	mu     sync.Mutex
	native *C.NtParticleDeposits
}

func NewParticleDeposits(options ParticleDepositsOptions) (*ParticleDeposits, error) {
	if err := ensureABI(); err != nil {
		return nil, err
	}
	state := &particleDepositsState{}
	input := options.native()
	if err := check(C.nt_particle_deposits_create(nil, &input, &state.native)); err != nil {
		return nil, err
	}
	return &ParticleDeposits{state}, nil
}

func (store *ParticleDeposits) withNative(call func(*C.NtParticleDeposits) error) error {
	if store == nil || store.state == nil {
		return StaleReference
	}
	state := store.state
	state.mu.Lock()
	defer state.mu.Unlock()
	if state.native == nil {
		return StaleReference
	}
	return call(state.native)
}

func (store *ParticleDeposits) Close() {
	if store == nil || store.state == nil {
		return
	}
	state := store.state
	state.mu.Lock()
	defer state.mu.Unlock()
	C.nt_particle_deposits_destroy(state.native)
	state.native = nil
}

func (store *ParticleDeposits) Add(contact ParticleContact) (uint64, error) {
	input := C.NtParticleContact{cell: contact.Cell.native(), at: C.NtTimeUs(contact.At),
		mass: C.uint32_t(contact.Mass), color: contact.Color.native()}
	var id C.uint64_t
	err := store.withNative(func(native *C.NtParticleDeposits) error {
		return check(C.nt_particle_deposits_add(native, input, &id))
	})
	return uint64(id), err
}

func (store *ParticleDeposits) Advance(now uint64) error {
	return store.withNative(func(native *C.NtParticleDeposits) error {
		return check(C.nt_particle_deposits_advance(native, C.NtTimeUs(now)))
	})
}

func particleDepositRows[Native, Row any](store *ParticleDeposits,
	read func(*C.NtParticleDeposits, *Native, C.size_t, *C.size_t) C.NtResult, decode func(Native) Row) ([]Row, error) {
	var rows []Row
	err := store.withNative(func(native *C.NtParticleDeposits) error {
		var count C.size_t
		if err := check(read(native, nil, 0, &count)); err != nil || count == 0 {
			return err
		}
		values := make([]Native, int(count))
		if err := check(read(native, unsafe.SliceData(values), count, &count)); err != nil {
			return err
		}
		rows = make([]Row, int(count))
		for i := range rows {
			rows[i] = decode(values[i])
		}
		return nil
	})
	return rows, err
}

func (store *ParticleDeposits) Piles(now uint64) ([]ParticlePile, error) {
	return particleDepositRows(store,
		func(native *C.NtParticleDeposits, out *C.NtParticlePile, capacity C.size_t, count *C.size_t) C.NtResult {
			return C.nt_particle_deposits_piles(native, C.NtTimeUs(now), out, capacity, count)
		},
		func(pile C.NtParticlePile) ParticlePile {
			return ParticlePile{Cell: pointFromNative(pile.cell), Contacts: uint32(pile.contacts),
				Mass: uint64(pile.mass), Color: colorFromNative(pile.color)}
		})
}

func (store *ParticleDeposits) Releases(now uint64) ([]ParticleRelease, error) {
	return particleDepositRows(store,
		func(native *C.NtParticleDeposits, out *C.NtParticleRelease, capacity C.size_t, count *C.size_t) C.NtResult {
			return C.nt_particle_deposits_releases(native, C.NtTimeUs(now), out, capacity, count)
		},
		func(release C.NtParticleRelease) ParticleRelease {
			return ParticleRelease{DepositID: uint64(release.deposit_id), Particle: particleFromNative(release.particle)}
		})
}

func (store *ParticleDeposits) Deadline(now uint64, motion Motion, frameIntervalUs uint64) (Deadline, error) {
	var value C.NtDeadline
	err := store.withNative(func(native *C.NtParticleDeposits) error {
		return check(C.nt_particle_deposits_deadline(native, C.NtTimeUs(now), C.NtMotionPolicy(motion),
			C.NtTimeUs(frameIntervalUs), &value))
	})
	return Deadline{Scheduled: bool(value.scheduled), At: uint64(value.at)}, err
}

type ParticleDepositPaint struct {
	PileGlyph string
	PileWidth WidthPolicy
	Release   ParticlePaintOptions
}

func DefaultParticleDepositPaint() ParticleDepositPaint {
	options := C.nt_particle_deposit_paint_default()
	return ParticleDepositPaint{
		PileGlyph: C.GoStringN(options.pile_glyph.data, C.int(options.pile_glyph.count)),
		PileWidth: WidthPolicy{Ambiguous: WidthChoice(options.pile_width.ambiguous), Emoji: WidthChoice(options.pile_width.emoji)},
		Release:   particlePaintFromNative(options.release),
	}
}

// DrawParticleDeposits paints held piles and releases through this component's clip.
func (context *Paint) DrawParticleDeposits(store *ParticleDeposits, options ParticleDepositPaint, now uint64, motion Motion) error {
	if err := context.check(); err != nil {
		return err
	}
	var arena RawArena
	defer arena.close()
	glyph, err := arenaByteView(&arena, options.PileGlyph)
	if err != nil {
		return err
	}
	release, err := options.Release.encode(&arena)
	if err != nil {
		return err
	}
	input := C.NtParticleDepositPaint{pile_glyph: glyph, pile_width: options.PileWidth.native(), release: release}
	return store.withNative(func(native *C.NtParticleDeposits) error {
		return check(C.nt_particle_deposits_draw(native, context.native, C.NtTimeUs(now), C.NtMotionPolicy(motion), &input))
	})
}
