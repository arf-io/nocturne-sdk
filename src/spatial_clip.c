#include "spatial_projection_internal.h"
#include "spatial_geometry_internal.h"

static void segment_vertex(const NtSpatialProjection *projection, const NtVec3 view[3],
                           const NtSpatialFraction *at, NtSpatialClipVertex *out)
{
    NtSpatialWeights weights;
    nt_spatial_exact_subtract(&weights.value[0], &at->divisor, &at->numerator);
    nt_spatial_exact_copy(&weights.value[1], &at->numerator);
    nt_spatial_exact_number(&weights.value[2], 0, 0);
    nt_spatial_exact_copy(&weights.divisor, &at->divisor);
    nt_spatial_clipped_vertex(projection, view, &weights, out);
}

static bool segment_interval(const NtSpatialProjection *projection, NtVec3 first, NtVec3 last,
                              NtSpatialFraction *begin, NtSpatialFraction *end)
{
    nt_spatial_fraction_set(begin,0);
    nt_spatial_fraction_set(end,1);
    for (NtSpatialPlane plane = NT_SPATIAL_NEAR; plane <= NT_SPATIAL_TOP; ++plane) {
        NtSpatialExact a, b;
        nt_spatial_plane_score(projection, plane, first, &a);
        nt_spatial_plane_score(projection, plane, last, &b);
        if (!nt_spatial_clip_fraction(begin,end,&a,&b)) return false;
    }
    return true;
}

NtResult nt_spatial_clip_segment(const NtSpatialProjection *projection, NtVec3 first, NtVec3 last,
                                  bool *visible, NtSpatialClippedSegment *out)
{
    if (!visible || !out) return NT_RESULT_INVALID_ARGUMENT;
    fenv_t saved;
    const NtResult status = nt_spatial_begin_projection(projection, &saved);
    if (status != NT_RESULT_OK) return status;
    if (!nt_spatial_finite_vec(first) || !nt_spatial_finite_vec(last))
        return nt_spatial_end_projection(&saved, NT_RESULT_INVALID_ARGUMENT);
    NtSpatialFraction begin, end;
    const bool inside = segment_interval(projection, first, last, &begin, &end);
    NtSpatialClippedSegment result;
    if (inside) {
        const NtVec3 view[3] = {first, last, {0,0,0}};
        segment_vertex(projection, view, &begin, &result.first);
        segment_vertex(projection, view, &end, &result.last);
    }
    if (!nt_finish_standard_fp(&saved)) return NT_RESULT_OUT_OF_RANGE;
    *visible = inside;
    if (inside) *out = result;
    return NT_RESULT_OK;
}

typedef struct NtSpatialClipCorner {
    NtSpatialPlane first, second, incoming;
} NtSpatialClipCorner;

typedef struct NtSpatialClipLoop {
    NtSpatialClipCorner corners[NT_SPATIAL_CLIP_MAX_VERTICES];
    size_t count;
} NtSpatialClipLoop;

static void scores(const NtSpatialProjection *projection, NtSpatialPlane plane,
                   const NtVec3 view[3], NtSpatialExact out[3])
{
    for (unsigned i = 0; i < 3; ++i) {
        if (plane >= NT_SPATIAL_WEIGHT_A)
            nt_spatial_exact_number(&out[i], i == (unsigned)(plane-NT_SPATIAL_WEIGHT_A), 0);
        else
            nt_spatial_plane_score(projection, plane, view[i], &out[i]);
    }
}

static bool corner_weights(const NtSpatialProjection *projection, const NtVec3 view[3],
                            const NtSpatialClipCorner *corner, NtSpatialWeights *out)
{
    NtSpatialExact first[3], second[3];
    scores(projection, corner->first, view, first);
    scores(projection, corner->second, view, second);
    return nt_spatial_intersect_weights(first, second, out);
}

static int evaluate_boundary(const NtSpatialExact boundary[3], const NtSpatialWeights *weights)
{
    NtSpatialExact accumulated;
    nt_spatial_exact_number(&accumulated, 0, 0);
    for (unsigned i = 0; i < 3; ++i) {
        NtSpatialExact product, sum;
        nt_spatial_exact_multiply(&product, &boundary[i], &weights->value[i]);
        nt_spatial_exact_add(&sum, &accumulated, &product);
        nt_spatial_exact_copy(&accumulated, &sum);
    }
    return nt_spatial_exact_sign(&accumulated);
}

static NtResult clip_loop(const NtSpatialProjection *projection, const NtVec3 view[3],
                          NtSpatialPlane clip, NtSpatialClipLoop *loop)
{
    NtSpatialExact boundary[3];
    int signs[NT_SPATIAL_CLIP_MAX_VERTICES];
    bool positive = false, negative = false;
    scores(projection, clip, view, boundary);
    if (!boundary[0].negative && !boundary[1].negative && !boundary[2].negative)
        return NT_RESULT_OK;
    for (size_t i = 0; i < loop->count; ++i) {
        NtSpatialWeights weights;
        if (!corner_weights(projection, view, &loop->corners[i], &weights)) return NT_RESULT_OUT_OF_RANGE;
        signs[i] = evaluate_boundary(boundary, &weights);
        positive |= signs[i] > 0;
        negative |= signs[i] < 0;
    }
    if (!negative) return NT_RESULT_OK;
    if (!positive) { loop->count = 0; return NT_RESULT_OK; }

    NtSpatialClipLoop cut = {0};
    for (size_t i = 0; i < loop->count; ++i) {
        NtSpatialClipCorner corner = loop->corners[i];
        const int before = signs[i ? i-1 : loop->count-1], now = signs[i];
        if (before*now < 0) {
            if (cut.count == NT_SPATIAL_CLIP_MAX_VERTICES) return NT_RESULT_OUT_OF_RANGE;
            const NtSpatialPlane incoming = before < 0 ? clip : corner.incoming;
            cut.corners[cut.count++] = (NtSpatialClipCorner){corner.incoming, clip, incoming};
        }
        if (now >= 0) {
            if (cut.count == NT_SPATIAL_CLIP_MAX_VERTICES) return NT_RESULT_OUT_OF_RANGE;
            if (before < 0 && now == 0) corner.incoming = clip;
            cut.corners[cut.count++] = corner;
        }
    }
    *loop = cut;
    return NT_RESULT_OK;
}

static NtResult publish_corners(const NtSpatialProjection *projection, const NtVec3 view[3],
                                const NtSpatialClipLoop *loop, NtSpatialClippedPolygon *out)
{
    out->count = loop->count < 3 ? 0 : loop->count;
    for (size_t i = 0; i < out->count; ++i) {
        NtSpatialWeights weights;
        if (!corner_weights(projection, view, &loop->corners[i], &weights)) return NT_RESULT_OUT_OF_RANGE;
        nt_spatial_clipped_vertex(projection, view, &weights, &out->vertices[i]);
    }
    return NT_RESULT_OK;
}

static NtResult triangle_result(const NtSpatialProjection *projection, const NtVec3 view[3],
                                NtSpatialClippedPolygon *out)
{
    /* Each corner is an intersection of two original boundary lines in
     * barycentric space. Keep those line identities through every cut so
     * intersections never depend on a previously rounded vertex. */
    NtSpatialClipLoop loop = {{
        {NT_SPATIAL_WEIGHT_B, NT_SPATIAL_WEIGHT_C, NT_SPATIAL_WEIGHT_B},
        {NT_SPATIAL_WEIGHT_C, NT_SPATIAL_WEIGHT_A, NT_SPATIAL_WEIGHT_C},
        {NT_SPATIAL_WEIGHT_A, NT_SPATIAL_WEIGHT_B, NT_SPATIAL_WEIGHT_A}
    }, 3};
    for (NtSpatialPlane plane = NT_SPATIAL_NEAR; plane <= NT_SPATIAL_TOP && loop.count >= 3; ++plane) {
        const NtResult result = clip_loop(projection, view, plane, &loop);
        if (result != NT_RESULT_OK) return result;
    }
    return publish_corners(projection, view, &loop, out);
}

NtResult nt_spatial_clip_triangle(const NtSpatialProjection *projection, const NtVec3 view[3],
                                   NtSpatialClippedPolygon *out)
{
    if (!view || !out) return NT_RESULT_INVALID_ARGUMENT;
    fenv_t saved;
    const NtResult begun = nt_spatial_begin_projection(projection, &saved);
    if (begun != NT_RESULT_OK) return begun;
    if (!nt_spatial_finite_vec(view[0]) || !nt_spatial_finite_vec(view[1]) || !nt_spatial_finite_vec(view[2]))
        return nt_spatial_end_projection(&saved, NT_RESULT_INVALID_ARGUMENT);
    NtSpatialClippedPolygon result = {0};
    const NtResult status = triangle_result(projection, view, &result);
    if (!nt_finish_standard_fp(&saved)) return NT_RESULT_OUT_OF_RANGE;
    if (status == NT_RESULT_OK) *out = result;
    return status;
}
