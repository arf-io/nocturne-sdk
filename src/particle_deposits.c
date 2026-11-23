#include <nocturne/particle_deposits.h>

#include "particle_internal.h"
#include "seed_internal.h"

typedef struct DepositEntry {
    NtParticleContact contact;
    uint64_t id;
} DepositEntry;

struct NtParticleDeposits {
    NtAllocator allocator;
    NtParticleEmitter release;
    NtTimeUs hold, watermark;
    uint64_t next_id;
    uint32_t capacity, count;
    DepositEntry entries[];
};

NT_DECLARE_ALIGNMENT(NtParticleEmitter);
NT_DECLARE_ALIGNMENT(DepositEntry);

static size_t deposit_alignment(void)
{
    const size_t emitter = NT_ALIGNMENT(NtParticleEmitter), entry = NT_ALIGNMENT(DepositEntry);
    return emitter > entry ? emitter : entry;
}

NtParticleDepositsOptions nt_particle_deposits_options_default(void)
{
    return (NtParticleDepositsOptions){.capacity=32,.hold_us=800000,
        .release={.lifetime_min_us=300000,.lifetime_max_us=500000,
                  .velocity={{-2,-5},{2,-2}},.acceleration={0,6},.color_easing=NT_EASE_OUT}};
}

NtParticleDepositPaint nt_particle_deposit_paint_default(void)
{
    return (NtParticleDepositPaint){.pile_glyph={"·",2},.pile_width={NT_WIDTH_ONE,NT_WIDTH_TWO},
                                    .release=nt_particle_paint_options_default()};
}

static size_t allocation_size(uint32_t capacity)
{
    return sizeof(NtParticleDeposits)+(size_t)capacity*sizeof(DepositEntry);
}

NtResult nt_particle_deposits_create(const NtAllocator *provided, const NtParticleDepositsOptions *options,
                                      NtParticleDeposits **out)
{
    NtAllocator allocator;
    if (!options || !out || !options->hold_us || !options->capacity || !nt_allocator_get(provided,&allocator))
        return NT_RESULT_INVALID_ARGUMENT;
    const NtParticleReleaseOptions release = options->release;
    if (options->capacity > NT_PARTICLE_MAX_DEPOSITS || release.count > NT_PARTICLE_MAX_RELEASE)
        return NT_RESULT_OUT_OF_RANGE;
    const NtParticleEmitterOptions emitter = {
        .seed=options->seed,.stream=options->stream,.births_per_tick=release.count,.capacity=release.count,
        .lifetime_min_us=release.lifetime_min_us,.lifetime_max_us=release.lifetime_max_us,
        .velocity=release.velocity,.acceleration=release.acceleration,
        .end_color=release.end_color,.color_easing=release.color_easing
    };
    if (release.count) {
        const NtResult result = nt_particle_options_validate(&emitter);
        if (result != NT_RESULT_OK) return result;
    }
    NtParticleDeposits *deposits = nt_allocate(&allocator,allocation_size(options->capacity),deposit_alignment());
    if (!deposits) return NT_RESULT_ALLOCATION_FAILED;
    deposits->allocator = allocator;
    deposits->release = (NtParticleEmitter){.options=emitter};
    deposits->hold = options->hold_us;
    deposits->watermark = 0;
    deposits->next_id = 0;
    deposits->capacity = options->capacity;
    deposits->count = 0;
    *out = deposits;
    return NT_RESULT_OK;
}

void nt_particle_deposits_destroy(NtParticleDeposits *deposits)
{
    if (!deposits) return;
    const NtAllocator allocator = deposits->allocator;
    nt_release(&allocator,deposits,allocation_size(deposits->capacity),deposit_alignment());
}

static bool holding(const NtParticleDeposits *deposits, const DepositEntry *entry, NtTimeUs now)
{
    return now-entry->contact.at < deposits->hold;
}

/* Used after hold has ended, so birth is representable and no later than now. */
static NtParticleEmitter released(const NtParticleDeposits *deposits, const DepositEntry *entry)
{
    NtParticleEmitter emitter = deposits->release;
    const NtPoint cell_center = {(double)entry->contact.cell.x+.5,(double)entry->contact.cell.y+.5};
    emitter.options.origin = (NtParticleRange){cell_center,cell_center};
    emitter.options.start_us = entry->contact.at+deposits->hold;
    emitter.options.seed = nt_seed_bits(emitter.options.seed,emitter.options.stream,entry->id);
    emitter.options.birth_color = entry->contact.color;
    return emitter;
}

static bool retained(const NtParticleDeposits *deposits, const DepositEntry *entry, NtTimeUs now)
{
    if (holding(deposits,entry,now)) return true;
    const NtParticleEmitter emitter = released(deposits,entry);
    for (uint32_t i = 0; i < emitter.options.births_per_tick; ++i) {
        NtParticleAge age;
        if (nt_particle_alive(&emitter,i,now,&age)) return true;
    }
    return false;
}

static void compact(NtParticleDeposits *deposits, NtTimeUs now)
{
    uint32_t kept = 0;
    for (uint32_t i = 0; i < deposits->count; ++i) {
        if (retained(deposits,&deposits->entries[i],now))
            deposits->entries[kept++] = deposits->entries[i];
    }
    deposits->count = kept;
    deposits->watermark = now;
}

NtResult nt_particle_deposits_add(NtParticleDeposits *deposits, NtParticleContact contact, uint64_t *id)
{
    if (!deposits || !contact.mass || contact.at < deposits->watermark) return NT_RESULT_INVALID_ARGUMENT;
    if (deposits->next_id == UINT64_MAX) return NT_RESULT_OUT_OF_RANGE;
    if (deposits->count == deposits->capacity) {
        uint32_t i = 0;
        while (i < deposits->count && retained(deposits,&deposits->entries[i],contact.at)) ++i;
        if (i == deposits->count) return NT_RESULT_OUT_OF_RANGE;
    }
    compact(deposits,contact.at);
    const uint64_t assigned = deposits->next_id++;
    deposits->entries[deposits->count++] = (DepositEntry){contact,assigned};
    if (id) *id = assigned;
    return NT_RESULT_OK;
}

NtResult nt_particle_deposits_advance(NtParticleDeposits *deposits, NtTimeUs now)
{
    if (!deposits || now < deposits->watermark) return NT_RESULT_INVALID_ARGUMENT;
    compact(deposits,now);
    return NT_RESULT_OK;
}

typedef struct PileAccumulator {
    NtCellPoint cell;
    uint32_t contacts;
    uint64_t mass, red, green, blue, alpha;
} PileAccumulator;

static size_t collect_piles(const NtParticleDeposits *deposits, NtTimeUs now, PileAccumulator *piles)
{
    size_t count = 0;
    for (uint32_t i = 0; i < deposits->count; ++i) {
        const DepositEntry *entry = &deposits->entries[i];
        if (!holding(deposits,entry,now)) continue;
        const NtParticleContact contact = entry->contact;
        size_t slot = 0;
        while (slot < count && (piles[slot].cell.x != contact.cell.x || piles[slot].cell.y != contact.cell.y)) ++slot;
        if (slot == count) piles[count++] = (PileAccumulator){.cell=contact.cell};
        PileAccumulator *pile = &piles[slot];
        const uint64_t mass = contact.mass;
        ++pile->contacts;
        pile->mass += mass;
        pile->red += mass*contact.color.red;
        pile->green += mass*contact.color.green;
        pile->blue += mass*contact.color.blue;
        pile->alpha += mass*contact.color.alpha;
    }
    return count;
}

static NtRgba pile_color(PileAccumulator pile)
{
    const uint64_t half = pile.mass/2;
    return (NtRgba){(uint8_t)((pile.red+half)/pile.mass),(uint8_t)((pile.green+half)/pile.mass),
                    (uint8_t)((pile.blue+half)/pile.mass),(uint8_t)((pile.alpha+half)/pile.mass)};
}

NtResult nt_particle_deposits_piles(const NtParticleDeposits *deposits, NtTimeUs now,
                                     NtParticlePile *piles, size_t capacity, size_t *count)
{
    if (!deposits || now < deposits->watermark || !count || (!piles && capacity)) return NT_RESULT_INVALID_ARGUMENT;
    PileAccumulator staged[NT_PARTICLE_MAX_DEPOSITS];
    const size_t found = collect_piles(deposits,now,staged);
    *count = found;
    if (!piles) return NT_RESULT_OK;
    if (capacity < found) return NT_RESULT_INSUFFICIENT_BUFFER;
    for (size_t i = 0; i < found; ++i) {
        const PileAccumulator pile = staged[i];
        piles[i] = (NtParticlePile){pile.cell,pile.contacts,pile.mass,pile_color(pile)};
    }
    return NT_RESULT_OK;
}

static NtResult collect_releases(const NtParticleDeposits *deposits, NtTimeUs now, NtParticleRelease *samples,
                                 size_t *count)
{
    size_t found = 0;
    for (uint32_t i = 0; i < deposits->count; ++i) {
        const DepositEntry *entry = &deposits->entries[i];
        if (holding(deposits,entry,now)) continue;
        const NtParticleEmitter emitter = released(deposits,entry);
        for (uint32_t j = 0; j < emitter.options.births_per_tick; ++j) {
            NtParticleAge age;
            if (!nt_particle_alive(&emitter,j,now,&age)) continue;
            NtParticleSample sample;
            const NtResult result = nt_particle_sample_age(&emitter,j,age,false,&sample);
            if (result != NT_RESULT_OK) return result;
            if (samples) samples[found] = (NtParticleRelease){entry->id,sample};
            ++found;
        }
    }
    *count = found;
    return NT_RESULT_OK;
}

NtResult nt_particle_deposits_releases(const NtParticleDeposits *deposits, NtTimeUs now,
                                        NtParticleRelease *samples, size_t capacity, size_t *count)
{
    if (!deposits || now < deposits->watermark || !count || (!samples && capacity)) return NT_RESULT_INVALID_ARGUMENT;
    size_t live;
    const NtResult result = collect_releases(deposits,now,NULL,&live);
    if (result != NT_RESULT_OK) return result;
    if (samples && capacity < live) { *count = live; return NT_RESULT_INSUFFICIENT_BUFFER; }
    if (samples) return collect_releases(deposits,now,samples,count);
    *count = live;
    return NT_RESULT_OK;
}

static bool valid_motion(NtMotionPolicy motion)
{
    return motion == NT_MOTION_FULL || motion == NT_MOTION_REDUCED;
}

NtResult nt_particle_deposits_deadline(const NtParticleDeposits *deposits, NtTimeUs now,
                                        NtMotionPolicy motion, NtTimeUs interval, NtDeadline *out)
{
    if (!deposits || now < deposits->watermark || !out || !valid_motion(motion)) return NT_RESULT_INVALID_ARGUMENT;
    NtDeadline next = {0};
    for (uint32_t i = 0; i < deposits->count; ++i) {
        const DepositEntry *entry = &deposits->entries[i];
        NtDeadline scheduled = {0};
        if (holding(deposits,entry,now)) {
            if (deposits->hold <= UINT64_MAX-entry->contact.at)
                scheduled = (NtDeadline){true,entry->contact.at+deposits->hold};
        } else if (deposits->release.options.births_per_tick) {
            const NtParticleEmitter emitter = released(deposits,entry);
            const NtResult result = nt_particle_emitter_deadline(&emitter,now,motion,interval,&scheduled);
            if (result != NT_RESULT_OK) return result;
        }
        if (scheduled.scheduled && (!next.scheduled || scheduled.at < next.at)) next = scheduled;
    }
    *out = next;
    return NT_RESULT_OK;
}

NtResult nt_particle_deposits_draw(const NtParticleDeposits *deposits, NtPaintContext *paint,
                                    NtTimeUs now, NtMotionPolicy motion, const NtParticleDepositPaint *options)
{
    if (!deposits || now < deposits->watermark || !paint || !options || !valid_motion(motion))
        return NT_RESULT_INVALID_ARGUMENT;
    NtResult result = nt_particle_glyph_validate(options->pile_glyph,options->pile_width);
    if (result != NT_RESULT_OK) return result;
    const bool has_release = deposits->release.options.births_per_tick != 0;
    if (has_release) {
        result = nt_particle_paint_validate(&options->release);
        if (result != NT_RESULT_OK) return result;
    }
    const NtCellRect clip = nt_paint_clip(paint);
    if (!clip.size.columns || !clip.size.rows) return NT_RESULT_OK;
    PileAccumulator staged[NT_PARTICLE_MAX_DEPOSITS];
    const size_t piles = collect_piles(deposits,now,staged);
    for (size_t i = 0; i < piles; ++i) {
        const PileAccumulator pile = staged[i];
        if (pile.cell.x < clip.origin.x || pile.cell.y < clip.origin.y ||
            pile.cell.x >= (int64_t)clip.origin.x+clip.size.columns ||
            pile.cell.y >= (int64_t)clip.origin.y+clip.size.rows) continue;
        const NtRgba color = pile_color(pile);
        if (color.alpha) {
            result = nt_paint_glyph(paint,pile.cell,options->pile_glyph,1,(NtCellStyle){.foreground=color});
            if (result != NT_RESULT_OK) return result;
        }
    }
    if (!has_release) return NT_RESULT_OK;
    for (uint32_t i = 0; i < deposits->count; ++i) {
        const DepositEntry *entry = &deposits->entries[i];
        if (holding(deposits,entry,now)) continue;
        const NtParticleEmitter emitter = released(deposits,entry);
        result = nt_particle_emitter_draw(&emitter,paint,now,motion,&options->release);
        if (result != NT_RESULT_OK) return result;
    }
    return NT_RESULT_OK;
}
