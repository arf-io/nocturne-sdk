#include <nocturne/field.h>

#include "animation_internal.h"
#include "fp_environment_internal.h"
#include "paint_internal.h"
/* Four dyadic spatial products, bias, clock adjustment and radian
 * conversion fit here even before adjacent partials are merged. */
#define NT_SCALED_SUM_CAPACITY 112
#include "scaled_ticks_internal.h"
#include "field_square_internal.h"

#include <float.h>
#include <math.h>

#if defined(__clang__) && !defined(__wasm__)
#pragma STDC FENV_ACCESS ON
#elif defined(_MSC_VER)
#pragma fenv_access(on)
#endif

static const double turn_radians = 0x1.921fb54442d18p+2;

NtRippleField nt_ripple_field_default(void)
{
    return (NtRippleField){.aspect={1,1},.last_radius=12,.width=1,.peak=1,.lifetime_us=1000000,
                           .rings=1,.falloff=NT_OPTICAL_GAUSSIAN,.fade=NT_EASE_IN};
}

NtWaveField nt_wave_field_default(void)
{
    return (NtWaveField){.cycles_per_unit={.125,0},.period_us=1200000};
}

static bool optical_valid(NtOpticalFalloff falloff)
{
    return falloff >= NT_OPTICAL_LINEAR && falloff <= NT_OPTICAL_INVERSE_QUADRATIC;
}

static double inverse_quadratic(NtScaledTerm distance, double peak, double fade)
{
    const NtScaledTerm unit = {.5,1};
    NtScaledSum numerator = {0}, divisor = {.partials={unit},.count=1};
    nt_scaled_add_product(&divisor,distance,distance);
    nt_scaled_add_product(&numerator,nt_scaled_term(peak,0),nt_scaled_term(fade,0));
    return nt_scaled_quotient(&numerator,&divisor,nt_scaled_value(&divisor));
}

static double optical_weight(NtOpticalFalloff falloff, double distance)
{
    if (falloff == NT_OPTICAL_LINEAR) return distance < 1 ? 1-distance : 0;
    if (falloff == NT_OPTICAL_GAUSSIAN) return distance < 40 ? exp(-.5*distance*distance) : 0;
    if (!isfinite(distance)) return 0;
    return inverse_quadratic(nt_scaled_term(distance,0),1,1);
}

NtResult nt_optical_falloff(NtOpticalFalloff falloff, double distance, double *out)
{
    if (!out || !optical_valid(falloff)) return NT_RESULT_INVALID_ARGUMENT;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    const bool valid = distance >= 0 && isfinite(distance);
    const double weight = valid ? optical_weight(falloff,distance) : 0;
    if (!nt_finish_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    if (!valid) return NT_RESULT_INVALID_ARGUMENT;
    *out = weight;
    return NT_RESULT_OK;
}

static bool finite_point(NtPoint p)
{
    return isfinite(p.x) && isfinite(p.y);
}

static NtResult validate_field(const NtField *field, NtMotionPolicy motion)
{
    if (!field || (motion != NT_MOTION_FULL && motion != NT_MOTION_REDUCED)) return NT_RESULT_INVALID_ARGUMENT;
    if (field->kind == NT_FIELD_WAVE) {
        const NtWaveField *wave = &field->as.wave;
        return finite_point(wave->origin) && finite_point(wave->cycles_per_unit) && isfinite(wave->phase_turns) ?
            NT_RESULT_OK : NT_RESULT_INVALID_ARGUMENT;
    }
    if (field->kind != NT_FIELD_RIPPLE) return NT_RESULT_INVALID_ARGUMENT;
    const NtRippleField *r = &field->as.ripple;
    if (!r->rings || !r->lifetime_us || !optical_valid(r->falloff) || r->fade < NT_LINEAR || r->fade > NT_STEP_END ||
        !finite_point(r->origin) || !finite_point(r->aspect) || r->aspect.x <= 0 || r->aspect.y <= 0 ||
        !(r->first_radius >= 0 && isfinite(r->first_radius) && r->last_radius >= 0 && isfinite(r->last_radius) &&
          r->width > 0 && isfinite(r->width) && r->peak >= 0 && r->peak <= 1)) return NT_RESULT_INVALID_ARGUMENT;
    return r->rings <= NT_FIELD_MAX_RINGS ? NT_RESULT_OK : NT_RESULT_OUT_OF_RANGE;
}

static bool ring_birth(const NtRippleField *r, uint32_t index, NtTimeUs *birth)
{
    if (index && r->spacing_us > (UINT64_MAX-r->start_us)/index) return false;
    *birth = r->start_us + index*r->spacing_us;
    return true;
}

static void add_scaled_product(NtScaledSum *out, const NtScaledSum *a, const NtScaledSum *b, double sign)
{
    for (size_t i = 0; i < a->count; ++i) {
        NtScaledTerm coefficient = a->partials[i];
        coefficient.fraction *= sign;
        nt_scaled_add_sum(out,b,coefficient);
    }
}

static void scaled_axis(NtScaledSum *out, double point, double origin, double ratio)
{
    const NtScaledTerm scale = nt_scaled_term(ratio,0);
    nt_scaled_add_product(out,nt_scaled_term(point,0),scale);
    nt_scaled_add_product(out,nt_scaled_term(-origin,0),scale);
}

static NtScaledTerm scaled_sqrt(NtScaledTerm value)
{
    if (value.exponent%2) {
        value.fraction *= 2;
        --value.exponent;
    }
    return nt_scaled_term(sqrt(value.fraction),value.exponent/2);
}

static int sum_sign(const NtScaledSum *sum)
{
    if (!sum->count) return 0;
    return sum->partials[sum->count-1].fraction > 0 ? 1 : -1;
}

static void negate_sum(NtScaledSum *sum)
{
    for (size_t i = 0; i < sum->count; ++i) sum->partials[i].fraction = -sum->partials[i].fraction;
}

static NtScaledTerm normalized_term(NtScaledTerm numerator, NtScaledTerm a, NtScaledTerm b)
{
    return nt_scaled_term(numerator.fraction/(a.fraction*b.fraction),numerator.exponent-a.exponent-b.exponent);
}

static double normalized_ratio(NtScaledTerm numerator, NtScaledTerm a, NtScaledTerm b)
{
    const NtScaledTerm value = normalized_term(numerator,a,b);
    return scalbn(value.fraction,value.exponent);
}

static double linear_band(const NtScaledSum *radius, const NtFieldSquare *scaled_square,
                          const NtScaledSum *scaled_width, NtScaledTerm distance, int side)
{
    NtScaledSum edge = *radius;
    const NtScaledTerm width = nt_scaled_value(scaled_width);
    nt_scaled_add_sum(&edge,scaled_width,nt_scaled_term(side,0));
    if (sum_sign(&edge) < 0) {
        /* The inner edge is below radius zero. Here both terms increase
         * the distance inside the band, so they cannot cancel. */
        negate_sum(&edge);
        nt_scaled_add(&edge,distance);
        return fmin(1,normalized_ratio(nt_scaled_value(&edge),width,(NtScaledTerm){.5,1}));
    }
    NtFieldSquare remaining = *scaled_square;
    nt_field_square_add(&remaining,&edge,-1);
    NtScaledTerm difference = nt_field_square_value(&remaining);
    if (difference.fraction*side >= 0) return 0;
    difference.fraction = fabs(difference.fraction);
    nt_scaled_add(&edge,distance);
    return fmin(1,normalized_ratio(difference,nt_scaled_value(&edge),width));
}

static double inverse_band(const NtFieldSquare *squared_distance, const NtScaledSum *radius,
                           const NtScaledSum *width, double peak, double fade, double estimate)
{
    /* In common lifetime units, weight = peak * fade * w^2 / (w^2 + d^2).
     * Apply the weights to the numerator before rounding the optical tail. */
    const NtScaledTerm unit = {.5,1};
    NtFieldSquare divisor = *squared_distance;
    if (radius) nt_field_square_add(&divisor,radius,1);
    NtFieldSquare squared_width = {0};
    nt_field_square_add(&squared_width,width,1);
    nt_field_square_scale(&divisor,&squared_width,unit);
    if (peak == 1 && fade == 1)
        return nt_field_square_weight(&squared_width,&divisor,squared_distance,radius,estimate);
    NtFieldSquare weighted = {0}, numerator = {0};
    nt_field_square_scale(&weighted,&squared_width,nt_scaled_term(peak,0));
    nt_field_square_scale(&numerator,&weighted,nt_scaled_term(fade,0));
    return nt_field_square_weight(&numerator,&divisor,squared_distance,radius,estimate);
}

static double axial_band(NtOpticalFalloff falloff, const NtScaledSum *radius, const NtScaledSum *distance,
                         const NtScaledSum *width, double peak, double fade)
{
    NtScaledSum difference = *distance;
    nt_scaled_add_sum(&difference,radius,(NtScaledTerm){-.5,1});
    if (sum_sign(&difference) < 0) negate_sum(&difference);
    const NtScaledTerm scale = nt_scaled_value(width);
    if (falloff == NT_OPTICAL_LINEAR) {
        NtScaledSum remaining = *width;
        nt_scaled_add_sum(&remaining,&difference,(NtScaledTerm){-.5,1});
        return sum_sign(&remaining) > 0 ? peak*fade*nt_scaled_quotient(&remaining,width,scale) : 0;
    }
    if (falloff == NT_OPTICAL_INVERSE_QUADRATIC) {
        NtFieldSquare squared = {0};
        nt_field_square_add(&squared,&difference,1);
        return inverse_band(&squared,NULL,width,peak,fade,0);
    }
    return peak*fade*optical_weight(falloff,nt_scaled_quotient(&difference,width,scale));
}

static double band_weight(const NtRippleField *r, const NtFieldSquare *scaled_square, const NtScaledSum *axial,
                          const NtScaledSum *scaled_width, NtScaledTerm distance, NtTimeUs age, double fade)
{
    /* Compare squared radii over the common integer lifetime denominator.
     * This keeps a narrow residual when large distances nearly coincide. */
    NtScaledSum radius = {0};
    nt_scaled_add_tick_product(&radius,r->first_radius,r->lifetime_us-age,1);
    nt_scaled_add_tick_product(&radius,r->last_radius,age,1);
    if (axial) return axial_band(r->falloff,&radius,axial,scaled_width,r->peak,fade);
    if (r->falloff == NT_OPTICAL_INVERSE_QUADRATIC && !radius.count)
        return inverse_band(scaled_square,NULL,scaled_width,r->peak,fade,0);
    NtFieldSquare residual = *scaled_square;
    nt_field_square_add(&residual,&radius,-1);
    NtScaledTerm difference = nt_field_square_value(&residual);
    if (!difference.fraction) return r->peak*fade;
    const int side = difference.fraction > 0 ? 1 : -1;
    if (r->falloff == NT_OPTICAL_LINEAR)
        return r->peak*fade*linear_band(&radius,scaled_square,scaled_width,distance,side);
    difference.fraction = fabs(difference.fraction);

    /* |distance-radius| = |distance^2-radius^2|/(distance+radius).
     * Its sign and small numerator are independent of the rounded norm. */
    NtScaledSum combined_radius = radius;
    nt_scaled_add(&combined_radius,distance);
    const NtScaledTerm normalized = normalized_term(difference,nt_scaled_value(&combined_radius),
                                                    nt_scaled_value(scaled_width));
    if (r->falloff == NT_OPTICAL_INVERSE_QUADRATIC) {
        const double estimate = inverse_quadratic(normalized,r->peak,fade);
        return inverse_band(scaled_square,&radius,scaled_width,r->peak,fade,estimate);
    }
    return r->peak*fade*optical_weight(r->falloff,scalbn(normalized.fraction,normalized.exponent));
}

static double ripple_fade(NtEasing easing, NtTimeUs remaining, NtTimeUs lifetime)
{
    const double progress = nt_animation_progress(remaining,lifetime);
    /* Equivalent formulas keep a positive tail at the final active tick. */
    if (easing == NT_SINE) {
        const double sine = sin((turn_radians*.25)*progress);
        return sine*sine;
    }
    if (easing == NT_EASE_OUT) return progress*(2-progress);
    double fade;
    (void)nt_ease(easing,progress,&fade);
    return fade;
}

static NtFieldSample ripple_sample(const NtRippleField *r, NtFieldQuery query)
{
    NtTimeUs birth;
    if (query.now < r->start_us || r->peak == 0) return (NtFieldSample){0};
    double strongest = 0;
    NtScaledSum x = {0}, y = {0};
    scaled_axis(&x,query.position.x,r->origin.x,r->aspect.x);
    scaled_axis(&y,query.position.y,r->origin.y,r->aspect.y);
    NtScaledSum clock = {0}, weighted_x = {0}, weighted_y = {0}, scaled_width = {0};
    nt_scaled_add_tick_product(&clock,1,r->lifetime_us,1);
    add_scaled_product(&weighted_x,&x,&clock,1);
    add_scaled_product(&weighted_y,&y,&clock,1);
    NtFieldSquare scaled_square = {0};
    NtScaledSum *axial = NULL;
    NtScaledTerm distance = {0};
    if (weighted_x.count && weighted_y.count) {
        nt_field_square_add(&scaled_square,&weighted_x,1);
        nt_field_square_add(&scaled_square,&weighted_y,1);
        distance = scaled_sqrt(nt_field_square_value(&scaled_square));
    } else {
        axial = weighted_x.count ? &weighted_x : &weighted_y;
        if (sum_sign(axial) < 0) negate_sum(axial);
    }
    nt_scaled_add_sum(&scaled_width,&clock,nt_scaled_term(r->width,0));
    for (uint32_t i = 0; i < r->rings; ++i) {
        if (!ring_birth(r,i,&birth) || birth > query.now) break;
        NtTimeUs age = query.now-birth;
        if (age >= r->lifetime_us) continue;
        if (query.motion == NT_MOTION_REDUCED) age = 0;
        const double fade = ripple_fade(r->fade,r->lifetime_us-age,r->lifetime_us);
        if (!fade) continue;
        const double weight = band_weight(r,&scaled_square,axial,&scaled_width,distance,age,fade);
        if (weight > strongest) strongest = weight;
    }
    return (NtFieldSample){strongest,strongest};
}

static void add_fraction(NtScaledSum *out, NtScaledTerm term)
{
    if (term.exponent >= DBL_MANT_DIG) return;
    if (term.exponent > 0) {
        double integer;
        term = nt_scaled_term(modf(scalbn(term.fraction,term.exponent),&integer),0);
    }
    nt_scaled_add(out,term);
}

static NtFieldSample wave_sample(const NtWaveField *wave, NtFieldQuery query)
{
    if (query.now < wave->start_us) return (NtFieldSample){0};
    const NtTimeUs period = wave->period_us ? wave->period_us : 1;
    const NtTimeUs ticks = query.motion == NT_MOTION_FULL ? (query.now-wave->start_us)%period : 0;

    NtScaledSum cycles = {0}, fractions = {0};
    scaled_axis(&cycles,query.position.x,wave->origin.x,wave->cycles_per_unit.x);
    scaled_axis(&cycles,query.position.y,wave->origin.y,wave->cycles_per_unit.y);
    nt_scaled_add(&cycles,nt_scaled_term(wave->phase_turns,0));
    for (size_t i = 0; i < cycles.count; ++i) add_fraction(&fractions,cycles.partials[i]);
    const NtScaledTerm local = nt_scaled_value(&fractions);
    const double approximation = scalbn(local.fraction,local.exponent) - (double)ticks/(double)period;
    const int quadrant = (int)floor(4*approximation+.5);
    nt_scaled_add(&fractions,nt_scaled_term(-.25*quadrant,0));

    NtScaledSum clock = {0}, numerator = {0}, radians = {0};
    nt_scaled_add_tick_product(&clock,1,period,1);
    add_scaled_product(&numerator,&fractions,&clock,1);
    nt_scaled_add_tick_product(&numerator,-1,ticks,1);
    nt_scaled_add_sum(&radians,&numerator,nt_scaled_term(turn_radians,0));
    const double angle = nt_scaled_quotient(&radians,&clock,nt_scaled_value(&clock));
    switch ((unsigned)quadrant%4) {
    case 1: return (NtFieldSample){cos(angle),1};
    case 2: return (NtFieldSample){-sin(angle),1};
    case 3: return (NtFieldSample){-cos(angle),1};
    default: return (NtFieldSample){sin(angle),1};
    }
}

static NtResult sample_field(const NtField *field, NtFieldQuery query, NtFieldSample *out)
{
    const NtResult valid = validate_field(field,query.motion);
    if (valid != NT_RESULT_OK) return valid;
    if (!finite_point(query.position)) return NT_RESULT_INVALID_ARGUMENT;
    *out = field->kind == NT_FIELD_RIPPLE ? ripple_sample(&field->as.ripple,query) : wave_sample(&field->as.wave,query);
    return NT_RESULT_OK;
}

NtResult nt_field_sample(const NtField *field, NtFieldQuery query, NtFieldSample *out)
{
    if (!out) return NT_RESULT_INVALID_ARGUMENT;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    NtFieldSample sample;
    const NtResult result = sample_field(field,query,&sample);
    if (!nt_finish_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    if (result == NT_RESULT_OK) *out = sample;
    return result;
}

static NtResult field_ink(const NtFieldInk *ink, double coordinate, double coverage, NtRgba *out)
{
    if (!(ink->opacity >= 0 && ink->opacity <= 1)) return NT_RESULT_INVALID_ARGUMENT;
    if (!coverage || !ink->opacity) {
        const NtResult valid = ink->palette ? nt_palette_validate(ink->palette) : NT_RESULT_OK;
        if (valid == NT_RESULT_OK) *out = (NtRgba){0};
        return valid;
    }
    NtColor color = nt_color_from_rgba(ink->color);
    NtGamutPolicy gamut = NT_GAMUT_CLIP;
    if (ink->palette) {
        const NtResult result = nt_palette_sample(ink->palette,coordinate,&color);
        if (result != NT_RESULT_OK) return result;
        gamut = ink->palette->interpolation.gamut;
    }
    color.alpha *= ink->opacity*coverage;
    if (!color.alpha) { *out = (NtRgba){0}; return NT_RESULT_OK; }
    return nt_color_to_rgba(color,gamut,out);
}

NtResult nt_field_color(const NtField *field, const NtFieldInk *ink, NtFieldQuery query, NtRgba *out)
{
    if (!ink || !out) return NT_RESULT_INVALID_ARGUMENT;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    NtFieldSample sample;
    NtRgba rgba;
    NtResult result = sample_field(field,query,&sample);
    if (result == NT_RESULT_OK) {
        const double coordinate = field->kind == NT_FIELD_WAVE ? .5+.5*sample.value : sample.value;
        result = field_ink(ink,coordinate,sample.coverage,&rgba);
    }
    if (!nt_finish_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    if (result == NT_RESULT_OK) *out = rgba;
    return result;
}

static int64_t min_i64(int64_t a, int64_t b) { return a < b ? a : b; }
static int64_t max_i64(int64_t a, int64_t b) { return a > b ? a : b; }

NtResult nt_field_draw(const NtField *field, const NtFieldInk *ink, NtPaintContext *paint,
                       NtCellRect bounds, NtTimeUs now, NtMotionPolicy motion)
{
    if (!ink || !paint) return NT_RESULT_INVALID_ARGUMENT;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    NtResult result = validate_field(field,motion);
    if (result == NT_RESULT_OK) {
        NtRgba validated;
        result = field_ink(ink,0,0,&validated);
    }
    if (!nt_finish_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    if (result != NT_RESULT_OK) return result;
    const NtCellRect clip = nt_paint_clip(paint);
    const int64_t left = max_i64(bounds.origin.x,clip.origin.x);
    const int64_t top = max_i64(bounds.origin.y,clip.origin.y);
    const int64_t right = min_i64((int64_t)INT32_MAX+1,min_i64((int64_t)bounds.origin.x+bounds.size.columns,
                                                            (int64_t)clip.origin.x+clip.size.columns));
    const int64_t bottom = min_i64((int64_t)INT32_MAX+1,min_i64((int64_t)bounds.origin.y+bounds.size.rows,
                                                             (int64_t)clip.origin.y+clip.size.rows));
    for (int64_t y = top; y < bottom; ++y) {
        for (int64_t x = left; x < right; ++x) {
            NtRgba color;
            result = nt_field_color(field,ink,(NtFieldQuery){{(double)x+.5,(double)y+.5},now,motion},&color);
            if (result != NT_RESULT_OK) return result;
            if (!color.alpha) continue;
            result = nt_paint_fill(paint,(NtCellRect){{(int32_t)x,(int32_t)y},{1,1}},color);
            if (result != NT_RESULT_OK) return result;
        }
    }
    return NT_RESULT_OK;
}

static void earliest(NtDeadline *deadline, NtTimeUs time)
{
    if (!deadline->scheduled || time < deadline->at) *deadline = (NtDeadline){true,time};
}

NtResult nt_field_deadline(const NtField *field, NtTimeUs now, NtMotionPolicy motion,
                           NtTimeUs interval, NtDeadline *out)
{
    if (!out) return NT_RESULT_INVALID_ARGUMENT;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    const NtResult valid = validate_field(field,motion);
    if (!nt_finish_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    if (valid != NT_RESULT_OK) return valid;
    NtDeadline next = {0};
    bool active = false;
    if (field->kind == NT_FIELD_WAVE) {
        if (now < field->as.wave.start_us) earliest(&next,field->as.wave.start_us);
        else active = field->as.wave.period_us != 0;
    } else {
        const NtRippleField *r = &field->as.ripple;
        for (uint32_t i = 0; i < r->rings; ++i) {
            NtTimeUs birth;
            if (!ring_birth(r,i,&birth)) break;
            if (now < birth) {
                earliest(&next,birth);
                break;
            }
            if (now-birth >= r->lifetime_us) continue;
            active = true;
            if (r->lifetime_us <= UINT64_MAX-birth) earliest(&next,birth+r->lifetime_us);
        }
    }
    if (!interval) interval = 16000;
    if (active && motion == NT_MOTION_FULL && interval <= UINT64_MAX-now) earliest(&next,now+interval);
    *out = next;
    return NT_RESULT_OK;
}
