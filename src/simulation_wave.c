#include "simulation_internal.h"

static const double radians_per_turn = 0x1.921fb54442d18p+2;

typedef struct WaveSum {
    NtSpatialExact height, dx, dz, velocity, laplacian;
} WaveSum;

static void wave_trig(NtWaterWave wave, NtPoint xz, double seconds, double *sine, double *cosine)
{
    NtSpatialExact x, z, time, phase, position, shifted, turns, one;
    nt_simulation_product(&x,wave.cycles_per_unit.x,xz.x);
    nt_simulation_product(&z,wave.cycles_per_unit.y,xz.y);
    nt_simulation_product(&time,wave.cycles_per_second,seconds);
    nt_spatial_exact_number(&phase,wave.phase_turns,0);
    nt_spatial_exact_add(&position,&x,&z);
    nt_spatial_exact_subtract(&shifted,&position,&time);
    nt_spatial_exact_add(&turns,&shifted,&phase);
    nt_spatial_exact_number(&one,1,0);

    /* The bounded quarter index fits exactly in binary64. Subtract it
     * before rounding the angle so cancellation retains small residuals. */
    const double quarters = nearbyint(4*nt_spatial_exact_quotient(&turns,&one));
    NtSpatialExact origin, residual, factor, radians;
    nt_spatial_exact_number(&origin,quarters,-2);
    nt_spatial_exact_subtract(&residual,&turns,&origin);
    nt_spatial_exact_number(&factor,radians_per_turn,0);
    nt_spatial_exact_multiply(&radians,&residual,&factor);
    const double angle = nt_spatial_exact_quotient(&radians,&one);
    const double s = sin(angle), c = cos(angle);
    const unsigned quadrant = (unsigned)((uint64_t)(int64_t)quarters & UINT64_C(3));
    switch (quadrant) {
    case 0: *sine = s; *cosine = c; break;
    case 1: *sine = c; *cosine = -s; break;
    case 2: *sine = -s; *cosine = -c; break;
    default: *sine = -c; *cosine = s; break;
    }
}

static void wave_accumulate(WaveSum *sum, NtWaterWave wave, double sine, double cosine)
{
    NtSpatialExact height, slope, kx, kz, frequency, contribution;
    nt_simulation_product(&height,wave.amplitude,cosine);
    nt_simulation_product(&slope,-wave.amplitude,sine);
    nt_spatial_exact_number(&kx,wave.cycles_per_unit.x,0);
    nt_spatial_exact_number(&kz,wave.cycles_per_unit.y,0);
    nt_spatial_exact_number(&frequency,-wave.cycles_per_second,0);

    nt_simulation_accumulate(&sum->height,&height);
    nt_spatial_exact_multiply(&contribution,&slope,&kx);
    nt_simulation_accumulate(&sum->dx,&contribution);
    nt_spatial_exact_multiply(&contribution,&slope,&kz);
    nt_simulation_accumulate(&sum->dz,&contribution);
    nt_spatial_exact_multiply(&contribution,&slope,&frequency);
    nt_simulation_accumulate(&sum->velocity,&contribution);

    NtSpatialExact x_squared, z_squared, wave_number_squared;
    nt_spatial_exact_multiply(&x_squared,&kx,&kx);
    nt_spatial_exact_multiply(&z_squared,&kz,&kz);
    nt_spatial_exact_add(&wave_number_squared,&x_squared,&z_squared);
    nt_spatial_exact_multiply(&contribution,&height,&wave_number_squared);
    nt_spatial_exact_negate(&contribution);
    nt_simulation_accumulate(&sum->laplacian,&contribution);
}

static double wave_round(const NtSpatialExact *sum, const NtSpatialExact *factor,
                         const NtSpatialExact *one)
{
    NtSpatialExact scaled;
    nt_spatial_exact_multiply(&scaled,sum,factor);
    return nt_spatial_exact_quotient(&scaled,one);
}

NtResult nt_water_wave_sample(const NtWaterWave *waves, size_t count, NtPoint xz,
                               double seconds, NtWaterWaveSample *out)
{
    if (!out || (count && !waves)) return NT_RESULT_INVALID_ARGUMENT;
    if (count > NT_WATER_MAX_WAVES) return NT_RESULT_OUT_OF_RANGE;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    if (!nt_simulation_bounded(xz.x) || !nt_simulation_bounded(xz.y) ||
        !nt_simulation_bounded(seconds))
        return nt_simulation_finish(&saved,NT_RESULT_INVALID_ARGUMENT);
    WaveSum sum = {0};
    for (size_t i = 0; i < count; ++i) {
        const NtWaterWave w = waves[i];
        if (!nt_simulation_bounded(w.amplitude) || !nt_simulation_bounded(w.cycles_per_unit.x) ||
            !nt_simulation_bounded(w.cycles_per_unit.y) || !nt_simulation_bounded(w.cycles_per_second) ||
            !nt_simulation_bounded(w.phase_turns))
            return nt_simulation_finish(&saved,NT_RESULT_INVALID_ARGUMENT);
        double sine, cosine;
        wave_trig(w,xz,seconds,&sine,&cosine);
        wave_accumulate(&sum,w,sine,cosine);
    }
    /* Apply the common turn-to-radian factors after summing each field. */
    NtSpatialExact one, radians, radians_squared;
    nt_spatial_exact_number(&one,1,0);
    nt_spatial_exact_number(&radians,radians_per_turn,0);
    nt_spatial_exact_multiply(&radians_squared,&radians,&radians);
    NtWaterWaveSample result = {
        .height = nt_spatial_exact_quotient(&sum.height,&one),
        .gradient = {wave_round(&sum.dx,&radians,&one),wave_round(&sum.dz,&radians,&one)},
        .vertical_velocity = wave_round(&sum.velocity,&radians,&one),
        .laplacian = wave_round(&sum.laplacian,&radians_squared,&one),
    };
    const double length = sqrt(1.0 + result.gradient.x*result.gradient.x + result.gradient.y*result.gradient.y);
    result.normal = (NtVec3){-result.gradient.x/length,1.0/length,-result.gradient.y/length};
    const NtResult status = nt_simulation_finish(&saved,NT_RESULT_OK);
    if (status == NT_RESULT_OK) *out = result;
    return status;
}
