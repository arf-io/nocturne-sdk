#include <nocturne/animation.h>
#include "fp_environment_internal.h"
#include "scaled_ticks_internal.h"
#include "spring_range_internal.h"

static bool spring_valid(const NtSpring *spring)
{
    return spring && spring->tuning.response_us &&
        isfinite(spring->tuning.position_tolerance) && spring->tuning.position_tolerance > 0 &&
        isfinite(spring->tuning.velocity_tolerance) && spring->tuning.velocity_tolerance > 0 &&
        isfinite(spring->initial_value) && isfinite(spring->initial_velocity) &&
        isfinite(spring->target) && isfinite(spring->settling_age) && spring->settling_age >= 1;
}

static double settling_bound(NtScaledTerm envelope, double tolerance)
{
    if (!envelope.fraction) return 1;
    const NtScaledTerm half = nt_scaled_term(tolerance, -1);
    const double logarithm = log(envelope.fraction / half.fraction) +
        (envelope.exponent - half.exponent) * 0.69314718055994530942;
    return (4.0 / 3.0) * logarithm;
}

static NtScaledSum spring_displacement(const NtSpring *spring)
{
    NtScaledSum displacement = {0};
    nt_scaled_add(&displacement, nt_scaled_term(spring->initial_value, 0));
    nt_scaled_add(&displacement, nt_scaled_term(-spring->target, 0));
    return displacement;
}

static double spring_settling_age(const NtSpring *spring)
{
    const double rate = 1000000.0 / (double)spring->tuning.response_us;
    const NtScaledTerm speed = nt_scaled_term(fabs(spring->initial_velocity), 0);
    const NtScaledSum difference = spring_displacement(spring);
    const NtScaledTerm distance = nt_scaled_value(&difference);
    /* (1+u)exp(-u/4) <= 2 and u exp(-u/4) <= 2 for all u >= 0.
     * Bound the residual and derivative by constants times exp(-3u/4).
     * This gives one fixed settling boundary for out-of-order sampling. */
    NtScaledSum position_bound = {0}, speed_bound = {0};
    nt_scaled_add(&position_bound, nt_scaled_term(fabs(distance.fraction), distance.exponent + 1));
    nt_scaled_add_product(&position_bound, speed, nt_scaled_term(2 / rate, 0));
    nt_scaled_add_product(&speed_bound, nt_scaled_term(fabs(distance.fraction), distance.exponent),
                          nt_scaled_term(2 * rate, 0));
    nt_scaled_add(&speed_bound, nt_scaled_term(speed.fraction, speed.exponent + 1));
    const double age = fmax(settling_bound(nt_scaled_value(&position_bound),
                                          spring->tuning.position_tolerance),
                            settling_bound(nt_scaled_value(&speed_bound),
                                          spring->tuning.velocity_tolerance));
    /* At 1600 both residuals are below half the least positive binary64
     * tolerance, even with maximum inputs and any allowed time constant. */
    return fmin(1600, fmax(1, age));
}

/* 1 - (1+u)exp(-u). Near zero, sum the integrated exponential series so
 * the O(u^2) position change survives cancellation of the linear terms. */
static double initial_blend(double u)
{
    double term = 0.5;
    double sum = term;
    for (int n = 1; n <= 10; ++n) {
        term *= -u * (n + 1) / (n * (n + 2));
        sum += term;
    }
    return u * u * sum;
}

static void add_million_ticks(NtScaledSum *sum, double value, uint64_t a, uint64_t b)
{
    /* Each sum has at most 33 bits. Multiplication by one million adds
     * at most 20 bits, so each part is exactly representable in binary64. */
    const uint64_t low = (uint64_t)(uint32_t)a + (uint32_t)b;
    const uint64_t high = (a >> 32) + (b >> 32);
    const NtScaledTerm factor = nt_scaled_term(value, 0);
    nt_scaled_add_product(sum, factor, nt_scaled_term((double)(low * 1000000), 0));
    nt_scaled_add_product(sum, factor, nt_scaled_term((double)(high * 1000000), 32));
}

static NtScaledTerm spring_decay(double logarithm)
{
    /* Splitting the exponential keeps its scale after a single exp()
     * would underflow. The expanded result may still be representable. */
    int exponent;
    const double third = frexp(exp(logarithm / 3.0), &exponent);
    return nt_scaled_term(third * third * third, exponent * 3);
}

static double spring_position(const NtSpring *spring, uint64_t elapsed, double u)
{
    const uint64_t response = spring->tuning.response_us;
    NtScaledSum divisor = {0}, momentum = {0};
    add_million_ticks(&divisor, 1, response, elapsed);
    nt_scaled_add_tick_product(&momentum, spring->initial_velocity, elapsed, response);

    /* With D = 1e6*(response+elapsed), set P = x0 + v0*elapsed*response/D.
     * The position is g + (P-g)*(1+u)*exp(-u). Keep D*(P-g) exact
     * through the cancellation before weighting or dividing. */
    NtScaledSum amplitude = momentum;
    add_million_ticks(&amplitude, spring->initial_value, response, elapsed);
    add_million_ticks(&amplitude, -spring->target, response, elapsed);
    if (!amplitude.count) return spring->target;

    NtScaledSum position = {0};
    if (u < 1.0 / 16.0) {
        /* P - (P-g)*(1-(1+u)*exp(-u)) near zero. The small correction
         * keeps a strictly inward sample inward even at a finite limit. */
        position = momentum;
        add_million_ticks(&position, spring->initial_value, response, elapsed);
        nt_scaled_add_sum(&position, &amplitude, nt_scaled_term(-initial_blend(u), 0));
    } else {
        add_million_ticks(&position, spring->target, response, elapsed);
        nt_scaled_add_sum(&position, &amplitude, spring_decay(log1p(u) - u));
    }
    const double approximate = nt_scaled_quotient(&position, &divisor, nt_scaled_value(&divisor));
    if (fabs(approximate) < DBL_MAX * .5) return approximate;
    NtScaledSum time_constant = {0};
    add_million_ticks(&time_constant, 1, response, 0);
    return nt_spring_near_limit(spring->target, &amplitude, &time_constant,
                                elapsed, response, approximate);
}

static double spring_velocity(const NtSpring *spring, uint64_t elapsed, double u)
{
    const uint64_t response = spring->tuning.response_us;
    NtScaledSum derivative = {0}, divisor = {0};
    nt_scaled_add_tick_product(&divisor, 1, response, response);
    /* The numerator is (g-x0)*elapsed*1e6 +
     * v0*(response-elapsed)*response. Subtract on the integer clock
     * to preserve the side of the velocity turning point. */
    add_million_ticks(&derivative, spring->target, elapsed, 0);
    add_million_ticks(&derivative, -spring->initial_value, elapsed, 0);
    const bool returning = elapsed > response;
    nt_scaled_add_tick_product(&derivative, returning ? -spring->initial_velocity : spring->initial_velocity,
                     returning ? elapsed - response : response - elapsed, response);
    NtScaledSum velocity = {0};
    if (u < 1.0 / 16.0) {
        velocity = derivative;
        nt_scaled_add_sum(&velocity, &derivative, nt_scaled_term(expm1(-u), 0));
    } else {
        nt_scaled_add_sum(&velocity, &derivative, spring_decay(-u));
    }
    const double approximate = nt_scaled_quotient(&velocity, &divisor, nt_scaled_value(&divisor));
    return fabs(approximate) < DBL_MAX * .5 ? approximate :
        nt_spring_near_limit(0, &derivative, &divisor, elapsed, response, approximate);
}

static NtStatus spring_sample(const NtSpring *spring, uint64_t now_us,
                              bool reduced_motion, NtSpringSample *out)
{
    const NtSpringSample settled = {spring->target, 0, NT_FINISHED};
    if (reduced_motion) {
        *out = settled;
        return NT_OK;
    }
    if (now_us < spring->start_us) {
        *out = (NtSpringSample){spring->initial_value, 0, NT_DELAYED};
        return NT_OK;
    }
    if (spring->initial_value == spring->target && spring->initial_velocity == 0) {
        *out = settled;
        return NT_OK;
    }
    const uint64_t elapsed = now_us - spring->start_us;
    if (!elapsed) {
        *out = (NtSpringSample){spring->initial_value, spring->initial_velocity, NT_RUNNING};
        return NT_OK;
    }
    const double u = (double)elapsed / (double)spring->tuning.response_us;
    *out = u >= spring->settling_age ? settled : (NtSpringSample){
        spring_position(spring, elapsed, u), spring_velocity(spring, elapsed, u), NT_RUNNING
    };
    return isfinite(out->value) && isfinite(out->velocity) ? NT_OK : NT_OUT_OF_RANGE;
}

NtStatus nt_spring_init(NtSpring *spring, NtSpringTuning tuning, double value,
                        double velocity, double target, uint64_t start_us)
{
    if (!spring) return NT_INVALID_ARGUMENT;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_OUT_OF_RANGE;
    NtSpring candidate = {tuning, value, velocity, target, start_us, 1};
    const bool valid = spring_valid(&candidate);
    if (valid) candidate.settling_age = spring_settling_age(&candidate);
    if (!nt_finish_standard_fp(&saved)) return NT_OUT_OF_RANGE;
    if (!valid) return NT_INVALID_ARGUMENT;
    *spring = candidate;
    return NT_OK;
}

NtStatus nt_spring_sample(const NtSpring *spring, uint64_t now_us,
                          bool reduced_motion, NtSpringSample *out)
{
    if (!spring || !out) return NT_INVALID_ARGUMENT;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_OUT_OF_RANGE;
    NtSpringSample candidate;
    const NtStatus status = spring_valid(spring) ? spring_sample(spring, now_us, reduced_motion, &candidate)
                                                : NT_INVALID_ARGUMENT;
    if (!nt_finish_standard_fp(&saved)) return NT_OUT_OF_RANGE;
    if (status == NT_OK) *out = candidate;
    return status;
}

NtStatus nt_spring_retarget(NtSpring *spring, uint64_t now_us, double target,
                            NtRetargetPolicy policy)
{
    if (!spring || policy < NT_RETARGET_KEEP_VELOCITY || policy > NT_RETARGET_SETTLE)
        return NT_INVALID_ARGUMENT;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_OUT_OF_RANGE;
    NtStatus status = NT_INVALID_ARGUMENT;
    NtSpring candidate;
    if (spring_valid(spring) && now_us >= spring->start_us && isfinite(target)) {
        NtSpringSample initial = {target, 0, NT_FINISHED};
        status = policy == NT_RETARGET_SETTLE ? NT_OK : spring_sample(spring, now_us, false, &initial);
        if (status == NT_OK) {
            const double velocity = policy == NT_RETARGET_RESET_VELOCITY ? 0 : initial.velocity;
            candidate = (NtSpring){spring->tuning, initial.value, velocity, target, now_us, 1};
            candidate.settling_age = spring_settling_age(&candidate);
        }
    }
    if (!nt_finish_standard_fp(&saved)) return NT_OUT_OF_RANGE;
    if (status == NT_OK) *spring = candidate;
    return status;
}
