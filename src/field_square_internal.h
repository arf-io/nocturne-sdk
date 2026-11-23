#ifndef NOCTURNE_INTERNAL_FIELD_SQUARE_H
#define NOCTURNE_INTERNAL_FIELD_SQUARE_H

#include "scaled_sum_internal.h"
#include "wide_product_internal.h"

#include <stdint.h>

/* Signed fixed-point accumulator for squared aspect-adjusted coordinates
 * multiplied by a 64-bit lifetime squared. The smallest possible input
 * product is on the 2^-4296 grid. Two weighted squared coordinates total
 * less than 2^4227; the radius and width terms fit well inside that bound.
 * The low words also hold products with the half-subnormal boundary
 * (2^-1075) when rounding a nonnegative optical quotient. */
enum { NT_FIELD_SQUARE_EXP = -5504, NT_FIELD_SQUARE_WORDS = 154 };

typedef struct NtFieldSquare { uint64_t words[NT_FIELD_SQUARE_WORDS]; } NtFieldSquare;

static inline void nt_field_square_add_word(NtFieldSquare *sum, unsigned index, uint64_t value, bool negative)
{
    while (value && index < NT_FIELD_SQUARE_WORDS) {
        const uint64_t previous = sum->words[index];
        const uint64_t next = negative ? previous-value : previous+value;
        sum->words[index++] = next;
        value = negative ? next > previous : next < previous;
    }
}

static inline void nt_field_square_add_term(NtFieldSquare *sum, NtScaledTerm term)
{
    if (!term.fraction) return;
    const unsigned shift = (unsigned)(term.exponent-DBL_MANT_DIG-NT_FIELD_SQUARE_EXP);
    const unsigned word = shift/64, offset = shift%64;
    const uint64_t magnitude = (uint64_t)scalbn(fabs(term.fraction),DBL_MANT_DIG);
    const bool negative = term.fraction < 0;
    nt_field_square_add_word(sum,word,magnitude << offset,negative);
    if (offset) nt_field_square_add_word(sum,word+1,magnitude >> (64-offset),negative);
}

static inline void nt_field_square_product(NtFieldSquare *sum, NtScaledTerm a, NtScaledTerm b)
{
    const double product = a.fraction*b.fraction;
    const int exponent = a.exponent+b.exponent;
    nt_field_square_add_term(sum,nt_scaled_term(product,exponent));
    nt_field_square_add_term(sum,nt_scaled_term(fma(a.fraction,b.fraction,-product),exponent));
}

static inline void nt_field_square_add(NtFieldSquare *sum, const NtScaledSum *side, double sign)
{
    for (size_t i = 0; i < side->count; ++i) {
        NtScaledTerm a = side->partials[i];
        a.fraction *= sign;
        for (size_t j = 0; j < side->count; ++j) nt_field_square_product(sum,a,side->partials[j]);
    }
}

/* Source is a nonnegative area. The scale can be signed. */
static inline void nt_field_square_scale(NtFieldSquare *sum, const NtFieldSquare *source, NtScaledTerm scale)
{
    if (!scale.fraction) return;
    for (unsigned i = 0; i < NT_FIELD_SQUARE_WORDS; ++i) {
        const uint64_t limb = source->words[i];
        if (!limb) continue;
        const int exponent = NT_FIELD_SQUARE_EXP+(int)i*64;
        nt_field_square_product(sum,nt_scaled_term((double)(uint32_t)limb,exponent),scale);
        nt_field_square_product(sum,nt_scaled_term((double)(limb >> 32),exponent+32),scale);
    }
}

static inline NtScaledTerm nt_field_square_value(const NtFieldSquare *sum)
{
    const bool negative = sum->words[NT_FIELD_SQUARE_WORDS-1] >> 63;
    uint64_t value[NT_FIELD_SQUARE_WORDS];
    const uint64_t *words = sum->words;
    if (negative) {
        uint64_t carry = 1;
        for (unsigned i = 0; i < NT_FIELD_SQUARE_WORDS; ++i) {
            value[i] = ~words[i]+carry;
            carry = carry && !value[i];
        }
        words = value;
    }
    unsigned used = NT_FIELD_SQUARE_WORDS;
    while (used && !words[used-1]) --used;
    if (!used) return (NtScaledTerm){0};
    unsigned bits = 64*(used-1);
    for (uint64_t top = words[used-1]; top; top >>= 1) ++bits;
    const unsigned shift = bits > DBL_MANT_DIG ? bits-DBL_MANT_DIG : 0;
    const unsigned word = shift/64, offset = shift%64;
    uint64_t mantissa = words[word] >> offset;
    if (offset && word+1 < used) mantissa |= words[word+1] << (64-offset);
    if (shift) {
        const unsigned guard_word = (shift-1)/64, guard_offset = (shift-1)%64;
        const uint64_t guard = UINT64_C(1) << guard_offset;
        if (words[guard_word] & guard) {
            bool sticky = (words[guard_word] & (guard-1)) != 0;
            for (unsigned i = 0; i < guard_word && !sticky; ++i) sticky = words[i] != 0;
            if (sticky || (mantissa & 1)) ++mantissa;
        }
    }
    const double significand = (double)mantissa;
    return nt_scaled_term(negative ? -significand : significand,NT_FIELD_SQUARE_EXP+(int)shift);
}

static inline int nt_field_square_midpoint(const NtFieldSquare *numerator, const NtFieldSquare *divisor,
                                           double a, double b)
{
    NtFieldSquare residual = *numerator;
    nt_field_square_scale(&residual,divisor,nt_scaled_term(-a,-1));
    nt_field_square_scale(&residual,divisor,nt_scaled_term(-b,-1));
    if (residual.words[NT_FIELD_SQUARE_WORDS-1] >> 63) return -1;
    for (unsigned i = 0; i < NT_FIELD_SQUARE_WORDS; ++i) if (residual.words[i]) return 1;
    return 0;
}

static inline void nt_field_product_word(uint64_t *out, unsigned index, uint64_t value)
{
    while (value) {
        const uint64_t previous = out[index];
        out[index++] += value;
        value = previous + value < previous;
    }
}

/* Each output has room for a product of the two unsigned operands. Skip
 * leading and trailing zero words, including the fixed-point padding. */
static inline void nt_field_product(uint64_t *out, const uint64_t *a, unsigned a_count,
                                   const uint64_t *b, unsigned b_count)
{
    while (a_count && !a[a_count-1]) --a_count;
    while (b_count && !b[b_count-1]) --b_count;
    unsigned first_a = 0, first_b = 0;
    while (first_a < a_count && !a[first_a]) ++first_a;
    while (first_b < b_count && !b[first_b]) ++first_b;
    for (unsigned i = first_a; i < a_count; ++i) {
        if (!a[i]) continue;
        for (unsigned j = first_b; j < b_count; ++j) {
            if (!b[j]) continue;
            const NtWideProduct product = nt_wide_product(a[i],b[j]);
            nt_field_product_word(out,i+j,product.low);
            nt_field_product_word(out,i+j+1,product.high);
        }
    }
}

static inline int nt_field_band_midpoint(const NtFieldSquare *numerator, const NtFieldSquare *divisor,
                                         const NtFieldSquare *distance_squared, const NtScaledSum *radius,
                                         double a, double b)
{
    /* For midpoint m, compare m*(w^2 + distance^2 + r^2) - numerator
     * with 2*m*r*distance. When both sides are positive the comparison
     * can be squared, so the unrounded distance never needs a square root. */
    NtFieldSquare left = {0}, radial = {0};
    nt_field_square_scale(&left,divisor,nt_scaled_term(a,-1));
    nt_field_square_scale(&left,divisor,nt_scaled_term(b,-1));
    nt_field_square_scale(&left,numerator,(NtScaledTerm){-.5,1});
    if (nt_field_square_value(&left).fraction <= 0) return 1;
    for (size_t i = 0; i < radius->count; ++i) {
        nt_field_square_product(&radial,radius->partials[i],nt_scaled_term(a,0));
        nt_field_square_product(&radial,radius->partials[i],nt_scaled_term(b,0));
    }

    const unsigned count = NT_FIELD_SQUARE_WORDS;
    uint64_t left_squared[3*NT_FIELD_SQUARE_WORDS] = {0};
    uint64_t radial_squared[2*NT_FIELD_SQUARE_WORDS] = {0};
    uint64_t right_squared[3*NT_FIELD_SQUARE_WORDS] = {0};
    /* The left product has one fewer fixed-point factor. Align its
     * integer representation with the third factor on the right. */
    const unsigned alignment_words = -NT_FIELD_SQUARE_EXP/64;
    nt_field_product(left_squared+alignment_words,left.words,count,left.words,count);
    nt_field_product(radial_squared,radial.words,count,radial.words,count);
    nt_field_product(right_squared,radial_squared,2*count,distance_squared->words,count);
    for (unsigned i = 3*count; i > 0; --i) {
        if (right_squared[i-1] > left_squared[i-1]) return 1;
        if (right_squared[i-1] < left_squared[i-1]) return -1;
    }
    return 0;
}

static inline int nt_field_weight_midpoint(const NtFieldSquare *numerator, const NtFieldSquare *divisor,
                                           const NtFieldSquare *distance_squared, const NtScaledSum *radius,
                                           double a, double b)
{
    return radius ? nt_field_band_midpoint(numerator,divisor,distance_squared,radius,a,b) :
                    nt_field_square_midpoint(numerator,divisor,a,b);
}

static inline double nt_field_square_weight(const NtFieldSquare *numerator, const NtFieldSquare *divisor,
                                            const NtFieldSquare *distance_squared, const NtScaledSum *radius,
                                            double estimate)
{
    const NtScaledTerm n = nt_field_square_value(numerator);
    if (!n.fraction) return 0;
    if (!radius) {
        const NtScaledTerm d = nt_field_square_value(divisor);
        estimate = fmin(1,scalbn(n.fraction/d.fraction,n.exponent-d.exponent));
    }
    for (;;) {
        const double lower = nextafter(estimate,0), upper = nextafter(estimate,INFINITY);
        const bool odd = odd_significand(estimate);
        if (estimate > 0) {
            const int comparison = nt_field_weight_midpoint(numerator,divisor,distance_squared,radius,lower,estimate);
            if (comparison < 0 || (!comparison && odd)) {
                estimate = lower;
                continue;
            }
        }
        if (estimate < 1) {
            const int comparison = nt_field_weight_midpoint(numerator,divisor,distance_squared,radius,estimate,upper);
            if (comparison > 0 || (!comparison && odd)) {
                estimate = upper;
                continue;
            }
        }
        return estimate;
    }
}

#endif
