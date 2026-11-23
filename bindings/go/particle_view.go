package nocturne

/*
#include <nocturne/particle.h>
*/
import "C"

type ParticlePaintOptions struct {
	Glyph        string
	Width        WidthPolicy
	Trail        ParticleTrailOptions
	TrailOpacity float64
}

func DefaultParticlePaintOptions() ParticlePaintOptions {
	return particlePaintFromNative(C.nt_particle_paint_options_default())
}

func particlePaintFromNative(options C.NtParticlePaintOptions) ParticlePaintOptions {
	return ParticlePaintOptions{Glyph: C.GoStringN(options.glyph.data, C.int(options.glyph.count)),
		Width:        WidthPolicy{Ambiguous: WidthChoice(options.width.ambiguous), Emoji: WidthChoice(options.width.emoji)},
		Trail:        ParticleTrailOptions{SpacingUs: uint64(options.trail.spacing_us), Samples: uint32(options.trail.samples)},
		TrailOpacity: float64(options.trail_opacity)}
}

func (options ParticlePaintOptions) native(arena *RawArena) (C.NtParticlePaintOptions, error) {
	if len(options.Glyph) > C.NT_PARTICLE_MAX_GLYPH_BYTES {
		return C.NtParticlePaintOptions{}, OutOfRange
	}
	return options.encode(arena)
}

func (options ParticlePaintOptions) encode(arena *RawArena) (C.NtParticlePaintOptions, error) {
	glyph, err := arenaByteView(arena, options.Glyph)
	if err != nil {
		return C.NtParticlePaintOptions{}, err
	}
	return C.NtParticlePaintOptions{glyph: glyph, width: options.Width.native(),
		trail:         C.NtParticleTrailOptions{spacing_us: C.NtTimeUs(options.Trail.SpacingUs), samples: C.uint32_t(options.Trail.Samples)},
		trail_opacity: C.double(options.TrailOpacity)}, nil
}

type ParticleViewOptions struct {
	Emitter         ParticleEmitterOptions
	Paint           ParticlePaintOptions
	Preferred       Size
	FrameIntervalUs uint64
}

func DefaultParticleViewOptions() ParticleViewOptions {
	return particleViewFromNative(C.nt_particle_view_options_default())
}

func particleViewFromNative(options C.NtParticleViewOptions) ParticleViewOptions {
	return ParticleViewOptions{Emitter: particleEmitterOptionsFromNative(options.emitter), Paint: particlePaintFromNative(options.paint),
		Preferred: sizeFromNative(options.preferred), FrameIntervalUs: uint64(options.frame_interval_us)}
}

func (options ParticleViewOptions) native(arena *RawArena) (C.NtParticleViewOptions, error) {
	paint, err := options.Paint.native(arena)
	if err != nil {
		return C.NtParticleViewOptions{}, err
	}
	return C.NtParticleViewOptions{emitter: options.Emitter.native(), paint: paint, preferred: options.Preferred.native(),
		frame_interval_us: C.NtTimeUs(options.FrameIntervalUs)}, nil
}

// MountParticleView copies the emitter and paint options into a clocked leaf.
func (tree *Tree) MountParticleView(parent Node, mount MountOptions, options ParticleViewOptions) (Node, error) {
	var arena RawArena
	defer arena.close()
	native, err := options.native(&arena)
	if err != nil {
		return Node{}, err
	}
	return tree.mountNative(parent, func(tree *C.NtTree, parent C.NtNodeId, out *C.NtNodeId) C.NtResult {
		return C.nt_particle_view_mount(tree, parent, mount.native(), &native, out)
	})
}

func (tree *Tree) SetParticleView(node Node, options ParticleViewOptions) error {
	var arena RawArena
	defer arena.close()
	native, err := options.native(&arena)
	if err != nil {
		return err
	}
	return tree.callNode(node, func(tree *C.NtTree, id C.NtNodeId) C.NtResult {
		return C.nt_particle_view_set(tree, id, &native)
	})
}

// DrawParticles paints an emitter through this component's local clip and owner.
func (context *Paint) DrawParticles(emitter *ParticleEmitter, options ParticlePaintOptions, now uint64, motion Motion) error {
	if err := context.check(); err != nil {
		return err
	}
	var arena RawArena
	defer arena.close()
	paint, err := options.native(&arena)
	if err != nil {
		return err
	}
	return emitter.withNative(func(native *C.NtParticleEmitter) error {
		return check(C.nt_particle_emitter_draw(native, context.native, C.NtTimeUs(now), C.NtMotionPolicy(motion), &paint))
	})
}
