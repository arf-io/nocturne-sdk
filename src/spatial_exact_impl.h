/* Shared implementation for fixed-width dyadic arithmetic. The
 * including translation unit supplies the value type, its limb bound
 * and the linkage of the arithmetic functions. */
#ifndef NT_EXACT_LINKAGE
#define NT_EXACT_LINKAGE
#endif

#include <assert.h>
#include <float.h>
#include <math.h>
#include <string.h>

#if defined(__clang__) && !defined(__wasm__)
#pragma STDC FENV_ACCESS ON
#elif defined(_MSC_VER)
#pragma fenv_access(on)
#endif

static void trim(NtSpatialExact *value)
{
    while (value->count && !value->limbs[value->count-1]) --value->count;
    if (!value->count) {
        value->offset = 0;
        value->negative = false;
        return;
    }
    unsigned first = 0;
    while (!value->limbs[first]) ++first;
    if (first) {
        value->count -= first;
        value->offset += (int)first;
        memmove(value->limbs, value->limbs+first, value->count*sizeof(value->limbs[0]));
    }
}

NT_EXACT_LINKAGE void nt_spatial_exact_number(NtSpatialExact *out, double value, int power)
{
    out->count = 0;
    out->offset = 0;
    out->negative = false;
    if (value == 0) return;
    int exponent;
    const double fraction = frexp(fabs(value), &exponent);
    const uint64_t significand = (uint64_t)scalbn(fraction, DBL_MANT_DIG);
    const int bit = exponent - DBL_MANT_DIG + power;
    int shift = bit % 32;
    out->offset = bit / 32;
    if (shift < 0) {
        shift += 32;
        --out->offset;
    }
    const uint64_t low = (uint64_t)(uint32_t)significand << shift;
    const uint64_t high = ((significand >> 32) << shift) | (low >> 32);
    out->limbs[0] = (uint32_t)low;
    out->limbs[1] = (uint32_t)high;
    out->limbs[2] = (uint32_t)(high >> 32);
    out->count = 3;
    out->negative = value < 0;
    trim(out);
}

NT_EXACT_LINKAGE void nt_spatial_exact_copy(NtSpatialExact *out, const NtSpatialExact *value)
{
    out->offset = value->offset;
    out->count = value->count;
    out->negative = value->negative;
    memcpy(out->limbs, value->limbs, value->count*sizeof(out->limbs[0]));
}

static uint32_t digit(const NtSpatialExact *value, int word)
{
    const unsigned index = (unsigned)(word-value->offset);
    return index < value->count ? value->limbs[index] : 0;
}

static int end(const NtSpatialExact *value)
{
    return value->offset+(int)value->count;
}

static int magnitude_order(const uint32_t *a, unsigned a_count, int a_offset,
                           const uint32_t *b, unsigned b_count, int b_offset)
{
    if (!a_count || !b_count) return (a_count != 0)-(b_count != 0);
    const int high = a_offset+(int)a_count, other = b_offset+(int)b_count;
    if (high != other) return high < other ? -1 : 1;
    const int low = a_offset < b_offset ? a_offset : b_offset;
    for (int word = high; word > low;) {
        --word;
        const uint32_t first = word >= a_offset ? a[word-a_offset] : 0;
        const uint32_t second = word >= b_offset ? b[word-b_offset] : 0;
        if (first != second) return first < second ? -1 : 1;
    }
    return 0;
}

static int compare_magnitude(const NtSpatialExact *a, const NtSpatialExact *b)
{
    return magnitude_order(a->limbs, a->count, a->offset, b->limbs, b->count, b->offset);
}

#ifndef NT_EXACT_ARITHMETIC_ONLY
NT_EXACT_LINKAGE int nt_spatial_exact_compare(const NtSpatialExact *a, const NtSpatialExact *b)
{
    if (a->negative != b->negative) return a->negative ? -1 : 1;
    const int order = compare_magnitude(a, b);
    return a->negative ? -order : order;
}
#endif

static void add_signed(NtSpatialExact *out, const NtSpatialExact *a, const NtSpatialExact *b,
                       bool b_negative)
{
    if (!b->count) { nt_spatial_exact_copy(out, a); return; }
    if (!a->count) {
        nt_spatial_exact_copy(out, b);
        out->negative = b_negative;
        return;
    }
    const int low = a->offset < b->offset ? a->offset : b->offset;
    const int upper = end(a) > end(b) ? end(a) : end(b);
    out->offset = low;
    out->count = (unsigned)(upper-low);
    assert(out->count < NT_SPATIAL_EXACT_LIMBS);
    out->negative = a->negative;
    if (a->negative == b_negative) {
        uint64_t carry = 0;
        for (int word = low; word < upper; ++word) {
            const uint64_t sum = (uint64_t)digit(a, word)+digit(b, word)+carry;
            out->limbs[word-low] = (uint32_t)sum;
            carry = sum >> 32;
        }
        if (carry) out->limbs[out->count++] = (uint32_t)carry;
    } else {
        const int order = compare_magnitude(a, b);
        const NtSpatialExact *larger = order >= 0 ? a : b;
        const NtSpatialExact *smaller = order >= 0 ? b : a;
        out->negative = order >= 0 ? a->negative : b_negative;
        uint64_t borrow = 0;
        for (int word = low; word < upper; ++word) {
            const uint64_t first = digit(larger, word), second = (uint64_t)digit(smaller, word)+borrow;
            out->limbs[word-low] = (uint32_t)(first-second);
            borrow = first < second;
        }
    }
    trim(out);
}

NT_EXACT_LINKAGE void nt_spatial_exact_add(NtSpatialExact *out, const NtSpatialExact *a, const NtSpatialExact *b)
{
    add_signed(out, a, b, b->negative);
}

NT_EXACT_LINKAGE void nt_spatial_exact_subtract(NtSpatialExact *out, const NtSpatialExact *a, const NtSpatialExact *b)
{
    add_signed(out, a, b, !b->negative);
}

static unsigned multiply_limbs(uint32_t *out, const uint32_t *a, unsigned a_count,
                                 const uint32_t *b, unsigned b_count)
{
    if (!a_count || !b_count) return 0;
    unsigned count = a_count+b_count;
    memset(out, 0, count*sizeof(out[0]));
    for (unsigned i = 0; i < a_count; ++i) {
        if (!a[i]) continue;
        uint64_t carry = 0;
        for (unsigned j = 0; j < b_count; ++j) {
            const uint64_t product = (uint64_t)a[i]*b[j]+out[i+j]+carry;
            out[i+j] = (uint32_t)product;
            carry = product >> 32;
        }
        out[i+b_count] = (uint32_t)carry;
    }
    while (count && !out[count-1]) --count;
    return count;
}

static unsigned multiply_magnitude(uint32_t *out, const NtSpatialExact *a, const NtSpatialExact *b)
{
    return multiply_limbs(out,a->limbs,a->count,b->limbs,b->count);
}

NT_EXACT_LINKAGE void nt_spatial_exact_multiply(NtSpatialExact *out, const NtSpatialExact *a, const NtSpatialExact *b)
{
    if (!a->count || !b->count) {
        nt_spatial_exact_number(out, 0, 0);
        return;
    }
    assert(a->count+b->count <= NT_SPATIAL_EXACT_LIMBS);
    out->count = multiply_magnitude(out->limbs, a, b);
    out->offset = a->offset+b->offset;
    out->negative = a->negative != b->negative;
    trim(out);
}

#ifndef NT_EXACT_ARITHMETIC_ONLY
/* The divisors are positive. The degree-thirteen products only live here,
 * leaving smaller arithmetic values for the geometry call frames. */
NT_EXACT_LINKAGE int nt_spatial_compare_ratios(const NtSpatialExact *a, const NtSpatialExact *a_divisor,
                              const NtSpatialExact *b, const NtSpatialExact *b_divisor)
{
    if (a->negative != b->negative) return a->negative ? -1 : 1;
    uint32_t left[2*NT_SPATIAL_EXACT_LIMBS], right[2*NT_SPATIAL_EXACT_LIMBS];
    const unsigned left_count = multiply_magnitude(left, a, b_divisor);
    const unsigned right_count = multiply_magnitude(right, b, a_divisor);
    const int order = magnitude_order(left, left_count, a->offset+b_divisor->offset,
                                      right, right_count, b->offset+a_divisor->offset);
    return a->negative ? -order : order;
}

NT_EXACT_LINKAGE int nt_spatial_exact_compare_scaled(const NtSpatialExact *a, const NtSpatialExact *b, double scale)
{
    NtSpatialExact factor, scaled;
    nt_spatial_exact_number(&factor, scale, 0);
    nt_spatial_exact_multiply(&scaled, b, &factor);
    return nt_spatial_exact_compare(a, &scaled);
}
#endif

static double leading(const NtSpatialExact *value)
{
    const unsigned high = value->count-1;
    /* Three limbs retain enough leading bits even when the top limb is 1. */
    const double second = high > 0 ? scalbn(value->limbs[high-1], -32) : 0;
    const double third = high > 1 ? scalbn(value->limbs[high-2], -64) : 0;
    return value->limbs[high] + (second+third);
}

static int compare_midpoint(const NtSpatialExact *positive, const NtSpatialExact *divisor,
                            double a, double b)
{
    NtSpatialExact first, second, midpoint, scaled;
    nt_spatial_exact_number(&first, a, -1);
    nt_spatial_exact_number(&second, b, -1);
    nt_spatial_exact_add(&midpoint, &first, &second);
    nt_spatial_exact_multiply(&scaled, divisor, &midpoint);
    return compare_magnitude(positive, &scaled);
}

static bool odd_significand(double value)
{
    int exponent;
    const double significand = value < DBL_MIN ? scalbn(value, DBL_MANT_DIG-DBL_MIN_EXP) :
        scalbn(frexp(value, &exponent), DBL_MANT_DIG);
    return fmod(significand, 2) != 0;
}

static bool quotient_fits(const NtSpatialExact *numerator, const NtSpatialExact *denominator)
{
    NtSpatialExact bound, scaled;
    nt_spatial_exact_number(&bound, DBL_MAX, 0);
    nt_spatial_exact_multiply(&scaled, denominator, &bound);
    return compare_magnitude(numerator, &scaled) <= 0;
}

/* Round a finite ratio to nearest even. The exact magnitude must fit the
 * finite range, including comparisons at the upper representable limit. */
NT_EXACT_LINKAGE double nt_spatial_exact_quotient(const NtSpatialExact *numerator, const NtSpatialExact *denominator)
{
    if (!numerator->count) return 0;
    assert(denominator->count);
    const bool negative = numerator->negative != denominator->negative;
    const double estimate = scalbn(leading(numerator)/leading(denominator),
                                  32*(end(numerator)-end(denominator)));
    double candidate = isfinite(estimate) ? estimate : DBL_MAX;
    if (candidate >= DBL_MAX*.5 && !quotient_fits(numerator, denominator))
        return negative ? -INFINITY : INFINITY;
    for (;;) {
        const bool odd = odd_significand(candidate);
        if (candidate > 0) {
            const double lower = nextafter(candidate, 0);
            const int order = compare_midpoint(numerator, denominator, lower, candidate);
            if (order < 0 || (order == 0 && odd)) {
                candidate = lower;
                continue;
            }
        }
        if (candidate < DBL_MAX) {
            const double upper = nextafter(candidate, INFINITY);
            const int order = compare_midpoint(numerator, denominator, candidate, upper);
            if (order > 0 || (order == 0 && odd)) {
                candidate = upper;
                continue;
            }
        }
        return negative ? -candidate : candidate;
    }
}
