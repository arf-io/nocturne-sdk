#ifndef NOCTURNE_INTERNAL_SPATIAL_EXACT_H
#define NOCTURNE_INTERNAL_SPATIAL_EXACT_H

#include <stdbool.h>
#include <stdint.h>

/* Values are signed dyadic integers with a shared exponent in limbs.
 * Ordinary expressions and quotient midpoint tests have at most seven
 * binary64 factors, spanning 14686 bits plus room for sums and alignment.
 * Comparing two ratios uses a separate double-width product. */
enum { NT_SPATIAL_EXACT_LIMBS = 480 };
typedef struct NtSpatialExact {
    uint32_t limbs[NT_SPATIAL_EXACT_LIMBS];
    int offset;
    unsigned count;
    bool negative;
} NtSpatialExact;

void nt_spatial_exact_number(NtSpatialExact *out, double value, int power);
void nt_spatial_exact_copy(NtSpatialExact *out, const NtSpatialExact *value);
void nt_spatial_exact_add(NtSpatialExact *out, const NtSpatialExact *a, const NtSpatialExact *b);
void nt_spatial_exact_subtract(NtSpatialExact *out, const NtSpatialExact *a, const NtSpatialExact *b);
void nt_spatial_exact_multiply(NtSpatialExact *out, const NtSpatialExact *a, const NtSpatialExact *b);
int nt_spatial_exact_compare(const NtSpatialExact *a, const NtSpatialExact *b);
int nt_spatial_exact_compare_scaled(const NtSpatialExact *a, const NtSpatialExact *b, double scale);
int nt_spatial_compare_ratios(const NtSpatialExact *a, const NtSpatialExact *a_divisor,
                              const NtSpatialExact *b, const NtSpatialExact *b_divisor);
double nt_spatial_exact_quotient(const NtSpatialExact *numerator, const NtSpatialExact *denominator);

static inline int nt_spatial_exact_sign(const NtSpatialExact *value)
{
    return value->count ? (value->negative ? -1 : 1) : 0;
}

static inline void nt_spatial_exact_negate(NtSpatialExact *value)
{
    if (value->count) value->negative = !value->negative;
}

#endif
