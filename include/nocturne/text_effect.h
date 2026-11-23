#ifndef NOCTURNE_TEXT_EFFECT_H
#define NOCTURNE_TEXT_EFFECT_H

#include <nocturne/motion.h>
#include <nocturne/paint.h>
#include <nocturne/text_paint.h>
#include <nocturne/timeline.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum NtTextEffectOrder {
    NT_TEXT_EFFECT_LOGICAL, NT_TEXT_EFFECT_REVERSE_LOGICAL,
    NT_TEXT_EFFECT_VISUAL, NT_TEXT_EFFECT_REVERSE_VISUAL
} NtTextEffectOrder;

typedef enum NtTextVisibility { NT_TEXT_REVEAL, NT_TEXT_HIDE, NT_TEXT_VISIBLE } NtTextVisibility;
typedef enum NtTextGlyphEffectKind {
    NT_TEXT_ORIGINAL_GLYPHS, NT_TEXT_SCRAMBLE, NT_TEXT_FLIPBOARD
} NtTextGlyphEffectKind;

typedef struct NtTextGlyphEffect {
    NtTextGlyphEffectKind kind;
    const NtByteView *alphabet;
    size_t count;
    NtTimeUs interval_us;
    uint32_t rotations;          /* Flipboard with zero rotations uses interval steps. */
} NtTextGlyphEffect;

typedef struct NtTextTrails {
    uint32_t count;
    NtTimeUs spacing_us;
    double decay;                /* Layer n uses opacity decay raised to n. */
} NtTextTrails;

typedef struct NtTextPose {
    NtByteView glyph;
    NtPoint offset;             /* Cell displacement before rounding. */
    double opacity, hue_degrees;
} NtTextPose;

typedef struct NtTextEffectSample {
    NtClusterView cluster;
    size_t visual_index, logical_index, rank, count;
    int32_t line_begin, line_end; /* Visual row extent, end exclusive. */
    NtTimeUs elapsed_us, local_us, duration_us;
    double progress, travel;     /* Increasing clock progress, and distance from the settled pose. */
    uint64_t seed, identity;
} NtTextEffectSample;

/* Called for running clusters, including trail samples. The callback may
 * combine arbitrary transformations. Input and output glyph bytes are
 * borrowed until painting returns. Keep both frames and inputs stable. */
typedef NtResult (*NtTextTransform)(void *context, const NtTextEffectSample *sample, NtTextPose *pose);

typedef struct NtTextEffect {
    NtTimeUs delay_us, duration_us;
    NtTrackSchedule schedule;
    NtTextEffectOrder order;
    NtTextVisibility visibility;
    NtEasing opacity_easing;
    NtTextGlyphEffect glyphs;
    uint64_t seed;
    NtTextTrails trails;
    NtTextTransform transform;
    void *context;
} NtTextEffect;

typedef struct NtTextMotion {
    NtPoint travel, orbit;
    double phase_turns, cycles, rank_turns, hue_degrees;
    NtEasing easing;
    bool seeded_phase;
} NtTextMotion;

/* Add phase-driven travel, elliptical motion and hue to a pose.
 * Offsets and hue are weighted by eased travel. */
NtResult nt_text_pose_motion(const NtTextEffectSample *sample, const NtTextMotion *motion, NtTextPose *pose);
/* Add independent signed cell offsets sampled from the cluster's seeded clock.
 * Each nonnegative extent is scaled by sample.travel. */
NtResult nt_text_pose_noise(const NtTextEffectSample *sample, NtPoint extent,
                            NtTimeUs interval_us, bool smooth, NtTextPose *pose);

/* Apply an effect to an immutable layout at options->elapsed_us. The
 * destination publishes atomically. Reduced motion settles the target. */
NtResult nt_text_effect_paint(const NtSurfaceView *view, NtCellPoint position,
                              const NtTextLayout *layout, const NtTextPaintOptions *options,
                              const NtTextEffect *effect);

/* Effect placement is component-local. Uses the mounted node's owner. */
NtResult nt_paint_text_effect(NtPaintContext *paint, NtCellPoint position,
                              const NtTextLayout *layout, const NtTextPaintOptions *options,
                              const NtTextEffect *effect);

#ifdef __cplusplus
}
#endif
#endif
