#ifndef NOCTURNE_PARTICLE_EFFECT_H
#define NOCTURNE_PARTICLE_EFFECT_H

#include <nocturne/particle.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NtParticleEffectMotion {
    bool radial;
    double direction_turns, sweep_turns;  /* The arc starts at direction. */
    double speed_min, speed_max;         /* Replaces Cartesian velocity in radial mode. */
    NtPoint sway_amplitude;
    NtTimeUs sway_period_us;              /* Zero disables sway. */
    NtTimeUs glitter_period_us;           /* Zero disables stepped opacity variation. */
    uint8_t glitter_min_alpha;            /* Minimum multiplier on a 0–255 scale. */
} NtParticleEffectMotion;

typedef struct NtParticleEffectOptions {
    NtParticleViewOptions view;
    NtParticleEffectMotion motion;
} NtParticleEffectOptions;

typedef enum NtParticleRecipe {
    NT_PARTICLE_RECIPE_BURST,
    NT_PARTICLE_RECIPE_NOVA,
    NT_PARTICLE_RECIPE_GLITTER,
    NT_PARTICLE_RECIPE_RAIN,
    NT_PARTICLE_RECIPE_SNOW,
    NT_PARTICLE_RECIPE_EMBERS,
    NT_PARTICLE_RECIPE_LEAVES,
    NT_PARTICLE_RECIPE_SPARKS,
    NT_PARTICLE_RECIPE_FIREWORK
} NtParticleRecipe;

/* Produces editable settings. Glyph views refer to immutable static bytes. */
NtResult nt_particle_effect_recipe(NtParticleRecipe recipe, NtParticleEffectOptions *out);
/* Standalone creation consumes only view.emitter and motion. The result is a
 * normal emitter for sampling, trails, deadlines and direct drawing. */
NtResult nt_particle_effect_emitter_create(const NtAllocator *allocator, const NtParticleEffectOptions *options,
                                           NtParticleEmitter **out);
NtResult nt_particle_effect_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount,
                                  const NtParticleEffectOptions *options, NtNodeId *out);
NtResult nt_particle_effect_set(NtTree *tree, NtNodeId node, const NtParticleEffectOptions *options);

enum { NT_VISUAL_BRANCH_MAX_SEGMENTS = 256 };

typedef struct NtVisualBranchOptions {
    uint64_t seed, stream;
    NtCellPoint start, end;
    uint32_t trunk_segments, branch_count, branch_segments;
    uint32_t jitter_cells, branch_reach_cells;
    NtTimeUs start_us, lifetime_us, reveal_us, flicker_us;
    NtRgba color;
    uint8_t flicker_min_alpha;
} NtVisualBranchOptions;

typedef struct NtVisualBranchSegment {
    uint32_t id, parent_id;   /* UINT32_MAX for the first segment. */
    NtCellPoint start, end;
    NtRgba color;
} NtVisualBranchSegment;

NtVisualBranchOptions nt_visual_branch_options_default(void);
/* Count query and insufficient-buffer behavior match particle sampling.
 * The returned prefix is in trunk-first, branch-major segment order. */
NtResult nt_visual_branch_sample(const NtVisualBranchOptions *options, NtTimeUs now, NtMotionPolicy motion,
                                 NtVisualBranchSegment *segments, size_t capacity, size_t *count);
NtResult nt_visual_branch_deadline(const NtVisualBranchOptions *options, NtTimeUs now, NtMotionPolicy motion,
                                   NtTimeUs frame_interval_us, NtDeadline *out);
/* Rasterize the sampled segments with an authored one-cell glyph. */
NtResult nt_visual_branch_draw(const NtVisualBranchOptions *options, NtPaintContext *paint, NtTimeUs now,
                               NtMotionPolicy motion, NtByteView glyph, NtWidthPolicy width);

#ifdef __cplusplus
}
#endif
#endif
