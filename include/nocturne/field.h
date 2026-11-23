#ifndef NOCTURNE_FIELD_H
#define NOCTURNE_FIELD_H

#include <nocturne/motion.h>
#include <nocturne/paint_sample.h>
#include <nocturne/component.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum NtOpticalFalloff {
    NT_OPTICAL_LINEAR,
    NT_OPTICAL_GAUSSIAN,
    NT_OPTICAL_INVERSE_QUADRATIC
} NtOpticalFalloff;

/* Evaluate the response to a nonnegative normalized distance. */
NtResult nt_optical_falloff(NtOpticalFalloff falloff, double distance, double *out);

enum { NT_FIELD_MAX_RINGS = 32 };

typedef struct NtRippleField {
    NtPoint origin, aspect;
    double first_radius, last_radius, width, peak;
    NtTimeUs start_us, lifetime_us, spacing_us;
    uint32_t rings;
    NtOpticalFalloff falloff;
    NtEasing fade;                  /* Applied to the remaining lifetime fraction. */
} NtRippleField;

typedef struct NtWaveField {
    NtPoint origin, cycles_per_unit;
    double phase_turns;
    NtTimeUs start_us, period_us;   /* Period zero holds the authored phase. */
} NtWaveField;

typedef enum NtFieldKind { NT_FIELD_RIPPLE, NT_FIELD_WAVE } NtFieldKind;

typedef struct NtField {
    NtFieldKind kind;
    union {
        NtRippleField ripple;
        NtWaveField wave;
    } as;
} NtField;

typedef struct NtFieldQuery {
    NtPoint position;
    NtTimeUs now;
    NtMotionPolicy motion;
} NtFieldQuery;

typedef struct NtFieldSample {
    double value;        /* Ripple strength in [0,1] or wave displacement in [-1,1]. */
    double coverage;     /* Ripple strength, or 1 while a wave is active. */
} NtFieldSample;

typedef struct NtFieldInk {
    NtRgba color;
    const NtPaletteRamp *palette;   /* Optional ramp in place of color. */
    double opacity;
} NtFieldInk;

NtRippleField nt_ripple_field_default(void);
NtWaveField nt_wave_field_default(void);
/* Descriptors and palette storage are borrowed for the call. */
NtResult nt_field_sample(const NtField *field, NtFieldQuery query, NtFieldSample *out);
NtResult nt_field_color(const NtField *field, const NtFieldInk *ink, NtFieldQuery query, NtRgba *out);
/* Fill local cell backgrounds, sampling each visible cell at its center. */
NtResult nt_field_draw(const NtField *field, const NtFieldInk *ink, NtPaintContext *paint,
                       NtCellRect bounds, NtTimeUs now, NtMotionPolicy motion);
NtResult nt_field_deadline(const NtField *field, NtTimeUs now, NtMotionPolicy motion,
                           NtTimeUs frame_interval_us, NtDeadline *out);

#ifdef __cplusplus
}
#endif
#endif
