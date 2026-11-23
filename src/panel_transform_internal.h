#ifndef NOCTURNE_INTERNAL_PANEL_TRANSFORM_H
#define NOCTURNE_INTERNAL_PANEL_TRANSFORM_H

#include <nocturne/panel_transform.h>
#include <fenv.h>

#include "scaled_sum_internal.h"

typedef struct NtPanelAxis {
    NtScaledSum origin, divisor;
    NtScaledTerm approximate_divisor;
} NtPanelAxis;

typedef struct NtPanelBasis {
    NtPanelAxis x, y;
    double cosine, sine;
    bool drawable;
} NtPanelBasis;

bool nt_panel_begin_geometry(fenv_t *saved);
bool nt_panel_finish_geometry(const fenv_t *saved);
NtResult nt_panel_basis(const NtPanelPose *pose, NtPanelBasis *out);
/* The caller holds the standard geometry environment. */
NtResult nt_panel_inverse_point(const NtPanelBasis *basis, NtPoint destination, NtPoint *out);
NtResult nt_panel_source_cell(NtPanelPixel pixel, NtCellRect region, NtCellPoint *out, bool *inside);
NtResult nt_panel_project_owned(const NtSurfaceView *view, const NtPanelLayer *layers, size_t count,
                                NtOwnerToken owner);

#endif
