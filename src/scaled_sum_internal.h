#ifndef NOCTURNE_INTERNAL_SCALED_SUM_H
#define NOCTURNE_INTERNAL_SCALED_SUM_H

#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>

/* The caller evaluates these sums under round-to-nearest with gradual
 * underflow and bounds the number of inserted partials. Panel projection
 * uses at most 60 and spring quotient rounding at most 54. */
typedef struct NtScaledTerm { double fraction; int exponent; } NtScaledTerm;
#ifndef NT_SCALED_SUM_CAPACITY
#define NT_SCALED_SUM_CAPACITY 60
#endif
typedef struct NtScaledSum { NtScaledTerm partials[NT_SCALED_SUM_CAPACITY]; size_t count; } NtScaledSum;

#if defined(__clang__) && !defined(__wasm__)
#pragma STDC FENV_ACCESS ON
#elif defined(_MSC_VER)
#pragma fenv_access(on)
#endif

static inline NtScaledTerm nt_scaled_term(double fraction, int exponent)
{
    int adjustment;
    const double normalized = frexp(fraction,&adjustment);
    return (NtScaledTerm){normalized,exponent+adjustment};
}

/* Each part has its own binary exponent. Products and addition residuals
 * retain their significands even below the range of an ordinary double.
 * Partials are ordered from lowest to highest magnitude. */
static inline void nt_scaled_add(NtScaledSum *sum, NtScaledTerm value)
{
    if (!value.fraction) return;
    size_t keep = 0;
    for (size_t i = 0; i < sum->count; ++i) {
        NtScaledTerm part = sum->partials[i];
        if (!value.fraction || value.exponent < part.exponent ||
            (value.exponent == part.exponent && fabs(value.fraction) < fabs(part.fraction))) {
            const NtScaledTerm swap = part;
            part = value;
            value = swap;
        }
        if (!part.fraction) continue;
        const int gap = value.exponent-part.exponent;
        /* A larger gap leaves the parts nonoverlapping. Avoid shifting
         * away a term that could survive a later cancellation. */
        if (gap > 64) {
            sum->partials[keep++] = part;
            continue;
        }
        const double other = scalbn(part.fraction,-gap);
        const double high = value.fraction+other;
        const double low = other-(high-value.fraction);
        if (low) sum->partials[keep++] = nt_scaled_term(low,value.exponent);
        value = nt_scaled_term(high,value.exponent);
    }
    if (value.fraction) sum->partials[keep++] = value;
    sum->count = keep;
}

static inline void nt_scaled_add_product(NtScaledSum *sum, NtScaledTerm a, NtScaledTerm b)
{
    if (!a.fraction || !b.fraction) return;
    const int exponent = a.exponent+b.exponent;
    const double high = a.fraction*b.fraction;
    nt_scaled_add(sum,nt_scaled_term(high,exponent));
    nt_scaled_add(sum,nt_scaled_term(fma(a.fraction,b.fraction,-high),exponent));
}

static inline NtScaledTerm nt_scaled_value(const NtScaledSum *sum)
{
    size_t count = sum->count;
    if (!count) return (NtScaledTerm){0};
    const NtScaledTerm top = sum->partials[--count];
    double high = top.fraction, low = 0;
    while (count) {
        const NtScaledTerm part = sum->partials[--count];
        const int gap = top.exponent-part.exponent;
        if (gap > 64) break;
        const double previous = high, other = scalbn(part.fraction,-gap);
        high = previous+other;
        low = other-(high-previous);
        if (low) break;
    }
    /* If the smaller partials push an exact midpoint the same way as
     * the residual, round away from the midpoint rather than to even. */
    if (low && count && ((low > 0) == (sum->partials[count-1].fraction > 0))) {
        const double adjustment = 2*low;
        const double rounded = high+adjustment;
        if (rounded-high == adjustment) high = rounded;
    }
    return nt_scaled_term(high,top.exponent);
}

static inline void nt_scaled_add_sum(NtScaledSum *sum, const NtScaledSum *source, NtScaledTerm factor)
{
    if (!factor.fraction) return;
    for (size_t i = 0; i < source->count; ++i)
        nt_scaled_add_product(sum,source->partials[i],factor);
}

static inline int quotient_order(const NtScaledSum *residual, const NtScaledSum *divisor)
{
    if (!residual->count) return 0;
    const bool below = (residual->partials[residual->count-1].fraction < 0) ==
        (divisor->partials[divisor->count-1].fraction > 0);
    return below ? -1 : 1;
}

static inline int nt_scaled_compare_boundary(const NtScaledSum *sum, const NtScaledSum *divisor, double boundary)
{
    NtScaledSum residual = *sum;
    nt_scaled_add_sum(&residual,divisor,nt_scaled_term(-boundary,0));
    return quotient_order(&residual,divisor);
}

static inline int compare_midpoint(const NtScaledSum *sum, const NtScaledSum *divisor, double a, double b)
{
    NtScaledSum residual = *sum;
    nt_scaled_add_sum(&residual,divisor,nt_scaled_term(-a,-1));
    nt_scaled_add_sum(&residual,divisor,nt_scaled_term(-b,-1));
    return quotient_order(&residual,divisor);
}

static inline double finite_coordinate(const NtScaledSum *sum, const NtScaledSum *divisor, double value)
{
    if (!(fabs(value) >= DBL_MAX*.5)) return value;
    /* A truly out-of-range quotient can round to the largest finite
     * double. Validate the original sum against that limit. */
    const double limit = copysign(DBL_MAX,value);
    const int order = nt_scaled_compare_boundary(sum,divisor,limit);
    if (value < 0 ? order < 0 : order > 0) return copysign(INFINITY,value);
    return isfinite(value) ? value : limit;
}

static inline bool odd_significand(double value)
{
    const double absolute = fabs(value);
    const double integer = absolute < DBL_MIN ?
        scalbn(absolute,DBL_MANT_DIG-DBL_MIN_EXP) :
        scalbn(nt_scaled_term(absolute,0).fraction,DBL_MANT_DIG);
    return fmod(integer,2) != 0;
}

static inline double nt_scaled_quotient(const NtScaledSum *sum, const NtScaledSum *divisor,
                                 NtScaledTerm approximate_divisor)
{
    const NtScaledTerm numerator = nt_scaled_value(sum);
    const double rounded = scalbn(numerator.fraction/approximate_divisor.fraction,
                                  numerator.exponent-approximate_divisor.exponent);
    double value = finite_coordinate(sum,divisor,rounded);
    if (!isfinite(value)) return value;
    if (fabs(value) > DBL_MIN && divisor->count == 1 &&
        fabs(divisor->partials[0].fraction) == .5) return value;

    /* Dividing rounded significands or scaling into the subnormal range
     * can lose a midpoint residual. Compare the original terms on both
     * sides, including the exact halfway case. */
    for (;;) {
        const double lower = nextafter(value,-INFINITY), upper = nextafter(value,INFINITY);
        const bool odd = odd_significand(value);
        if (isfinite(lower)) {
            const int order = compare_midpoint(sum,divisor,lower,value);
            if (order < 0 || (!order && odd)) {
                value = lower;
                continue;
            }
        }
        if (isfinite(upper)) {
            const int order = compare_midpoint(sum,divisor,value,upper);
            if (order > 0 || (!order && odd)) {
                value = upper;
                continue;
            }
        }
        return value;
    }
}

#endif
