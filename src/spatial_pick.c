#include "spatial_projection_internal.h"

static void ray_scores(const NtSpatialProjection *projection, const NtVec3 view[3],
                        double numerator, double denominator, bool horizontal, NtSpatialExact out[3])
{
    NtSpatialExact factor, product, slope, scale, position;
    nt_spatial_exact_number(&factor, numerator, 0);
    nt_spatial_exact_number(&product, horizontal ? projection->half_width : projection->half_height, 0);
    nt_spatial_exact_multiply(&slope, &factor, &product);
    nt_spatial_exact_number(&scale, denominator, 0);
    for (unsigned i = 0; i < 3; ++i) {
        nt_spatial_exact_number(&factor, projection->kind == NT_SPATIAL_PERSPECTIVE ? -view[i].z : 1, 0);
        nt_spatial_exact_multiply(&product, &slope, &factor);
        nt_spatial_exact_number(&factor, horizontal ? view[i].x : view[i].y, 0);
        nt_spatial_exact_multiply(&position, &factor, &scale);
        nt_spatial_exact_subtract(&out[i], &position, &product);
    }
}

static bool ray_weights(const NtSpatialProjection *projection, NtSpatialScreenRatio screen,
                        const NtVec3 view[3], NtSpatialWeights *weights)
{
    NtSpatialExact horizontal[3], vertical[3];
    ray_scores(projection, view, screen.numerator.x, screen.denominator.x, true, horizontal);
    ray_scores(projection, view, screen.numerator.y, screen.denominator.y, false, vertical);
    return nt_spatial_intersect_weights(horizontal, vertical, weights) &&
        !weights->value[0].negative && !weights->value[1].negative && !weights->value[2].negative;
}

static NtSpatialHit hit_value(const NtSpatialPickTriangle *triangle, size_t index,
                               const NtSpatialWeights *weights, const NtSpatialExact *depth)
{
    NtSpatialExact x, y;
    const NtVec3 *view = triangle->view;
    const NtSpatialExact *divisor = &weights->divisor;
    nt_spatial_weighted(weights, view[0].x, view[1].x, view[2].x, &x);
    nt_spatial_weighted(weights, view[0].y, view[1].y, view[2].y, &y);
    const double at = nt_spatial_exact_quotient(depth, divisor);
    return (NtSpatialHit){
        .object_id = triangle->object_id, .primitive_id = triangle->primitive_id,
        .triangle_index = index,
        .view = {nt_spatial_exact_quotient(&x, divisor), nt_spatial_exact_quotient(&y, divisor), -at},
        .weights = nt_spatial_weights_value(weights),
        .depth = at
    };
}

bool nt_spatial_nearest_weights(const NtSpatialProjection *projection, NtSpatialScreenRatio screen,
                               const NtSpatialPickTriangle *triangles, const NtSpatialScreenBounds *bounds,
                               size_t count, size_t *index, NtSpatialWeights *selected,
                               NtSpatialExact *best_depth)
{
    /* The screen bounds and this grid center use the same monotone rounding.
     * The exact ratio is still needed for triangle and depth predicates. */
    const NtSpatialScreen rounded = {screen.numerator.x/screen.denominator.x,
                                      screen.numerator.y/screen.denominator.y};
    bool found = false;
    for (size_t i = 0; i < count; ++i) {
        if (bounds && (bounds[i].empty ||
            rounded.x < bounds[i].minimum.x || rounded.x > bounds[i].maximum.x ||
            rounded.y < bounds[i].minimum.y || rounded.y > bounds[i].maximum.y)) continue;
        const NtVec3 *view = triangles[i].view;
        NtSpatialWeights weights;
        if (!ray_weights(projection, screen, view, &weights)) continue;
        NtSpatialExact depth;
        nt_spatial_weighted(&weights, -view[0].z, -view[1].z, -view[2].z, &depth);
        if (nt_spatial_exact_compare_scaled(&depth, &weights.divisor, projection->near_depth) < 0 ||
            nt_spatial_exact_compare_scaled(&depth, &weights.divisor, projection->far_depth) > 0) continue;
        if (found && nt_spatial_compare_ratios(&depth, &weights.divisor, best_depth, &selected->divisor) >= 0) continue;
        nt_spatial_exact_copy(best_depth, &depth);
        nt_spatial_exact_copy(&selected->divisor, &weights.divisor);
        for (unsigned j = 0; j < 3; ++j) nt_spatial_exact_copy(&selected->value[j], &weights.value[j]);
        *index = i;
        found = true;
    }
    return found;
}

NtResult nt_spatial_pick(const NtSpatialProjection *projection, NtSpatialScreen screen,
                         const NtSpatialPickTriangle *triangles, size_t count,
                         bool *hit, NtSpatialHit *out)
{
    if (!projection || (count && !triangles) || !hit || !out)
        return NT_RESULT_INVALID_ARGUMENT;
    if (count > SIZE_MAX/sizeof(triangles[0])) return NT_RESULT_OUT_OF_RANGE;
    fenv_t saved;
    const NtResult status = nt_spatial_begin_projection(projection, &saved);
    if (status != NT_RESULT_OK) return status;
    if (!nt_spatial_screen_finite(screen)) return nt_spatial_end_projection(&saved, NT_RESULT_INVALID_ARGUMENT);
    for (size_t i = 0; i < count; ++i)
        for (unsigned j = 0; j < 3; ++j)
            if (!nt_spatial_finite_vec(triangles[i].view[j]))
                return nt_spatial_end_projection(&saved, NT_RESULT_INVALID_ARGUMENT);
    NtSpatialWeights selected;
    NtSpatialExact depth;
    size_t index;
    const NtSpatialScreenRatio ray = {screen,{1,1}};
    const bool found = nt_spatial_screen_inside(screen) &&
        nt_spatial_nearest_weights(projection, ray, triangles, NULL, count, &index, &selected, &depth);
    NtSpatialHit value;
    if (found) value = hit_value(&triangles[index], index, &selected, &depth);
    if (!nt_finish_standard_fp(&saved)) return NT_RESULT_OUT_OF_RANGE;
    *hit = found;
    if (found) *out = value;
    return NT_RESULT_OK;
}
