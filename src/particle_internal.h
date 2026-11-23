#ifndef NOCTURNE_INTERNAL_PARTICLE_H
#define NOCTURNE_INTERNAL_PARTICLE_H

#include <nocturne/particle_effect.h>

#include "allocator.h"

struct NtParticleEmitter {
    NtAllocator allocator;
    NtParticleEmitterOptions options;
    NtParticleEffectMotion motion;
};

typedef struct NtParticleRangeId { uint64_t first; size_t count; } NtParticleRangeId;
typedef struct NtParticleAge { NtTimeUs born, lifetime, age; } NtParticleAge;

NtResult nt_particle_options_validate(const NtParticleEmitterOptions *options);
NtResult nt_particle_effect_motion_validate(const NtParticleEffectMotion *motion);
NtResult nt_particle_glyph_validate(NtByteView glyph, NtWidthPolicy width);
NtResult nt_particle_paint_validate(const NtParticlePaintOptions *options);
NtResult nt_particle_trail_validate(NtParticleTrailOptions trail);
NtParticleRangeId nt_particle_recent(const NtParticleEmitter *emitter, NtTimeUs now);
bool nt_particle_alive(const NtParticleEmitter *emitter, uint64_t id, NtTimeUs now, NtParticleAge *out);
NtResult nt_particle_sample_age(const NtParticleEmitter *emitter, uint64_t id, NtParticleAge age,
                                 bool preserve_cell, NtParticleSample *out);
uint32_t nt_particle_trail_count(NtParticleAge age, NtParticleTrailOptions trail);

#endif
