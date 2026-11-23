#ifndef NOCTURNE_INTERNAL_WIDE_PRODUCT_H
#define NOCTURNE_INTERNAL_WIDE_PRODUCT_H

#include <stdint.h>

typedef struct NtWideProduct { uint64_t high, low; } NtWideProduct;

static inline NtWideProduct nt_wide_product(uint64_t a, uint64_t b)
{
    const uint64_t low = (uint64_t)(uint32_t)a * (uint32_t)b;
    const uint64_t mid_a = (a >> 32) * (uint32_t)b, mid_b = (uint64_t)(uint32_t)a * (b >> 32);
    const uint64_t carry = (low >> 32) + (uint32_t)mid_a + (uint32_t)mid_b;
    return (NtWideProduct){(a >> 32)*(b >> 32) + (mid_a >> 32) + (mid_b >> 32) + (carry >> 32),
                         (carry << 32) | (uint32_t)low};
}

#endif
