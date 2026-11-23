#ifndef NOCTURNE_INTERNAL_SCALED_TICKS_H
#define NOCTURNE_INTERNAL_SCALED_TICKS_H

#include "scaled_sum_internal.h"
#include "wide_product_internal.h"

static inline void nt_scaled_add_tick_product(NtScaledSum *sum, double value, uint64_t a, uint64_t b)
{
    if (!value || !a || !b) return;
    /* Three exactly representable chunks cover the full 128-bit product. */
    const NtWideProduct product = nt_wide_product(a,b);
    const uint64_t low = product.low & ((UINT64_C(1) << 43) - 1);
    const uint64_t middle = (product.low >> 43) |
        ((product.high & ((UINT64_C(1) << 22) - 1)) << 21);
    const uint64_t high = product.high >> 22;
    const NtScaledTerm factor = nt_scaled_term(value,0);
    nt_scaled_add_product(sum,factor,nt_scaled_term((double)low,0));
    nt_scaled_add_product(sum,factor,nt_scaled_term((double)middle,43));
    nt_scaled_add_product(sum,factor,nt_scaled_term((double)high,86));
}

#endif
