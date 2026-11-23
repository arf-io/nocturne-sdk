#include "spring_range_internal.h"

#include <string.h>

/* Polynomial terms combine binary64 values with at most two 64-bit clock
 * factors. This range includes their significands and the midpoint terms. */
enum { EXACT_WORDS = 76, EXACT_ORIGIN = -1200, PRECISION_WORDS = 256, REDUCTION_BITS = 12 };
typedef struct { uint32_t words[EXACT_WORDS]; } ExactInteger;
typedef struct { uint32_t words[PRECISION_WORDS + 1]; } Fraction;
typedef struct {
    uint32_t low[EXACT_WORDS + PRECISION_WORDS + 1];
    uint32_t high[EXACT_WORDS + PRECISION_WORDS + 1];
    size_t fractional_words;
} DecayBounds;

static void add_words(uint32_t *a, const uint32_t *b, size_t count)
{
    uint64_t carry = 0;
    for (size_t i = 0; i < count; ++i) {
        const uint64_t sum = (uint64_t)a[i] + b[i] + carry;
        a[i] = (uint32_t)sum;
        carry = sum >> 32;
    }
}

static void subtract_words(uint32_t *a, const uint32_t *b, size_t count)
{
    uint64_t borrow = 0;
    for (size_t i = 0; i < count; ++i) {
        const uint64_t take = (uint64_t)b[i] + borrow;
        const uint64_t old = a[i];
        a[i] = (uint32_t)(old - take);
        borrow = old < take;
    }
}

static void multiply_words(uint32_t *out, const uint32_t *a, size_t a_count,
                           const uint32_t *b, size_t b_count)
{
    memset(out, 0, (a_count + b_count) * sizeof(*out));
    for (size_t i = 0; i < a_count; ++i) {
        uint64_t carry = 0;
        for (size_t j = 0; j < b_count; ++j) {
            const uint64_t term = (uint64_t)a[i] * b[j] + out[i + j] + carry;
            out[i + j] = (uint32_t)term;
            carry = term >> 32;
        }
        out[i + b_count] = (uint32_t)carry;
    }
}

static bool any_words(const uint32_t *value, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        if (value[i]) return true;
    return false;
}

static void increment(uint32_t *value, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        if (++value[i]) break;
}

static Fraction multiply_fraction(const Fraction *a, const Fraction *b, size_t n, bool upward)
{
    uint32_t product[2 * (PRECISION_WORDS + 1)];
    multiply_words(product, a->words, n + 1, b->words, n + 1);
    Fraction result = {0};
    memcpy(result.words, product + n, (n + 1) * sizeof(*result.words));
    if (upward && any_words(product, n)) increment(result.words, n + 1);
    return result;
}

static void divide_fraction(Fraction *value, size_t n, uint32_t divisor, bool upward)
{
    uint32_t remainder = 0;
    for (size_t i = n + 1; i > 0; --i) {
        const uint64_t part = ((uint64_t)remainder << 32) | value->words[i - 1];
        value->words[i - 1] = (uint32_t)(part / divisor);
        remainder = (uint32_t)(part % divisor);
    }
    if (upward && remainder) increment(value->words, n + 1);
}

static void exponential_bounds(uint64_t elapsed, uint64_t response, size_t n,
                               Fraction *low, Fraction *high)
{
    const size_t fractional_bits = n * 32 - REDUCTION_BITS;
    uint64_t remainder = elapsed % response;
    /* Emit exact fractional bits of elapsed/response, then scale by 2^-12. */
    Fraction argument_low = {0};
    argument_low.words[fractional_bits / 32] =
        (uint32_t)((elapsed / response) << (fractional_bits % 32));
    for (size_t i = fractional_bits; i > 0; --i) {
        if (remainder >= response - remainder) {
            remainder -= response - remainder;
            argument_low.words[(i - 1) / 32] |= UINT32_C(1) << ((i - 1) % 32);
        } else {
            remainder += remainder;
        }
    }
    Fraction argument_high = argument_low;
    if (remainder) increment(argument_high.words, n + 1);
    Fraction one = {0};
    one.words[n] = 1;
    *low = *high = one;
    Fraction term_low = one, term_high = one;
    const Fraction unit = {{1}};
    for (uint32_t k = 1; ; ++k) {
        term_low = multiply_fraction(&term_low, &argument_low, n, false);
        term_high = multiply_fraction(&term_high, &argument_high, n, true);
        divide_fraction(&term_low, n, k, false);
        divide_fraction(&term_high, n, k, true);
        if (k & 1) {
            subtract_words(low->words, term_high.words, n + 1);
            subtract_words(high->words, term_low.words, n + 1);
        } else {
            add_words(low->words, term_low.words, n + 1);
            add_words(high->words, term_high.words, n + 1);
        }
        /* The scaled argument is below 1/32. The alternating remainder
         * is smaller than this last term, now at most one fixed-point unit. */
        if (term_high.words[0] <= 1 && !any_words(term_high.words + 1, n)) {
            if (k & 1) add_words(high->words, unit.words, n + 1);
            else subtract_words(low->words, unit.words, n + 1);
            break;
        }
    }
    if (high->words[n]) *high = one;
    for (int i = 0; i < REDUCTION_BITS; ++i) {
        *low = multiply_fraction(low, low, n, false);
        *high = multiply_fraction(high, high, n, true);
    }
}

static int sum_sign(const NtScaledSum *sum)
{
    if (!sum->count) return 0;
    return sum->partials[sum->count - 1].fraction < 0 ? -1 : 1;
}

static ExactInteger magnitude(const NtScaledSum *sum, int sign)
{
    ExactInteger result = {0};
    for (size_t i = 0; i < sum->count; ++i) {
        const NtScaledTerm term = sum->partials[i];
        const uint64_t mantissa = (uint64_t)scalbn(fabs(term.fraction), 53);
        const unsigned bit = (unsigned)(term.exponent - 53 - EXACT_ORIGIN);
        const unsigned shift = bit % 32, word = bit / 32;
        const uint64_t low = (uint64_t)(uint32_t)mantissa << shift;
        const uint64_t high = ((mantissa >> 32) << shift) | (low >> 32);
        ExactInteger addend = {0};
        addend.words[word] = (uint32_t)low;
        addend.words[word + 1] = (uint32_t)high;
        addend.words[word + 2] = (uint32_t)(high >> 32);
        if ((term.fraction < 0) == (sign < 0))
            add_words(result.words, addend.words, EXACT_WORDS);
        else
            subtract_words(result.words, addend.words, EXACT_WORDS);
    }
    return result;
}

static int compare_product(const uint32_t *product, size_t n, const ExactInteger *threshold)
{
    for (size_t i = EXACT_WORDS + n + 1; i > 0; --i) {
        size_t index = i - 1;
        uint32_t boundary = index >= n && index - n < EXACT_WORDS ?
            threshold->words[index - n] : 0;
        if (product[index] != boundary) return product[index] > boundary ? 1 : -1;
    }
    return 0;
}

static int compare_boundary(const DecayBounds *bounds, const NtScaledSum *divisor,
                            double base, int amplitude_direction, double lower, double upper)
{
    NtScaledSum threshold = {0};
    nt_scaled_add_sum(&threshold, divisor, nt_scaled_term(-base, 0));
    /* The midpoint, including a and b identical for the finite limit,
     * need not itself be representable. */
    nt_scaled_add_sum(&threshold, divisor, nt_scaled_term(lower, -1));
    nt_scaled_add_sum(&threshold, divisor, nt_scaled_term(upper, -1));
    const int threshold_sign = sum_sign(&threshold);
    if (amplitude_direction != threshold_sign) return amplitude_direction;
    const ExactInteger limit = magnitude(&threshold, threshold_sign);
    if (compare_product(bounds->low, bounds->fractional_words, &limit) > 0)
        return amplitude_direction;
    if (compare_product(bounds->high, bounds->fractional_words, &limit) < 0)
        return -amplitude_direction;
    return 0;
}

static uint64_t floating_bits(double value)
{
    uint64_t result;
    memcpy(&result, &value, sizeof(result));
    return result;
}

static double from_floating_bits(uint64_t bits)
{
    double value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

double nt_spring_near_limit(double base, const NtScaledSum *numerator, const NtScaledSum *divisor,
                            uint64_t elapsed, uint64_t response, double approximate)
{
    const int numerator_sign = sum_sign(numerator);
    if (!numerator_sign) return base;
    if (elapsed / response >= 128) {
        /* At this age the maximum possible displacement is smaller than
         * half an ulp of a high-magnitude base. It can still cross a limit
         * when the target is exactly on it. */
        return fabs(base) == DBL_MAX && (base < 0) == (numerator_sign < 0) ?
            copysign(INFINITY, base) : base;
    }

    const int orientation = approximate < 0 ? -1 : 1;
    const double oriented_base = orientation * base;
    const int direction = numerator_sign * orientation;
    if (oriented_base == DBL_MAX && direction > 0) return copysign(INFINITY, approximate);
    const ExactInteger exact = magnitude(numerator, numerator_sign);
    for (size_t n = 8; n <= PRECISION_WORDS; n *= 2) {
        Fraction low, high;
        exponential_bounds(elapsed, response, n, &low, &high);
        DecayBounds bounds = {.fractional_words = n};
        multiply_words(bounds.low, exact.words, EXACT_WORDS, low.words, n + 1);
        multiply_words(bounds.high, exact.words, EXACT_WORDS, high.words, n + 1);
        const int limit = compare_boundary(&bounds, divisor, oriented_base, direction,
                                           DBL_MAX, DBL_MAX);
        if (limit > 0) return copysign(INFINITY, approximate);
        if (!limit) continue;

        /* Positive binary64 encodings are ordered by magnitude. Start
         * with the ordinary sample and bisect only if its cell is wrong. */
        uint64_t first = 0, last = floating_bits(DBL_MAX);
        uint64_t candidate = floating_bits(fmin(DBL_MAX, fabs(approximate)));
        for (;;) {
            const double value = from_floating_bits(candidate);
            const double lower = nextafter(value, 0);
            const double upper = nextafter(value, INFINITY);
            const int below = value ? compare_boundary(&bounds, divisor, oriented_base, direction,
                                                       lower, value) : 1;
            if (!below) break;
            if (below < 0) {
                last = candidate - 1;
            } else {
                const int above = isfinite(upper) ?
                    compare_boundary(&bounds, divisor, oriented_base, direction, value, upper) : -1;
                if (!above) break;
                if (above < 0) return copysign(value, approximate);
                first = candidate + 1;
            }
            candidate = first + (last - first) / 2;
        }
    }
    /* The exact exponential at a nonzero rational age cannot tie a
     * rational boundary. Fail atomically if the precision bound is reached. */
    return copysign(INFINITY, approximate);
}
