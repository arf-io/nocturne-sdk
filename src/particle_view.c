#include "particle_internal.h"

#include "controls_internal.h"
#include "fp_environment_internal.h"

#include <math.h>
#include <string.h>

#if defined(__clang__) && !defined(__wasm__)
#pragma STDC FENV_ACCESS ON
#elif defined(_MSC_VER)
#pragma fenv_access(on)
#endif

typedef struct ParticleView {
    NtParticleEmitter emitter;
    NtParticlePaintOptions paint;
    char glyph[NT_PARTICLE_MAX_GLYPH_BYTES];
    NtCellSize preferred;
    NtTimeUs frame_interval, now;
    NtMotionPolicy motion;
} ParticleView;

NT_DECLARE_ALIGNMENT(ParticleView);

NtParticlePaintOptions nt_particle_paint_options_default(void)
{
    return (NtParticlePaintOptions){.glyph={"·",2},.width={NT_WIDTH_ONE,NT_WIDTH_TWO},.trail_opacity=.35};
}

NtParticleViewOptions nt_particle_view_options_default(void)
{
    return (NtParticleViewOptions){.emitter=nt_particle_emitter_options_default(),
                                   .paint=nt_particle_paint_options_default(),.preferred={32,9}};
}

NtResult nt_particle_paint_validate(const NtParticlePaintOptions *options)
{
    if (!options || !options->glyph.count || !options->glyph.data) return NT_RESULT_INVALID_ARGUMENT;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    const bool valid_opacity = options->trail_opacity >= 0 && options->trail_opacity <= 1;
    if (!nt_finish_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    if (!valid_opacity) return NT_RESULT_INVALID_ARGUMENT;
    if (options->glyph.count > NT_PARTICLE_MAX_GLYPH_BYTES) return NT_RESULT_OUT_OF_RANGE;
    const NtResult trail = nt_particle_trail_validate(options->trail);
    if (trail != NT_RESULT_OK) return trail;
    return nt_particle_glyph_validate(options->glyph,options->width);
}

NtResult nt_particle_glyph_validate(NtByteView glyph, NtWidthPolicy width)
{
    if (!glyph.count || !glyph.data) return NT_RESULT_INVALID_ARGUMENT;
    NtGlyphMetrics metrics;
    const NtResult result = nt_glyph_measure(glyph,width,&metrics);
    if (result != NT_RESULT_OK) return result;
    if (metrics.columns != 1 || (metrics.presentation != NT_GLYPH_TEXT && metrics.presentation != NT_GLYPH_EMOJI))
        return NT_RESULT_INVALID_ARGUMENT;
    return NT_RESULT_OK;
}

static NtResult stamp(NtPaintContext *paint, NtCellRect clip, NtByteView glyph, NtParticleSample sample,
                      double opacity, uint32_t numerator, uint32_t denominator)
{
    if (!sample.color.alpha) return NT_RESULT_OK;
    /* Sample conversion is pure. Restore the caller environment before
     * paint, which may invoke the caller's frame allocator. */
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    const NtPoint point = sample.position;
    sample.color.alpha = (uint8_t)round(sample.color.alpha*opacity*numerator/denominator);
    const bool visible = sample.color.alpha && point.x >= clip.origin.x && point.y >= clip.origin.y &&
        point.x < (double)clip.origin.x+clip.size.columns && point.y < (double)clip.origin.y+clip.size.rows &&
        point.x < (double)INT32_MAX+1 && point.y < (double)INT32_MAX+1;
    NtCellPoint cell = {0};
    if (visible) cell = (NtCellPoint){(int32_t)floor(point.x),(int32_t)floor(point.y)};
    if (!nt_finish_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    if (!visible) return NT_RESULT_OK;
    return nt_paint_glyph(paint,cell,glyph,1,(NtCellStyle){.foreground=sample.color});
}

static NtResult draw_particles(const NtParticleEmitter *emitter, NtPaintContext *paint, NtTimeUs now,
                               NtMotionPolicy motion, const NtParticlePaintOptions *options)
{
    const NtCellRect clip = nt_paint_clip(paint);
    if (!clip.size.columns || !clip.size.rows) return NT_RESULT_OK;
    NtParticleEmitter stationary;
    if (motion == NT_MOTION_REDUCED) {
        stationary = *emitter;
        stationary.motion.sway_period_us = 0;
        emitter = &stationary;
    }
    const NtParticleRangeId range = nt_particle_recent(emitter,now);
    for (size_t i = 0; i < range.count; ++i) {
        const uint64_t id = range.first+i;
        NtParticleAge age;
        if (!nt_particle_alive(emitter,id,now,&age)) continue;
        if (motion == NT_MOTION_REDUCED) age.age = 0;
        /* Below this cutoff even the largest alpha rounds to zero. */
        const uint32_t length = options->trail_opacity >= 1.0/1024 ?
                                nt_particle_trail_count(age,options->trail) : 0;
        for (uint32_t j = 0; j < length; ++j) {
            NtParticleAge earlier = age;
            earlier.age -= (length-j)*options->trail.spacing_us;
            NtParticleSample sample;
            NtResult result = nt_particle_sample_age(emitter,id,earlier,true,&sample);
            if (result != NT_RESULT_OK) return result;
            result = stamp(paint,clip,options->glyph,sample,options->trail_opacity,j+1,length+1);
            if (result != NT_RESULT_OK) return result;
        }
        NtParticleSample sample;
        NtResult result = nt_particle_sample_age(emitter,id,age,true,&sample);
        if (result == NT_RESULT_OK) result = stamp(paint,clip,options->glyph,sample,1,1,1);
        if (result != NT_RESULT_OK) return result;
    }
    return NT_RESULT_OK;
}

NtResult nt_particle_emitter_draw(const NtParticleEmitter *emitter, NtPaintContext *paint, NtTimeUs now,
                                  NtMotionPolicy motion, const NtParticlePaintOptions *options)
{
    if (!emitter || !paint || (motion != NT_MOTION_FULL && motion != NT_MOTION_REDUCED))
        return NT_RESULT_INVALID_ARGUMENT;
    const NtResult result = nt_particle_paint_validate(options);
    return result == NT_RESULT_OK ? draw_particles(emitter,paint,now,motion,options) : result;
}

static NtResult measure_view(const void *value, NtMeasureContext *context, NtConstraints constraints, NtCellSize *out)
{
    const ParticleView *view = value;
    if (nt_measure_child_count(context)) return NT_RESULT_INVALID_ARGUMENT;
    *out = nt_control_fit(view->preferred,constraints);
    return NT_RESULT_OK;
}

static NtResult update_view(void *value, NtUpdateInfo info, NtUpdateReply *reply)
{
    ParticleView *view = value;
    view->now = info.now;
    view->motion = info.motion;
    reply->invalidate = NT_INVALIDATE_PAINT;
    return nt_particle_emitter_deadline(&view->emitter,view->now,view->motion,view->frame_interval,&reply->next);
}

static NtResult paint_view(const void *value, NtPaintContext *paint, NtCellSize assigned)
{
    (void)assigned;
    const ParticleView *view = value;
    return draw_particles(&view->emitter,paint,view->now,view->motion,&view->paint);
}

static void dispose_view(void *value)
{
    ParticleView *view = value;
    const NtAllocator allocator = view->emitter.allocator;
    nt_release(&allocator,view,sizeof(*view),NT_ALIGNMENT(ParticleView));
}

static const NtComponentOps view_ops = {
    measure_view,nt_control_leaf,update_view,paint_view,NULL,dispose_view
};

static NtResult validate_view(const NtParticleViewOptions *options, const NtParticleEffectMotion *motion)
{
    if (!options) return NT_RESULT_INVALID_ARGUMENT;
    NtResult result = nt_particle_options_validate(&options->emitter);
    if (result == NT_RESULT_OK && motion) result = nt_particle_effect_motion_validate(motion);
    return result == NT_RESULT_OK ? nt_particle_paint_validate(&options->paint) : result;
}

static void copy_options(ParticleView *view, const NtParticleViewOptions *options,
                         const NtParticleEffectMotion *motion)
{
    memmove(view->glyph,options->paint.glyph.data,options->paint.glyph.count);
    view->emitter.options = options->emitter;
    view->emitter.motion = motion ? *motion : (NtParticleEffectMotion){0};
    view->paint = options->paint;
    view->paint.glyph.data = view->glyph;
    view->preferred = options->preferred;
    view->frame_interval = options->frame_interval_us;
}

static NtResult mount_view(NtTree *tree, NtNodeId parent, NtMountOptions mount,
                           const NtParticleViewOptions *options, const NtParticleEffectMotion *motion,
                           NtNodeId *out)
{
    NtResult result = nt_control_validate_mount(tree,parent,out);
    if (result != NT_RESULT_OK) return result;
    result = validate_view(options,motion);
    if (result != NT_RESULT_OK) return result;
    tree->busy = true;
    ParticleView *view = nt_allocate(&tree->allocator,sizeof(*view),NT_ALIGNMENT(ParticleView));
    tree->busy = false;
    if (!view) return NT_RESULT_ALLOCATION_FAILED;
    *view = (ParticleView){.emitter={.allocator=tree->allocator},.now=tree->watermark,.motion=NT_MOTION_FULL};
    copy_options(view,options,motion);
    result = nt_tree_mount(tree,parent,&view_ops,view,mount,out);
    if (result != NT_RESULT_OK) {
        tree->busy = true;
        dispose_view(view);
        tree->busy = false;
    }
    return result;
}

NtResult nt_particle_view_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount,
                                const NtParticleViewOptions *options, NtNodeId *out)
{
    return mount_view(tree,parent,mount,options,NULL,out);
}

NtResult nt_particle_effect_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount,
                                  const NtParticleEffectOptions *options, NtNodeId *out)
{
    return mount_view(tree,parent,mount,options ? &options->view : NULL,options ? &options->motion : NULL,out);
}

static NtResult set_view(NtTree *tree, NtNodeId node, const NtParticleViewOptions *options,
                         const NtParticleEffectMotion *motion)
{
    if (!tree) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    void *state;
    NtResult result = nt_control_lookup(tree,node,&view_ops,&state);
    if (result != NT_RESULT_OK) return result;
    result = validate_view(options,motion);
    if (result != NT_RESULT_OK) return result;
    ParticleView *view = state;
    copy_options(view,options,motion);
    view->now = tree->watermark;
    tree->nodes[node.slot].deadline = (NtDeadline){true,view->now};
    nt_tree_dirty(tree,NT_INVALIDATE_LAYOUT);
    return NT_RESULT_OK;
}

NtResult nt_particle_view_set(NtTree *tree, NtNodeId node, const NtParticleViewOptions *options)
{
    return set_view(tree,node,options,NULL);
}

NtResult nt_particle_effect_set(NtTree *tree, NtNodeId node, const NtParticleEffectOptions *options)
{
    return set_view(tree,node,options ? &options->view : NULL,options ? &options->motion : NULL);
}
