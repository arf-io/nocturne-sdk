#ifndef NOCTURNE_INTERNAL_SPATIAL_NORM_H
#define NOCTURNE_INTERNAL_SPATIAL_NORM_H

#include "scaled_sum_internal.h"

#include <limits.h>

static inline NtScaledSum nt_spatial_difference(double a, double b)
{
    NtScaledSum result = {0};
    nt_scaled_add(&result, nt_scaled_term(a, 0));
    nt_scaled_add(&result, nt_scaled_term(-b, 0));
    return result;
}

static inline void nt_spatial_add_square(NtScaledSum *out, const NtScaledSum *value, int sign)
{
    for (size_t i = 0; i < value->count; ++i) {
        NtScaledTerm factor = value->partials[i];
        factor.fraction *= sign;
        nt_scaled_add_sum(out, value, factor);
    }
}

static inline NtScaledSum nt_spatial_norm_squared(const NtScaledSum components[3])
{
    NtScaledSum squared = {0};
    for (size_t i = 0; i < 3; ++i) nt_spatial_add_square(&squared, &components[i], 1);
    return squared;
}

static inline int nt_spatial_length_order(const NtScaledSum *squared, double prefix,
                                          NtScaledTerm a, NtScaledTerm b)
{
    NtScaledSum distance = {0};
    --a.exponent;
    --b.exponent;
    nt_scaled_add(&distance, a);
    nt_scaled_add(&distance, b);
    nt_scaled_add(&distance, nt_scaled_term(-prefix, 0));
    if (!distance.count || distance.partials[distance.count-1].fraction < 0) return 1;
    NtScaledSum residual = *squared;
    nt_spatial_add_square(&residual, &distance, -1);
    if (!residual.count) return 0;
    return residual.partials[residual.count-1].fraction > 0 ? 1 : -1;
}

/* Round prefix + sqrt(squared) once. Compare neighboring midpoints by
 * squaring their exact difference from prefix, including the overflow
 * midpoint between DBL_MAX and the next theoretical binary64 value. */
static inline double nt_spatial_round_length(const NtScaledSum *squared, double prefix)
{
    if (!squared->count) return prefix;
    NtScaledTerm estimate = nt_scaled_value(squared);
    const double root = estimate.exponent % 2 ? sqrt(estimate.fraction*2) : sqrt(estimate.fraction);
    if (estimate.exponent % 2) --estimate.exponent;
    const double rough = prefix + scalbn(root, estimate.exponent/2);
    double candidate = isfinite(rough) ? rough : DBL_MAX;
    for (;;) {
        const NtScaledTerm middle = nt_scaled_term(candidate, 0);
        const bool odd = odd_significand(candidate);
        if (candidate > 0) {
            const double lower = nextafter(candidate, 0);
            const int order = nt_spatial_length_order(squared, prefix, nt_scaled_term(lower, 0), middle);
            if (order < 0 || (order == 0 && odd)) {
                candidate = lower;
                continue;
            }
        }
        const double upper = nextafter(candidate, INFINITY);
        const NtScaledTerm next = isfinite(upper) ? nt_scaled_term(upper, 0) :
                                                   (NtScaledTerm){.5, DBL_MAX_EXP+1};
        const int order = nt_spatial_length_order(squared, prefix, middle, next);
        if (order < 0 || (order == 0 && !odd)) return candidate;
        if (!isfinite(upper)) return upper;
        candidate = upper;
    }
}

static inline bool nt_spatial_unit_terms(const NtScaledTerm components[3], NtVec3 *out)
{
    int exponent = INT_MIN;
    for (size_t i = 0; i < 3; ++i)
        if (components[i].fraction && components[i].exponent > exponent) exponent = components[i].exponent;
    if (exponent == INT_MIN) return false;
    double values[3];
    for (size_t i = 0; i < 3; ++i)
        values[i] = scalbn(components[i].fraction, components[i].exponent-exponent);
    const double norm = hypot(hypot(values[0], values[1]), values[2]);
    *out = (NtVec3){values[0]/norm, values[1]/norm, values[2]/norm};
    return true;
}

static inline bool nt_spatial_unit_sums(const NtScaledSum sums[3], NtVec3 *out)
{
    const NtScaledTerm components[] = {nt_scaled_value(&sums[0]), nt_scaled_value(&sums[1]),
                                       nt_scaled_value(&sums[2])};
    return nt_spatial_unit_terms(components, out);
}

#endif
