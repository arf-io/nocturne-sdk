#ifndef NOCTURNE_CONTENT_H
#define NOCTURNE_CONTENT_H

#include <nocturne/canvas.h>
#include <nocturne/paint_sample.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NtContentCell {
    NtCellPoint position;
    NtByteView glyph;
    uint8_t columns;
    NtCellStyle style;
    NtOwnerToken owner;
} NtContentCell;

/* Copy caller-supplied cells into a new owned frame in array order. A
 * background-only cell has zero columns and empty glyph and foreground.
 * Errors leave the caller's output pointer unchanged. */
NtResult nt_frame_from_cells(const NtAllocator *allocator, NtCellSize size,
                              const NtContentCell *cells, size_t count, NtFrame **out);

typedef struct NtImageView {
    const NtRgba *pixels;
    NtCellSize size;
    size_t stride, count; /* Measured in pixels, with row-major storage. */
} NtImageView;

typedef enum NtImageFilter { NT_IMAGE_NEAREST, NT_IMAGE_BILINEAR } NtImageFilter;

typedef struct NtImageSampling {
    NtImageFilter filter;
    NtPaintAddress horizontal, vertical;
} NtImageSampling;

/* Normalized coordinates. Bilinear interpolates between texel centers
 * in premultiplied linear light, and returns straight encoded sRGB. */
NtResult nt_image_sample(const NtImageView *image, NtImageSampling sampling,
                          double u, double v, NtRgba *out);

/* Resample the image to a physical sample extent and reduce each 2x4 tile
 * to an independently owned frame. Edge padding is transparent. */
NtResult nt_image_rasterize(const NtImageView *image, NtImageSampling sampling,
                             NtCellSize extent, NtSamplingMode mode, NtOwnerToken owner,
                             const NtAllocator *allocator, NtFrame **out);

#ifdef __cplusplus
}
#endif
#endif
