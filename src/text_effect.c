#include "allocator.h"
#include "animation_internal.h"
#include "text_effect_internal.h"
#include "text_layout_internal.h"

#include <math.h>
#include <string.h>

NT_DECLARE_ALIGNMENT(EffectEntry);
NT_DECLARE_ALIGNMENT(EffectGlyph);

static bool unit(double value)
{
    return isfinite(value) && value >= 0 && value <= 1;
}

static bool point_finite(NtPoint value)
{
    return isfinite(value.x) && isfinite(value.y);
}

static bool pose_valid(const NtTextPose *pose)
{
    return pose && point_finite(pose->offset) && isfinite(pose->hue_degrees) && unit(pose->opacity);
}

static bool bytes_equal(NtByteView a, NtByteView b)
{
    return a.count == b.count && (!a.count || (a.data && b.data && !memcmp(a.data,b.data,a.count)));
}

static NtResult renderable(NtByteView glyph, NtWidthPolicy width, uint8_t *columns)
{
    NtGlyphMetrics metrics;
    const NtResult result = nt_glyph_measure(glyph,width,&metrics);
    if (result != NT_RESULT_OK) return result;
    if ((metrics.presentation != NT_GLYPH_TEXT && metrics.presentation != NT_GLYPH_EMOJI) ||
        !metrics.columns || metrics.columns > 2) return NT_RESULT_INVALID_ARGUMENT;
    *columns = metrics.columns;
    return NT_RESULT_OK;
}

static NtResult sample_random(uint64_t seed, uint64_t identity, NtTimeUs at, NtTimeUs interval,
                              bool smooth, double low, double high, double *out)
{
    const NtRandomTrack track = {low,high,interval,seed,identity,smooth};
    return nt_random_track_sample(&track,at,out);
}

NtResult nt_text_pose_motion(const NtTextEffectSample *sample, const NtTextMotion *motion, NtTextPose *pose)
{
    if (!sample || !motion || !pose_valid(pose) || !unit(sample->progress) ||
        !point_finite(motion->travel) || !point_finite(motion->orbit) ||
        !isfinite(motion->phase_turns) || !isfinite(motion->cycles) ||
        !isfinite(motion->rank_turns) || !isfinite(motion->hue_degrees))
        return NT_RESULT_INVALID_ARGUMENT;
    double weight;
    if (nt_ease(motion->easing,sample->travel,&weight) != NT_OK) return NT_RESULT_INVALID_ARGUMENT;
    double phase = motion->phase_turns + motion->cycles * sample->progress +
        motion->rank_turns * (double)sample->rank;
    if (motion->seeded_phase) {
        double shift;
        const NtResult result = sample_random(sample->seed,sample->identity,0,1,false,0,1,&shift);
        if (result != NT_RESULT_OK) return result;
        phase += shift;
    }
    if (!isfinite(phase)) return NT_RESULT_OUT_OF_RANGE;
    const double angle = fmod(phase,1) * 6.28318530717958647692;
    NtTextPose next = *pose;
    next.offset.x += weight * motion->travel.x + weight * cos(angle) * motion->orbit.x;
    next.offset.y += weight * motion->travel.y + weight * sin(angle) * motion->orbit.y;
    next.hue_degrees += weight * motion->hue_degrees;
    if (!pose_valid(&next)) return NT_RESULT_OUT_OF_RANGE;
    *pose = next;
    return NT_RESULT_OK;
}

NtResult nt_text_pose_noise(const NtTextEffectSample *sample, NtPoint extent,
                            NtTimeUs interval_us, bool smooth, NtTextPose *pose)
{
    if (!sample || !pose_valid(pose) || !unit(sample->travel) || !point_finite(extent) ||
        extent.x < 0 || extent.y < 0 || !interval_us) return NT_RESULT_INVALID_ARGUMENT;
    NtPoint noise;
    NtResult result = sample_random(sample->seed,sample->identity,sample->local_us,interval_us,
                                    smooth,-1,1,&noise.x);
    if (result == NT_RESULT_OK)
        result = sample_random(sample->seed ^ UINT64_C(0x7266686578),sample->identity,sample->local_us,
                               interval_us,smooth,-1,1,&noise.y);
    if (result != NT_RESULT_OK) return result;
    NtTextPose next = *pose;
    next.offset.x += sample->travel * noise.x * extent.x;
    next.offset.y += sample->travel * noise.y * extent.y;
    if (!pose_valid(&next)) return NT_RESULT_OUT_OF_RANGE;
    *pose = next;
    return NT_RESULT_OK;
}

static int compare_visual(const void *left, const void *right)
{
    const EffectEntry *a = left, *b = right;
    return a->visual < b->visual ? -1 : a->visual > b->visual;
}

static int compare_logical(const void *left, const void *right)
{
    const EffectEntry *a = left, *b = right;
    if (a->cluster.source.begin != b->cluster.source.begin)
        return a->cluster.source.begin < b->cluster.source.begin ? -1 : 1;
    return compare_visual(left,right);
}

static NtResult prepare_entries(const NtAllocator *allocator, const NtTextLayout *layout,
                                const NtTextEffect *effect, EffectWork *work)
{
    const size_t count = nt_text_layout_cluster_count(layout);
    if (!count) return NT_RESULT_OK;
    if (count > SIZE_MAX / sizeof(EffectEntry)) return NT_RESULT_OUT_OF_RANGE;
    EffectEntry *entries = nt_allocate(allocator,count*sizeof(*entries),NT_ALIGNMENT(EffectEntry));
    if (!entries) return NT_RESULT_ALLOCATION_FAILED;
    work->entries = entries;
    work->count = count;
    const NtByteView source = nt_text_layout_source(layout);
    for (size_t i = 0; i < count; ++i) {
        NtClusterView cluster;
        const NtResult result = nt_text_layout_cluster(layout,i,&cluster);
        if (result != NT_RESULT_OK) return result;
        entries[i] = (EffectEntry){.visual=i,.cluster=cluster,
            .identity=cluster.id ? cluster.id : UINT64_MAX-(uint64_t)i,
            .tab=cluster.source.end-cluster.source.begin == 1 && source.data[cluster.source.begin] == '\t'};
    }
    qsort(entries,count,sizeof(*entries),compare_logical);
    for (size_t i = 0; i < count; ++i) entries[i].logical = i;
    qsort(entries,count,sizeof(*entries),compare_visual);
    work->first_start = UINT64_MAX;
    for (size_t first = 0; first < count;) {
        size_t end = first + 1;
        while (end < count && entries[end].cluster.position.y == entries[first].cluster.position.y) ++end;
        const int32_t left = entries[first].cluster.position.x;
        const NtClusterView last = entries[end-1].cluster;
        const int32_t right = last.position.x + (int32_t)last.columns;
        for (size_t i = first; i < end; ++i) {
            EffectEntry *entry = &entries[i];
            entry->line_begin = left;
            entry->line_end = right;
            switch (effect->order) {
                case NT_TEXT_EFFECT_LOGICAL: entry->rank = entry->logical; break;
                case NT_TEXT_EFFECT_REVERSE_LOGICAL: entry->rank = count-1-entry->logical; break;
                case NT_TEXT_EFFECT_VISUAL: entry->rank = i; break;
                case NT_TEXT_EFFECT_REVERSE_VISUAL: entry->rank = count-1-i; break;
            }
            NtTrackSlot slot;
            const NtResult result = nt_track_schedule(&effect->schedule,(uint64_t)entry->rank,
                                                       entry->identity,effect->duration_us,&slot);
            if (result != NT_RESULT_OK) return result;
            if (slot.delay_us > UINT64_MAX-effect->delay_us) return NT_RESULT_OUT_OF_RANGE;
            entry->start = effect->delay_us + slot.delay_us;
            entry->duration = slot.span_us;
            if (entry->duration > UINT64_MAX-entry->start) return NT_RESULT_OUT_OF_RANGE;
            if (entry->duration) {
                if (entry->start < work->first_start) work->first_start = entry->start;
                if (entry->start+entry->duration > work->last_end) work->last_end = entry->start+entry->duration;
            }
        }
        first = end;
    }
    return NT_RESULT_OK;
}

NtResult nt_text_effect_prepare(const NtAllocator *allocator, const NtTextLayout *layout,
                                const NtTextEffect *effect, EffectWork *work)
{
    *work = (EffectWork){.effect=effect,.width=nt_text_layout_width_policy(layout)};
    const NtTextGlyphEffect glyphs = effect->glyphs;
    if (effect->order < NT_TEXT_EFFECT_LOGICAL || effect->order > NT_TEXT_EFFECT_REVERSE_VISUAL ||
        effect->visibility < NT_TEXT_REVEAL || effect->visibility > NT_TEXT_VISIBLE ||
        effect->opacity_easing < NT_LINEAR || effect->opacity_easing > NT_STEP_END ||
        glyphs.kind < NT_TEXT_ORIGINAL_GLYPHS || glyphs.kind > NT_TEXT_FLIPBOARD ||
        !unit(effect->trails.decay) || (effect->trails.count && !effect->trails.spacing_us))
        return NT_RESULT_INVALID_ARGUMENT;
    if (glyphs.kind != NT_TEXT_ORIGINAL_GLYPHS) {
        if (!glyphs.alphabet || !glyphs.count || !glyphs.interval_us) return NT_RESULT_INVALID_ARGUMENT;
        if (glyphs.count > SIZE_MAX / sizeof(EffectGlyph)) return NT_RESULT_OUT_OF_RANGE;
        EffectGlyph *alphabet = nt_allocate(allocator,glyphs.count*sizeof(*alphabet),NT_ALIGNMENT(EffectGlyph));
        if (!alphabet) return NT_RESULT_ALLOCATION_FAILED;
        work->glyphs = alphabet;
        work->glyph_count = glyphs.count;
        for (size_t i = 0; i < glyphs.count; ++i) {
            uint8_t columns;
            const NtResult result = renderable(glyphs.alphabet[i],work->width,&columns);
            if (result != NT_RESULT_OK) return result;
            alphabet[i] = (EffectGlyph){glyphs.alphabet[i],columns};
            ++work->widths[columns];
        }
    }
    return prepare_entries(allocator,layout,effect,work);
}

void nt_text_effect_release(const NtAllocator *allocator, EffectWork *work)
{
    nt_release(allocator,work->glyphs,work->glyph_count*sizeof(EffectGlyph),NT_ALIGNMENT(EffectGlyph));
    nt_release(allocator,work->entries,work->count*sizeof(EffectEntry),NT_ALIGNMENT(EffectEntry));
}

static size_t alphabet_index(const EffectWork *work, unsigned columns, NtByteView original)
{
    size_t index = 0;
    for (size_t i = 0; i < work->glyph_count; ++i) {
        if (work->glyphs[i].columns != columns) continue;
        if (bytes_equal(original,work->glyphs[i].glyph)) return index;
        ++index;
    }
    return index;
}

typedef struct FractionProduct { uint64_t whole, remainder; } FractionProduct;

/* Return the quotient and remainder of numerator * factor / denominator.
 * Numerator is below denominator, so the quotient fits even if the product
 * does not. The wide path doubles the quotient and residue one bit at a time. */
static FractionProduct scale_fraction(uint64_t numerator, uint64_t factor, uint64_t denominator)
{
    if (!factor || numerator <= UINT64_MAX / factor) {
        const uint64_t product = numerator * factor;
        return (FractionProduct){product / denominator,product % denominator};
    }
    FractionProduct product = {0};
    for (uint64_t bit = UINT64_C(1) << 63; bit; bit >>= 1) {
        product.whole *= 2;
        if (product.remainder >= denominator-product.remainder) {
            product.remainder -= denominator-product.remainder;
            ++product.whole;
        } else {
            product.remainder *= 2;
        }
        if (!(factor & bit)) continue;
        if (product.remainder >= denominator-numerator) {
            product.remainder -= denominator-numerator;
            ++product.whole;
        } else {
            product.remainder += numerator;
        }
    }
    return product;
}

static size_t rotation_steps(NtTimeUs tick, NtTimeUs duration, uint32_t rotations,
                             size_t count, size_t distance)
{
    /* Drop whole rotations before scaling the fractional rotation by the
     * alphabet width. Add the quantized forward distance on the same clock. */
    const uint64_t phase = scale_fraction(tick,rotations,duration).remainder;
    const FractionProduct rotation = scale_fraction(phase,(uint64_t)count,duration);
    const FractionProduct forward = scale_fraction(tick,(uint64_t)distance,duration);
    const bool carry = rotation.remainder >= duration-forward.remainder;
    return (size_t)((rotation.whole+forward.whole+carry) % (uint64_t)count);
}

static NtResult substitute(const EffectWork *work, const NtTextEffectSample *sample, NtTextPose *pose)
{
    const unsigned columns = sample->cluster.columns;
    if (!work->glyphs || columns > 2 || !work->widths[columns]) return NT_RESULT_OK;
    const NtTextGlyphEffect glyphs = work->effect->glyphs;
    const size_t count = work->widths[columns];
    const bool scramble = glyphs.kind == NT_TEXT_SCRAMBLE;
    double random;
    const NtResult result = sample_random(sample->seed,sample->identity,scramble ? sample->local_us : 0,
                                          glyphs.interval_us,false,0,1,&random);
    if (result != NT_RESULT_OK) return result;
    size_t index = (size_t)(random * (double)count);
    if (index >= count) index = count-1;
    if (!scramble) {
        size_t steps;
        if (glyphs.rotations) {
            const size_t target = alphabet_index(work,columns,pose->glyph);
            const size_t distance = target == count ? count : (target+count-index)%count;
            const NtTimeUs tick = sample->local_us - sample->local_us % glyphs.interval_us;
            steps = rotation_steps(tick,sample->duration_us,glyphs.rotations,count,distance);
        } else {
            steps = (size_t)((sample->local_us / glyphs.interval_us) % (uint64_t)count);
        }
        index = (index+steps) % count;
    }
    for (size_t i = 0; i < work->glyph_count; ++i) {
        if (work->glyphs[i].columns != columns) continue;
        if (index == 0) {
            pose->glyph = work->glyphs[i].glyph;
            break;
        }
        --index;
    }
    return NT_RESULT_OK;
}

NtResult nt_text_effect_sample(const EffectWork *work, const EffectEntry *entry,
                               NtTimeUs elapsed, NtMotionPolicy motion, bool trail, NtTextPose *pose)
{
    const NtTextEffect *effect = work->effect;
    NtAnimationSample clock;
    const NtTimeUs local = nt_animation_timing(elapsed,entry->start,entry->duration,NT_ONCE,&clock);
    if (motion == NT_MOTION_REDUCED) {
        clock.phase = NT_FINISHED;
        clock.progress = 1;
    }
    *pose = (NtTextPose){.glyph=entry->cluster.glyph,.opacity=1};
    if (trail && clock.phase != NT_RUNNING) {
        pose->opacity = 0;
        return NT_RESULT_OK;
    }
    const bool hide = effect->visibility == NT_TEXT_HIDE;
    if (effect->visibility != NT_TEXT_VISIBLE) {
        double alpha;
        if (nt_ease(effect->opacity_easing,clock.progress,&alpha) != NT_OK)
            return NT_RESULT_INVALID_ARGUMENT;
        pose->opacity = hide ? 1-alpha : alpha;
    }
    if (clock.phase != NT_RUNNING) return NT_RESULT_OK;
    const NtTextEffectSample sample = {.cluster=entry->cluster,.visual_index=entry->visual,
        .logical_index=entry->logical,.rank=entry->rank,.count=work->count,
        .line_begin=entry->line_begin,.line_end=entry->line_end,.elapsed_us=elapsed,
        .local_us=local,.duration_us=entry->duration,.progress=clock.progress,
        .travel=hide ? clock.progress : nt_animation_progress(entry->duration-local,entry->duration),
        .seed=effect->seed,.identity=entry->identity};
    NtResult result = entry->tab ? NT_RESULT_OK : substitute(work,&sample,pose);
    if (result == NT_RESULT_OK && effect->transform) result = effect->transform(effect->context,&sample,pose);
    if (result != NT_RESULT_OK) return result;
    if (!pose_valid(pose)) return NT_RESULT_INVALID_ARGUMENT;
    if (!bytes_equal(pose->glyph,entry->cluster.glyph)) {
        uint8_t columns;
        if (entry->tab || renderable(pose->glyph,work->width,&columns) != NT_RESULT_OK ||
            columns != entry->cluster.columns) return NT_RESULT_INVALID_ARGUMENT;
    }
    return NT_RESULT_OK;
}

NtResult nt_text_effect_color(NtCellStyle *color, const NtTextPose *pose, double opacity)
{
    if (pose->hue_degrees && color->foreground.alpha) {
        NtColor hsl;
        NtResult result = nt_color_convert(nt_color_from_rgba(color->foreground),NT_COLOR_HSL,NT_GAMUT_CLIP,&hsl);
        if (result != NT_RESULT_OK) return result;
        hsl.channel[0] += fmod(pose->hue_degrees,360);
        result = nt_color_to_rgba(hsl,NT_GAMUT_CLIP,&color->foreground);
        if (result != NT_RESULT_OK) return result;
    }
    color->foreground.alpha = (uint8_t)round(color->foreground.alpha*opacity);
    color->background.alpha = (uint8_t)round(color->background.alpha*opacity);
    return NT_RESULT_OK;
}
