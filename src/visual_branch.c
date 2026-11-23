#include "particle_internal.h"

#include "line_raster.h"
#include "seed_internal.h"
#include "wide_product_internal.h"

typedef struct BranchPoint { int64_t x, y; } BranchPoint;
typedef struct BranchLine { BranchPoint start, end; uint32_t steps; } BranchLine;

NtVisualBranchOptions nt_visual_branch_options_default(void)
{
    return (NtVisualBranchOptions){.start={2,4},.end={29,4},.trunk_segments=7,
        .branch_count=4,.branch_segments=3,.jitter_cells=1,.branch_reach_cells=4,
        .lifetime_us=380000,.reveal_us=8000,.flicker_us=45000,
        .color={178,224,255,255},.flicker_min_alpha=135};
}

static NtResult validate_options(const NtVisualBranchOptions *options, NtMotionPolicy motion, uint32_t *total)
{
    if (!options || !options->lifetime_us || !options->trunk_segments ||
        (options->branch_count &&
         (options->trunk_segments < 2 || !options->branch_segments || !options->branch_reach_cells)) ||
        (motion != NT_MOTION_FULL && motion != NT_MOTION_REDUCED)) return NT_RESULT_INVALID_ARGUMENT;
    const uint64_t count = options->trunk_segments + (uint64_t)options->branch_count * options->branch_segments;
    if (count > NT_VISUAL_BRANCH_MAX_SEGMENTS) return NT_RESULT_OUT_OF_RANGE;
    *total = (uint32_t)count;
    return NT_RESULT_OK;
}

static uint32_t revealed(const NtVisualBranchOptions *options, uint32_t total, NtTimeUs age, NtMotionPolicy motion)
{
    if (!options->reveal_us || motion == NT_MOTION_REDUCED) return total;
    const uint64_t index = age/options->reveal_us;
    return index >= total-1 ? total : (uint32_t)index+1;
}

static bool vertical_line(BranchLine line)
{
    return nt_line_magnitude(line.end.y-line.start.y) > nt_line_magnitude(line.end.x-line.start.x);
}

static int64_t interpolate_cell(int64_t a, int64_t b, uint32_t index, uint32_t steps)
{
    const int64_t delta = (b-a)*index;
    return a + (delta < 0 ? (delta-(int64_t)steps/2)/steps : (delta+steps/2)/steps);
}

static uint64_t branch_stream(const NtVisualBranchOptions *options)
{
    return nt_seed_bits(options->seed,options->stream,0);
}

static BranchPoint vertex(const NtVisualBranchOptions *options, BranchLine line, uint32_t path, uint32_t index)
{
    if (!index) return line.start;
    if (index == line.steps) return line.end;
    const int64_t radius = options->jitter_cells;
    const uint64_t draw = nt_seed_bits(branch_stream(options),4+path,index);
    const int64_t jitter = (int64_t)(draw%(2*(uint64_t)radius+1))-radius;
    BranchPoint point = {interpolate_cell(line.start.x,line.end.x,index,line.steps),
                         interpolate_cell(line.start.y,line.end.y,index,line.steps)};
    if (vertical_line(line)) point.x += jitter;
    else point.y += jitter;
    return point;
}

static BranchLine split_line(const NtVisualBranchOptions *options, BranchLine trunk, uint32_t branch,
                             uint32_t *attachment)
{
    const uint64_t seed = branch_stream(options);
    *attachment = 1+(uint32_t)(nt_seed_bits(seed,1,branch)%(trunk.steps-1));
    const BranchPoint origin = vertex(options,trunk,0,*attachment);
    const int64_t reach = options->branch_reach_cells;
    const int64_t lateral = nt_seed_bits(seed,2,branch)%2 ? reach : -reach;
    BranchPoint end;
    if (vertical_line(trunk))
        end = (BranchPoint){origin.x+lateral,origin.y+(trunk.end.y < trunk.start.y ? -reach/2 : reach/2)};
    else
        end = (BranchPoint){origin.x+(trunk.end.x < trunk.start.x ? -reach/2 : reach/2),origin.y+lateral};
    return (BranchLine){origin,end,options->branch_segments};
}

static bool cell_point(BranchPoint point, NtCellPoint *out)
{
    if (point.x < INT32_MIN || point.x > INT32_MAX || point.y < INT32_MIN || point.y > INT32_MAX)
        return false;
    *out = (NtCellPoint){(int32_t)point.x,(int32_t)point.y};
    return true;
}

static unsigned fade_alpha(unsigned alpha, NtTimeUs age, NtTimeUs lifetime)
{
    const NtWideProduct value = nt_wide_product(2*alpha,lifetime-age);
    unsigned low = 0, high = alpha+1;
    while (high-low > 1) {
        const unsigned middle = (low+high)/2;
        const NtWideProduct threshold = nt_wide_product(2*middle-1,lifetime);
        if (value.high > threshold.high || (value.high == threshold.high && value.low >= threshold.low))
            low = middle;
        else high = middle;
    }
    return low;
}

NtResult nt_visual_branch_sample(const NtVisualBranchOptions *options, NtTimeUs now, NtMotionPolicy motion,
                                 NtVisualBranchSegment *segments, size_t capacity, size_t *count)
{
    if (!count || (!segments && capacity)) return NT_RESULT_INVALID_ARGUMENT;
    uint32_t total;
    const NtResult valid = validate_options(options,motion,&total);
    if (valid != NT_RESULT_OK) return valid;
    if (now < options->start_us || now-options->start_us >= options->lifetime_us) {
        *count = 0;
        return NT_RESULT_OK;
    }
    const NtTimeUs age = now-options->start_us;
    const uint32_t length = revealed(options,total,age,motion);
    const bool full = motion == NT_MOTION_FULL;
    const unsigned alpha = full ? fade_alpha(options->color.alpha,age,options->lifetime_us) : options->color.alpha;
    const bool flicker = full && options->flicker_us;
    const uint64_t flicker_key = flicker ? nt_seed_bits(branch_stream(options),3,age/options->flicker_us) : 0;
    const BranchLine trunk = {{options->start.x,options->start.y},{options->end.x,options->end.y},options->trunk_segments};
    BranchLine line = trunk;
    uint32_t path = 0, index = 0, parent = UINT32_MAX;
    NtVisualBranchSegment staged[NT_VISUAL_BRANCH_MAX_SEGMENTS];
    for (uint32_t id = 0; id < length; ++id) {
        if (index == line.steps) {
            uint32_t attachment;
            line = split_line(options,trunk,path,&attachment);
            ++path;
            index = 0;
            parent = attachment-1;
        }
        NtVisualBranchSegment segment = {.id=id,.parent_id=parent,.color=options->color};
        if (!cell_point(vertex(options,line,path,index),&segment.start) ||
            !cell_point(vertex(options,line,path,index+1),&segment.end)) return NT_RESULT_OUT_OF_RANGE;
        const unsigned minimum = options->flicker_min_alpha;
        const unsigned multiplier = flicker ? minimum + nt_seed_bits(flicker_key,0,id)%(256-minimum) : 255;
        segment.color.alpha = (uint8_t)((alpha*multiplier+127)/255);
        staged[id] = segment;
        parent = id;
        ++index;
    }
    *count = length;
    if (!segments) return NT_RESULT_OK;
    if (capacity < length) return NT_RESULT_INSUFFICIENT_BUFFER;
    for (uint32_t i = 0; i < length; ++i) segments[i] = staged[i];
    return NT_RESULT_OK;
}

static void schedule_after(NtDeadline *deadline, NtTimeUs now, NtTimeUs delay)
{
    if (delay <= UINT64_MAX-now && (!deadline->scheduled || now+delay < deadline->at))
        *deadline = (NtDeadline){true,now+delay};
}

NtResult nt_visual_branch_deadline(const NtVisualBranchOptions *options, NtTimeUs now, NtMotionPolicy motion,
                                   NtTimeUs interval, NtDeadline *out)
{
    if (!out) return NT_RESULT_INVALID_ARGUMENT;
    uint32_t total;
    const NtResult valid = validate_options(options,motion,&total);
    if (valid != NT_RESULT_OK) return valid;
    NtDeadline next = {0};
    if (now < options->start_us) next = (NtDeadline){true,options->start_us};
    else {
        const NtTimeUs age = now-options->start_us;
        if (age < options->lifetime_us) {
            schedule_after(&next,now,options->lifetime_us-age);
            if (motion == NT_MOTION_FULL) {
                schedule_after(&next,now,interval ? interval : 16000);
                if (revealed(options,total,age,motion) < total)
                    schedule_after(&next,now,options->reveal_us-age%options->reveal_us);
                if (options->flicker_us)
                    schedule_after(&next,now,options->flicker_us-age%options->flicker_us);
            }
        }
    }
    *out = next;
    return NT_RESULT_OK;
}

typedef struct BranchPaint {
    NtPaintContext *paint;
    NtCellRect clip;
    NtVisualBranchSegment segment;
    NtByteView glyph;
    bool shared_start;
} BranchPaint;

static NtResult paint_cell(const void *opaque, int64_t x, int64_t y)
{
    const BranchPaint *draw = opaque;
    const NtVisualBranchSegment segment = draw->segment;
    if (draw->shared_start && x == segment.start.x && y == segment.start.y) return NT_RESULT_OK;
    if (x < draw->clip.origin.x || y < draw->clip.origin.y ||
        x >= (int64_t)draw->clip.origin.x+draw->clip.size.columns ||
        y >= (int64_t)draw->clip.origin.y+draw->clip.size.rows) return NT_RESULT_OK;
    return nt_paint_glyph(draw->paint,(NtCellPoint){(int32_t)x,(int32_t)y},draw->glyph,1,
                         (NtCellStyle){.foreground=segment.color});
}

NtResult nt_visual_branch_draw(const NtVisualBranchOptions *options, NtPaintContext *paint, NtTimeUs now,
                               NtMotionPolicy motion, NtByteView glyph, NtWidthPolicy width)
{
    if (!paint) return NT_RESULT_INVALID_ARGUMENT;
    NtResult result = nt_particle_glyph_validate(glyph,width);
    if (result != NT_RESULT_OK) return result;
    NtVisualBranchSegment segments[NT_VISUAL_BRANCH_MAX_SEGMENTS];
    size_t count;
    result = nt_visual_branch_sample(options,now,motion,segments,NT_VISUAL_BRANCH_MAX_SEGMENTS,&count);
    if (result != NT_RESULT_OK) return result;
    const NtCellRect clip = nt_paint_clip(paint);
    if (!clip.size.columns || !clip.size.rows) return NT_RESULT_OK;
    for (size_t i = 0; i < count; ++i) {
        const NtVisualBranchSegment segment = segments[i];
        if (!segment.color.alpha) continue;
        const bool shared_start = segment.parent_id != UINT32_MAX && segments[segment.parent_id].color.alpha;
        const BranchPaint draw = {paint,clip,segment,glyph,shared_start};
        result = nt_line_raster(segment.start,segment.end,clip,paint_cell,&draw);
        if (result != NT_RESULT_OK) return result;
    }
    return NT_RESULT_OK;
}
