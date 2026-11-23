#ifndef NOCTURNE_PLOT_H
#define NOCTURNE_PLOT_H

#include <nocturne/canvas.h>
#include <nocturne/component.h>
#include <nocturne/motion.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NtPlotDomain { double min, max; } NtPlotDomain;
typedef struct NtPlotBounds { NtPlotDomain x, y; } NtPlotBounds;
typedef struct NtPlotViewport {
    NtPlotBounds domain;
    NtCellRect cells;
    double cell_aspect; /* Physical cell width divided by height. Zero selects 0.5. */
    bool equal_units;
} NtPlotViewport;

/* Inclusive domain, x increasing right and y increasing up. Project to
 * continuous cell edges. A constant axis places its value at the center. */
NtResult nt_plot_project(const NtPlotViewport *viewport, NtPoint value, NtPoint *out);
/* Clip a directed segment to the inclusive domain. A hidden segment leaves
 * both endpoints untouched. Endpoints and visibility are written on success. */
NtResult nt_plot_clip_segment(NtPlotBounds domain, NtPoint a, NtPoint b,
                              bool *visible, NtPoint *first, NtPoint *last);

typedef enum NtPlotKind {
    NT_PLOT_BARS, NT_PLOT_PIE, NT_PLOT_DONUT, NT_PLOT_SCATTER, NT_PLOT_LINE, NT_PLOT_EDGES
} NtPlotKind;
typedef struct NtPlotDatum { NtPoint point; NtRgba color; } NtPlotDatum;
typedef struct NtPlotReveal { NtTimeUs start_us, duration_us; NtEasing easing; } NtPlotReveal;
typedef struct NtPlotOptions {
    NtPlotKind kind;
    const NtPlotDatum *data;
    size_t count, data_limit;    /* Zero limit uses the full supplied array. */
    size_t sample_stride;        /* Zero or one uses every sample. */
    NtPlotBounds domain;
    bool auto_x, auto_y;
    double baseline;            /* Bars extend from this value to datum.y. */
    double hole_ratio;          /* Donut inner radius in (0,1). Zero selects 0.5. */
    double cell_aspect;
    bool equal_units, sample_colors, ascii;
    NtSamplingMode sampling;
    NtCellSize preferred;
    NtRgba foreground, background;
    NtPlotReveal reveal;
} NtPlotOptions;

/* The mount owns the dataset. Bars use datum.y, pies and donuts use
 * nonnegative datum.y weights, edges take consecutive endpoint pairs.
 * Set replaces the complete configuration and dataset on success. */
NtResult nt_plot_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount,
                       const NtPlotOptions *options, NtNodeId *out);
NtResult nt_plot_set(NtTree *tree, NtNodeId node, const NtPlotOptions *options);

typedef struct NtDigitsOptions {
    int64_t value;
    uint8_t fractional_digits;   /* Decimal placement, from 0 to 18. */
    bool ascii;
    NtRgba foreground, background;
} NtDigitsOptions;

/* Draw signed fixed-point numbers in five-row, three-column digit strokes. */
NtResult nt_digits_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount,
                         const NtDigitsOptions *options, NtNodeId *out);
NtResult nt_digits_set(NtTree *tree, NtNodeId node, const NtDigitsOptions *options);

#ifdef __cplusplus
}
#endif
#endif
