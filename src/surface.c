#include "surface_internal.h"

#include "allocator.h"
#include "color_internal.h"
#include "frame_internal.h"
#include "fp_environment_internal.h"
#include "panel_transform_internal.h"
#include <string.h>

#if defined(__clang__) && !defined(__wasm__)
#pragma STDC FENV_ACCESS ON
#elif defined(_MSC_VER)
#pragma fenv_access(on)
#endif

static int64_t right(NtCellRect rect)
{
    return (int64_t)rect.origin.x + rect.size.columns;
}

static int64_t bottom(NtCellRect rect)
{
    return (int64_t)rect.origin.y + rect.size.rows;
}

static bool rect_contains(NtCellRect rect, int64_t x, int64_t y)
{
    return x >= rect.origin.x && x < right(rect) && y >= rect.origin.y && y < bottom(rect);
}

static NtCellRect intersect(NtCellRect a, NtCellRect b)
{
    const int32_t x = a.origin.x > b.origin.x ? a.origin.x : b.origin.x;
    const int32_t y = a.origin.y > b.origin.y ? a.origin.y : b.origin.y;
    const int64_t end_x = right(a) < right(b) ? right(a) : right(b);
    const int64_t end_y = bottom(a) < bottom(b) ? bottom(a) : bottom(b);
    if (end_x <= x || end_y <= y) return (NtCellRect){0};
    return (NtCellRect){{x, y}, {(uint32_t)(end_x - x), (uint32_t)(end_y - y)}};
}

static bool translate(NtCellPoint origin, NtCellPoint local, NtCellPoint *out)
{
    const int64_t x = (int64_t)origin.x + local.x, y = (int64_t)origin.y + local.y;
    if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX) return false;
    *out = (NtCellPoint){(int32_t)x, (int32_t)y};
    return true;
}

static NtCellRect frame_bounds(const NtFrame *frame)
{
    return (NtCellRect){{0, 0}, nt_frame_size(frame)};
}

static NtResult mask_valid(const NtSurfaceMask *mask)
{
    if (mask->kind < NT_MASK_RECTANGLE || mask->kind > NT_MASK_ALPHA)
        return NT_RESULT_INVALID_ARGUMENT;
    if (mask->kind == NT_MASK_ROUNDED_RECTANGLE && (!isfinite(mask->radius) || mask->radius < 0.0))
        return NT_RESULT_INVALID_ARGUMENT;
    if (mask->kind == NT_MASK_ALPHA && mask->bounds.size.columns && mask->bounds.size.rows) {
        const size_t columns = mask->bounds.size.columns, rows = mask->bounds.size.rows;
        if (!mask->alpha || mask->stride < columns) return NT_RESULT_INVALID_ARGUMENT;
        if (rows - 1 > (SIZE_MAX - columns) / mask->stride) return NT_RESULT_OUT_OF_RANGE;
        if ((rows - 1) * mask->stride + columns > mask->count) return NT_RESULT_INSUFFICIENT_BUFFER;
    }
    return NT_RESULT_OK;
}

static uint8_t mask_alpha(const NtSurfaceMask *mask, int64_t x, int64_t y)
{
    const NtCellRect bounds = mask->bounds;
    if (!rect_contains(bounds, x, y)) return 0;
    if (mask->kind == NT_MASK_RECTANGLE) return 255;
    const size_t col = (size_t)(x - bounds.origin.x), row = (size_t)(y - bounds.origin.y);
    if (mask->kind == NT_MASK_ALPHA) return mask->alpha[row * mask->stride + col];

    if (mask->kind == NT_MASK_DIAMOND) {
        const uint64_t width = bounds.size.columns, height = bounds.size.rows;
        const uint64_t px = 2 * (uint64_t)col + 1, py = 2 * (uint64_t)row + 1;
        const uint64_t dx = px > width ? px - width : width - px;
        const uint64_t dy = py > height ? py - height : height - py;
        return dx * height <= (height - dy) * width ? 255 : 0;
    }
    const double half_width = bounds.size.columns * 0.5, half_height = bounds.size.rows * 0.5;
    const double dx = fabs((double)col + 0.5 - half_width);
    const double dy = fabs((double)row + 0.5 - half_height);
    if (mask->kind == NT_MASK_ROUNDED_RECTANGLE) {
        const double radius = fmin(mask->radius, fmin(half_width, half_height));
        const double edge_x = fmax(0.0, dx - (half_width - radius));
        const double edge_y = fmax(0.0, dy - (half_height - radius));
        return hypot(edge_x, edge_y) <= radius ? 255 : 0;
    }
    const double u = dx / half_width, v = dy / half_height;
    return u*u + v*v <= 1.0 ? 255 : 0;
}

NtResult nt_surface_root(NtFrame *frame, NtSurfaceView *out)
{
    if (!frame || !out) return NT_RESULT_INVALID_ARGUMENT;
    const NtCellRect bounds = frame_bounds(frame);
    *out = (NtSurfaceView){.frame = frame, .bounds = bounds, .clip = bounds};
    return NT_RESULT_OK;
}

NtResult nt_surface_child(const NtSurfaceView *parent, NtCellRect placement,
                          const NtSurfaceMask *mask, NtSurfaceView *out)
{
    if (!parent || !parent->frame || !out) return NT_RESULT_INVALID_ARGUMENT;
    for (const NtSurfaceView *ancestor = parent; ancestor; ancestor = ancestor->parent) {
        if (ancestor == out) return NT_RESULT_INVALID_ARGUMENT;
    }
    NtSurfaceView view = {.frame = parent->frame, .parent = parent, .bounds = placement};
    if (!translate(parent->bounds.origin, placement.origin, &view.bounds.origin))
        return NT_RESULT_OUT_OF_RANGE;
    view.clip = intersect(parent->clip, view.bounds);
    if (mask) {
        const NtResult valid = mask_valid(mask);
        if (valid != NT_RESULT_OK) return valid;
        NtSurfaceMask translated = *mask;
        if (!translate(view.bounds.origin, mask->bounds.origin, &translated.bounds.origin))
            return NT_RESULT_OUT_OF_RANGE;
        const NtResult result = nt_surface_attach_mask(&view, &translated);
        if (result != NT_RESULT_OK) return result;
    }
    *out = view;
    return NT_RESULT_OK;
}

NtResult nt_surface_attach_mask(NtSurfaceView *view, const NtSurfaceMask *mask)
{
    const NtResult result = mask_valid(mask);
    if (result != NT_RESULT_OK) return result;
    view->masked = true;
    view->mask = *mask;
    view->clip = intersect(view->clip, mask->bounds);
    return NT_RESULT_OK;
}

NtResult nt_surface_limit(NtSurfaceView *view, NtCellPoint parent_origin, NtCellRect clip)
{
    if (!translate(parent_origin, clip.origin, &clip.origin)) return NT_RESULT_OUT_OF_RANGE;
    view->clip = intersect(view->clip, clip);
    return NT_RESULT_OK;
}

static NtCellRect effective_clip(const NtSurfaceView *view)
{
    return intersect(view->clip, frame_bounds(view->frame));
}

NtResult nt_surface_clip(const NtSurfaceView *view, NtCellRect *out_frame_bounds)
{
    if (!view || !view->frame || !out_frame_bounds) return NT_RESULT_INVALID_ARGUMENT;
    *out_frame_bounds = effective_clip(view);
    return NT_RESULT_OK;
}

typedef struct MaskCoverage {
    double weight;
    bool visible;
} MaskCoverage;

static MaskCoverage coverage(const NtSurfaceView *view, NtCellRect clip, int64_t x, int64_t y)
{
    if (!rect_contains(clip, x, y)) return (MaskCoverage){0};
    double weight = 1.0;
    for (const NtSurfaceView *ancestor = view; ancestor; ancestor = ancestor->parent) {
        if (!ancestor->masked) continue;
        const uint8_t alpha = mask_alpha(&ancestor->mask, x, y);
        if (!alpha) return (MaskCoverage){0};
        weight *= alpha / 255.0;
    }
    return (MaskCoverage){weight, true};
}

bool nt_surface_contains_frame(const NtSurfaceView *view, NtCellPoint position)
{
    return coverage(view, effective_clip(view), position.x, position.y).visible;
}

NtResult nt_surface_coverage(const NtSurfaceView *view, NtCellPoint local, double *out)
{
    if (!view || !view->frame || !out) return NT_RESULT_INVALID_ARGUMENT;
    NtCellPoint position;
    if (!translate(view->bounds.origin, local, &position)) return NT_RESULT_OUT_OF_RANGE;
    const MaskCoverage sample = coverage(view, effective_clip(view), position.x, position.y);
    *out = sample.visible && sample.weight == 0.0 ? nextafter(0.0, 1.0) : sample.weight;
    return NT_RESULT_OK;
}

typedef struct PaintSpan {
    const NtSurfaceView *view;
    NtCellPoint lead;
    uint8_t columns;
    double opacity, remap_opacity, weight;
} PaintSpan;

/* Beyond this many non-full masks 255*(254/255)^n is already below 0.5.
 * The space also covers the binary divisors of both opacity factors. */
enum { ALPHA_MASK_LIMIT = 2048, ALPHA_LIMBS = (8 * ALPHA_MASK_LIMIT + 2400) / 32 };
typedef struct AlphaInteger {
    uint32_t digits[ALPHA_LIMBS];
    size_t length;
} AlphaInteger;

static void alpha_multiply(AlphaInteger *integer, uint32_t scale)
{
    uint64_t carry = 0;
    for (size_t i = 0; i < integer->length; ++i) {
        const uint64_t product = (uint64_t)integer->digits[i] * scale + carry;
        integer->digits[i] = (uint32_t)product;
        carry = product >> 32;
    }
    if (carry) integer->digits[integer->length++] = (uint32_t)carry;
}

static void alpha_shift(AlphaInteger *integer, unsigned bits)
{
    const size_t words = bits / 32;
    memmove(integer->digits + words, integer->digits, integer->length * sizeof(uint32_t));
    memset(integer->digits, 0, words * sizeof(uint32_t));
    integer->length += words;
    alpha_multiply(integer, UINT32_C(1) << (bits % 32));
}

static int alpha_order(const AlphaInteger *a, const AlphaInteger *b)
{
    if (a->length != b->length) return a->length > b->length ? 1 : -1;
    for (size_t i = a->length; i-- > 0;) {
        if (a->digits[i] != b->digits[i]) return a->digits[i] > b->digits[i] ? 1 : -1;
    }
    return 0;
}

static void alpha_opacity(PaintSpan paint, AlphaInteger *out, unsigned *divisor_bits)
{
    int pose_exponent;
    const uint64_t pose = (uint64_t)ldexp(frexp(paint.opacity,&pose_exponent),53);
    if (paint.remap_opacity == 1) {
        *out = (AlphaInteger){{(uint32_t)pose,(uint32_t)(pose >> 32)},2};
        *divisor_bits = (unsigned)(53-pose_exponent);
        return;
    }
    int remap_exponent;
    const uint64_t remap = (uint64_t)ldexp(frexp(paint.remap_opacity,&remap_exponent),53);
    /* Split the two 53-bit factors into limbs. The middle and high sums
     * fit in uint64_t because the upper limbs hold at most 21 bits. */
    const uint64_t a0 = (uint32_t)pose, a1 = pose >> 32;
    const uint64_t b0 = (uint32_t)remap, b1 = remap >> 32;
    const uint64_t low = a0*b0;
    const uint64_t middle = a1*b0+a0*b1+(low >> 32);
    const uint64_t high = a1*b1+(middle >> 32);
    *out = (AlphaInteger){{(uint32_t)low,(uint32_t)middle,(uint32_t)high,(uint32_t)(high >> 32)},4};
    *divisor_bits = (unsigned)(106-pose_exponent-remap_exponent);
}

static int alpha_cell_order(PaintSpan paint, unsigned offset, uint8_t source,
                            uint32_t limit_numerator, uint32_t limit_denominator)
{
    if (!paint.opacity || !paint.remap_opacity || !source) return -1;
    unsigned divisor_bits;
    AlphaInteger numerator;
    alpha_opacity(paint,&numerator,&divisor_bits);
    AlphaInteger denominator = {{limit_numerator}, 1};
    alpha_multiply(&numerator, source * limit_denominator);
    unsigned factors = 0;
    for (const NtSurfaceView *view = paint.view; view; view = view->parent) {
        if (!view->masked) continue;
        const uint8_t byte = mask_alpha(&view->mask, (int64_t)paint.lead.x + offset, paint.lead.y);
        if (!byte) return -1;
        if (byte == 255) continue;
        if (++factors > ALPHA_MASK_LIMIT) return -1;
        alpha_multiply(&numerator, byte);
        alpha_multiply(&denominator, 255);
    }
    alpha_shift(&denominator,divisor_bits);
    return alpha_order(&numerator, &denominator);
}

static int compare_source_alpha(PaintSpan paint, uint8_t source,
                                uint32_t limit_numerator, uint32_t limit_denominator)
{
    int order = 1;
    for (unsigned i = 0; i < paint.columns; ++i) {
        const int cell = alpha_cell_order(paint, i, source, limit_numerator, limit_denominator);
        if (cell < 0) return -1;
        if (!cell) order = 0;
    }
    return order;
}

static bool alpha_at_least(PaintSpan paint, uint8_t source, uint8_t destination, unsigned lower)
{
    if (destination > lower) return true;
    /* Solve D + X*(255-D)/255 >= lower + 1/2 for the source alpha X. */
    return compare_source_alpha(paint, source, (2u * (lower - destination) + 1u) * 255u,
                                2u * (255u - destination)) >= 0;
}

/* This guard bounds floating-point error in the approximate byte product.
 * Close results are decided by integer comparison, including exact ties. */
static const double alpha_guard = 1e-9;

static double source_alpha(NtRgba source, PaintSpan paint)
{
    return source.alpha * paint.weight * paint.opacity * paint.remap_opacity;
}

static bool contributes(NtRgba source, PaintSpan paint)
{
    const double alpha = source_alpha(source, paint);
    return fabs(alpha - 0.5) <= alpha_guard ?
        alpha_at_least(paint, source.alpha, 0, 0) : alpha >= 0.5;
}

static uint8_t round_alpha(double alpha, NtRgba source, NtRgba destination, PaintSpan paint)
{
    const double lower = floor(alpha);
    if (fabs(alpha - lower - 0.5) <= alpha_guard) {
        return (uint8_t)((unsigned)lower +
            alpha_at_least(paint, source.alpha, destination.alpha, (unsigned)lower));
    }
    return (uint8_t)round(alpha);
}

static bool round_linear_up(uint8_t dest, uint8_t src, unsigned lower,
                            uint8_t dest_alpha, uint8_t src_alpha, PaintSpan paint)
{
    /* Compare the linear weighted byte with lower + 1/2. The sign of the
     * coefficient decides the direction of the exact alpha comparison. */
    const int source_side = 2 * ((int)src - (int)lower) - 1;
    const int dest_side = 2 * ((int)dest - (int)lower) - 1;
    const int coefficient = 255 * source_side - dest_alpha * dest_side;
    const int offset = 255 * dest_alpha * dest_side;
    if (!coefficient) return offset >= 0;
    if (coefficient > 0) {
        return offset >= 0 || compare_source_alpha(paint, src_alpha, (uint32_t)-offset,
                                                    (uint32_t)coefficient) >= 0;
    }
    return offset > 0 && compare_source_alpha(paint, src_alpha, (uint32_t)offset,
                                              (uint32_t)-coefficient) <= 0;
}

static uint8_t blend_channel(uint8_t dest, uint8_t src, uint8_t dest_alpha, uint8_t src_alpha,
                             double weight, PaintSpan paint)
{
    if (src <= 10 && dest <= 10) {
        /* Transfer encoding is linear for this entire byte interval. */
        const double byte = dest + ((double)src - dest) * weight;
        const double lower = floor(byte);
        if (fabs(byte - lower - 0.5) <= alpha_guard) {
            return (uint8_t)((unsigned)lower +
                round_linear_up(dest, src, (unsigned)lower, dest_alpha, src_alpha, paint));
        }
        return (uint8_t)round(byte);
    }
    const double base = nt_srgb_decode(dest / 255.0);
    const double top = nt_srgb_decode(src / 255.0);
    return (uint8_t)round(255.0 * nt_srgb_encode(base + (top - base) * weight));
}

static NtRgba source_over(NtRgba destination, NtRgba source, PaintSpan paint)
{
    const double top = source_alpha(source, paint);
    const double alpha = top + destination.alpha * (1.0 - top / 255.0);
    const uint8_t output_alpha = round_alpha(alpha, source, destination, paint);
    if (!destination.alpha || top == 255.0) {
        source.alpha = output_alpha;
        return source;
    }
    const double weight = top / alpha;
    return (NtRgba){
        blend_channel(destination.red, source.red, destination.alpha, source.alpha, weight, paint),
        blend_channel(destination.green, source.green, destination.alpha, source.alpha, weight, paint),
        blend_channel(destination.blue, source.blue, destination.alpha, source.alpha, weight, paint),
        output_alpha
    };
}

static void store_background(FrameCell *cell, NtRgba color, NtOwnerToken owner)
{
    cell->background = (NtCellBackground){color,owner};
    cell->attached_background = false;
}

static void paint_background(FrameCell *cell, NtRgba source, PaintSpan paint, NtOwnerToken owner)
{
    store_background(cell,source_over(cell->background.color,source,paint),owner);
}

NtResult nt_surface_fill(const NtSurfaceView *view, NtCellRect local,
                         NtRgba background, NtOwnerToken owner)
{
    if (!view || !view->frame) return NT_RESULT_INVALID_ARGUMENT;
    NtCellRect area = local;
    if (!translate(view->bounds.origin, local.origin, &area.origin)) return NT_RESULT_OUT_OF_RANGE;
    area = intersect(area, effective_clip(view));
    if (!background.alpha) return NT_RESULT_OK;
    for (int64_t y = area.origin.y; y < bottom(area); ++y) {
        for (int64_t x = area.origin.x; x < right(area); ++x) {
            const NtCellPoint position = {(int32_t)x, (int32_t)y};
            const PaintSpan paint = {view,position,1,1,1,coverage(view,area,x,y).weight};
            if (contributes(background, paint)) {
                const size_t index = nt_frame_index(view->frame, position);
                paint_background(&view->frame->cells[index], background, paint, owner);
            }
        }
    }
    return NT_RESULT_OK;
}

static MaskCoverage footprint_coverage(const NtSurfaceView *view, NtCellRect clip,
                                       NtCellPoint position, uint8_t columns)
{
    const MaskCoverage first = coverage(view, clip, position.x, position.y);
    if (columns == 1 || !first.visible) return first;
    const MaskCoverage second = coverage(view, clip, (int64_t)position.x + 1, position.y);
    return (MaskCoverage){fmin(first.weight, second.weight), second.visible};
}

static double glyph_coverage(const NtSurfaceView *view, NtCellRect clip, const NtFrame *frame,
                             NtCellPoint position, uint8_t columns)
{
    const double weight = footprint_coverage(view, clip, position, columns).weight;
    if (!weight) return 0.0;
    const size_t first = nt_frame_index(frame, position);
    for (unsigned offset = 0; offset < columns; ++offset) {
        const size_t index = first + offset, lead = nt_frame_lead(frame, index);
        const NtCellPoint lead_position = {position.x + (int32_t)offset - (int32_t)(index - lead),
                                           position.y};
        if (frame->cells[lead].columns == 2 &&
            !footprint_coverage(view, clip, lead_position, 2).visible)
            return 0.0;
    }
    return weight;
}

static void paint_glyph(NtFrame *frame, size_t index, NtByteView glyph, char *copy,
                        uint8_t columns, NtRgba foreground, PaintSpan paint, NtOwnerToken owner)
{
    const NtRgba backdrop = frame->cells[nt_frame_lead(frame, index)].foreground;
    nt_frame_store_glyph(frame, index, copy, glyph.count, columns,
                         source_over(backdrop, foreground, paint), owner, true);
}

NtResult nt_surface_write_glyph(const NtSurfaceView *view, NtCellPoint local, NtByteView glyph,
                                uint8_t columns, NtCellStyle style, NtOwnerToken owner, bool isolate_math)
{
    if (!view || !view->frame || (columns != 1 && columns != 2)) return NT_RESULT_INVALID_ARGUMENT;
    NtCellPoint position;
    if (!translate(view->bounds.origin, local, &position)) return NT_RESULT_OUT_OF_RANGE;
    NtResult result = nt_frame_validate_glyph(glyph);
    if (result != NT_RESULT_OK) return result;
    NtFrame *frame = view->frame;
    const NtCellRect clip = effective_clip(view);
    fenv_t saved;
    if (isolate_math && !nt_begin_standard_fp(&saved)) return NT_RESULT_OUT_OF_RANGE;
    const PaintSpan foreground = {view, position, columns, 1, 1,
                                  glyph_coverage(view, clip, frame, position, columns)};
    const bool write_ink = contributes(style.foreground, foreground);
    NtRgba ink = {0};
    size_t lead = 0;
    if (write_ink) {
        lead = nt_frame_index(frame, position);
        ink = source_over(frame->cells[nt_frame_lead(frame, lead)].foreground, style.foreground, foreground);
    }
    NtRgba backgrounds[2] = {{0}};
    size_t indices[2] = {0};
    for (unsigned offset = 0; offset < columns; ++offset) {
        const int64_t x = (int64_t)position.x + offset;
        const double weight = coverage(view, clip, x, position.y).weight;
        if (!weight) continue;
        const PaintSpan background = {view,{(int32_t)x,position.y},1,1,1,weight};
        if (contributes(style.background, background)) {
            const size_t index = nt_frame_index(frame, background.lead);
            indices[offset] = index;
            backgrounds[offset] = source_over(frame->cells[index].background.color, style.background, background);
        }
    }
    if (isolate_math && !nt_finish_standard_fp(&saved)) return NT_RESULT_OUT_OF_RANGE;
    char *copy = NULL;
    if (write_ink) {
        result = nt_frame_copy_glyph(frame, glyph, &copy);
        if (result != NT_RESULT_OK) return result;
    }
    for (unsigned offset = 0; offset < columns; ++offset) {
        if (backgrounds[offset].alpha)
            store_background(&frame->cells[indices[offset]], backgrounds[offset], owner);
    }
    if (copy) nt_frame_store_glyph(frame, lead, copy, glyph.count, columns, ink, owner, true);
    return NT_RESULT_OK;
}

NtResult nt_surface_glyph(const NtSurfaceView *view, NtCellPoint local, NtByteView glyph,
                          uint8_t columns, NtCellStyle style, NtOwnerToken owner)
{
    return nt_surface_write_glyph(view, local, glyph, columns, style, owner, false);
}

NtResult nt_surface_erase_glyph(const NtSurfaceView *view, NtCellPoint local)
{
    if (!view || !view->frame) return NT_RESULT_INVALID_ARGUMENT;
    NtCellPoint position;
    if (!translate(view->bounds.origin, local, &position)) return NT_RESULT_OUT_OF_RANGE;
    const NtCellRect clip = effective_clip(view);
    if (!coverage(view, clip, position.x, position.y).visible) return NT_RESULT_OK;
    NtFrame *frame = view->frame;
    const size_t index = nt_frame_index(frame, position), lead = nt_frame_lead(frame, index);
    const uint8_t columns = frame->cells[lead].columns;
    position.x -= (int32_t)(index - lead);
    if (columns && footprint_coverage(view, clip, position, columns).visible)
        nt_frame_remove_glyph(frame, lead, true);
    return NT_RESULT_OK;
}

typedef struct SurfaceComposition {
    NtFrame *target;
    NtFrame stage;
    bool staged;
} SurfaceComposition;

static NtResult stage_composition(SurfaceComposition *composition)
{
    if (composition->staged) return NT_RESULT_OK;
    const NtResult result = nt_frame_stage(composition->target, &composition->stage);
    if (result == NT_RESULT_OK) composition->staged = true;
    return result;
}

static const NtFrame *composed_frame(const SurfaceComposition *composition)
{
    return composition->staged ? &composition->stage : composition->target;
}

typedef struct SurfaceOwners {
    NtSurfaceOwnerPolicy policy;
    NtSurfaceMapOwner map;
    void *context;
    NtOwnerToken forced;
} SurfaceOwners;

static bool owners_valid(SurfaceOwners owners)
{
    return owners.forced || (owners.policy >= NT_SURFACE_OWNERS_CLEAR &&
        owners.policy <= NT_SURFACE_OWNERS_MAP &&
        (owners.policy != NT_SURFACE_OWNERS_MAP || owners.map));
}

static NtResult map_owner(SurfaceOwners owners, NtOwnerToken owner, NtOwnerToken *out)
{
    *out = owners.forced;
    if (owners.forced || !owner || owners.policy == NT_SURFACE_OWNERS_CLEAR) return NT_RESULT_OK;
    if (owners.policy == NT_SURFACE_OWNERS_MAP) return owners.map(owners.context,owner,out);
    *out = owner;
    return NT_RESULT_OK;
}

static NtResult stage_owner(SurfaceComposition *composition, SurfaceOwners owners,
                            NtOwnerToken source_owner, NtOwnerToken *out)
{
    const NtResult result = map_owner(owners,source_owner,out);
    return result == NT_RESULT_OK ? stage_composition(composition) : result;
}

static NtResult stage_background(SurfaceComposition *composition, SurfaceOwners owners, size_t index,
                                 NtOwnerToken source_owner, NtRgba color)
{
    NtOwnerToken owner;
    const NtResult result = stage_owner(composition,owners,source_owner,&owner);
    if (result != NT_RESULT_OK) return result;
    store_background(&composition->stage.cells[index],color,owner);
    return NT_RESULT_OK;
}

static NtResult mapped_glyph(SurfaceComposition *composition, SurfaceOwners owners,
                             const FrameCell *source, NtOwnerToken *owner, char **copy)
{
    const NtResult result = stage_owner(composition,owners,source->glyph_owner,owner);
    if (result != NT_RESULT_OK) return result;
    return nt_frame_copy_glyph(&composition->stage,(NtByteView){source->glyph,source->bytes},copy);
}

static NtResult stage_glyph(SurfaceComposition *composition, SurfaceOwners owners, size_t index,
                            const FrameCell *source, NtRgba color)
{
    NtOwnerToken owner;
    char *copy;
    const NtResult result = mapped_glyph(composition,owners,source,&owner,&copy);
    if (result != NT_RESULT_OK) return result;
    nt_frame_store_glyph(&composition->stage,index,copy,source->bytes,source->columns,color,owner,true);
    return NT_RESULT_OK;
}

static NtResult compose_background(SurfaceComposition *composition, SurfaceOwners owners, size_t index,
                                   NtCellBackground source, PaintSpan paint)
{
    if (!contributes(source.color,paint)) return NT_RESULT_OK;
    NtOwnerToken owner;
    const NtResult result = stage_owner(composition,owners,source.owner,&owner);
    if (result != NT_RESULT_OK) return result;
    paint_background(&composition->stage.cells[index],source.color,paint,owner);
    return NT_RESULT_OK;
}

static NtResult compose_cell(SurfaceComposition *composition, const NtSurfaceLayer *layer,
                             SurfaceOwners owners, const NtSurfaceView *view, NtCellRect clip,
                             NtCellPoint position)
{
    const PaintSpan background = {view, position, 1, layer->opacity, 1,
                                  coverage(view, clip, position.x, position.y).weight};
    if (!background.weight) return NT_RESULT_OK;
    const NtCellPoint from = {
        (int32_t)((int64_t)layer->region.origin.x + position.x - view->bounds.origin.x),
        (int32_t)((int64_t)layer->region.origin.y + position.y - view->bounds.origin.y)
    };
    const FrameCell *source = &layer->source->cells[nt_frame_index(layer->source, from)];
    const size_t index = nt_frame_index(composition->target, position);
    const NtResult result = compose_background(composition,owners,index,source->background,background);
    if (result != NT_RESULT_OK) return result;
    if (!source->columns || !source->foreground.alpha) return NT_RESULT_OK;
    const NtFrame *current = composed_frame(composition);
    const PaintSpan foreground = {view, position, source->columns, layer->opacity, 1,
                                  glyph_coverage(view, clip, current, position, source->columns)};
    if (!contributes(source->foreground, foreground)) return NT_RESULT_OK;
    NtOwnerToken owner;
    char *copy;
    const NtResult mapped = mapped_glyph(composition,owners,source,&owner,&copy);
    if (mapped != NT_RESULT_OK) return mapped;
    paint_glyph(&composition->stage,index,(NtByteView){source->glyph,source->bytes},copy,
                source->columns,source->foreground,foreground,owner);
    return NT_RESULT_OK;
}

static bool region_fits(const NtFrame *source, NtCellRect region)
{
    const NtCellSize size = nt_frame_size(source);
    return region.origin.x >= 0 && region.origin.y >= 0 &&
        right(region) <= size.columns && bottom(region) <= size.rows;
}

static NtResult compose_layer(SurfaceComposition *composition, const NtSurfaceView *parent,
                              const NtSurfaceLayer *layer)
{
    const SurfaceOwners owners = {layer->owners,layer->map_owner,layer->owner_context,0};
    if (!layer->source || !nt_unit_interval(layer->opacity) || !owners_valid(owners))
        return NT_RESULT_INVALID_ARGUMENT;
    const NtCellRect region = layer->region;
    if (!region_fits(layer->source,region)) return NT_RESULT_OUT_OF_RANGE;
    NtSurfaceView view;
    const NtResult result = nt_surface_child(parent, (NtCellRect){layer->position, region.size},
                                             layer->mask, &view);
    if (result != NT_RESULT_OK || !layer->opacity) return result;
    const NtCellRect clip = effective_clip(&view);
    for (int64_t y = clip.origin.y; y < bottom(clip); ++y) {
        for (int64_t x = clip.origin.x; x < right(clip); ++x) {
            const NtResult status = compose_cell(composition, layer, owners, &view, clip,
                                                 (NtCellPoint){(int32_t)x, (int32_t)y});
            if (status != NT_RESULT_OK) return status;
        }
    }
    return NT_RESULT_OK;
}

static NtResult finish_composition(SurfaceComposition *composition, NtResult result)
{
    if (composition->staged) {
        if (result == NT_RESULT_OK) nt_frame_publish(composition->target,&composition->stage);
        else nt_frame_discard(&composition->stage);
    }
    return result;
}

NtResult nt_surface_compose(const NtSurfaceView *view, const NtSurfaceLayer *layers, size_t count)
{
    if (!view || !view->frame || (count && !layers)) return NT_RESULT_INVALID_ARGUMENT;
    if (count > SIZE_MAX / sizeof(*layers)) return NT_RESULT_OUT_OF_RANGE;
    SurfaceComposition composition = {.target = view->frame};
    for (size_t i = 0; i < count; ++i) {
        const NtResult result = compose_layer(&composition, view, &layers[i]);
        if (result != NT_RESULT_OK) return finish_composition(&composition,result);
    }
    return finish_composition(&composition,NT_RESULT_OK);
}

typedef struct ProjectedCell {
    const FrameCell *source, *glyph;
    double opacity, mask;
} ProjectedCell;

NT_DECLARE_ALIGNMENT(ProjectedCell);

static NtResult project_cell(const NtPanelLayer *layer, const NtPanelBasis *basis,
                             const NtSurfaceView *view, NtCellRect clip, NtCellPoint position,
                             ProjectedCell *out)
{
    *out = (ProjectedCell){0};
    fenv_t saved;
    if (!nt_panel_begin_geometry(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    const MaskCoverage mask = coverage(view,clip,position.x,position.y);
    const NtPoint destination = {(double)position.x-view->bounds.origin.x+.5,
                                  (double)position.y-view->bounds.origin.y+.5};
    NtPoint inverse;
    const NtResult inverse_result = mask.visible ?
        nt_panel_inverse_point(basis,destination,&inverse) : NT_RESULT_OK;
    if (!nt_panel_finish_geometry(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    if (inverse_result != NT_RESULT_OK || !mask.visible) return inverse_result;
    const NtCellRect region = layer->region;
    NtPanelPixel pixel = {inverse,1};
    if (layer->remap) {
        const NtPanelSample sample = {destination,inverse,view->bounds.size,region.size,layer->elapsed_us};
        const NtResult result = layer->remap(layer->remap_context,&sample,&pixel);
        if (result != NT_RESULT_OK) return result;
    }
    NtCellPoint from;
    bool inside;
    const NtResult sampled = nt_panel_source_cell(pixel,region,&from,&inside);
    if (sampled != NT_RESULT_OK || !inside) return sampled;
    const FrameCell *source = &layer->source->cells[nt_frame_index(layer->source,from)];
    const FrameCell *glyph = source-source->continuation;
    const int64_t lead = (int64_t)from.x-source->continuation;
    if (!glyph->columns || lead < region.origin.x || lead+glyph->columns > right(region)) glyph = NULL;
    *out = (ProjectedCell){source,glyph,pixel.opacity,mask.weight};
    return NT_RESULT_OK;
}

/* A projected glyph may overwrite an existing wide glyph only when the
 * remap and view admit each half of the existing glyph. */
static bool replaceable(const NtFrame *frame, size_t index, uint8_t width,
                        const ProjectedCell *row, size_t columns, size_t column)
{
    for (size_t i = 0; i < width; ++i) {
        const FrameCell *cell = &frame->cells[index+i];
        const size_t x = column+i;
        if (cell->continuation && (!x || !row[x-1].source)) return false;
        if (cell->columns == 2 && (x+1 >= columns || !row[x+1].source)) return false;
    }
    return true;
}

typedef struct ProjectedColors { NtRgba background, foreground; } ProjectedColors;

static ProjectedColors projected_colors(const NtFrame *frame, size_t index, PaintSpan paint,
                                        const ProjectedCell *row, size_t columns, size_t column,
                                        bool skip_glyph)
{
    ProjectedColors colors = {0};
    const ProjectedCell cell = row[column];
    const NtRgba background = cell.source->background.color;
    if (contributes(background,paint))
        colors.background = source_over(frame->cells[index].background.color,background,paint);
    const FrameCell *ink = cell.glyph;
    if (skip_glyph || !ink || !ink->foreground.alpha) return colors;

    paint.columns = ink->columns;
    if (paint.columns == 2) {
        if (column+1 >= columns || !row[column+1].source) return colors;
        const ProjectedCell next = row[column+1];
        if (cell.source->continuation && next.glyph != ink) return colors;
        paint.remap_opacity = fmin(cell.opacity,next.opacity);
        paint.weight = fmin(cell.mask,next.mask);
    }
    if (contributes(ink->foreground,paint) && replaceable(frame,index,paint.columns,row,columns,column)) {
        const NtRgba backdrop = frame->cells[nt_frame_lead(frame,index)].foreground;
        colors.foreground = source_over(backdrop,ink->foreground,paint);
    }
    return colors;
}

static NtResult compose_projected_row(SurfaceComposition *composition, SurfaceOwners owners,
                                      const NtSurfaceView *view, NtCellRect clip, int32_t y,
                                      double opacity, const ProjectedCell *row)
{
    const size_t columns = clip.size.columns;
    bool occupied = false;
    for (size_t i = 0; i < columns; ++i) {
        const bool skip_glyph = occupied;
        occupied = false;
        const ProjectedCell cell = row[i];
        const FrameCell *source = cell.source;
        if (!source) continue;
        const NtCellPoint position = {(int32_t)((int64_t)clip.origin.x+(int64_t)i),y};
        const size_t index = nt_frame_index(composition->target,position);
        const PaintSpan paint = {view,position,1,opacity,cell.opacity,cell.mask};
        fenv_t saved;
        if (!nt_panel_begin_geometry(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
        const ProjectedColors colors = projected_colors(composed_frame(composition),index,paint,
                                                         row,columns,i,skip_glyph);
        if (!nt_panel_finish_geometry(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
        if (colors.background.alpha) {
            const NtResult result = stage_background(composition,owners,index,source->background.owner,
                                                      colors.background);
            if (result != NT_RESULT_OK) return result;
        }
        if (colors.foreground.alpha) {
            const NtResult result = stage_glyph(composition,owners,index,cell.glyph,colors.foreground);
            if (result != NT_RESULT_OK) return result;
            occupied = cell.glyph->columns == 2;
        }
    }
    return NT_RESULT_OK;
}

static NtResult compose_projected(SurfaceComposition *composition, const NtSurfaceView *parent,
                                  const NtPanelLayer *layer, NtOwnerToken forced_owner)
{
    const SurfaceOwners owners = {layer->owners,layer->map_owner,layer->owner_context,forced_owner};
    if (!layer->source || !owners_valid(owners))
        return NT_RESULT_INVALID_ARGUMENT;
    NtPanelBasis basis;
    NtResult result = nt_panel_basis(&layer->pose,&basis);
    if (result != NT_RESULT_OK) return result;
    if (!region_fits(layer->source,layer->region)) return NT_RESULT_OUT_OF_RANGE;
    fenv_t saved;
    if (!nt_panel_begin_geometry(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    NtSurfaceView view;
    result = nt_surface_child(parent,layer->viewport,layer->mask,&view);
    if (!nt_panel_finish_geometry(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    if (result != NT_RESULT_OK) return result;
    const NtCellRect clip = effective_clip(&view);
    if (!clip.size.columns || !clip.size.rows || !layer->region.size.columns || !layer->region.size.rows ||
        !basis.drawable) return NT_RESULT_OK;
    const size_t columns = clip.size.columns;
    if (columns > SIZE_MAX/sizeof(ProjectedCell)) return NT_RESULT_OUT_OF_RANGE;
    const size_t bytes = columns*sizeof(ProjectedCell);
    const NtAllocator *allocator = &composition->target->allocator;
    ProjectedCell *row = nt_allocate(allocator,bytes,NT_ALIGNMENT(ProjectedCell));
    if (!row) return NT_RESULT_ALLOCATION_FAILED;
    for (int64_t y = clip.origin.y; y < bottom(clip) && result == NT_RESULT_OK; ++y) {
        for (size_t i = 0; i < columns && result == NT_RESULT_OK; ++i) {
            const NtCellPoint position = {(int32_t)((int64_t)clip.origin.x+(int64_t)i),(int32_t)y};
            result = project_cell(layer,&basis,&view,clip,position,&row[i]);
        }
        if (result == NT_RESULT_OK)
            result = compose_projected_row(composition,owners,&view,clip,(int32_t)y,layer->pose.opacity,row);
    }
    nt_release(allocator,row,bytes,NT_ALIGNMENT(ProjectedCell));
    return result;
}

NtResult nt_panel_project_owned(const NtSurfaceView *view, const NtPanelLayer *layers, size_t count,
                                NtOwnerToken owner)
{
    if (!view || !view->frame || (count && !layers)) return NT_RESULT_INVALID_ARGUMENT;
    if (count > SIZE_MAX/sizeof(*layers)) return NT_RESULT_OUT_OF_RANGE;
    SurfaceComposition composition = {.target=view->frame};
    for (size_t i = 0; i < count; ++i) {
        const NtResult result = compose_projected(&composition,view,&layers[i],owner);
        if (result != NT_RESULT_OK) return finish_composition(&composition,result);
    }
    return finish_composition(&composition,NT_RESULT_OK);
}

NtResult nt_surface_project_panels(const NtSurfaceView *view, const NtPanelLayer *layers, size_t count)
{
    return nt_panel_project_owned(view,layers,count,0);
}
