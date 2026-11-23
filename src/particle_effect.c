#include "particle_internal.h"

#include "fp_environment_internal.h"

#include <math.h>

#if defined(__clang__) && !defined(__wasm__)
#pragma STDC FENV_ACCESS ON
#elif defined(_MSC_VER)
#pragma fenv_access(on)
#endif

NtResult nt_particle_effect_motion_validate(const NtParticleEffectMotion *motion)
{
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    const bool radial = !motion->radial ||
        (motion->direction_turns >= 0 && motion->direction_turns < 1 &&
         motion->sweep_turns >= 0 && motion->sweep_turns <= 1 &&
         motion->speed_min >= 0 && motion->speed_min <= motion->speed_max && isfinite(motion->speed_max));
    const bool sway = isfinite(motion->sway_amplitude.x) && isfinite(motion->sway_amplitude.y);
    if (!nt_finish_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    return radial && sway ? NT_RESULT_OK : NT_RESULT_INVALID_ARGUMENT;
}

static void configure_burst(NtParticleEffectOptions *options)
{
    NtParticleEmitterOptions *emitter = &options->view.emitter;
    emitter->origin = (NtParticleRange){{16,4},{16,4}};
    emitter->period_us = 0;
    emitter->births_per_tick = 24;
    emitter->lifetime_min_us = 350000;
    emitter->lifetime_max_us = 650000;
    emitter->birth_color = (NtRgba){255,231,172,255};
    emitter->end_color = (NtRgba){255,114,46,0};
    emitter->color_easing = NT_EASE_OUT;
    options->motion.radial = true;
    options->motion.sweep_turns = 1;
    options->motion.speed_min = 6;
    options->motion.speed_max = 14;
}

static void configure_fall(NtParticleEffectOptions *options)
{
    NtParticleEmitterOptions *emitter = &options->view.emitter;
    emitter->origin = (NtParticleRange){{0,0},{32,0}};
    emitter->births_per_tick = 3;
    emitter->period_us = 90000;
    emitter->velocity = (NtParticleRange){{1,13},{3,19}};
    emitter->lifetime_min_us = 400000;
    emitter->lifetime_max_us = 850000;
    emitter->birth_color = (NtRgba){115,181,219,220};
    emitter->end_color = (NtRgba){93,126,168,0};
    options->view.paint.glyph = (NtByteView){"/",1};
}

NtResult nt_particle_effect_recipe(NtParticleRecipe recipe, NtParticleEffectOptions *out)
{
    if (!out) return NT_RESULT_INVALID_ARGUMENT;
    NtParticleEffectOptions options = {.view=nt_particle_view_options_default()};
    NtParticleEmitterOptions *emitter = &options.view.emitter;
    NtParticlePaintOptions *paint = &options.view.paint;
    NtParticleEffectMotion *motion = &options.motion;
    switch (recipe) {
    case NT_PARTICLE_RECIPE_BURST:
        configure_burst(&options);
        break;
    case NT_PARTICLE_RECIPE_NOVA:
        configure_burst(&options);
        emitter->births_per_tick = 40;
        emitter->lifetime_max_us = 1000000;
        motion->speed_max = 24;
        paint->trail = (NtParticleTrailOptions){35000,5};
        break;
    case NT_PARTICLE_RECIPE_GLITTER:
        emitter->origin = (NtParticleRange){{1,1},{31,8}};
        emitter->birth_color = (NtRgba){220,199,255,255};
        emitter->end_color = (NtRgba){131,157,240,0};
        motion->glitter_period_us = 70000;
        motion->glitter_min_alpha = 56;
        paint->glyph = (NtByteView){"+",1};
        break;
    case NT_PARTICLE_RECIPE_RAIN:
        configure_fall(&options);
        paint->trail = (NtParticleTrailOptions){22000,3};
        break;
    case NT_PARTICLE_RECIPE_SNOW:
        configure_fall(&options);
        emitter->velocity = (NtParticleRange){{-.3,1.4},{.6,3.2}};
        emitter->lifetime_min_us = 2000000;
        emitter->lifetime_max_us = 2800000;
        emitter->capacity = 128;
        emitter->birth_color = (NtRgba){232,244,255,235};
        emitter->end_color = (NtRgba){205,221,255,0};
        motion->sway_amplitude = (NtPoint){1.3,0};
        motion->sway_period_us = 1600000;
        paint->glyph = (NtByteView){"*",1};
        break;
    case NT_PARTICLE_RECIPE_EMBERS:
        emitter->origin = (NtParticleRange){{12,8},{20,8}};
        emitter->velocity = (NtParticleRange){{-2,-7},{2,-3}};
        emitter->acceleration = (NtPoint){0,1.5};
        emitter->birth_color = (NtRgba){255,207,100,255};
        emitter->end_color = (NtRgba){180,55,34,0};
        motion->sway_amplitude = (NtPoint){.7,0};
        motion->sway_period_us = 900000;
        motion->glitter_period_us = 65000;
        motion->glitter_min_alpha = 145;
        break;
    case NT_PARTICLE_RECIPE_LEAVES:
        configure_fall(&options);
        emitter->births_per_tick = 1;
        emitter->period_us = 190000;
        emitter->lifetime_min_us = 1800000;
        emitter->lifetime_max_us = 3400000;
        emitter->velocity = (NtParticleRange){{1,1.2},{3,2}};
        emitter->acceleration = (NtPoint){.1,.25};
        emitter->birth_color = (NtRgba){244,170,78,250};
        emitter->end_color = (NtRgba){170,78,53,0};
        motion->sway_amplitude = (NtPoint){1.6,.3};
        motion->sway_period_us = 1250000;
        paint->glyph = (NtByteView){"~",1};
        break;
    case NT_PARTICLE_RECIPE_SPARKS:
        configure_burst(&options);
        motion->direction_turns = .625;
        motion->sweep_turns = .25;
        emitter->acceleration = (NtPoint){0,13};
        emitter->lifetime_min_us = 140000;
        emitter->lifetime_max_us = 400000;
        paint->trail = (NtParticleTrailOptions){26000,3};
        break;
    case NT_PARTICLE_RECIPE_FIREWORK:
        configure_burst(&options);
        emitter->births_per_tick = 56;
        emitter->lifetime_min_us = 650000;
        emitter->lifetime_max_us = 1350000;
        emitter->acceleration = (NtPoint){0,7};
        emitter->birth_color = (NtRgba){156,224,255,255};
        emitter->end_color = (NtRgba){134,125,230,0};
        paint->trail = (NtParticleTrailOptions){42000,7};
        break;
    default:
        return NT_RESULT_INVALID_ARGUMENT;
    }
    *out = options;
    return NT_RESULT_OK;
}
