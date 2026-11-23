#ifndef NOCTURNE_PARTICLE_DEPOSITS_H
#define NOCTURNE_PARTICLE_DEPOSITS_H

#include <nocturne/particle.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { NT_PARTICLE_MAX_DEPOSITS = 256, NT_PARTICLE_MAX_RELEASE = 8 };

typedef struct NtParticleReleaseOptions {
    uint32_t count;                     /* Zero means the contact just expires. */
    NtTimeUs lifetime_min_us, lifetime_max_us;
    NtParticleRange velocity;
    NtPoint acceleration;
    NtRgba end_color;
    NtEasing color_easing;
} NtParticleReleaseOptions;

typedef struct NtParticleDepositsOptions {
    uint64_t seed, stream;
    uint32_t capacity;
    NtTimeUs hold_us;
    NtParticleReleaseOptions release;
} NtParticleDepositsOptions;

typedef struct NtParticleContact {
    NtCellPoint cell;
    NtTimeUs at;
    uint32_t mass;
    NtRgba color;
} NtParticleContact;

typedef struct NtParticlePile {
    NtCellPoint cell;
    uint32_t contacts;
    uint64_t mass;
    NtRgba color;                       /* Mass-weighted mean of straight RGBA. */
} NtParticlePile;

typedef struct NtParticleRelease {
    uint64_t deposit_id;
    NtParticleSample particle;
} NtParticleRelease;

typedef struct NtParticleDeposits NtParticleDeposits;
typedef struct NtParticleDepositPaint {
    NtByteView pile_glyph;
    NtWidthPolicy pile_width;
    NtParticlePaintOptions release;
} NtParticleDepositPaint;

NtParticleDepositsOptions nt_particle_deposits_options_default(void);
NtParticleDepositPaint nt_particle_deposit_paint_default(void);
NtResult nt_particle_deposits_create(const NtAllocator *allocator, const NtParticleDepositsOptions *options,
                                      NtParticleDeposits **out);
void nt_particle_deposits_destroy(NtParticleDeposits *deposits);

/* Mutations are monotonic, possibly at the same clock value. An accepted
 * contact owns a slot through its hold and last live release. A full
 * store rejects the new contact with OUT_OF_RANGE and changes nothing. */
NtResult nt_particle_deposits_add(NtParticleDeposits *deposits, NtParticleContact contact, uint64_t *id);
NtResult nt_particle_deposits_advance(NtParticleDeposits *deposits, NtTimeUs now);

/* Queries do not advance the committed clock. NULL/zero queries the count.
 * Insufficient space writes only the required count. Piles are in order
 * of oldest contributing contact, releases in deposit then particle order. */
NtResult nt_particle_deposits_piles(const NtParticleDeposits *deposits, NtTimeUs now,
                                     NtParticlePile *piles, size_t capacity, size_t *count);
NtResult nt_particle_deposits_releases(const NtParticleDeposits *deposits, NtTimeUs now,
                                        NtParticleRelease *samples, size_t capacity, size_t *count);
NtResult nt_particle_deposits_deadline(const NtParticleDeposits *deposits, NtTimeUs now,
                                        NtMotionPolicy motion, NtTimeUs frame_interval_us, NtDeadline *out);
/* Paint piles, then released particles and their trails in local cells. */
NtResult nt_particle_deposits_draw(const NtParticleDeposits *deposits, NtPaintContext *paint,
                                    NtTimeUs now, NtMotionPolicy motion, const NtParticleDepositPaint *options);

#ifdef __cplusplus
}
#endif
#endif
