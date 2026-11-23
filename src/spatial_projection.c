#include "spatial_projection_internal.h"

NtResult nt_spatial_begin_projection(const NtSpatialProjection *p, fenv_t *saved)
{
    if (!p) return NT_RESULT_INVALID_ARGUMENT;
    if (!nt_begin_standard_fp(saved)) return NT_RESULT_OUT_OF_RANGE;
    const bool valid = (p->kind == NT_SPATIAL_PERSPECTIVE || p->kind == NT_SPATIAL_ORTHOGRAPHIC) &&
        isfinite(p->half_width) && isfinite(p->half_height) && p->half_width > 0 && p->half_height > 0 &&
        isfinite(p->near_depth) && isfinite(p->far_depth) && p->near_depth > 0 && p->far_depth > p->near_depth;
    if (valid) return NT_RESULT_OK;
    return nt_spatial_end_projection(saved, NT_RESULT_INVALID_ARGUMENT);
}

bool nt_spatial_screen_finite(NtSpatialScreen screen)
{
    return isfinite(screen.x) && isfinite(screen.y);
}

bool nt_spatial_screen_inside(NtSpatialScreen screen)
{
    return fabs(screen.x) <= 1 && fabs(screen.y) <= 1;
}

static void extent(double half, const NtSpatialExact *scale, NtSpatialExact *out)
{
    NtSpatialExact factor;
    nt_spatial_exact_number(&factor, half, 0);
    nt_spatial_exact_multiply(out, &factor, scale);
}

void nt_spatial_plane_score(const NtSpatialProjection *p, NtSpatialPlane plane, NtVec3 view,
                             NtSpatialExact *out)
{
    NtSpatialExact a, b;
    if (plane == NT_SPATIAL_NEAR || plane == NT_SPATIAL_FAR) {
        const bool near = plane == NT_SPATIAL_NEAR;
        nt_spatial_exact_number(&a, near ? -view.z : p->far_depth, 0);
        nt_spatial_exact_number(&b, near ? p->near_depth : -view.z, 0);
        nt_spatial_exact_subtract(out, &a, &b);
        return;
    }
    const bool horizontal = plane == NT_SPATIAL_LEFT || plane == NT_SPATIAL_RIGHT;
    const double component = horizontal ? view.x : view.y;
    const double half = horizontal ? p->half_width : p->half_height;
    nt_spatial_exact_number(&a, p->kind == NT_SPATIAL_PERSPECTIVE ? -view.z : 1, 0);
    extent(half, &a, &b);
    const bool positive = plane == NT_SPATIAL_LEFT || plane == NT_SPATIAL_BOTTOM;
    nt_spatial_exact_number(&a, positive ? component : -component, 0);
    nt_spatial_exact_add(out, &b, &a);
}

bool nt_spatial_intersect_weights(const NtSpatialExact first[3], const NtSpatialExact second[3],
                                  NtSpatialWeights *out)
{
    NtSpatialExact a, b;
    for (unsigned i = 0; i < 3; ++i) {
        const unsigned j = (i+1)%3, k = (i+2)%3;
        nt_spatial_exact_multiply(&a, &first[j], &second[k]);
        nt_spatial_exact_multiply(&b, &first[k], &second[j]);
        nt_spatial_exact_subtract(&out->value[i], &a, &b);
    }
    nt_spatial_exact_add(&a, &out->value[0], &out->value[1]);
    nt_spatial_exact_add(&out->divisor, &a, &out->value[2]);
    if (out->divisor.negative) {
        nt_spatial_exact_negate(&out->divisor);
        for (unsigned i = 0; i < 3; ++i) nt_spatial_exact_negate(&out->value[i]);
    }
    return out->divisor.count != 0;
}

void nt_spatial_weighted(const NtSpatialWeights *weights, double a, double b, double c,
                         NtSpatialExact *out)
{
    const double coordinates[] = {a,b,c};
    nt_spatial_exact_number(out, 0, 0);
    for (unsigned i = 0; i < 3; ++i) {
        NtSpatialExact factor, product, sum;
        nt_spatial_exact_number(&factor, coordinates[i], 0);
        nt_spatial_exact_multiply(&product, &weights->value[i], &factor);
        nt_spatial_exact_add(&sum, out, &product);
        nt_spatial_exact_copy(out, &sum);
    }
}

NtVec3 nt_spatial_weights_value(const NtSpatialWeights *weights)
{
    const NtSpatialExact *d = &weights->divisor;
    return (NtVec3){nt_spatial_exact_quotient(&weights->value[0], d),
                    nt_spatial_exact_quotient(&weights->value[1], d),
                    nt_spatial_exact_quotient(&weights->value[2], d)};
}

void nt_spatial_project_vertex(const NtSpatialProjection *p, const NtSpatialExact *x,
                                const NtSpatialExact *y, const NtSpatialExact *depth,
                                const NtSpatialExact *divisor, NtSpatialProjected *out)
{
    const NtSpatialExact *scale = p->kind == NT_SPATIAL_PERSPECTIVE ? depth : divisor;
    NtSpatialExact boundary;
    extent(p->half_width, scale, &boundary);
    out->screen.x = nt_spatial_exact_quotient(x, &boundary);
    extent(p->half_height, scale, &boundary);
    out->screen.y = nt_spatial_exact_quotient(y, &boundary);
    out->depth = nt_spatial_exact_quotient(depth, divisor);
}

void nt_spatial_clipped_vertex(const NtSpatialProjection *p, const NtVec3 view[3],
                                const NtSpatialWeights *weights, NtSpatialClipVertex *out)
{
    NtSpatialExact x, y, depth;
    nt_spatial_weighted(weights, view[0].x, view[1].x, view[2].x, &x);
    nt_spatial_weighted(weights, view[0].y, view[1].y, view[2].y, &y);
    nt_spatial_weighted(weights, -view[0].z, -view[1].z, -view[2].z, &depth);
    nt_spatial_project_vertex(p, &x, &y, &depth, &weights->divisor, &out->projected);
    out->view = (NtVec3){nt_spatial_exact_quotient(&x, &weights->divisor),
                         nt_spatial_exact_quotient(&y, &weights->divisor), -out->projected.depth};
    out->weights = nt_spatial_weights_value(weights);
}

NtResult nt_spatial_project(const NtSpatialProjection *p, NtVec3 view,
                            bool *visible, NtSpatialProjected *out)
{
    if (!visible || !out) return NT_RESULT_INVALID_ARGUMENT;
    fenv_t saved;
    const NtResult status = nt_spatial_begin_projection(p, &saved);
    if (status != NT_RESULT_OK) return status;
    if (!nt_spatial_finite_vec(view)) return nt_spatial_end_projection(&saved, NT_RESULT_INVALID_ARGUMENT);
    NtSpatialProjected value;
    bool inside = true;
    for (NtSpatialPlane plane = NT_SPATIAL_NEAR; plane <= NT_SPATIAL_TOP; ++plane) {
        NtSpatialExact score;
        nt_spatial_plane_score(p, plane, view, &score);
        if (score.negative) { inside = false; break; }
    }
    if (inside) {
        NtSpatialExact x, y, depth, one;
        nt_spatial_exact_number(&x, view.x, 0);
        nt_spatial_exact_number(&y, view.y, 0);
        nt_spatial_exact_number(&depth, -view.z, 0);
        nt_spatial_exact_number(&one, 1, 0);
        nt_spatial_project_vertex(p, &x, &y, &depth, &one, &value);
    }
    if (!nt_finish_standard_fp(&saved)) return NT_RESULT_OUT_OF_RANGE;
    *visible = inside;
    if (inside) *out = value;
    return NT_RESULT_OK;
}

static double unproject_axis(double coordinate, double half, const NtSpatialExact *scale,
                              const NtSpatialExact *one)
{
    NtSpatialExact a, b, product;
    nt_spatial_exact_number(&a, coordinate, 0);
    extent(half, scale, &b);
    nt_spatial_exact_multiply(&product, &a, &b);
    return nt_spatial_exact_quotient(&product, one);
}

NtResult nt_spatial_unproject(const NtSpatialProjection *p, NtSpatialScreen screen,
                              double depth, NtVec3 *out)
{
    if (!out) return NT_RESULT_INVALID_ARGUMENT;
    fenv_t saved;
    const NtResult status = nt_spatial_begin_projection(p, &saved);
    if (status != NT_RESULT_OK) return status;
    if (!nt_spatial_screen_finite(screen) || !isfinite(depth) ||
        !nt_spatial_screen_inside(screen) || depth < p->near_depth || depth > p->far_depth)
        return nt_spatial_end_projection(&saved, NT_RESULT_INVALID_ARGUMENT);
    NtSpatialExact scale, one;
    nt_spatial_exact_number(&scale, p->kind == NT_SPATIAL_PERSPECTIVE ? depth : 1, 0);
    nt_spatial_exact_number(&one, 1, 0);
    const NtVec3 value = {unproject_axis(screen.x, p->half_width, &scale, &one),
                          unproject_axis(screen.y, p->half_height, &scale, &one), -depth};
    const bool finite = nt_spatial_finite_vec(value);
    if (!nt_finish_standard_fp(&saved) || !finite) return NT_RESULT_OUT_OF_RANGE;
    *out = value;
    return NT_RESULT_OK;
}
