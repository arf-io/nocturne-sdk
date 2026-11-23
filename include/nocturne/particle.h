#ifndef NOCTURNE_PARTICLE_H
#define NOCTURNE_PARTICLE_H

#include <nocturne/component.h>
#include <nocturne/motion.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    NT_PARTICLE_MAX_CAPACITY = 4096, NT_PARTICLE_MAX_BATCH = 4096,
    NT_PARTICLE_MAX_TRAIL_SAMPLES = 16, NT_PARTICLE_MAX_GLYPH_BYTES = 128
};

typedef struct NtParticleRange { NtPoint min, max; } NtParticleRange;

typedef struct NtParticleEmitterOptions {
    uint64_t seed, stream;
    NtTimeUs start_us, period_us;       /* Zero period produces one batch. */
    uint64_t emission_limit;           /* Zero allows the full representable schedule. */
    uint32_t births_per_tick, capacity;
    NtTimeUs lifetime_min_us, lifetime_max_us;
    NtParticleRange origin, velocity;
    NtPoint acceleration;
    NtRgba birth_color, end_color;
    NtEasing color_easing;
} NtParticleEmitterOptions;

typedef struct NtParticleEmitter NtParticleEmitter;
typedef struct NtParticleSample {
    uint64_t id;                       /* Zero-based birth ordinal. */
    NtTimeUs born_us, age_us, lifetime_us;
    double progress;
    NtPoint position, velocity;
    NtRgba color;
} NtParticleSample;
typedef struct NtParticleTrailOptions { NtTimeUs spacing_us; uint32_t samples; } NtParticleTrailOptions;

NtParticleEmitterOptions nt_particle_emitter_options_default(void);
NtResult nt_particle_emitter_create(const NtAllocator *allocator, const NtParticleEmitterOptions *options,
                                    NtParticleEmitter **out);
void nt_particle_emitter_destroy(NtParticleEmitter *emitter);
/* Sample live particles among the newest capacity scheduled births, in ID
 * order. NULL/zero queries the count. Insufficient capacity writes only
 * the required count. Other failures leave both outputs untouched. */
NtResult nt_particle_emitter_sample(const NtParticleEmitter *emitter, NtTimeUs now,
                                    NtParticleSample *samples, size_t capacity, size_t *count);
/* Pure analytic trajectory, independent of the presentation budget.
 * An unborn, expired or unscheduled ID gives alive=false and no sample. */
NtResult nt_particle_emitter_particle(const NtParticleEmitter *emitter, uint64_t id, NtTimeUs now,
                                      bool *alive, NtParticleSample *out);
/* Samples behind a live head with the given ID, oldest first. Does not
 * include the head or any time before birth. Uses the same buffer rules. */
NtResult nt_particle_emitter_trail(const NtParticleEmitter *emitter, uint64_t id, NtTimeUs now,
                                   NtParticleTrailOptions trail, NtParticleSample *samples,
                                   size_t capacity, size_t *count);
/* Next admitted birth, expiry or full-motion sample. A zero frame interval
 * selects 16 ms. No deadline beyond the representable clock is returned. */
NtResult nt_particle_emitter_deadline(const NtParticleEmitter *emitter, NtTimeUs now,
                                      NtMotionPolicy motion, NtTimeUs frame_interval_us, NtDeadline *out);

typedef struct NtParticlePaintOptions {
    NtByteView glyph;                   /* One display cell under width. */
    NtWidthPolicy width;
    NtParticleTrailOptions trail;
    double trail_opacity;
} NtParticlePaintOptions;
typedef struct NtParticleViewOptions {
    NtParticleEmitterOptions emitter;
    NtParticlePaintOptions paint;
    NtCellSize preferred;
    NtTimeUs frame_interval_us;
} NtParticleViewOptions;

NtParticlePaintOptions nt_particle_paint_options_default(void);
NtParticleViewOptions nt_particle_view_options_default(void);
/* Draw in component-local cell coordinates. Reduced motion keeps births
 * and expiry while using stationary birth positions and no trails. */
NtResult nt_particle_emitter_draw(const NtParticleEmitter *emitter, NtPaintContext *paint, NtTimeUs now,
                                  NtMotionPolicy motion, const NtParticlePaintOptions *options);
/* A timed leaf with owned emitter settings and glyph bytes. Set replaces
 * its complete configuration and keeps the latest observed motion policy. */
NtResult nt_particle_view_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount,
                                const NtParticleViewOptions *options, NtNodeId *out);
NtResult nt_particle_view_set(NtTree *tree, NtNodeId node, const NtParticleViewOptions *options);

#ifdef __cplusplus
}
#endif
#endif
