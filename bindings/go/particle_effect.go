package nocturne

/*
#include <nocturne/particle_effect.h>
*/
import "C"

// ParticleEffectMotion adds radial velocity, sway and stepped opacity to an emitter.
type ParticleEffectMotion struct {
	Radial                        bool
	DirectionTurns, SweepTurns    float64
	SpeedMin, SpeedMax            float64
	SwayAmplitude                 ContinuousPoint
	SwayPeriodUs, GlitterPeriodUs uint64
	GlitterMinAlpha               uint8
}

func (motion ParticleEffectMotion) native() C.NtParticleEffectMotion {
	return C.NtParticleEffectMotion{
		radial: C.bool(motion.Radial), direction_turns: C.double(motion.DirectionTurns),
		sweep_turns: C.double(motion.SweepTurns), speed_min: C.double(motion.SpeedMin), speed_max: C.double(motion.SpeedMax),
		sway_amplitude: motion.SwayAmplitude.native(), sway_period_us: C.NtTimeUs(motion.SwayPeriodUs),
		glitter_period_us: C.NtTimeUs(motion.GlitterPeriodUs), glitter_min_alpha: C.uint8_t(motion.GlitterMinAlpha),
	}
}

// ParticleEffect holds editable emitter, paint, layout and motion options.
type ParticleEffect struct {
	View   ParticleViewOptions
	Motion ParticleEffectMotion
}

// ParticleEffectRecipe returns an independent copy of the native recipe defaults.
func ParticleEffectRecipe(recipe ParticleRecipe) (ParticleEffect, error) {
	if err := ensureABI(); err != nil {
		return ParticleEffect{}, err
	}
	var options C.NtParticleEffectOptions
	if err := check(C.nt_particle_effect_recipe(C.NtParticleRecipe(recipe), &options)); err != nil {
		return ParticleEffect{}, err
	}
	motion := options.motion
	return ParticleEffect{View: particleViewFromNative(options.view), Motion: ParticleEffectMotion{
		Radial: bool(motion.radial), DirectionTurns: float64(motion.direction_turns),
		SweepTurns: float64(motion.sweep_turns), SpeedMin: float64(motion.speed_min), SpeedMax: float64(motion.speed_max),
		SwayAmplitude: plotPointFromNative(motion.sway_amplitude), SwayPeriodUs: uint64(motion.sway_period_us),
		GlitterPeriodUs: uint64(motion.glitter_period_us), GlitterMinAlpha: uint8(motion.glitter_min_alpha),
	}}, nil
}

func (effect ParticleEffect) native(arena *RawArena) (C.NtParticleEffectOptions, error) {
	view, err := effect.View.native(arena)
	if err != nil {
		return C.NtParticleEffectOptions{}, err
	}
	return C.NtParticleEffectOptions{view: view, motion: effect.Motion.native()}, nil
}

// NewParticleEffectEmitter copies the emitter and motion options into an owned schedule.
func NewParticleEffectEmitter(effect ParticleEffect) (*ParticleEmitter, error) {
	if err := ensureABI(); err != nil {
		return nil, err
	}
	state := &particleEmitterState{}
	var native C.NtParticleEffectOptions
	native.view.emitter = effect.View.Emitter.native()
	native.motion = effect.Motion.native()
	if err := check(C.nt_particle_effect_emitter_create(nil, &native, &state.native)); err != nil {
		return nil, err
	}
	return &ParticleEmitter{state}, nil
}

// MountAuthoredParticleEffect copies an effect into a clocked particle leaf.
func (tree *Tree) MountAuthoredParticleEffect(parent Node, mount MountOptions, effect ParticleEffect) (Node, error) {
	var arena RawArena
	defer arena.close()
	native, err := effect.native(&arena)
	if err != nil {
		return Node{}, err
	}
	return tree.mountNative(parent, func(tree *C.NtTree, parent C.NtNodeId, out *C.NtNodeId) C.NtResult {
		return C.nt_particle_effect_mount(tree, parent, mount.native(), &native, out)
	})
}

// SetAuthoredParticleEffect replaces a mounted particle leaf's view and motion law.
func (tree *Tree) SetAuthoredParticleEffect(node Node, effect ParticleEffect) error {
	var arena RawArena
	defer arena.close()
	native, err := effect.native(&arena)
	if err != nil {
		return err
	}
	return tree.callNode(node, func(tree *C.NtTree, id C.NtNodeId) C.NtResult {
		return C.nt_particle_effect_set(tree, id, &native)
	})
}
