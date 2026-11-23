#ifndef NOCTURNE_INTERNAL_SEED_H
#define NOCTURNE_INTERNAL_SEED_H

#include <stdint.h>

/* SplitMix64's finalizer. All arithmetic intentionally wraps in uint64_t. */
static inline uint64_t nt_seed_bits(uint64_t seed, uint64_t stream, uint64_t index)
{
    uint64_t value = seed ^ (stream * UINT64_C(0xd1342543de82ef95));
    value += (index + 1) * UINT64_C(0x9e3779b97f4a7c15);
    value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}

static inline double nt_seed_unit(uint64_t seed, uint64_t stream, uint64_t index)
{
    return (double)(nt_seed_bits(seed,stream,index) >> 11) / 9007199254740992.0;
}

#endif
