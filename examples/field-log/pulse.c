#include "pulse.h"

static NtResult pulse_measure(const void *state, NtMeasureContext *context,
                               NtConstraints limits, NtCellSize *out)
{
    (void)state;
    if (nt_measure_child_count(context)) return NT_RESULT_INVALID_ARGUMENT;
    NtCellSize size = {7, 1};
    if (limits.columns.bounded && size.columns > limits.columns.max) size.columns = limits.columns.max;
    if (limits.rows.bounded && size.rows > limits.rows.max) size.rows = limits.rows.max;
    if (size.columns < limits.columns.min) size.columns = limits.columns.min;
    if (size.rows < limits.rows.min) size.rows = limits.rows.min;
    *out = size;
    return NT_RESULT_OK;
}

static NtResult pulse_layout(const void *state, NtLayoutContext *context, NtCellSize size)
{
    (void)state;
    (void)size;
    return nt_layout_child_count(context) ? NT_RESULT_INVALID_ARGUMENT : NT_RESULT_OK;
}

static NtResult pulse_tick(void *state, NtUpdateInfo time, NtUpdateReply *reply)
{
    Pulse *pulse = state;
    const NtTimeUs period = 120000;
    const unsigned phase = time.motion == NT_MOTION_REDUCED ? 0 : (unsigned)(time.now / period % 4);
    if (phase != pulse->phase) {
        pulse->phase = phase;
        reply->invalidate = NT_INVALIDATE_PAINT;
    }
    const NtTimeUs wait = period - time.now % period;
    if (time.motion == NT_MOTION_FULL && wait <= UINT64_MAX - time.now)
        reply->next = (NtDeadline){true, time.now + wait};
    return NT_RESULT_OK;
}

static NtResult pulse_paint(const void *state, NtPaintContext *paint, NtCellSize size)
{
    const Pulse *pulse = state;
    const char title[] = {'P','u','l','s','e',' ',(char)('0' + pulse->phase)};
    for (uint32_t i = 0; size.rows && i < size.columns && i < sizeof(title); ++i) {
        const NtResult result = nt_paint_glyph(paint, (NtCellPoint){(int32_t)i,0},
                                             (NtByteView){title + i, 1}, 1, pulse->colors);
        if (result != NT_RESULT_OK) return result;
    }
    return NT_RESULT_OK;
}

const NtComponentOps pulse_ops = {
    .measure = pulse_measure, .layout = pulse_layout, .update = pulse_tick, .paint = pulse_paint,
};
