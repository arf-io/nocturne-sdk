/* Pattern families adapted from native/src/source_material_sampler.c at
 * 53191655ec77. Copyright (c) 2025-2026 Damien Heiser. MIT, see LICENSE. */
#include <nocturne/paint_sample.h>

#include "paint_internal.h"

static const double full_turn = 6.2831853071795864769;

static bool easing_valid(NtEasing easing)
{
    return easing >= NT_LINEAR && easing <= NT_STEP_END;
}

static NtResult animation_result(NtStatus result)
{
    return result == NT_OK ? NT_RESULT_OK : result == NT_OUT_OF_RANGE ?
           NT_RESULT_OUT_OF_RANGE : NT_RESULT_INVALID_ARGUMENT;
}

NtResult nt_gradient_sample(const NtGradient *gradient, double coordinate, NtColor *out)
{
    if (!gradient || !out || !isfinite(coordinate) || !gradient->stops || !gradient->count ||
        !nt_address_valid(gradient->address) || !nt_color_interpolation_valid(gradient->interpolation))
        return NT_RESULT_INVALID_ARGUMENT;
    if (gradient->count > SIZE_MAX / sizeof(*gradient->stops)) return NT_RESULT_OUT_OF_RANGE;

    const NtGradientStop *stops = gradient->stops;
    const size_t count = gradient->count;
    for (size_t i = 0; i < count; ++i) {
        const NtGradientStop stop = stops[i];
        if (!nt_unit_interval(stop.position) || !easing_valid(stop.from_here) ||
            (i && stop.position < stops[i - 1].position))
            return NT_RESULT_INVALID_ARGUMENT;
        NtColor checked;
        const NtResult result = nt_color_convert(stop.color, stop.color.space,
                                                 gradient->interpolation.gamut, &checked);
        if (result != NT_RESULT_OK) return result;
    }

    const double t = nt_address_coordinate(coordinate, gradient->address);
    size_t next = 0;
    while (next < count && stops[next].position <= t) ++next;
    const NtColorInterpolation spec = gradient->interpolation;
    if (!next || next == count || stops[next - 1].position == t)
        return nt_color_convert(stops[next ? next - 1 : 0].color, spec.space, spec.gamut, out);
    const NtGradientStop a = stops[next - 1], b = stops[next];
    const double fraction = (t - a.position) / (b.position - a.position);
    double eased;
    const NtStatus status = nt_ease(a.from_here, fraction, &eased);
    if (status != NT_OK) return animation_result(status);
    return nt_color_mix(a.color, b.color, eased, spec, out);
}

NtResult nt_palette_validate(const NtPaletteRamp *ramp)
{
    if (!ramp || !ramp->palette.colors || !ramp->palette.count ||
        !nt_address_valid(ramp->address) || !nt_color_interpolation_valid(ramp->interpolation) ||
        !easing_valid(ramp->easing) || (ramp->filter != NT_PALETTE_STEP &&
                                        ramp->filter != NT_PALETTE_LINEAR))
        return NT_RESULT_INVALID_ARGUMENT;
    const size_t count = ramp->palette.count;
    return count > UINT32_MAX || count > SIZE_MAX / sizeof(NtRgba) ? NT_RESULT_OUT_OF_RANGE : NT_RESULT_OK;
}

NtResult nt_palette_sample(const NtPaletteRamp *ramp, double coordinate, NtColor *out)
{
    if (!out || !isfinite(coordinate)) return NT_RESULT_INVALID_ARGUMENT;
    const NtResult valid = nt_palette_validate(ramp);
    if (valid != NT_RESULT_OK) return valid;
    const size_t count = ramp->palette.count;
    const NtRgba *colors = ramp->palette.colors;
    const NtColorInterpolation spec = ramp->interpolation;
    if (count == 1) return nt_color_convert(nt_color_from_rgba(colors[0]), spec.space, spec.gamut, out);

    const double t = nt_address_coordinate(coordinate, ramp->address);
    const bool cyclic = ramp->address == NT_PAINT_REPEAT;
    const size_t segments = ramp->filter == NT_PALETTE_STEP || cyclic ? count : count - 1;
    const double intervals = (double)segments;
    size_t index = nt_sample_interval(t, segments);
    if (index == count) index = count - 1;
    const NtColor a = nt_color_from_rgba(colors[index]);
    if (ramp->filter == NT_PALETTE_STEP || (!cyclic && index == count - 1))
        return nt_color_convert(a, spec.space, spec.gamut, out);
    const NtColor b = nt_color_from_rgba(colors[(index + 1) % count]);
    const double left = (double)index / intervals, right = (double)(index + 1) / intervals;
    double eased;
    const NtStatus status = nt_ease(ramp->easing, (t - left) / (right - left), &eased);
    if (status != NT_OK) return animation_result(status);
    return nt_color_mix(a, b, eased, spec, out);
}

static bool valid_sample(NtPaintSample sample)
{
    return isfinite(sample.x) && isfinite(sample.y) &&
           (sample.motion == NT_MOTION_FULL || sample.motion == NT_MOTION_REDUCED);
}

static double sine(double cycles)
{
    const double phase = fmod(cycles, 1.0);
    const double magnitude = fabs(phase);
    if (magnitude == 0.0 || magnitude == 0.5) return 0.0;
    if (magnitude == 0.25) return copysign(1.0, phase);
    if (magnitude == 0.75) return -copysign(1.0, phase);
    return sin(phase * full_turn);
}

typedef struct RotationPhase {
    int quadrant, direction;
    NtTimeUs ticks;
} RotationPhase;

static RotationPhase rotation_phase(NtTimeUs elapsed, NtTimeUs period, bool reverse)
{
    NtTimeUs remainder = elapsed % period;
    int quadrant = 0;
    /* Divide four times the remainder by the period without overflowing. */
    for (unsigned bit = 0; bit < 2; ++bit) {
        const bool carry = remainder >= period - remainder;
        remainder = carry ? remainder - (period - remainder) : remainder + remainder;
        quadrant = 2 * quadrant + carry;
    }
    const int direction = reverse ? -1 : 1;
    if (remainder > period - remainder)
        return (RotationPhase){(quadrant + 1) * direction, -direction, period - remainder};
    return (RotationPhase){quadrant * direction, direction, remainder};
}

static void quadrant_axes(int quadrant, double radians, double *cs, double *sn)
{
    const double cosine = cos(radians), sine_value = sin(radians);
    switch ((quadrant % 4 + 4) % 4) {
    case 0: *cs = cosine; *sn = sine_value; break;
    case 1: *cs = -sine_value; *sn = cosine; break;
    case 2: *cs = -cosine; *sn = -sine_value; break;
    default: *cs = sine_value; *sn = -cosine; break;
    }
}

static double swing_sine(RotationPhase phase, NtTimeUs period)
{
    const int quadrant = (phase.quadrant % 4 + 4) % 4;
    if (quadrant % 2 == 0 && period % 3 == 0 && phase.ticks == period / 3)
        return quadrant == 0 ? phase.direction * 0.5 : phase.direction * -0.5;
    const double radians = (full_turn / 4.0) * ((double)phase.ticks / (double)period);
    double cosine, sine_value;
    quadrant_axes(quadrant, phase.direction * radians, &cosine, &sine_value);
    return sine_value;
}

static double fused_sum_tail(double a, double b, double c, double fused)
{
    const double product = fma(a, b, 0.0);
    const double sum = product + c;
    const double added = sum - product;
    const double addition_tail = (product - (sum - added)) + (c - added);
    return ((sum - fused) + fma(a, b, -product)) + addition_tail;
}

static double spin_residual(double base, RotationPhase phase, NtTimeUs period)
{
    if (!phase.ticks) return base;
    if (phase.ticks == period - phase.ticks) return base + 45.0 * phase.direction;

    /* Cancel authored and clock offsets in the numerator before dividing.
     * Both 32-bit halves and their integer products by 90 are exact. */
    const double scale = 90.0 * phase.direction;
    const double period_high = (double)(period >> 32) * 0x1p32;
    const double period_low = (double)(uint32_t)period;
    const double offset_high = scale * (double)(phase.ticks >> 32) * 0x1p32;
    const double offset_low = scale * (double)(uint32_t)phase.ticks;
    const double high = fma(base, period_high, offset_high);
    const double low = fma(base, period_low, offset_low);
    const double sum = high + low;
    /* A rounded zero can hide a small residual at a grid boundary. */
    const double numerator = sum != 0.0 ? sum :
        fused_sum_tail(base, period_high, offset_high, high) +
        fused_sum_tail(base, period_low, offset_low, low);
    return numerator / (double)period;
}

static void rotation_axes(const NtPaintField *field, NtTimeUs elapsed, double *cs, double *sn)
{
    int base_quadrant;
    const double base = remquo(field->angle_degrees, 90.0, &base_quadrant);
    int quadrant = base_quadrant % 4;
    double residual = base;
    if (field->rotation != NT_PAINT_FIXED) {
        const NtTimeUs period = field->rotation_period_us;
        RotationPhase phase = rotation_phase(elapsed, period, field->reverse_rotation);
        if (field->rotation == NT_PAINT_SPIN) {
            quadrant += phase.quadrant;
            residual = spin_residual(base, phase, period);
            if (fabs(residual) > 45.0) {
                /* Recenter the integer phase at the adjacent quadrant
                 * before the next cancellation near 90 degrees. */
                quadrant += phase.direction;
                phase.ticks = period - phase.ticks;
                phase.direction = -phase.direction;
                residual = spin_residual(base, phase, period);
            }
        } else {
            const double offset = swing_sine(phase, period) * field->swing_degrees;
            int offset_quadrant, sum_quadrant;
            const double offset_residual = remquo(offset, 90.0, &offset_quadrant);
            const double sum = base + offset_residual;
            const double added = sum - base;
            const double tail = (base - (sum - added)) + (offset_residual - added);
            residual = remquo(sum, 90.0, &sum_quadrant) + tail;
            quadrant += offset_quadrant % 4 + sum_quadrant % 4;
        }
    }
    quadrant_axes(quadrant, residual * (full_turn / 360.0), cs, sn);
}

static double pattern_value(NtPaintPattern pattern, double x, double y, double phase, double amplitude)
{
    /* Reduce sinusoidal bases before combining them with phase, so
     * large finite coordinates do not erase a small time offset. */
    const double px = fmod(x, 1.0), py = fmod(y, 1.0), p = fmod(phase, 1.0);
    switch (pattern) {
    case NT_PAINT_LINEAR: return x;
    case NT_PAINT_RADIAL: return hypot(x, y);
    case NT_PAINT_CONIC: return x == 0.0 && y == 0.0 ? 0.0 :
        nt_wrap_positive(atan2(y, x) / full_turn, 1.0);
    case NT_PAINT_WAVE: return x + amplitude * sine(py + p);
    case NT_PAINT_PLASMA:
        return (sine(px + p) + sine(py - p) +
                sine(fmod(x * 0.5, 1.0) + fmod(y * 0.5, 1.0) + p) +
                sine(fmod(hypot(x, y), 1.0) - p)) / 4.0;
    case NT_PAINT_MOIRE:
        return sine(px + p) + sine(px + fmod(x * 0.03, 1.0) + fmod(y * 0.5, 1.0) - p);
    case NT_PAINT_AURORA:
        return y * 0.5 + amplitude * sine(fmod(x * 0.5, 1.0) + p) + sine(px * 3.0 - p) / 5.0;
    case NT_PAINT_INTERFERENCE:
        return (sine(px + p) + sine(py - p) + sine(px - py + p)) / 3.0;
    case NT_PAINT_CHEVRONS: return fabs(nt_wrap_positive(x, 2.0) - 1.0) + y;
    case NT_PAINT_DIAMOND: return fabs(x) + fabs(y);
    case NT_PAINT_CHECKER:
        return ((nt_wrap_positive(x, 2.0) >= 1.0) != (nt_wrap_positive(y, 2.0) >= 1.0)) ? 1.0 : 0.0;
    case NT_PAINT_UNIFORM: return 0.0;
    }
    return NAN;
}

NtResult nt_paint_coordinate(const NtPaintField *field, NtPaintSample sample, double *out)
{
    if (!field || !out || !valid_sample(sample) ||
        field->pattern < NT_PAINT_LINEAR || field->pattern > NT_PAINT_UNIFORM ||
        field->rotation < NT_PAINT_FIXED || field->rotation > NT_PAINT_SWING ||
        (field->rotation != NT_PAINT_FIXED && !field->rotation_period_us) ||
        !isfinite(field->origin_x) || !isfinite(field->origin_y) ||
        !isfinite(field->scale_x) || field->scale_x <= 0.0 ||
        !isfinite(field->scale_y) || field->scale_y <= 0.0 ||
        !isfinite(field->angle_degrees) || !isfinite(field->swing_degrees) ||
        !isfinite(field->phase) || !isfinite(field->wave_amplitude))
        return NT_RESULT_INVALID_ARGUMENT;

    const NtTimeUs elapsed = sample.motion == NT_MOTION_REDUCED ? 0 : sample.elapsed_us;
    double phase = field->phase;
    if (field->phase_track) {
        NtAnimationSample timing;
        const NtStatus status = nt_tween_sample(field->phase_track, elapsed, &timing);
        if (status != NT_OK) return animation_result(status);
        phase += timing.value;
        if (!isfinite(phase)) return NT_RESULT_OUT_OF_RANGE;
    }
    if (field->pattern == NT_PAINT_UNIFORM) {
        *out = phase;
        return NT_RESULT_OK;
    }

    const double dx = sample.x - field->origin_x, dy = sample.y - field->origin_y;
    if (!isfinite(dx) || !isfinite(dy)) return NT_RESULT_OUT_OF_RANGE;
    double cs, sn;
    rotation_axes(field, elapsed, &cs, &sn);
    const double x = (dx * cs + dy * sn) / field->scale_x;
    const double y = (dy * cs - dx * sn) / field->scale_y;
    if (!isfinite(x) || !isfinite(y)) return NT_RESULT_OUT_OF_RANGE;
    const double position = pattern_value(field->pattern, x, y, phase, field->wave_amplitude) + phase;
    if (!isfinite(position)) return NT_RESULT_OUT_OF_RANGE;
    *out = position;
    return NT_RESULT_OK;
}

static NtResult sample_opacity(const NtPaint *paint, NtPaintSample sample, double *out)
{
    const NtKeyframeTrack *track = paint->opacity_track;
    double opacity = paint->opacity;
    if (track) {
        const NtTimeUs elapsed = sample.motion == NT_MOTION_REDUCED ? 0 : sample.elapsed_us;
        if (!track->keys || !track->count || track->count > SIZE_MAX / sizeof(*track->keys))
            return NT_RESULT_INVALID_ARGUMENT;
        for (size_t i = 0; i < track->count; ++i)
            if (!nt_unit_interval(track->keys[i].value)) return NT_RESULT_INVALID_ARGUMENT;
        NtAnimationSample factor;
        const NtStatus status = nt_keyframes_sample(track, elapsed, &factor);
        if (status != NT_OK) return animation_result(status);
        opacity *= factor.value;
    }
    *out = opacity;
    return NT_RESULT_OK;
}

NtResult nt_paint_sample(const NtPaint *paint, NtPaintSample sample, NtColor *out)
{
    if (!paint || !out || !valid_sample(sample) || !nt_unit_interval(paint->opacity) ||
        paint->kind < NT_PAINT_SOLID || paint->kind > NT_PAINT_PALETTE)
        return NT_RESULT_INVALID_ARGUMENT;
    double opacity;
    NtResult result = sample_opacity(paint, sample, &opacity);
    if (result != NT_RESULT_OK) return result;

    NtColor color;
    if (paint->kind == NT_PAINT_SOLID) {
        result = nt_color_convert(paint->content.solid, paint->content.solid.space, NT_GAMUT_CLIP, &color);
    } else {
        double coordinate;
        result = nt_paint_coordinate(&paint->field, sample, &coordinate);
        if (result != NT_RESULT_OK) return result;
        result = paint->kind == NT_PAINT_GRADIENT ?
            nt_gradient_sample(&paint->content.gradient, coordinate, &color) :
            nt_palette_sample(&paint->content.palette, coordinate, &color);
    }
    if (result != NT_RESULT_OK) return result;
    color.alpha *= opacity;
    *out = color;
    return NT_RESULT_OK;
}

NtResult nt_paint_sample_rgba(const NtPaint *paint, NtPaintSample sample,
                              NtGamutPolicy gamut, NtRgba *out)
{
    if (!out || !nt_gamut_valid(gamut)) return NT_RESULT_INVALID_ARGUMENT;
    NtColor color;
    const NtResult result = nt_paint_sample(paint, sample, &color);
    return result == NT_RESULT_OK ? nt_color_to_rgba(color, gamut, out) : result;
}
