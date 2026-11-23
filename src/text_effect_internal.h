#ifndef NOCTURNE_INTERNAL_TEXT_EFFECT_H
#define NOCTURNE_INTERNAL_TEXT_EFFECT_H

#include <nocturne/text_effect.h>

typedef struct EffectEntry {
    NtClusterView cluster;
    size_t visual, logical, rank;
    uint64_t identity;
    NtTimeUs start, duration;
    int32_t line_begin, line_end;
    bool tab;
} EffectEntry;

typedef struct EffectGlyph { NtByteView glyph; uint8_t columns; } EffectGlyph;

typedef struct EffectWork {
    const NtTextEffect *effect;
    NtWidthPolicy width;
    EffectEntry *entries;
    size_t count;
    EffectGlyph *glyphs;
    size_t glyph_count, widths[3];
    NtTimeUs first_start, last_end;
} EffectWork;

NtResult nt_text_effect_prepare(const NtAllocator *allocator, const NtTextLayout *layout,
                                const NtTextEffect *effect, EffectWork *work);
void nt_text_effect_release(const NtAllocator *allocator, EffectWork *work);
NtResult nt_text_effect_sample(const EffectWork *work, const EffectEntry *entry,
                               NtTimeUs elapsed, NtMotionPolicy motion, bool trail, NtTextPose *pose);
NtResult nt_text_effect_color(NtCellStyle *color, const NtTextPose *pose, double opacity);

#endif
