#ifndef NOCTURNE_INTERNAL_CANVAS_H
#define NOCTURNE_INTERNAL_CANVAS_H

#include <nocturne/canvas.h>

static inline bool nt_sampling_valid(NtSamplingMode mode)
{
    return mode >= NT_SAMPLE_CELL && mode <= NT_SAMPLE_HYBRID;
}

NtResult nt_frame_write_sampled_cell(NtFrame *frame, NtCellPoint position, const NtSampledCell *cell);
NtResult nt_frame_write_samples(NtFrame *frame, NtCellPoint position, const NtCellSamples *samples,
                                 NtSamplingMode mode);

#endif
