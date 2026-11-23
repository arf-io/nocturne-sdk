#include "indicator.h"

#include <nocturne/animation.h>

#include <stdlib.h>

typedef struct Indicator {
    IndicatorStats *stats;
    char glyph;
    NtTimeUs elapsed;
} Indicator;

static uint32_t measure_cell(NtExtentRange range)
{
    if (range.bounded && range.max == 0) return 0;
    return range.min > 1 ? range.min : 1;
}

static NtResult measure(const void *state, NtMeasureContext *context,
                        NtConstraints constraints, NtCellSize *out)
{
    (void)state;
    if (nt_measure_child_count(context)) return NT_RESULT_INVALID_ARGUMENT;
    *out = (NtCellSize){measure_cell(constraints.columns),measure_cell(constraints.rows)};
    return NT_RESULT_OK;
}

static NtResult layout(const void *state, NtLayoutContext *context, NtCellSize assigned)
{
    (void)state;
    (void)assigned;
    return nt_layout_child_count(context) ? NT_RESULT_INVALID_ARGUMENT : NT_RESULT_OK;
}

static NtResult tick(void *state, NtUpdateInfo time, NtUpdateReply *out)
{
    Indicator *indicator = state;
    ++indicator->stats->updates;
    indicator->elapsed += time.elapsed;
    const NtTween tween = {0,1,20,0,NT_LINEAR,NT_ONCE};
    NtAnimationSample sample;
    const NtTimeUs elapsed = time.motion == NT_MOTION_REDUCED ? 20 : indicator->elapsed;
    if (nt_tween_sample(&tween,elapsed,&sample) != NT_OK) return NT_RESULT_OUT_OF_RANGE;
    indicator->glyph = sample.value < .5 ? '-' : '+';
    *out = (NtUpdateReply){NT_INVALIDATE_PAINT,{sample.phase != NT_FINISHED,time.now + 10}};
    return NT_RESULT_OK;
}

static NtResult paint(const void *state, NtPaintContext *context, NtCellSize assigned)
{
    const Indicator *indicator = state;
    if (indicator->stats->defer_paint) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    if (!assigned.columns || !assigned.rows) return NT_RESULT_OK;
    const NtCellStyle style = {{200,245,255,255},{12,18,38,255}};
    return nt_paint_glyph(context,(NtCellPoint){0,0},(NtByteView){&indicator->glyph,1},1,style);
}

static NtResult event(void *state, NtEventContext *context, const NtEvent *input, NtEventReply *out)
{
    IndicatorStats *stats = ((Indicator *)state)->stats;
    if (nt_event_phase(context) != NT_EVENT_TARGET) return NT_RESULT_INVALID_ARGUMENT;
    if (input->kind == NT_EVENT_FOCUS_GAIN) ++stats->focus_gains;
    if (input->kind == NT_EVENT_FOCUS_LOSS) ++stats->focus_losses;
    *out = (NtEventReply){0};
    return NT_RESULT_OK;
}

static void dispose(void *state)
{
    Indicator *indicator = state;
    ++indicator->stats->disposed;
    free(indicator);
}

NtResult indicator_mount(NtTree *tree, NtNodeId parent, IndicatorStats *stats, NtNodeId *out)
{
    const NtComponentOps ops = {measure,layout,tick,paint,event,dispose};
    const NtMountOptions options = {true,true,true};
    Indicator *state = malloc(sizeof(*state));
    if (!state) return NT_RESULT_ALLOCATION_FAILED;
    *state = (Indicator){stats,'-',0};
    const NtResult result = nt_tree_mount(tree,parent,&ops,state,options,out);
    if (result != NT_RESULT_OK) free(state);
    return result;
}
