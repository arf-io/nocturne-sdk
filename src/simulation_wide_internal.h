#ifndef NOCTURNE_INTERNAL_SIMULATION_WIDE_H
#define NOCTURNE_INTERNAL_SIMULATION_WIDE_H

#include "simulation_internal.h"
#include <string.h>

/* Homogeneous tetrahedral and squared pose expressions can require
 * wider dyadics than spatial queries. The extra width stays local to
 * these simulation operations. */
enum { NT_SIMULATION_WIDE_LIMBS = 1280 };
typedef struct NtSimulationWide {
    uint32_t limbs[NT_SIMULATION_WIDE_LIMBS];
    int offset;
    unsigned count;
    bool negative;
} NtSimulationWide;

void nt_simulation_wide_number(NtSimulationWide *out, double value, int power);
void nt_simulation_wide_copy(NtSimulationWide *out, const NtSimulationWide *value);
void nt_simulation_wide_add(NtSimulationWide *out, const NtSimulationWide *a, const NtSimulationWide *b);
void nt_simulation_wide_subtract(NtSimulationWide *out, const NtSimulationWide *a, const NtSimulationWide *b);
void nt_simulation_wide_multiply(NtSimulationWide *out, const NtSimulationWide *a, const NtSimulationWide *b);
double nt_simulation_wide_quotient(const NtSimulationWide *n, const NtSimulationWide *d);
unsigned nt_simulation_multiply_limbs(uint32_t *out, const uint32_t *a, unsigned a_count,
                                      const uint32_t *b, unsigned b_count);
int nt_simulation_compare_limbs(const uint32_t *a, unsigned a_count, int a_offset,
                                const uint32_t *b, unsigned b_count, int b_offset);
bool nt_simulation_norm_within(const NtSpatialExact n[3], const NtSpatialExact *limit);
double nt_simulation_round_norm(const NtSpatialExact n[3], const NtSpatialExact *unit, double estimate);
double nt_simulation_round_parallel_angle(const NtSpatialExact n[3], const NtSpatialExact *unit,
                                          const NtSpatialExact *initial, double angle_sum,
                                          bool opposite, bool *reversed);
void nt_simulation_angle_residual(const NtSpatialExact n[3], const NtSpatialExact *unit,
                                  const NtSpatialExact *initial, double angle_sum, NtSpatialExact *out);
void nt_simulation_tiny_vector(const NtSpatialExact turn[3], NtVec3 initial_axis, double angle,
                               const NtSpatialExact *unit, NtSpatialExact out[3]);
double nt_simulation_round_tiny_angle(const NtSpatialExact turn[3], NtVec3 initial_axis, double angle,
                                      const NtSpatialExact *unit, double estimate);

static inline void nt_simulation_widen(NtSimulationWide *out, const NtSpatialExact *value)
{
    out->offset = value->offset;
    out->count = value->count;
    out->negative = value->negative;
    memcpy(out->limbs,value->limbs,value->count*sizeof(value->limbs[0]));
}

#endif
