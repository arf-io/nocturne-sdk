#include "simulation_wide_internal.h"
#include "spatial_geometry_internal.h"

#define NtSpatialExact NtSimulationWide
#define NT_SPATIAL_EXACT_LIMBS NT_SIMULATION_WIDE_LIMBS
#define NT_EXACT_ARITHMETIC_ONLY
#define nt_spatial_exact_number nt_simulation_wide_number
#define nt_spatial_exact_copy nt_simulation_wide_copy
#define nt_spatial_exact_add nt_simulation_wide_add
#define nt_spatial_exact_subtract nt_simulation_wide_subtract
#define nt_spatial_exact_multiply nt_simulation_wide_multiply
#define nt_spatial_exact_quotient nt_simulation_wide_quotient
#include "spatial_exact_impl.h"
#undef nt_spatial_exact_quotient
#undef nt_spatial_exact_multiply
#undef nt_spatial_exact_subtract
#undef nt_spatial_exact_add
#undef nt_spatial_exact_copy
#undef nt_spatial_exact_number
#undef NT_EXACT_ARITHMETIC_ONLY
#undef NT_EXACT_LINKAGE
#undef NT_SPATIAL_EXACT_LIMBS
#undef NtSpatialExact

unsigned nt_simulation_multiply_limbs(uint32_t *out, const uint32_t *a, unsigned a_count,
                                      const uint32_t *b, unsigned b_count)
{
    return multiply_limbs(out,a,a_count,b,b_count);
}

int nt_simulation_compare_limbs(const uint32_t *a, unsigned a_count, int a_offset,
                                const uint32_t *b, unsigned b_count, int b_offset)
{
    return magnitude_order(a,a_count,a_offset,b,b_count,b_offset);
}

static void norm_square(const NtSpatialExact n[3], NtSimulationWide *out)
{
    nt_simulation_wide_number(out,0,0);
    for (unsigned i=0;i<3;++i) {
        NtSimulationWide value, squared, sum;
        nt_simulation_widen(&value,&n[i]);
        nt_simulation_wide_multiply(&squared,&value,&value);
        nt_simulation_wide_add(&sum,out,&squared);
        nt_simulation_wide_copy(out,&sum);
    }
}

bool nt_simulation_norm_within(const NtSpatialExact n[3], const NtSpatialExact *limit)
{
    NtSimulationWide squared, radius, bound;
    norm_square(n,&squared);
    nt_simulation_widen(&radius,limit);
    nt_simulation_wide_multiply(&bound,&radius,&radius);
    return compare_magnitude(&squared,&bound)<=0;
}

static int compare_squared_midpoint(const NtSimulationWide *squared, const NtSimulationWide *divisor,
                                     const NtSimulationWide *shift, bool reversed,
                                     const NtSimulationWide *radical_squared, double lower, double upper)
{
    NtSimulationWide a, b, midpoint, square, scaled;
    nt_simulation_wide_number(&a,lower,-1);
    nt_simulation_wide_number(&b,upper,-1);
    if (shift) {
        nt_simulation_wide_add(&scaled,&a,&b);
        scaled.negative = reversed;
        nt_simulation_wide_add(&midpoint,&scaled,shift);
        if (midpoint.negative) return reversed ? -1 : 1;
    } else nt_simulation_wide_add(&midpoint,&a,&b);
    nt_simulation_wide_multiply(&square,&midpoint,&midpoint);
    nt_simulation_wide_multiply(&scaled,&square,divisor);
    if (radical_squared && radical_squared->count) {
        nt_simulation_wide_subtract(&midpoint,squared,&scaled);
        const int sign = midpoint.negative ? -1 : 1;
        if (!midpoint.count || midpoint.negative==radical_squared->negative)
            return radical_squared->negative ? -1 : 1;
        nt_simulation_wide_multiply(&square,&midpoint,&midpoint);
        return sign*compare_magnitude(&square,radical_squared);
    }
    const int order = compare_magnitude(squared,&scaled);
    return reversed ? -order : order;
}

static double round_squared_norm(const NtSimulationWide *squared, const NtSimulationWide *divisor,
                                 const NtSimulationWide *shift, bool reversed,
                                 const NtSimulationWide *radical_squared, double estimate)
{
    if (!squared->count && !shift) return 0;
    /* The input norm is bounded by the angular step or quaternion unit
     * range. Refine its finite estimate by exact squared midpoints. */
    double candidate = estimate;
    for (;;) {
        const bool odd = odd_significand(candidate);
        if (candidate>0) {
            const double lower = nextafter(candidate,0);
            const int order = compare_squared_midpoint(squared,divisor,shift,reversed,radical_squared,lower,candidate);
            if (order<0 || (order==0 && odd)) {
                candidate = lower;
                continue;
            }
        }
        const double upper = nextafter(candidate,INFINITY);
        const int order = compare_squared_midpoint(squared,divisor,shift,reversed,radical_squared,candidate,upper);
        if (order<0 || (order==0 && !odd)) return candidate;
        candidate = upper;
    }
}

double nt_simulation_round_norm(const NtSpatialExact n[3], const NtSpatialExact *unit, double estimate)
{
    NtSimulationWide squared, radius, divisor;
    norm_square(n,&squared);
    nt_simulation_widen(&radius,unit);
    nt_simulation_wide_multiply(&divisor,&radius,&radius);
    return round_squared_norm(&squared,&divisor,NULL,false,NULL,estimate);
}

static void subtract_squared_angle(const NtSimulationWide *squared, const NtSimulationWide *divisor,
                                   const NtSimulationWide *angle, NtSimulationWide *difference)
{
    NtSimulationWide power, scaled;
    nt_simulation_wide_multiply(&power,angle,angle);
    nt_simulation_wide_multiply(&scaled,&power,divisor);
    nt_simulation_wide_subtract(difference,squared,&scaled);
}

static void scaled_wide_ratio(NtSimulationWide *n, const NtSimulationWide *d, NtSpatialExact *out)
{
    const int shift = n->offset+(int)n->count-d->offset-(int)d->count;
    n->offset -= shift;
    nt_spatial_exact_number(out,nt_simulation_wide_quotient(n,d),32*shift);
}

double nt_simulation_round_parallel_angle(const NtSpatialExact n[3], const NtSpatialExact *unit,
                                          const NtSpatialExact *initial, double angle_sum,
                                          bool opposite, bool *reversed)
{
    NtSimulationWide squared, radius, divisor, shift;
    norm_square(n,&squared);
    nt_simulation_widen(&radius,unit);
    nt_simulation_wide_multiply(&divisor,&radius,&radius);
    nt_simulation_widen(&shift,initial);
    shift.negative = !opposite;
    *reversed = false;
    double estimate = angle_sum;
    if (opposite) {
        NtSimulationWide difference, sum, denominator;
        subtract_squared_angle(&squared,&divisor,&shift,&difference);
        if (!difference.count) return 0;
        *reversed = difference.negative;
        difference.negative = false;
        /* Rationalize |increment-initial| to seed the midpoint
         * search even when the rounded angles are equal. */
        nt_simulation_wide_number(&sum,estimate,0);
        nt_simulation_wide_multiply(&denominator,&sum,&divisor);
        estimate = nt_simulation_wide_quotient(&difference,&denominator);
    }
    return round_squared_norm(&squared,&divisor,&shift,*reversed,NULL,estimate);
}

void nt_simulation_angle_residual(const NtSpatialExact n[3], const NtSpatialExact *unit,
                                  const NtSpatialExact *initial, double angle_sum, NtSpatialExact *out)
{
    NtSimulationWide radius, divisor, starting, squared, difference, sum, denominator;
    nt_simulation_widen(&radius,unit);
    nt_simulation_wide_multiply(&divisor,&radius,&radius);
    nt_simulation_widen(&starting,initial);
    norm_square(n,&squared);
    subtract_squared_angle(&squared,&divisor,&starting,&difference);
    if (!difference.count) {
        nt_spatial_exact_number(out,0,0);
        return;
    }
    nt_simulation_wide_number(&sum,angle_sum,0);
    nt_simulation_wide_multiply(&denominator,&sum,&divisor);
    scaled_wide_ratio(&difference,&denominator,out);
}

static NT_SIMULATION_NOINLINE void add_axis_component(const NtSpatialExact *term, const NtSpatialExact *axis_term,
                               const NtSimulationWide *q, const NtSimulationWide *root, NtSpatialExact *out)
{
    NtSimulationWide x, k, weighted, sum;
    nt_simulation_widen(&x,term);
    nt_simulation_widen(&k,axis_term);
    if (x.negative==k.negative || !x.count || !k.count) {
        nt_simulation_wide_multiply(&weighted,&x,root);
        nt_simulation_wide_add(&sum,&weighted,&k);
        scaled_wide_ratio(&sum,root,out);
        return;
    }

    /* For opposite signs, rationalize x+k/sqrt(q) with exact
     * squares. Rounding the positive denominator preserves the
     * significant bits of the residual, even at cancellation. */
    NtSimulationWide square, difference, denominator;
    nt_simulation_wide_multiply(&square,&x,&x);
    nt_simulation_wide_multiply(&weighted,&square,q);
    nt_simulation_wide_multiply(&square,&k,&k);
    nt_simulation_wide_subtract(&difference,&weighted,&square);
    difference.negative ^= x.negative;
    x.negative = k.negative = false;
    nt_simulation_wide_multiply(&weighted,&x,root);
    nt_simulation_wide_add(&sum,&weighted,&k);
    nt_simulation_wide_multiply(&denominator,&sum,root);
    scaled_wide_ratio(&difference,&denominator,out);
}

static NT_SIMULATION_NOINLINE void tiny_axis_weights(const NtSpatialExact turn[3], NtVec3 initial_axis,
                                                     double angle, const NtSpatialExact *unit,
                                                     NtSimulationWide *q, NtSimulationWide *root,
                                                     NtSpatialExact weights[3])
{
    NtSpatialExact axis[3], cross[3], angle_value, half;
    nt_simulation_exact_vec(initial_axis,axis);
    nt_exact_cross(turn,axis,cross);
    nt_spatial_exact_number(&angle_value,angle,0);
    nt_spatial_exact_number(&half,.5,0);
    NtSimulationWide scaled, one;
    norm_square(axis,q);
    const int shift = (q->offset+(int)q->count)/2;
    nt_simulation_wide_copy(&scaled,q);
    scaled.offset -= 2*shift;
    nt_simulation_wide_number(&one,1,0);
    const double length = sqrt(nt_simulation_wide_quotient(&scaled,&one));
    nt_simulation_wide_number(root,length,32*shift);

    for (unsigned i=0;i<3;++i) {
        NtSpatialExact before, twist, sum;
        nt_spatial_exact_multiply(&before,&axis[i],unit);
        nt_spatial_exact_multiply(&twist,&cross[i],&half);
        nt_spatial_exact_add(&sum,&before,&twist);
        nt_spatial_exact_multiply(&weights[i],&sum,&angle_value);
    }
}

void nt_simulation_tiny_vector(const NtSpatialExact turn[3], NtVec3 initial_axis, double angle,
                               const NtSpatialExact *unit, NtSpatialExact out[3])
{
    NtSimulationWide q, root;
    NtSpatialExact weights[3];
    tiny_axis_weights(turn,initial_axis,angle,unit,&q,&root,weights);
    for (unsigned i=0;i<3;++i) add_axis_component(&turn[i],&weights[i],&q,&root,&out[i]);
}

typedef struct TinyAngleTerms {
    NtSimulationWide squared, divisor, radical_squared;
} TinyAngleTerms;

static NT_SIMULATION_NOINLINE void tiny_angle_terms(const NtSpatialExact turn[3], NtVec3 initial_axis,
                                                    double angle, const NtSpatialExact *unit, TinyAngleTerms *out)
{
    /* For a=turn/unit and b=angle*axis/|axis|, the squared angle through
     * fourth order is |a+b|^2-|a×b|^2/12. Keep the initial axis norm as
     * a square root, so a rounded unit vector cannot move a midpoint. */
    NtSpatialExact axis[3], cross[3], dot;
    nt_simulation_exact_vec(initial_axis,axis);
    nt_exact_cross(turn,axis,cross);
    nt_exact_dot(turn,axis,&dot);
    NtSimulationWide twelve, coefficient, start, start_squared, q, radius, base;
    NtSimulationWide term, scaled, sum;
    nt_simulation_wide_number(&twelve,12,0);
    nt_simulation_wide_number(&start,angle,0);
    nt_simulation_wide_multiply(&start_squared,&start,&start);
    norm_square(axis,&q);
    nt_simulation_widen(&radius,unit);
    nt_simulation_wide_multiply(&scaled,&radius,&radius);
    nt_simulation_wide_multiply(&base,&scaled,&q);
    nt_simulation_wide_multiply(&out->divisor,&base,&twelve);

    norm_square(turn,&scaled);
    nt_simulation_wide_multiply(&term,&scaled,&q);
    nt_simulation_wide_multiply(&scaled,&start_squared,&base);
    nt_simulation_wide_add(&sum,&term,&scaled);
    nt_simulation_wide_multiply(&scaled,&sum,&twelve);
    norm_square(cross,&term);
    nt_simulation_wide_multiply(&sum,&term,&start_squared);
    nt_simulation_wide_subtract(&out->squared,&scaled,&sum);

    nt_simulation_wide_number(&coefficient,24,0);
    nt_simulation_widen(&scaled,&dot);
    nt_simulation_wide_multiply(&sum,&start,&scaled);
    nt_simulation_wide_multiply(&scaled,&sum,&radius);
    nt_simulation_wide_multiply(&sum,&scaled,&coefficient);
    nt_simulation_wide_multiply(&scaled,&sum,&sum);
    nt_simulation_wide_multiply(&out->radical_squared,&scaled,&q);
    out->radical_squared.negative = sum.negative;
}

double nt_simulation_round_tiny_angle(const NtSpatialExact turn[3], NtVec3 initial_axis, double angle,
                                      const NtSpatialExact *unit, double estimate)
{
    TinyAngleTerms terms;
    tiny_angle_terms(turn,initial_axis,angle,unit,&terms);
    return round_squared_norm(&terms.squared,&terms.divisor,NULL,false,&terms.radical_squared,estimate);
}
