#include "particle_internal.h"

#include <nocturne/color.h>

#include "animation_internal.h"
#include "fp_environment_internal.h"
#include "scaled_ticks_internal.h"
#include "seed_internal.h"

#include <math.h>

NT_DECLARE_ALIGNMENT(NtParticleEmitter);

NtParticleEmitterOptions nt_particle_emitter_options_default(void)
{
    return (NtParticleEmitterOptions){
        .period_us=100000,.births_per_tick=1,.capacity=64,
        .lifetime_min_us=800000,.lifetime_max_us=800000,
        .birth_color={171,221,255,255},.end_color={171,221,255,0},.color_easing=NT_LINEAR
    };
}

static bool finite_point(NtPoint point)
{
    return isfinite(point.x) && isfinite(point.y);
}

static bool valid_range(NtParticleRange range)
{
    return finite_point(range.min) && finite_point(range.max) &&
           range.min.x <= range.max.x && range.min.y <= range.max.y;
}

NtResult nt_particle_options_validate(const NtParticleEmitterOptions *options)
{
    if (!options || !options->births_per_tick || !options->capacity || !options->lifetime_min_us ||
        options->lifetime_max_us < options->lifetime_min_us ||
        options->color_easing < NT_LINEAR || options->color_easing > NT_STEP_END)
        return NT_RESULT_INVALID_ARGUMENT;
    if (options->capacity > NT_PARTICLE_MAX_CAPACITY || options->births_per_tick > NT_PARTICLE_MAX_BATCH)
        return NT_RESULT_OUT_OF_RANGE;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    const bool valid = valid_range(options->origin) && valid_range(options->velocity) &&
                       finite_point(options->acceleration);
    if (!nt_finish_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    return valid ? NT_RESULT_OK : NT_RESULT_INVALID_ARGUMENT;
}

static NtResult create_emitter(const NtAllocator *provided, const NtParticleEmitterOptions *options,
                               const NtParticleEffectMotion *motion, NtParticleEmitter **out)
{
    NtAllocator allocator;
    if (!out || !nt_allocator_get(provided,&allocator)) return NT_RESULT_INVALID_ARGUMENT;
    NtResult valid = nt_particle_options_validate(options);
    if (valid != NT_RESULT_OK) return valid;
    if (motion) valid = nt_particle_effect_motion_validate(motion);
    if (valid != NT_RESULT_OK) return valid;
    NtParticleEmitter *emitter = nt_allocate(&allocator,sizeof(*emitter),NT_ALIGNMENT(NtParticleEmitter));
    if (!emitter) return NT_RESULT_ALLOCATION_FAILED;
    *emitter = (NtParticleEmitter){.allocator=allocator,.options=*options};
    if (motion) emitter->motion = *motion;
    *out = emitter;
    return NT_RESULT_OK;
}

NtResult nt_particle_emitter_create(const NtAllocator *provided, const NtParticleEmitterOptions *options,
                                    NtParticleEmitter **out)
{
    return create_emitter(provided,options,NULL,out);
}

NtResult nt_particle_effect_emitter_create(const NtAllocator *allocator, const NtParticleEffectOptions *options,
                                           NtParticleEmitter **out)
{
    if (!options) return NT_RESULT_INVALID_ARGUMENT;
    return create_emitter(allocator,&options->view.emitter,&options->motion,out);
}

void nt_particle_emitter_destroy(NtParticleEmitter *emitter)
{
    if (!emitter) return;
    const NtAllocator allocator = emitter->allocator;
    nt_release(&allocator,emitter,sizeof(*emitter),NT_ALIGNMENT(NtParticleEmitter));
}

static uint64_t last_id(const NtParticleEmitterOptions *options)
{
    const uint64_t maximum = options->emission_limit ? options->emission_limit-1 : UINT64_MAX;
    const uint64_t batch_last = options->births_per_tick-1;
    return !options->period_us && batch_last < maximum ? batch_last : maximum;
}

static bool born_at(const NtParticleEmitterOptions *options, uint64_t id, NtTimeUs *out)
{
    if (id > last_id(options)) return false;
    const uint64_t batch = id/options->births_per_tick;
    if (options->period_us && batch > (UINT64_MAX-options->start_us)/options->period_us)
        return false;
    *out = options->start_us+batch*options->period_us;
    return true;
}

NtParticleRangeId nt_particle_recent(const NtParticleEmitter *emitter, NtTimeUs now)
{
    const NtParticleEmitterOptions *options = &emitter->options;
    if (now < options->start_us) return (NtParticleRangeId){0};
    const uint64_t latest_batch = options->period_us ? (now-options->start_us)/options->period_us : 0;
    const uint64_t maximum = last_id(options);
    const uint64_t batch_size = options->births_per_tick;
    uint64_t last = maximum;
    if (latest_batch < maximum/batch_size)
        last = latest_batch*batch_size+(batch_size-1);
    const size_t count = last < options->capacity-1 ? (size_t)last+1 : options->capacity;
    return (NtParticleRangeId){last-(count-1),count};
}

/* Each property gets its own draw in a stream. Using a second seed mix
 * keeps the caller's stream separate from the per-property substream. */
static uint64_t particle_bits(const NtParticleEmitterOptions *options, uint64_t id, uint64_t channel)
{
    const uint64_t stream_seed = nt_seed_bits(options->seed,options->stream,0);
    return nt_seed_bits(stream_seed,channel,id);
}

static double particle_unit(const NtParticleEmitterOptions *options, uint64_t id, uint64_t channel)
{
    return (double)(particle_bits(options,id,channel) >> 11) / 9007199254740992.;
}

bool nt_particle_alive(const NtParticleEmitter *emitter, uint64_t id, NtTimeUs now, NtParticleAge *out)
{
    const NtParticleEmitterOptions *options = &emitter->options;
    NtTimeUs birth;
    if (!born_at(options,id,&birth) || now < birth) return false;
    const NtTimeUs age = now-birth;
    const NtTimeUs variation = options->lifetime_max_us-options->lifetime_min_us;
    const NtTimeUs lifetime = options->lifetime_min_us +
        (variation ? particle_bits(options,id,0)%(variation+1) : 0);
    if (age >= lifetime) return false;
    *out = (NtParticleAge){birth,lifetime,age};
    return true;
}

static NtPoint random_point(const NtParticleEmitterOptions *options, NtParticleRange range,
                             uint64_t id, uint64_t channel)
{
    const double x = particle_unit(options,id,channel);
    const double y = particle_unit(options,id,channel+1);
    return (NtPoint){nt_animation_mix(range.min.x,range.max.x,x),nt_animation_mix(range.min.y,range.max.y,y)};
}

static const double turn_radians = 0x1.921fb54442d18p+2;

static int turn_quadrant(double turn)
{
    return (int)floor(turn*4+.5);
}

static NtPoint quadrant_vector(int quadrant, double radians)
{
    const double x = cos(radians), y = sin(radians);
    switch ((unsigned)quadrant%4) {
    case 1: return (NtPoint){-y,x};
    case 2: return (NtPoint){-x,-y};
    case 3: return (NtPoint){y,-x};
    default: return (NtPoint){x,y};
    }
}

static NtPoint initial_velocity(const NtParticleEmitter *emitter, uint64_t id)
{
    const NtParticleEmitterOptions *options = &emitter->options;
    const NtParticleEffectMotion *motion = &emitter->motion;
    if (!motion->radial) return random_point(options,options->velocity,id,3);
    const double draw = particle_unit(options,id,5);
    const int quadrant = turn_quadrant(motion->direction_turns + motion->sweep_turns*draw);
    NtScaledSum arc = {0};
    nt_scaled_add(&arc,nt_scaled_term(motion->direction_turns,0));
    nt_scaled_add(&arc,nt_scaled_term(-.25*quadrant,0));
    nt_scaled_add_product(&arc,nt_scaled_term(motion->sweep_turns,0),nt_scaled_term(draw,0));
    /* Convert the exact turn residual before rounding. A fractional turn
     * below the smallest double can still give a representable angle. */
    NtScaledSum radians = {0};
    nt_scaled_add_sum(&radians,&arc,nt_scaled_term(turn_radians,0));
    const NtScaledTerm unit = {.5,1};
    const NtScaledSum divisor = {.partials={unit},.count=1};
    const NtPoint direction = quadrant_vector(quadrant,nt_scaled_quotient(&radians,&divisor,unit));
    const double speed = nt_animation_mix(motion->speed_min,motion->speed_max,particle_unit(options,id,6));
    return (NtPoint){direction.x*speed,direction.y*speed};
}

static NtPoint shifted_wave(double phase, NtTimeUs ticks, double step, double approximate_offset,
                           NtTimeUs period, const NtScaledSum *divisor, NtScaledTerm divisor_value)
{
    const int quadrant = turn_quadrant(phase+approximate_offset);
    /* The seed phase is an exact multiple of 2^-53. Subtract the nearby
     * quadrant first, then combine the dyadic phase with integer clock
     * ticks before division. This keeps tiny shifts at a turning point. */
    NtScaledSum numerator = {0};
    nt_scaled_add_tick_product(&numerator,phase-.25*quadrant,period,1);
    nt_scaled_add_tick_product(&numerator,step,ticks,1);
    NtScaledSum radians = {0};
    nt_scaled_add_sum(&radians,&numerator,nt_scaled_term(turn_radians,0));
    return quadrant_vector(quadrant,nt_scaled_quotient(&radians,divisor,divisor_value));
}

typedef struct ParticleSway {
    double cosine, midpoint_cosine, half_step_sine;
    NtTimeUs period;
} ParticleSway;

static ParticleSway sway_at(const NtParticleEmitter *emitter, uint64_t id, NtTimeUs age)
{
    const NtTimeUs period = emitter->motion.sway_period_us;
    const double phase = particle_unit(&emitter->options,id,7);
    const NtTimeUs remainder = age%period;
    const bool backward = remainder > period/2;
    const NtTimeUs ticks = backward ? period-remainder : remainder;
    const double step = backward ? -1. : 1.;
    const double approximate = step*(double)ticks/(double)period;
    NtScaledSum divisor = {0};
    nt_scaled_add_tick_product(&divisor,1,period,1);
    const NtScaledTerm divisor_value = nt_scaled_value(&divisor);
    const NtPoint current = shifted_wave(phase,ticks,step,approximate,period,&divisor,divisor_value);
    ParticleSway wave = {.cosine=current.x,.period=period};
    if (!ticks) return wave;

    /* sin(a+d)-sin(a) = 2*cos(a+d/2)*sin(d/2). The signed clock
     * remainder and this identity preserve both ends of a long cycle. */
    const NtPoint midpoint = shifted_wave(phase,ticks,step*.5,approximate*.5,period,&divisor,divisor_value);
    NtScaledSum half_ticks = {0};
    nt_scaled_add_tick_product(&half_ticks,step*.5*turn_radians,ticks,1);
    const double radians = nt_scaled_quotient(&half_ticks,&divisor,divisor_value);
    wave.midpoint_cosine = midpoint.x;
    wave.half_step_sine = sin(radians);
    return wave;
}

static bool same_color(NtRgba a, NtRgba b)
{
    return a.red == b.red && a.green == b.green && a.blue == b.blue && a.alpha == b.alpha;
}

static NtResult color_at(const NtParticleEmitterOptions *options, double progress, NtRgba *out)
{
    if (progress == 0 || same_color(options->birth_color,options->end_color)) {
        *out = options->birth_color;
        return NT_RESULT_OK;
    }
    double weight;
    (void)nt_ease(options->color_easing,progress,&weight);
    if (weight == 0 || weight == 1) {
        *out = weight == 0 ? options->birth_color : options->end_color;
        return NT_RESULT_OK;
    }
    NtColor mixed;
    const NtColorInterpolation policy = {NT_COLOR_LINEAR_SRGB,NT_GAMUT_CLIP,
                                         NT_HUE_SHORTER,NT_ALPHA_PREMULTIPLIED};
    const NtResult result = nt_color_mix(nt_color_from_rgba(options->birth_color),
                                         nt_color_from_rgba(options->end_color),weight,policy,&mixed);
    return result == NT_RESULT_OK ? nt_color_to_rgba(mixed,NT_GAMUT_CLIP,out) : result;
}

static double divide_motion(const NtScaledSum *sum, double units, bool preserve_cell)
{
    const NtScaledTerm scale = nt_scaled_term(units,0);
    const NtScaledSum divisor = {.partials={scale},.count=1};
    const double nearest = nt_scaled_quotient(sum,&divisor,scale);
    /* Nearest rounding can move a point across a cell boundary only
     * by landing on that boundary. Keep the exact side for painting. */
    if (preserve_cell && nearest >= INT32_MIN && nearest <= (double)INT32_MAX+1 &&
        floor(nearest) == nearest && nt_scaled_compare_boundary(sum,&divisor,nearest) < 0)
        return nextafter(nearest,-INFINITY);
    return nearest;
}

static void add_wave_product(NtScaledSum *sum, double amplitude, double component, double factor, NtScaledTerm scale)
{
    if (!amplitude || !component || !factor) return;
    NtScaledSum contribution = {0};
    nt_scaled_add_product(&contribution,nt_scaled_term(amplitude,0),nt_scaled_term(component,0));
    NtScaledSum weighted = {0};
    nt_scaled_add_sum(&weighted,&contribution,nt_scaled_term(factor,0));
    nt_scaled_add_sum(sum,&weighted,scale);
}

static double sway_speed(double velocity, double acceleration, NtTimeUs age, double amplitude, ParticleSway wave)
{
    /* Use a common denominator of one million times the sway period.
     * Keep the oscillator derivative and ballistic speed together through
     * the range check, even when an individual contribution is huge. */
    NtScaledSum numerator = {0}, divisor = {0};
    nt_scaled_add_tick_product(&numerator,velocity,1000000,wave.period);
    nt_scaled_add_tick_product(&numerator,acceleration,age,wave.period);
    add_wave_product(&numerator,amplitude,wave.cosine,turn_radians,nt_scaled_term(1000000000000.,0));
    nt_scaled_add_tick_product(&divisor,1000000.,wave.period,1);
    return nt_scaled_quotient(&numerator,&divisor,nt_scaled_value(&divisor));
}

static void motion_at(double origin, double velocity, double acceleration, NtTimeUs age,
                      double amplitude, ParticleSway wave,
                      bool preserve_cell, double *position, double *speed)
{
    *position = origin;
    *speed = velocity;
    /* For u integer microseconds, use common denominators 2e12 and 1e6
     * for position and velocity. The integer products stay exact even
     * when u or u squared exceeds the precision of a double. */
    if ((age && (velocity || acceleration)) || (amplitude && wave.midpoint_cosine && wave.half_step_sine)) {
        NtScaledSum numerator = {0};
        const NtScaledTerm scale = nt_scaled_term(2000000000000.,0);
        nt_scaled_add_product(&numerator,nt_scaled_term(origin,0),scale);
        nt_scaled_add_tick_product(&numerator,velocity,age,2000000);
        nt_scaled_add_tick_product(&numerator,acceleration,age,age);
        add_wave_product(&numerator,amplitude,wave.midpoint_cosine,2*wave.half_step_sine,scale);
        *position = divide_motion(&numerator,2000000000000.,preserve_cell);
    }
    if (amplitude && wave.cosine) *speed = sway_speed(velocity,acceleration,age,amplitude,wave);
    else if (age && acceleration) {
        NtScaledSum derivative = {0};
        const NtScaledTerm scale = nt_scaled_term(1000000.,0);
        nt_scaled_add_product(&derivative,nt_scaled_term(velocity,0),scale);
        nt_scaled_add_tick_product(&derivative,acceleration,age,1);
        *speed = divide_motion(&derivative,1000000.,false);
    }
}

NtResult nt_particle_sample_age(const NtParticleEmitter *emitter, uint64_t id, NtParticleAge age,
                                 bool preserve_cell, NtParticleSample *out)
{
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    const NtParticleEmitterOptions *options = &emitter->options;
    const NtPoint origin = random_point(options,options->origin,id,1);
    const NtPoint velocity = initial_velocity(emitter,id);
    const NtPoint acceleration = options->acceleration;
    const NtParticleEffectMotion *motion = &emitter->motion;
    ParticleSway wave = {0};
    if (motion->sway_period_us && (motion->sway_amplitude.x || motion->sway_amplitude.y))
        wave = sway_at(emitter,id,age.age);
    NtParticleSample sample = {.id=id,.born_us=age.born,.age_us=age.age,.lifetime_us=age.lifetime};
    motion_at(origin.x,velocity.x,acceleration.x,age.age,motion->sway_amplitude.x,wave,
              preserve_cell,&sample.position.x,&sample.velocity.x);
    motion_at(origin.y,velocity.y,acceleration.y,age.age,motion->sway_amplitude.y,wave,
              preserve_cell,&sample.position.y,&sample.velocity.y);
    NtResult result = NT_RESULT_OUT_OF_RANGE;
    if (finite_point(sample.position) && finite_point(sample.velocity)) {
        sample.progress = nt_animation_progress(age.age,age.lifetime);
        result = color_at(options,sample.progress,&sample.color);
        if (result == NT_RESULT_OK && motion->glitter_period_us) {
            const unsigned low = motion->glitter_min_alpha;
            const uint64_t key = particle_bits(options,id,8);
            const unsigned alpha = low + nt_seed_bits(key,0,age.age/motion->glitter_period_us)%(256-low);
            sample.color.alpha = (uint8_t)((sample.color.alpha*alpha+127)/255);
        }
    }
    if (!nt_finish_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    if (result == NT_RESULT_OK) *out = sample;
    return result;
}

NtResult nt_particle_emitter_particle(const NtParticleEmitter *emitter, uint64_t id, NtTimeUs now,
                                      bool *alive, NtParticleSample *out)
{
    if (!emitter || !alive || !out) return NT_RESULT_INVALID_ARGUMENT;
    NtParticleAge age;
    if (!nt_particle_alive(emitter,id,now,&age)) {
        *alive = false;
        return NT_RESULT_OK;
    }
    const NtResult result = nt_particle_sample_age(emitter,id,age,false,out);
    if (result == NT_RESULT_OK) *alive = true;
    return result;
}

static NtResult recent_samples(const NtParticleEmitter *emitter, NtTimeUs now, NtParticleSample *samples,
                               size_t *count)
{
    const NtParticleRangeId range = nt_particle_recent(emitter,now);
    *count = 0;
    for (size_t i = 0; i < range.count; ++i) {
        const uint64_t id = range.first+i;
        NtParticleAge age;
        if (!nt_particle_alive(emitter,id,now,&age)) continue;
        NtParticleSample sample;
        const NtResult result = nt_particle_sample_age(emitter,id,age,false,&sample);
        if (result != NT_RESULT_OK) return result;
        if (samples) samples[*count] = sample;
        ++*count;
    }
    return NT_RESULT_OK;
}

NtResult nt_particle_emitter_sample(const NtParticleEmitter *emitter, NtTimeUs now,
                                    NtParticleSample *samples, size_t capacity, size_t *count)
{
    if (!emitter || !count || (!samples && capacity)) return NT_RESULT_INVALID_ARGUMENT;
    size_t live;
    NtResult result = recent_samples(emitter,now,NULL,&live);
    if (result != NT_RESULT_OK) return result;
    *count = live;
    if (!samples) return NT_RESULT_OK;
    if (capacity < live) return NT_RESULT_INSUFFICIENT_BUFFER;
    return recent_samples(emitter,now,samples,&live);
}

NtResult nt_particle_trail_validate(NtParticleTrailOptions trail)
{
    if (trail.samples > NT_PARTICLE_MAX_TRAIL_SAMPLES) return NT_RESULT_OUT_OF_RANGE;
    if (trail.samples && !trail.spacing_us) return NT_RESULT_INVALID_ARGUMENT;
    return NT_RESULT_OK;
}

uint32_t nt_particle_trail_count(NtParticleAge age, NtParticleTrailOptions trail)
{
    if (!trail.samples) return 0;
    const uint64_t possible = age.age/trail.spacing_us;
    return possible < trail.samples ? (uint32_t)possible : trail.samples;
}

NtResult nt_particle_emitter_trail(const NtParticleEmitter *emitter, uint64_t id, NtTimeUs now,
                                   NtParticleTrailOptions trail, NtParticleSample *samples,
                                   size_t capacity, size_t *count)
{
    if (!emitter || !count || (!samples && capacity)) return NT_RESULT_INVALID_ARGUMENT;
    const NtResult valid = nt_particle_trail_validate(trail);
    if (valid != NT_RESULT_OK) return valid;
    NtParticleAge head;
    if (!nt_particle_alive(emitter,id,now,&head)) {
        *count = 0;
        return NT_RESULT_OK;
    }
    const uint32_t length = nt_particle_trail_count(head,trail);
    NtParticleSample staged[NT_PARTICLE_MAX_TRAIL_SAMPLES];
    for (uint32_t i = 0; i < length; ++i) {
        NtParticleAge age = head;
        age.age -= (length-i)*trail.spacing_us;
        const NtResult result = nt_particle_sample_age(emitter,id,age,false,&staged[i]);
        if (result != NT_RESULT_OK) return result;
    }
    *count = length;
    if (!samples) return NT_RESULT_OK;
    if (capacity < length) return NT_RESULT_INSUFFICIENT_BUFFER;
    for (uint32_t i = 0; i < length; ++i) samples[i] = staged[i];
    return NT_RESULT_OK;
}

static void earliest(NtDeadline *deadline, NtTimeUs time)
{
    if (!deadline->scheduled || time < deadline->at) *deadline = (NtDeadline){true,time};
}

NtResult nt_particle_emitter_deadline(const NtParticleEmitter *emitter, NtTimeUs now,
                                      NtMotionPolicy motion, NtTimeUs interval, NtDeadline *out)
{
    if (!emitter || !out || (motion != NT_MOTION_FULL && motion != NT_MOTION_REDUCED))
        return NT_RESULT_INVALID_ARGUMENT;
    const NtParticleRangeId range = nt_particle_recent(emitter,now);
    NtDeadline deadline = {0};
    if (!range.count) earliest(&deadline,emitter->options.start_us);
    else {
        const uint64_t last = range.first+(range.count-1);
        NtTimeUs birth;
        if (last != UINT64_MAX && born_at(&emitter->options,last+1,&birth) && birth > now)
            earliest(&deadline,birth);
    }
    bool has_live = false;
    for (size_t i = 0; i < range.count; ++i) {
        NtParticleAge age;
        if (!nt_particle_alive(emitter,range.first+i,now,&age)) continue;
        has_live = true;
        if (age.lifetime <= UINT64_MAX-age.born) earliest(&deadline,age.born+age.lifetime);
        const NtTimeUs glitter = emitter->motion.glitter_period_us;
        if (motion == NT_MOTION_FULL && glitter) {
            const NtTimeUs wait = glitter-age.age%glitter;
            if (wait <= UINT64_MAX-now) earliest(&deadline,now+wait);
        }
    }
    if (has_live && motion == NT_MOTION_FULL) {
        if (!interval) interval = 16000;
        if (interval <= UINT64_MAX-now) earliest(&deadline,now+interval);
    }
    *out = deadline;
    return NT_RESULT_OK;
}
