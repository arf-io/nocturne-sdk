#include <nocturne/animation.h>

#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "spring line %d: %s\n", __LINE__, #condition); exit(EXIT_FAILURE); \
} } while (0)

static const NtSpringTuning tuning = {125000, 1e-5, 1e-4};

static NtSpringSample sample(const NtSpring *spring, uint64_t at, bool reduced)
{
    NtSpringSample result;
    CHECK(nt_spring_sample(spring, at, reduced, &result) == NT_OK);
    return result;
}

static NtSpringSample reference(double initial, double speed, double target, double seconds)
{
    double frequency = 8;
    double offset = initial - target;
    double slope = speed + offset * frequency;
    double decay = exp(-frequency * seconds);
    return (NtSpringSample){
        target + decay * (offset + seconds * slope),
        decay * (speed - frequency * seconds * slope),
        NT_RUNNING
    };
}

static void continuity(void)
{
    const uint64_t start = 39000, at = 164000;
    NtSpring spring;
    CHECK(nt_spring_init(&spring, tuning, 2, 7, 12, start) == NT_OK);
    NtSpringSample initial = sample(&spring, start, false);
    CHECK(initial.phase == NT_RUNNING && initial.value == 2 && initial.velocity == 7);
    NtSpringSample delayed = sample(&spring, start - 1, false);
    CHECK(delayed.phase == NT_DELAYED && delayed.value == 2 && delayed.velocity == 0);
    for (unsigned i = 16; i; --i) {
        uint64_t delta = i * 20000;
        NtSpringSample actual = sample(&spring, start + delta, false);
        NtSpringSample expected = reference(2, 7, 12, delta / 1000000.0);
        CHECK(actual.phase == NT_RUNNING && fabs(actual.value - expected.value) < 1e-12);
        CHECK(fabs(actual.velocity - expected.velocity) < 1e-12);
    }
    NtSpringSample original = sample(&spring, at, false);
    NtSpring reset = spring;
    CHECK(nt_spring_retarget(&spring, at, -5, NT_RETARGET_KEEP_VELOCITY) == NT_OK);
    CHECK(nt_spring_retarget(&reset, at, -5, NT_RETARGET_RESET_VELOCITY) == NT_OK);
    NtSpringSample continued = sample(&spring, at, false);
    NtSpringSample restarted = sample(&reset, at, false);
    CHECK(continued.value == original.value && continued.velocity == original.velocity);
    CHECK(restarted.value == original.value && restarted.velocity == 0);
    continued = sample(&spring, at + 1, false);
    restarted = sample(&reset, at + 1, false);
    CHECK(continued.value > original.value && continued.velocity > 0);
    CHECK(restarted.value < original.value && restarted.velocity < 0);
    CHECK(fabs((continued.value - original.value) * 1e6 - original.velocity) < 1e-3);
    printf("retarget kept value %.9g and velocity %.9g, reset kept position\n",
           original.value, original.velocity);
}

static void settling_and_playback(void)
{
    NtSpring spring;
    CHECK(nt_spring_init(&spring, tuning, 3, 8, 3, 0) == NT_OK);
    NtSpringSample turnaround = sample(&spring, 125000, false);
    CHECK(turnaround.phase == NT_RUNNING && turnaround.velocity == 0);
    CHECK(fabs(turnaround.value - (3 + 1 / exp(1.0))) < 1e-12);
    CHECK(nt_spring_init(&spring, tuning, 6, -2, 3, 0) == NT_OK);
    uint64_t running = 0, finished = 3000000;
    CHECK(sample(&spring, finished, false).phase == NT_FINISHED);
    while (finished - running > 1) {
        uint64_t middle = running + (finished - running) / 2;
        if (sample(&spring, middle, false).phase == NT_FINISHED) finished = middle;
        else running = middle;
    }
    NtSpringSample before = sample(&spring, running, false);
    NtSpringSample after = sample(&spring, finished, false);
    NtSpringSample physical = reference(6, -2, 3, finished / 1000000.0);
    CHECK(before.phase == NT_RUNNING && after.phase == NT_FINISHED);
    CHECK(after.value == 3 && after.velocity == 0);
    CHECK(fabs(after.value - physical.value) < tuning.position_tolerance);
    CHECK(fabs(physical.velocity) < tuning.velocity_tolerance);
    CHECK(fabs(before.value - after.value) < tuning.position_tolerance);
    CHECK(fabs(before.velocity) < tuning.velocity_tolerance);
    CHECK(sample(&spring, UINT64_MAX, false).phase == NT_FINISHED);
    CHECK(sample(&spring, running, false).value == before.value);

    NtPlayback playback;
    uint64_t elapsed;
    CHECK(nt_playback_init(&playback, 100) == NT_OK);
    CHECK(nt_playback_pause(&playback, 100100) == NT_OK);
    CHECK(nt_playback_elapsed(&playback, 100200, &elapsed) == NT_OK && elapsed == 100000);
    NtSpringSample active = sample(&spring, elapsed, false);
    NtSpringSample reduced = sample(&spring, elapsed, true);
    CHECK(reduced.phase == NT_FINISHED && reduced.value == 3 && reduced.velocity == 0);
    CHECK(sample(&spring, elapsed, false).value == active.value);
    CHECK(nt_playback_seek(&playback, 100201, 250000) == NT_OK);
    CHECK(nt_playback_resume(&playback, 100300) == NT_OK);
    CHECK(nt_playback_cancel(&playback, 200300) == NT_OK);
    CHECK(nt_playback_elapsed(&playback, 700300, &elapsed) == NT_OK && elapsed == 350000);
    CHECK(sample(&spring, elapsed, false).phase == NT_RUNNING);
    CHECK(nt_spring_retarget(&spring, elapsed, 7, NT_RETARGET_SETTLE) == NT_OK);
    after = sample(&spring, elapsed, false);
    CHECK(after.phase == NT_FINISHED && after.value == 7 && after.velocity == 0);
    printf("settled at %llu us within both tolerances, pause/seek/cancel and reduction passed\n",
           (unsigned long long)finished);
}

static void range_and_validation(void)
{
    const double minimum = nextafter(0.0, 1.0);
    NtSpringTuning strict = {1000000, minimum, minimum};
    NtSpring spring;
    CHECK(nt_spring_init(&spring, strict, DBL_MAX, 0, -DBL_MAX, 0) == NT_OK);
    NtSpringSample middle = sample(&spring, 1000000, false);
    CHECK(fabs(middle.value / DBL_MAX - (4 / exp(1.0) - 1)) < 1e-14);
    CHECK(fabs(middle.velocity / DBL_MAX + 2 / exp(1.0)) < 1e-14);
    CHECK(nt_spring_init(&spring, strict, DBL_MAX, 0, 0, 0) == NT_OK);
    NtSpringSample tail = sample(&spring, 900000000, false);
    double expected = exp(log(DBL_MAX) + log(901.0) - 900);
    CHECK(tail.phase == NT_RUNNING && fabs(tail.value / expected - 1) < 1e-12);

    strict.response_us = UINT64_MAX;
    CHECK(nt_spring_init(&spring, strict, 0, 0, 1e300, UINT64_MAX - 1) == NT_OK);
    NtSpringSample short_step = sample(&spring, UINT64_MAX, false);
    double u = 1 / (double)UINT64_MAX;
    CHECK(fabs(short_step.value / (1e300 * (0.5 * u * u)) - 1) < 1e-14);
    CHECK(nt_spring_retarget(&spring, UINT64_MAX - 2, 1, NT_RETARGET_SETTLE) == NT_INVALID_ARGUMENT);
    CHECK(sample(&spring, UINT64_MAX, false).value == short_step.value);

    CHECK(nt_spring_init(&spring, tuning, DBL_MAX, DBL_MAX, DBL_MAX, 0) == NT_OK);
    NtSpringSample output = {3, 4, NT_DELAYED};
    CHECK(nt_spring_sample(&spring, 100000, false, &output) == NT_OUT_OF_RANGE);
    CHECK(output.value == 3 && output.velocity == 4 && output.phase == NT_DELAYED);
    CHECK(nt_spring_retarget(&spring, 100000, 1, NT_RETARGET_KEEP_VELOCITY) == NT_OUT_OF_RANGE);
    CHECK(sample(&spring, 0, false).velocity == DBL_MAX);
    CHECK(nt_spring_retarget(&spring, 100000, 1, NT_RETARGET_SETTLE) == NT_OK);
    CHECK(sample(&spring, 100000, false).value == 1);
    CHECK(nt_spring_init(&spring, (NtSpringTuning){0, 1, 1}, 0, 0, 0, 0) == NT_INVALID_ARGUMENT);
    CHECK(nt_spring_retarget(&spring, 100001, NAN, NT_RETARGET_KEEP_VELOCITY) == NT_INVALID_ARGUMENT);
    CHECK(nt_spring_retarget(&spring, 100001, 1, (NtRetargetPolicy)4) == NT_INVALID_ARGUMENT);
    CHECK(sample(&spring, 100001, false).value == 1);
    puts("wide input, tiny progress, overflow and atomic error checks passed");
}

static void cancellation_and_clock_edges(void)
{
    NtSpring spring;
    const NtSpringTuning seconds = {1000000, 1e-7, 1e-7};
    const uint64_t clock = UINT64_C(1) << 63;
    for (int sign = -1; sign <= 1; sign += 2) {
        CHECK(nt_spring_init(&spring, seconds, sign * 0x1p100, -sign * 0x1p101, sign, 0) == NT_OK);
        const double position = sample(&spring, 1000000, false).value;
        const double velocity = sample(&spring, 2000000, false).velocity;
        CHECK(fabs(position - sign * (1 - 2 * exp(-1.0))) < 1e-13);
        CHECK(fabs(velocity - sign * 2 * exp(-2.0)) < 1e-13);

        const NtSpringTuning long_clock = {clock, 1e-7, 1e-7};
        CHECK(nt_spring_init(&spring, long_clock, 0, sign * 0x1p500, 0, 0) == NT_OK);
        const double adjacent = sign * 0x1p437 * exp(-1.0);
        CHECK(fabs(sample(&spring, clock - 1, false).velocity / adjacent - 1) < 1e-13);
        CHECK(sample(&spring, clock, false).velocity == 0);
        CHECK(fabs(sample(&spring, clock + 1, false).velocity / adjacent + 1) < 1e-13);

        CHECK(nt_spring_init(&spring, seconds, sign * DBL_MAX, sign, sign * DBL_MAX, 0) == NT_OK);
        NtSpringSample unchanged = {0, 0, NT_DELAYED};
        CHECK(nt_spring_sample(&spring, 1, false, &unchanged) == NT_OUT_OF_RANGE);
        CHECK(unchanged.phase == NT_DELAYED && unchanged.value == 0 && unchanged.velocity == 0);
        CHECK(nt_spring_retarget(&spring, 1, 7, NT_RETARGET_KEEP_VELOCITY) == NT_OUT_OF_RANGE);
        CHECK(sample(&spring, 0, false).value == sign * DBL_MAX);
        CHECK(sample(&spring, 1, true).value == sign * DBL_MAX);

        const NtSpringTuning early = {64000000, 1e-7, 1e-7};
        const double goal = sign * DBL_MAX;
        const double inward = nextafter(goal, 0);
        const double tangent = sign * 0x1.02p+972;
        CHECK(nt_spring_init(&spring, early, inward, tangent, goal, 0) == NT_OK);
        CHECK(sample(&spring, 500000, false).value == goal);
        CHECK(nt_spring_init(&spring, early, inward, nextafter(tangent, sign * INFINITY), goal, 0) == NT_OK);
        CHECK(nt_spring_sample(&spring, 500000, false, &unchanged) == NT_OUT_OF_RANGE);
        CHECK(unchanged.phase == NT_DELAYED && unchanged.value == 0 && unchanged.velocity == 0);

        const NtSpringTuning slow = {1000000000, 1e-10, 1e-10};
        CHECK(nt_spring_init(&spring, slow, inward, 0, 0, 0) == NT_OK);
        NtSpringSample contraction = sample(&spring, 2, false);
        CHECK(contraction.phase == NT_RUNNING && fabs(contraction.value) <= fabs(inward));
        CHECK(contraction.velocity * sign < 0);
        CHECK(nt_spring_init(&spring, slow, 0, goal, 0, 0) == NT_OK);
        const NtSpringSample moving = sample(&spring, 2, false);
        CHECK(moving.phase == NT_RUNNING && moving.value * sign > 0);
        CHECK(fabs(moving.velocity) <= DBL_MAX);

        const double derivative_edge = -sign * 0x1.6fc2a2c515da4p+1022;
        const NtSpringTuning half_second = {500000, 1e-10, 1e-10};
        CHECK(nt_spring_init(&spring, half_second, derivative_edge, 0, goal, 0) == NT_OK);
        CHECK(sample(&spring, 500000, false).velocity == goal);
        const double faster = nextafter(derivative_edge, -sign * INFINITY);
        CHECK(nt_spring_init(&spring, half_second, faster, 0, goal, 0) == NT_OK);
        CHECK(nt_spring_sample(&spring, 500000, false, &unchanged) == NT_OUT_OF_RANGE);
        CHECK(unchanged.phase == NT_DELAYED && unchanged.value == 0 && unchanged.velocity == 0);
    }
    puts("large cancellation, exact adjacent clock ticks and outward finite limits passed");
}

static void floating_point_environment(void)
{
    fenv_t saved;
    CHECK(fegetenv(&saved) == 0 && fesetenv(FE_DFL_ENV) == 0);
    NtSpring standard, directed;
    CHECK(nt_spring_init(&standard, tuning, 2, 7, 12, 0) == NT_OK);
    const NtSpringSample expected = sample(&standard, 125000, false);
    CHECK(fesetround(FE_DOWNWARD) == 0 && feclearexcept(FE_ALL_EXCEPT) == 0);
    CHECK(feraiseexcept(FE_DIVBYZERO) == 0);
    CHECK(nt_spring_init(&directed, tuning, 2, 7, 12, 0) == NT_OK);
    const NtSpringSample observed = sample(&directed, 125000, false);
    CHECK(expected.value == observed.value && expected.velocity == observed.velocity);
    CHECK(nt_spring_retarget(&directed, 125000, 3, NT_RETARGET_KEEP_VELOCITY) == NT_OK);
    CHECK(fegetround() == FE_DOWNWARD && fetestexcept(FE_ALL_EXCEPT) == FE_DIVBYZERO);
#ifdef FE_DFL_DISABLE_DENORMS_ENV
    CHECK(fesetenv(FE_DFL_ENV) == 0);
    const double minimum = nextafter(0.0, 1.0);
    CHECK(fesetenv(FE_DFL_DISABLE_DENORMS_ENV) == 0);
    CHECK(feraiseexcept(FE_DIVBYZERO) == 0);
    const NtSpringTuning strict = {125000, minimum, minimum};
    CHECK(nt_spring_init(&directed, strict, minimum, 0, 1, 0) == NT_OK);
    const NtSpringSample tiny = sample(&directed, 0, false);
    CHECK(tiny.phase == NT_RUNNING && fetestexcept(FE_ALL_EXCEPT) == FE_DIVBYZERO);
    CHECK(fesetenv(FE_DFL_ENV) == 0);
    CHECK(tiny.value == minimum);
#endif
    CHECK(fesetenv(&saved) == 0);
    puts("directed rounding and caller exception flags passed");
}

int main(void)
{
    continuity();
    settling_and_playback();
    range_and_validation();
    cancellation_and_clock_edges();
    floating_point_environment();
    return EXIT_SUCCESS;
}
