#include "spatial_internal.h"
#include "allocator.h"
#include "spatial_norm_internal.h"

typedef struct NtSpatialSegment {
    NtVec3 start, end, tangent;
    double cumulative;
} NtSpatialSegment;

struct NtSpatialPath {
    NtAllocator allocator;
    size_t count;
    NtRepeat boundary;
    NtSpatialSegment segments[];
};

NT_DECLARE_ALIGNMENT(NtAllocator);
NT_DECLARE_ALIGNMENT(NtSpatialSegment);

static size_t path_alignment(void)
{
    const size_t allocator = NT_ALIGNMENT(NtAllocator), segment = NT_ALIGNMENT(NtSpatialSegment);
    return allocator > segment ? allocator : segment;
}

static size_t path_bytes(size_t count)
{
    return sizeof(NtSpatialPath) + count * sizeof(NtSpatialSegment);
}

static NtResult prepare_segments(NtSpatialPath *path, const NtSpatialPathOptions *options)
{
    double cumulative = 0;
    for (size_t i = 0; i < path->count; ++i) {
        const NtVec3 a = options->points[i], b = options->points[(i+1)%options->count];
        const NtScaledSum delta[] = {nt_spatial_difference(b.x, a.x), nt_spatial_difference(b.y, a.y),
                                     nt_spatial_difference(b.z, a.z)};
        const NtScaledSum squared = nt_spatial_norm_squared(delta);
        NtVec3 tangent;
        if (!nt_spatial_unit_sums(delta, &tangent)) return NT_RESULT_INVALID_ARGUMENT;
        const double next = nt_spatial_round_length(&squared, cumulative);
        if (!isfinite(next) || next <= cumulative) return NT_RESULT_OUT_OF_RANGE;
        path->segments[i] = (NtSpatialSegment){a, b, tangent, next};
        cumulative = next;
    }
    return NT_RESULT_OK;
}

NtResult nt_spatial_path_create(const NtAllocator *provided, const NtSpatialPathOptions *options,
                                 NtSpatialPath **out)
{
    NtAllocator allocator;
    if (!options || !options->points || !out || options->count < (options->closed ? 3u : 2u) ||
        options->boundary < NT_ONCE || options->boundary > NT_PING_PONG ||
        !nt_allocator_get(provided, &allocator)) return NT_RESULT_INVALID_ARGUMENT;
    const size_t segments = options->closed ? options->count : options->count-1;
    if (segments > (SIZE_MAX-sizeof(NtSpatialPath))/sizeof(NtSpatialSegment)) return NT_RESULT_OUT_OF_RANGE;
    for (size_t i = 0; i < options->count; ++i)
        if (!nt_spatial_finite_vec(options->points[i])) return NT_RESULT_INVALID_ARGUMENT;
    NtSpatialPath *path = nt_allocate(&allocator, path_bytes(segments), path_alignment());
    if (!path) return NT_RESULT_ALLOCATION_FAILED;
    path->allocator = allocator;
    path->count = segments;
    path->boundary = options->boundary;
    NtResult result = NT_RESULT_OUT_OF_RANGE;
    fenv_t saved;
    if (nt_begin_standard_fp(&saved)) {
        result = prepare_segments(path, options);
        if (!nt_finish_standard_fp(&saved)) result = NT_RESULT_OUT_OF_RANGE;
    }
    if (result != NT_RESULT_OK) {
        nt_spatial_path_destroy(path);
        return result;
    }
    *out = path;
    return NT_RESULT_OK;
}

void nt_spatial_path_destroy(NtSpatialPath *path)
{
    if (!path) return;
    const NtAllocator allocator = path->allocator;
    nt_release(&allocator, path, path_bytes(path->count), path_alignment());
}

NtResult nt_spatial_path_length(const NtSpatialPath *path, double *out)
{
    if (!path || !out) return NT_RESULT_INVALID_ARGUMENT;
    *out = path->segments[path->count-1].cumulative;
    return NT_RESULT_OK;
}

static double path_distance(NtRepeat boundary, double distance, double length, int *direction)
{
    if (boundary == NT_ONCE) return fmax(0, fmin(length, distance));
    if (boundary == NT_LOOP) {
        const double remainder = fmod(distance, length);
        return remainder < 0 ? length+remainder : remainder;
    }
    int legs;
    const double remainder = remquo(distance, length, &legs);
    const bool reversed = legs%2 != 0;
    *direction = (reversed != (remainder < 0)) ? -1 : 1;
    return reversed ? length-fabs(remainder) : fabs(remainder);
}

static double interpolate(double a, double b, const NtScaledSum *left, const NtScaledSum *right,
                          const NtScaledSum *span, NtScaledTerm divisor)
{
    NtScaledSum weighted = {0};
    nt_scaled_add_sum(&weighted, left, nt_scaled_term(a, 0));
    nt_scaled_add_sum(&weighted, right, nt_scaled_term(b, 0));
    return nt_scaled_quotient(&weighted, span, divisor);
}

static NtVec3 position_at(NtSpatialSegment segment, double begin, double at)
{
    const double end = segment.cumulative;
    if (at == begin) return segment.start;
    if (at == end) return segment.end;
    /* Keep cumulative-distance differences and weighted endpoint products
     * together. The normalized fraction alone can round to zero. */
    NtScaledSum left = {0}, right = {0}, span = {0};
    nt_scaled_add(&left, nt_scaled_term(end, 0));
    nt_scaled_add(&left, nt_scaled_term(-at, 0));
    nt_scaled_add(&right, nt_scaled_term(at, 0));
    nt_scaled_add(&right, nt_scaled_term(-begin, 0));
    nt_scaled_add(&span, nt_scaled_term(end, 0));
    nt_scaled_add(&span, nt_scaled_term(-begin, 0));
    const NtScaledTerm divisor = nt_scaled_value(&span);
    const NtVec3 a = segment.start, b = segment.end;
    return (NtVec3){interpolate(a.x, b.x, &left, &right, &span, divisor),
                    interpolate(a.y, b.y, &left, &right, &span, divisor),
                    interpolate(a.z, b.z, &left, &right, &span, divisor)};
}

NtResult nt_spatial_path_sample(const NtSpatialPath *path, double distance, NtSpatialPathSample *out)
{
    if (!path || !out || !isfinite(distance)) return NT_RESULT_INVALID_ARGUMENT;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_OUT_OF_RANGE;
    const double length = path->segments[path->count-1].cumulative;
    int direction = 1;
    const double at = path_distance(path->boundary, distance, length, &direction);
    size_t lower = 0, upper = path->count-1;
    while (lower < upper) {
        const size_t center = lower+(upper-lower)/2;
        const double end = path->segments[center].cumulative;
        if (at < end || (at == end && direction < 0)) upper = center;
        else lower = center+1;
    }
    const NtSpatialSegment selected = path->segments[lower];
    const double start = lower ? path->segments[lower-1].cumulative : 0;
    const NtSpatialPathSample sample = {
        .position = position_at(selected, start, at),
        .tangent = {direction*selected.tangent.x, direction*selected.tangent.y, direction*selected.tangent.z},
        .distance = at == 0 ? 0 : at,
        .length = length
    };
    const bool finite = nt_spatial_finite_vec(sample.position);
    if (!nt_finish_standard_fp(&saved) || !finite) return NT_RESULT_OUT_OF_RANGE;
    *out = sample;
    return NT_RESULT_OK;
}
