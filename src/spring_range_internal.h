#ifndef NOCTURNE_INTERNAL_SPRING_RANGE_H
#define NOCTURNE_INTERNAL_SPRING_RANGE_H

#include "scaled_sum_internal.h"
#include <stdint.h>

/* Round a near-limit value base + (numerator/divisor)*exp(-elapsed/response).
 * The polynomial numerator is exact, and the divisor is positive. */
double nt_spring_near_limit(double base, const NtScaledSum *numerator, const NtScaledSum *divisor,
                            uint64_t elapsed, uint64_t response, double approximate);

#endif
