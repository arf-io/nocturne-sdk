#ifndef NOCTURNE_CANVAS_H
#define NOCTURNE_CANVAS_H

#include <nocturne/blend.h>
#include <nocturne/frame.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum NtSamplingMode {
    NT_SAMPLE_CELL,
    NT_SAMPLE_HALF_BLOCK,
    NT_SAMPLE_BRAILLE,
    NT_SAMPLE_HYBRID
} NtSamplingMode;

typedef struct NtCanvasPixel {
    NtRgba color;
    NtOwnerToken owner;
    bool covered;                  /* Geometric coverage, independent of brightness. */
} NtCanvasPixel;

typedef struct NtCellSamples {
    NtCanvasPixel pixels[8];       /* Row-major 2 by 4, top-left first. */
    NtRgba background[8];          /* Colors beneath geometric fragments. */
} NtCellSamples;

typedef struct NtSampledCell {
    char glyph[4];                 /* Null-terminated UTF-8. */
    size_t glyph_size;             /* Excludes the terminator. */
    NtCellStyle style;
    NtOwnerToken owner;
} NtSampledCell;

NtResult nt_sampling_mode_parse(const char *name, NtSamplingMode *out);
/* Pure sample reduction. Geometric braille preserves all eight dots,
 * including a full mask. Output colors are straight RGBA8. */
NtResult nt_cell_sample(const NtCellSamples *samples, NtSamplingMode mode, NtSampledCell *out);

typedef struct NtCanvas NtCanvas;

typedef struct NtCanvasInk {
    NtRgba color;
    NtOwnerToken owner;
    const NtCompositeOptions *composite;
} NtCanvasInk;

/* Coordinates are signed sample-grid positions. Each cell encodes a 2 by
 * 4 sample tile when rasterized. Either sample extent may be zero. */
NtResult nt_canvas_create(const NtAllocator *allocator, NtCellSize samples, NtCanvas **out);
void nt_canvas_destroy(NtCanvas *canvas);
NtCellSize nt_canvas_size(const NtCanvas *canvas);
NtResult nt_canvas_clear(NtCanvas *canvas, NtRgba background);
NtResult nt_canvas_pixel(const NtCanvas *canvas, NtCellPoint sample, NtCanvasPixel *out);

/* Draw clipped primitives with the supplied ink. Lines include endpoints.
 * Filled triangles include their edges, with a collinear triangle drawing
 * the outer segment. Rectangle dimensions are sample counts. Errors
 * preserve the previous canvas. */
NtResult nt_canvas_point(NtCanvas *canvas, NtCellPoint point, NtCanvasInk ink);
NtResult nt_canvas_line(NtCanvas *canvas, NtCellPoint a, NtCellPoint b, NtCanvasInk ink);
NtResult nt_canvas_triangle(NtCanvas *canvas, NtCellPoint a, NtCellPoint b, NtCellPoint c,
                             NtCanvasInk ink);
NtResult nt_canvas_fill(NtCanvas *canvas, NtCellRect rect, NtCanvasInk ink);
NtResult nt_canvas_border(NtCanvas *canvas, NtCellRect rect, NtCanvasInk ink);

/* Create a frame with ownership of its glyph bytes. The optional allocator
 * belongs to the resulting frame. Edge samples outside the canvas use
 * its background. Errors preserve the caller's output pointer. */
NtResult nt_canvas_render(const NtCanvas *canvas, NtSamplingMode mode,
                           const NtAllocator *allocator, NtFrame **out);

#ifdef __cplusplus
}
#endif
#endif
