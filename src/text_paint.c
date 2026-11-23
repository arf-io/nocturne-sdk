#include "text_paint_internal.h"

#include "color_internal.h"
#include "frame_internal.h"
#include "text_effect_internal.h"

#include <math.h>

static NtTextPaintStyle text_style(const NtTextPaintOptions *options, NtClusterView cluster)
{
    if (cluster.id) {
        for (size_t i = options->count; i-- > 0;) {
            const NtTextPaintSpan span = options->spans[i];
            if (cluster.source.begin >= span.source.begin && cluster.source.begin < span.source.end)
                return span.style;
        }
    }
    return options->base;
}

static NtResult paint_style(NtTextPaintStyle style, NtClusterView cluster,
                             const NtTextPaintOptions *options, NtTimeUs elapsed, NtCellStyle *out)
{
    const NtPaintSample sample = {cluster.position.x + 0.5, cluster.position.y + 0.5,
                                   elapsed, options->motion};
    NtCellStyle result = style.color;
    if (style.foreground) {
        const NtResult status = nt_paint_sample_rgba(style.foreground, sample, options->gamut,
                                                     &result.foreground);
        if (status != NT_RESULT_OK) return status;
    }
    if (style.background) {
        const NtResult status = nt_paint_sample_rgba(style.background, sample, options->gamut,
                                                     &result.background);
        if (status != NT_RESULT_OK) return status;
    }
    *out = result;
    return NT_RESULT_OK;
}

typedef struct TextDrawing {
    const NtSurfaceView *target;
    const NtTextLayout *layout;
    const NtTextPaintOptions *options;
    const EffectWork *effect;
    NtSurfaceView view;
    NtFrame stage;
    int64_t left, top, right, bottom;
    NtCellRect clip;
    NtOwnerToken owner;
    NtTextStyleResolve resolve_style;
    void *style_context;
    bool stage_frame, force_owner, staged;
} TextDrawing;

static NtResult draw_cluster(TextDrawing *draw, NtClusterView cluster, bool tab, NtTextPose pose,
                             NtTimeUs elapsed, double opacity)
{
    const double dx = round(pose.offset.x), dy = round(pose.offset.y);
    if (dx < INT32_MIN || dx > INT32_MAX || dy < INT32_MIN || dy > INT32_MAX)
        return NT_RESULT_OUT_OF_RANGE;
    const int64_t x = draw->left + cluster.position.x + (int32_t)dx;
    const int64_t y = draw->top + cluster.position.y + (int32_t)dy;
    const int64_t begin = x > draw->clip.origin.x ? x : draw->clip.origin.x;
    const int64_t end = x + cluster.columns < draw->right ? x + cluster.columns : draw->right;
    if (!opacity || y < draw->clip.origin.y || y >= draw->bottom || begin >= end) return NT_RESULT_OK;
    bool visible = false;
    for (int64_t px = begin; px < end && !visible; ++px) {
        double coverage;
        const NtResult result = nt_surface_coverage(&draw->view,(NtCellPoint){(int32_t)px,(int32_t)y},&coverage);
        if (result != NT_RESULT_OK) return result;
        visible = coverage != 0;
    }
    if (!visible) return NT_RESULT_OK;

    NtTextPaintStyle style = text_style(draw->options,cluster);
    if (draw->resolve_style) {
        const NtResult result = draw->resolve_style(draw->style_context, cluster, &style);
        if (result != NT_RESULT_OK) return result;
    }
    if (draw->force_owner) style.owner = draw->owner;
    NtCellStyle color;
    NtResult result = paint_style(style,cluster,draw->options,elapsed,&color);
    if (result == NT_RESULT_OK && draw->effect) result = nt_text_effect_color(&color,&pose,opacity);
    if (result != NT_RESULT_OK || (!color.foreground.alpha && !color.background.alpha)) return result;
    if (draw->stage_frame && !draw->staged) {
        result = nt_frame_stage(draw->target->frame,&draw->stage);
        if (result != NT_RESULT_OK) return result;
        draw->staged = true;
        draw->view.frame = &draw->stage;
    }
    if (!tab)
        return nt_surface_glyph(&draw->view,(NtCellPoint){(int32_t)x,(int32_t)y},pose.glyph,
                                (uint8_t)cluster.columns,color,style.owner);
    for (int64_t px = begin; px < end && result == NT_RESULT_OK; ++px)
        result = nt_surface_glyph(&draw->view,(NtCellPoint){(int32_t)px,(int32_t)y},pose.glyph,1,color,style.owner);
    return result;
}

static NtResult draw_pass(TextDrawing *draw, NtTimeUs elapsed, bool trail, double opacity)
{
    const NtTextLayout *layout = draw->layout;
    const NtByteView source = nt_text_layout_source(layout);
    const size_t count = nt_text_layout_cluster_count(layout);
    for (size_t i = 0; i < count; ++i) {
        NtClusterView cluster;
        NtTextPose pose;
        bool tab;
        NtResult result;
        if (draw->effect) {
            const EffectEntry *entry = &draw->effect->entries[i];
            cluster = entry->cluster;
            tab = entry->tab;
            result = nt_text_effect_sample(draw->effect,entry,elapsed,draw->options->motion,trail,&pose);
        } else {
            result = nt_text_layout_cluster(layout,i,&cluster);
            if (result != NT_RESULT_OK) return result;
            tab = cluster.source.end-cluster.source.begin == 1 && source.data[cluster.source.begin] == '\t';
            pose = (NtTextPose){.glyph=cluster.glyph,.opacity=1};
        }
        if (result != NT_RESULT_OK) return result;
        result = draw_cluster(draw,cluster,tab,pose,elapsed,pose.opacity * opacity);
        if (result != NT_RESULT_OK) return result;
    }
    return NT_RESULT_OK;
}

static NtResult draw_trails(TextDrawing *draw)
{
    const EffectWork *work = draw->effect;
    const NtTextTrails trails = work->effect->trails;
    const NtTimeUs elapsed = draw->options->elapsed_us;
    if (!trails.count || !trails.decay || draw->options->motion == NT_MOTION_REDUCED ||
        !work->last_end || elapsed < work->first_start) return NT_RESULT_OK;
    const uint64_t oldest = (elapsed-work->first_start) / trails.spacing_us;
    const uint64_t newest = elapsed >= work->last_end ? (elapsed-work->last_end) / trails.spacing_us + 1 : 1;
    uint64_t n = oldest < trails.count ? oldest : trails.count;
    for (; n >= newest; --n) {
        const double opacity = pow(trails.decay,(double)n);
        if (!opacity) continue;
        const NtResult result = draw_pass(draw,elapsed-n*trails.spacing_us,true,opacity);
        if (result != NT_RESULT_OK) return result;
    }
    return NT_RESULT_OK;
}

static NtResult paint_text(const NtSurfaceView *view, NtCellPoint position,
                           const NtTextLayout *layout, const NtTextPaintOptions *options,
                           const NtTextEffect *effect, bool stage_frame,
                           bool force_owner, NtOwnerToken owner,
                           NtTextStyleResolve resolve_style, void *style_context)
{
    if (!view || !layout || !options || (options->count && !options->spans) ||
        !nt_gamut_valid(options->gamut) ||
        (options->motion != NT_MOTION_FULL && options->motion != NT_MOTION_REDUCED))
        return NT_RESULT_INVALID_ARGUMENT;
    if (options->count > SIZE_MAX / sizeof(*options->spans)) return NT_RESULT_OUT_OF_RANGE;
    const NtByteView source = nt_text_layout_source(layout);
    for (size_t i = 0; i < options->count; ++i) {
        const NtByteSpan span = options->spans[i].source;
        if (span.begin > span.end || span.end > source.count) return NT_RESULT_OUT_OF_RANGE;
    }
    NtCellRect clip;
    NtResult result = nt_surface_clip(view,&clip);
    if (result != NT_RESULT_OK) return result;
    const int64_t left = (int64_t)view->bounds.origin.x + position.x;
    const int64_t top = (int64_t)view->bounds.origin.y + position.y;
    if (left < INT32_MIN || left > INT32_MAX || top < INT32_MIN || top > INT32_MAX)
        return NT_RESULT_OUT_OF_RANGE;

    EffectWork work = {0};
    TextDrawing draw = {.target=view,.layout=layout,.options=options,.effect=effect ? &work : NULL,
        .view=*view,.left=left,.top=top,.right=(int64_t)clip.origin.x+clip.size.columns,
        .bottom=(int64_t)clip.origin.y+clip.size.rows,.clip=clip,.owner=owner,
        .stage_frame=stage_frame,.force_owner=force_owner,
        .resolve_style=resolve_style,.style_context=style_context};
    draw.view.bounds.origin = (NtCellPoint){0};
    if (effect) result = nt_text_effect_prepare(&view->frame->allocator,layout,effect,&work);
    if (result == NT_RESULT_OK && clip.size.columns && clip.size.rows) {
        if (effect) result = draw_trails(&draw);
        if (result == NT_RESULT_OK) result = draw_pass(&draw,options->elapsed_us,false,1);
    }
    if (draw.staged) {
        if (result == NT_RESULT_OK) nt_frame_publish(view->frame,&draw.stage);
        else nt_frame_discard(&draw.stage);
    }
    if (effect) nt_text_effect_release(&view->frame->allocator,&work);
    return result;
}

NtResult nt_text_paint(const NtSurfaceView *view, NtCellPoint position,
                       const NtTextLayout *layout, const NtTextPaintOptions *options)
{
    return paint_text(view,position,layout,options,NULL,true,false,0,NULL,NULL);
}

NtResult nt_text_paint_candidate(const NtSurfaceView *view, NtCellPoint position,
                                 const NtTextLayout *layout, const NtTextPaintOptions *options,
                                 NtOwnerToken owner)
{
    return paint_text(view,position,layout,options,NULL,false,true,owner,NULL,NULL);
}

NtResult nt_text_paint_resolved(const NtSurfaceView *view, NtCellPoint position,
                                const NtTextLayout *layout, const NtTextPaintOptions *options,
                                NtTextStyleResolve resolve, void *context, bool mounted, NtOwnerToken owner)
{
    return paint_text(view, position, layout, options, NULL, true, mounted, owner, resolve, context);
}

NtResult nt_text_effect_paint(const NtSurfaceView *view, NtCellPoint position,
                              const NtTextLayout *layout, const NtTextPaintOptions *options,
                              const NtTextEffect *effect)
{
    return effect ? paint_text(view,position,layout,options,effect,true,false,0,NULL,NULL) : NT_RESULT_INVALID_ARGUMENT;
}

NtResult nt_text_effect_paint_owner(const NtSurfaceView *view, NtCellPoint position,
                                    const NtTextLayout *layout, const NtTextPaintOptions *options,
                                    const NtTextEffect *effect, NtOwnerToken owner)
{
    return effect ? paint_text(view,position,layout,options,effect,true,true,owner,NULL,NULL) : NT_RESULT_INVALID_ARGUMENT;
}
