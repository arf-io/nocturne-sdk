#ifndef NOCTURNE_PAINT_SAMPLE_H
#define NOCTURNE_PAINT_SAMPLE_H

#include <nocturne/animation.h>
#include <nocturne/color.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum NtPaintAddress {
    NT_PAINT_PAD, NT_PAINT_REPEAT, NT_PAINT_REFLECT
} NtPaintAddress;

typedef struct NtGradientStop {
    double position;
    NtColor color;
    NtEasing from_here;
} NtGradientStop;

typedef struct NtGradient {
    const NtGradientStop *stops;
    size_t count;
    NtPaintAddress address;
    NtColorInterpolation interpolation;
} NtGradient;

typedef enum NtPaletteFilter {
    NT_PALETTE_LINEAR, NT_PALETTE_STEP
} NtPaletteFilter;

typedef struct NtPaletteRamp {
    NtPaletteView palette;
    NtPaletteFilter filter;
    NtPaintAddress address;
    NtColorInterpolation interpolation;
    NtEasing easing;
} NtPaletteRamp;

/* Stops are nondecreasing in [0,1]. Equal positions form a hard stop,
 * and the last stop at the position wins. Coordinates are finite. A
 * repeating linear palette includes the last-to-first interval. */
NtResult nt_gradient_sample(const NtGradient *gradient, double coordinate, NtColor *out);
NtResult nt_palette_sample(const NtPaletteRamp *ramp, double coordinate, NtColor *out);

typedef enum NtPaintPattern {
    NT_PAINT_LINEAR,
    NT_PAINT_RADIAL,
    NT_PAINT_CONIC,
    NT_PAINT_WAVE,
    NT_PAINT_PLASMA,
    NT_PAINT_MOIRE,
    NT_PAINT_AURORA,
    NT_PAINT_INTERFERENCE,
    NT_PAINT_CHEVRONS,
    NT_PAINT_DIAMOND,
    NT_PAINT_CHECKER,
    NT_PAINT_UNIFORM
} NtPaintPattern;

typedef enum NtPaintRotation {
    NT_PAINT_FIXED, NT_PAINT_SPIN, NT_PAINT_SWING
} NtPaintRotation;

typedef struct NtPaintField {
    NtPaintPattern pattern;
    double origin_x, origin_y;
    double scale_x, scale_y;
    double angle_degrees;
    NtPaintRotation rotation;
    NtTimeUs rotation_period_us;
    double swing_degrees;
    bool reverse_rotation;
    double phase, wave_amplitude;
    const NtTween *phase_track;
} NtPaintField;

typedef struct NtPaintSample {
    double x, y;
    NtTimeUs elapsed_us;
    NtMotionPolicy motion;
} NtPaintSample;

/* Transform coordinates from the supplied origin to rotated, scaled axes
 * and return the pattern's scalar plus phase. Sampling takes explicit
 * time. Reduced motion samples all animated controls at elapsed time zero. */
NtResult nt_paint_coordinate(const NtPaintField *field, NtPaintSample sample, double *out);

typedef enum NtPaintKind {
    NT_PAINT_SOLID, NT_PAINT_GRADIENT, NT_PAINT_PALETTE
} NtPaintKind;

typedef struct NtPaint {
    NtPaintKind kind;
    NtPaintField field; /* Used by gradient and palette paint. */
    union {
        NtColor solid;
        NtGradient gradient;
        NtPaletteRamp palette;
    } content;
    double opacity;
    const NtKeyframeTrack *opacity_track; /* Optional normalized factor. */
} NtPaint;

/* Descriptors and all arrays are borrowed through the call. Sampling is
 * allocation-free and stateless. Opacity multiplies straight alpha. All
 * result-pointer APIs preserve outputs on invalid input or overflow. */
NtResult nt_paint_sample(const NtPaint *paint, NtPaintSample sample, NtColor *out);
NtResult nt_paint_sample_rgba(const NtPaint *paint, NtPaintSample sample,
                              NtGamutPolicy gamut, NtRgba *out);

#ifdef __cplusplus
}
#endif
#endif
