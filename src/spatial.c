#include "spatial_internal.h"

/* An inverse translation numerator and determinant each have at most six
 * triple products. The quotient midpoint tests add two copies of the
 * determinant, so leave room for 3 * 6 * 8 expansion terms. */
#define NT_SCALED_SUM_CAPACITY 144
#include "spatial_norm_internal.h"

static NtScaledTerm term(double value)
{
    return nt_scaled_term(value, 0);
}

static double evaluate(const NtScaledSum *sum)
{
    static const NtScaledSum one = {{{.5, 1}}, 1};
    return nt_scaled_quotient(sum, &one, one.partials[0]);
}

static void product(NtScaledSum *sum, double a, double b)
{
    nt_scaled_add_product(sum, term(a), term(b));
}

static double dot(NtVec3 a, NtVec3 b, double bias)
{
    NtScaledSum sum = {0};
    product(&sum, a.x, b.x);
    product(&sum, a.y, b.y);
    product(&sum, a.z, b.z);
    nt_scaled_add(&sum, term(bias));
    return evaluate(&sum);
}

static NtScaledSum difference_of_products(double a, double b, double c, double d)
{
    NtScaledSum sum = {0};
    product(&sum, a, b);
    product(&sum, -c, d);
    return sum;
}

static void cross(NtVec3 a, NtVec3 b, NtScaledSum out[3])
{
    out[0] = difference_of_products(a.y, b.z, a.z, b.y);
    out[1] = difference_of_products(a.z, b.x, a.x, b.z);
    out[2] = difference_of_products(a.x, b.y, a.y, b.x);
}

static bool unit_vec(NtVec3 vector, NtVec3 *unit)
{
    const NtScaledTerm components[] = {term(vector.x), term(vector.y), term(vector.z)};
    return nt_spatial_unit_terms(components, unit);
}

static NtResult finish_vec(const fenv_t *saved, NtVec3 value, NtVec3 *out)
{
    const bool finite = nt_spatial_finite_vec(value);
    if (!nt_finish_standard_fp(saved) || !finite) return NT_RESULT_OUT_OF_RANGE;
    *out = value;
    return NT_RESULT_OK;
}

static NtResult finish_scalar(const fenv_t *saved, double value, double *out)
{
    const bool finite = isfinite(value);
    if (!nt_finish_standard_fp(saved) || !finite) return NT_RESULT_OUT_OF_RANGE;
    *out = value;
    return NT_RESULT_OK;
}

static double affine_component(double base, double coefficient, double value)
{
    NtScaledSum sum = {0};
    product(&sum, coefficient, value);
    nt_scaled_add(&sum, term(base));
    return evaluate(&sum);
}

static NtResult vector_mix(NtVec3 a, NtVec3 b, double scale, NtVec3 *out)
{
    if (!out || !nt_spatial_finite_vec(a) || !nt_spatial_finite_vec(b) || !isfinite(scale))
        return NT_RESULT_INVALID_ARGUMENT;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_OUT_OF_RANGE;
    const NtVec3 value = {affine_component(a.x, scale, b.x), affine_component(a.y, scale, b.y),
                          affine_component(a.z, scale, b.z)};
    return finish_vec(&saved, value, out);
}

NtResult nt_spatial_vec_add(NtVec3 a, NtVec3 b, NtVec3 *out)
{
    return vector_mix(a, b, 1, out);
}

NtResult nt_spatial_vec_subtract(NtVec3 a, NtVec3 b, NtVec3 *out)
{
    return vector_mix(a, b, -1, out);
}

NtResult nt_spatial_vec_scale(NtVec3 vector, double factor, NtVec3 *out)
{
    return vector_mix((NtVec3){0}, vector, factor, out);
}

NtResult nt_spatial_vec_dot(NtVec3 a, NtVec3 b, double *out)
{
    if (!out || !nt_spatial_finite_vec(a) || !nt_spatial_finite_vec(b)) return NT_RESULT_INVALID_ARGUMENT;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_OUT_OF_RANGE;
    return finish_scalar(&saved, dot(a, b, 0), out);
}

NtResult nt_spatial_vec_cross(NtVec3 a, NtVec3 b, NtVec3 *out)
{
    if (!out || !nt_spatial_finite_vec(a) || !nt_spatial_finite_vec(b)) return NT_RESULT_INVALID_ARGUMENT;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_OUT_OF_RANGE;
    NtScaledSum values[3];
    cross(a, b, values);
    return finish_vec(&saved, (NtVec3){evaluate(&values[0]), evaluate(&values[1]), evaluate(&values[2])}, out);
}

NtResult nt_spatial_vec_length(NtVec3 vector, double *out)
{
    if (!out || !nt_spatial_finite_vec(vector)) return NT_RESULT_INVALID_ARGUMENT;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_OUT_OF_RANGE;
    const NtScaledSum components[] = {nt_spatial_difference(vector.x, 0),
                                      nt_spatial_difference(vector.y, 0),
                                      nt_spatial_difference(vector.z, 0)};
    const NtScaledSum squared = nt_spatial_norm_squared(components);
    return finish_scalar(&saved, nt_spatial_round_length(&squared, 0), out);
}

NtResult nt_spatial_vec_normalize(NtVec3 vector, NtVec3 *out)
{
    if (!out || !nt_spatial_finite_vec(vector)) return NT_RESULT_INVALID_ARGUMENT;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_OUT_OF_RANGE;
    NtVec3 value;
    if (!unit_vec(vector, &value)) {
        return nt_finish_standard_fp(&saved) ? NT_RESULT_INVALID_ARGUMENT : NT_RESULT_OUT_OF_RANGE;
    }
    return finish_vec(&saved, value, out);
}

NtSpatialTransform nt_spatial_transform_identity(void)
{
    return (NtSpatialTransform){.linear = {{1,0,0}, {0,1,0}, {0,0,1}}};
}

NtSpatialTransformOptions nt_spatial_transform_options_default(void)
{
    return (NtSpatialTransformOptions){.scale = {1,1,1}, .rotation_axis = {0,1,0}};
}

static bool finite_transform(const NtSpatialTransform *transform)
{
    if (!transform || !nt_spatial_finite_vec(transform->translation)) return false;
    for (size_t i = 0; i < 3; ++i)
        for (size_t j = 0; j < 3; ++j)
            if (!isfinite(transform->linear[i][j])) return false;
    return true;
}

static NtVec3 row(const NtSpatialTransform *transform, size_t i)
{
    return (NtVec3){transform->linear[i][0], transform->linear[i][1], transform->linear[i][2]};
}

static NtVec3 column(const NtSpatialTransform *transform, size_t i)
{
    return (NtVec3){transform->linear[0][i], transform->linear[1][i], transform->linear[2][i]};
}

static NtVec3 apply(const NtSpatialTransform *transform, NtVec3 vector, NtVec3 bias)
{
    return (NtVec3){dot(row(transform, 0), vector, bias.x), dot(row(transform, 1), vector, bias.y),
                    dot(row(transform, 2), vector, bias.z)};
}

static NtResult finish_transform(const fenv_t *saved, const NtSpatialTransform *value, NtSpatialTransform *out)
{
    const bool valid = finite_transform(value);
    if (!nt_finish_standard_fp(saved) || !valid) return NT_RESULT_OUT_OF_RANGE;
    *out = *value;
    return NT_RESULT_OK;
}

static double anchored(NtVec3 linear, NtVec3 pivot, double component, double translation)
{
    NtScaledSum sum = {0};
    product(&sum, linear.x, -pivot.x);
    product(&sum, linear.y, -pivot.y);
    product(&sum, linear.z, -pivot.z);
    nt_scaled_add(&sum, term(component));
    nt_scaled_add(&sum, term(translation));
    return evaluate(&sum);
}

static double rotation_diagonal(double axis, double cosine, double complement)
{
    const double square = axis*axis;
    if (square > .5) return fma(-complement, fma(-axis, axis, 1), 1);
    return fma(square, complement, cosine);
}

NtResult nt_spatial_transform_create(const NtSpatialTransformOptions *options, NtSpatialTransform *out)
{
    if (!options || !out || !nt_spatial_finite_vec(options->translation) ||
        !nt_spatial_finite_vec(options->pivot) || !nt_spatial_finite_vec(options->scale) ||
        !nt_spatial_finite_vec(options->rotation_axis) || !isfinite(options->rotation_radians))
        return NT_RESULT_INVALID_ARGUMENT;

    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_OUT_OF_RANGE;
    NtSpatialTransform value = nt_spatial_transform_identity();
    if (options->rotation_radians != 0) {
        NtVec3 axis;
        if (!unit_vec(options->rotation_axis, &axis))
            return nt_finish_standard_fp(&saved) ? NT_RESULT_INVALID_ARGUMENT : NT_RESULT_OUT_OF_RANGE;
        const double c = cos(options->rotation_radians), s = sin(options->rotation_radians);
        const double half_sine = sin(.5 * options->rotation_radians);
        const double d = 2 * half_sine * half_sine;
        const double x = axis.x, y = axis.y, z = axis.z;
        value = (NtSpatialTransform){.linear = {
            {rotation_diagonal(x,c,d), fma(x*y,d,-z*s), fma(x*z,d,y*s)},
            {fma(y*x,d,z*s), rotation_diagonal(y,c,d), fma(y*z,d,-x*s)},
            {fma(z*x,d,-y*s), fma(z*y,d,x*s), rotation_diagonal(z,c,d)}
        }};
    }
    const double scale[] = {options->scale.x, options->scale.y, options->scale.z};
    for (size_t i = 0; i < 3; ++i)
        for (size_t j = 0; j < 3; ++j)
            value.linear[i][j] = fmax(-1, fmin(1, value.linear[i][j])) * scale[j];
    const NtVec3 pivot = options->pivot, move = options->translation;
    value.translation = (NtVec3){anchored(row(&value, 0), pivot, pivot.x, move.x),
                                 anchored(row(&value, 1), pivot, pivot.y, move.y),
                                 anchored(row(&value, 2), pivot, pivot.z, move.z)};
    return finish_transform(&saved, &value, out);
}

NtResult nt_spatial_transform_compose(const NtSpatialTransform *outer,
                                      const NtSpatialTransform *inner, NtSpatialTransform *out)
{
    if (!out || !finite_transform(outer) || !finite_transform(inner)) return NT_RESULT_INVALID_ARGUMENT;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_OUT_OF_RANGE;
    NtSpatialTransform result;
    for (size_t i = 0; i < 3; ++i)
        for (size_t j = 0; j < 3; ++j)
            result.linear[i][j] = dot(row(outer, i), column(inner, j), 0);
    result.translation = apply(outer, inner->translation, outer->translation);
    return finish_transform(&saved, &result, out);
}

NtResult nt_spatial_transform_inverse(const NtSpatialTransform *transform, NtSpatialTransform *out)
{
    if (!out || !finite_transform(transform)) return NT_RESULT_INVALID_ARGUMENT;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_OUT_OF_RANGE;
    NtScaledSum adjugate[3][3];
    for (size_t i = 0; i < 3; ++i) {
        const size_t column_a = (i+1)%3, column_b = (i+2)%3;
        for (size_t j = 0; j < 3; ++j) {
            const size_t row_a = (j+1)%3, row_b = (j+2)%3;
            adjugate[i][j] = difference_of_products(transform->linear[row_a][column_a],
                transform->linear[row_b][column_b], transform->linear[row_a][column_b],
                transform->linear[row_b][column_a]);
        }
    }
    NtScaledSum determinant = {0};
    for (size_t i = 0; i < 3; ++i)
        nt_scaled_add_sum(&determinant, &adjugate[0][i], term(transform->linear[i][0]));
    if (!determinant.count)
        return nt_finish_standard_fp(&saved) ? NT_RESULT_INVALID_ARGUMENT : NT_RESULT_OUT_OF_RANGE;
    const NtScaledTerm divisor = nt_scaled_value(&determinant);
    const double offset[] = {-transform->translation.x, -transform->translation.y, -transform->translation.z};
    double translation[3];
    NtSpatialTransform inverse;
    for (size_t i = 0; i < 3; ++i) {
        NtScaledSum moved = {0};
        for (size_t j = 0; j < 3; ++j) {
            inverse.linear[i][j] = nt_scaled_quotient(&adjugate[i][j], &determinant, divisor);
            nt_scaled_add_sum(&moved, &adjugate[i][j], term(offset[j]));
        }
        translation[i] = nt_scaled_quotient(&moved, &determinant, divisor);
    }
    inverse.translation = (NtVec3){translation[0], translation[1], translation[2]};
    return finish_transform(&saved, &inverse, out);
}

static NtResult transform_vector(const NtSpatialTransform *transform, NtVec3 vector, bool translate, NtVec3 *out)
{
    if (!out || !finite_transform(transform) || !nt_spatial_finite_vec(vector)) return NT_RESULT_INVALID_ARGUMENT;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_OUT_OF_RANGE;
    return finish_vec(&saved, apply(transform, vector, translate ? transform->translation : (NtVec3){0}), out);
}

NtResult nt_spatial_transform_point(const NtSpatialTransform *transform, NtVec3 point, NtVec3 *out)
{
    return transform_vector(transform, point, true, out);
}

NtResult nt_spatial_transform_direction(const NtSpatialTransform *transform, NtVec3 direction, NtVec3 *out)
{
    return transform_vector(transform, direction, false, out);
}

static bool finite_camera(const NtSpatialCamera *camera)
{
    return camera && nt_spatial_finite_vec(camera->position) && nt_spatial_finite_vec(camera->right) &&
        nt_spatial_finite_vec(camera->up) && nt_spatial_finite_vec(camera->backward);
}

static double simple_dot(NtVec3 a, NtVec3 b)
{
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

static bool camera_basis_valid(const NtSpatialCamera *camera)
{
    const double tolerance = 64 * DBL_EPSILON;
    const NtVec3 r = camera->right, u = camera->up, b = camera->backward;
    const NtVec3 cross_r_u = {r.y*u.z-r.z*u.y, r.z*u.x-r.x*u.z, r.x*u.y-r.y*u.x};
    return fabs(simple_dot(r, r)-1) <= tolerance && fabs(simple_dot(u, u)-1) <= tolerance &&
        fabs(simple_dot(b, b)-1) <= tolerance && fabs(simple_dot(r, u)) <= tolerance &&
        fabs(simple_dot(r, b)) <= tolerance && fabs(simple_dot(u, b)) <= tolerance &&
        fabs(simple_dot(cross_r_u, b)-1) <= tolerance;
}

static NtResult finish_camera(const fenv_t *saved, const NtSpatialCamera *camera, NtSpatialCamera *out)
{
    const bool finite = finite_camera(camera);
    if (!nt_finish_standard_fp(saved) || !finite) return NT_RESULT_OUT_OF_RANGE;
    *out = *camera;
    return NT_RESULT_OK;
}

static void cross_difference(NtScaledSum *out, double a, const NtScaledSum *b,
                             double c, const NtScaledSum *d)
{
    nt_scaled_add_sum(out, b, term(a));
    nt_scaled_add_sum(out, d, term(-c));
}

NtResult nt_spatial_camera_look_at(NtVec3 position, NtVec3 target, NtVec3 up_hint, NtSpatialCamera *out)
{
    if (!out || !nt_spatial_finite_vec(position) || !nt_spatial_finite_vec(target) ||
        !nt_spatial_finite_vec(up_hint)) return NT_RESULT_INVALID_ARGUMENT;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_OUT_OF_RANGE;
    const NtScaledSum ray[] = {nt_spatial_difference(position.x, target.x),
                               nt_spatial_difference(position.y, target.y),
                               nt_spatial_difference(position.z, target.z)};
    NtScaledSum right[3] = {0};
    cross_difference(&right[0], up_hint.y, &ray[2], up_hint.z, &ray[1]);
    cross_difference(&right[1], up_hint.z, &ray[0], up_hint.x, &ray[2]);
    cross_difference(&right[2], up_hint.x, &ray[1], up_hint.y, &ray[0]);

    NtSpatialCamera camera = {.position = position};
    if (!nt_spatial_unit_sums(ray, &camera.backward) || !nt_spatial_unit_sums(right, &camera.right))
        return nt_finish_standard_fp(&saved) ? NT_RESULT_INVALID_ARGUMENT : NT_RESULT_OUT_OF_RANGE;
    NtScaledSum up[3];
    cross(camera.backward, camera.right, up);
    (void)nt_spatial_unit_sums(up, &camera.up);
    return finish_camera(&saved, &camera, out);
}

NtResult nt_spatial_camera_orbit(NtVec3 target, double radius, double azimuth,
                                double elevation, NtSpatialCamera *out)
{
    const double pole = 0x1.921fb54442d18p+0;
    if (!out || !nt_spatial_finite_vec(target) || !isfinite(radius) || !isfinite(azimuth) ||
        !isfinite(elevation) || fabs(elevation) > pole || radius <= 0) return NT_RESULT_INVALID_ARGUMENT;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_OUT_OF_RANGE;
    const double ca = cos(azimuth), sa = sin(azimuth);
    const double ce = fabs(elevation) == pole ? 0 : cos(elevation), se = sin(elevation);
    NtSpatialCamera camera = {.right = {ca, 0, -sa}, .up = {-sa*se, ce, -ca*se},
                              .backward = {sa*ce, se, ca*ce}};
    camera.position = (NtVec3){affine_component(target.x, radius, camera.backward.x),
                               affine_component(target.y, radius, camera.backward.y),
                               affine_component(target.z, radius, camera.backward.z)};
    return finish_camera(&saved, &camera, out);
}

NtResult nt_spatial_camera_fly(const NtSpatialCamera *camera, NtVec3 local_translation,
                              double yaw, double pitch, double roll, NtSpatialCamera *out)
{
    if (!out || !finite_camera(camera) || !nt_spatial_finite_vec(local_translation) ||
        !isfinite(yaw) || !isfinite(pitch) || !isfinite(roll)) return NT_RESULT_INVALID_ARGUMENT;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_OUT_OF_RANGE;
    if (!camera_basis_valid(camera))
        return nt_finish_standard_fp(&saved) ? NT_RESULT_INVALID_ARGUMENT : NT_RESULT_OUT_OF_RANGE;

    const NtVec3 r = camera->right, u = camera->up, b = camera->backward;
    const NtSpatialTransform world = {.linear = {{r.x,u.x,b.x}, {r.y,u.y,b.y}, {r.z,u.z,b.z}}};
    NtSpatialCamera value = *camera;
    value.position = apply(&world, local_translation, camera->position);
    if (yaw != 0 || pitch != 0 || roll != 0) {
        const double cy = cos(yaw), sy = sin(yaw), cp = cos(pitch), sp = sin(pitch);
        const double cr = cos(roll), sr = sin(roll);
        const NtVec3 up = {-cy*sr+sy*sp*cr, cp*cr, sy*sr+cy*sp*cr};
        const NtVec3 backward = {sy*cp, -sp, cy*cp};
        (void)unit_vec(apply(&world, backward, (NtVec3){0}), &value.backward);
        NtScaledSum axes[3];
        cross(apply(&world, up, (NtVec3){0}), value.backward, axes);
        (void)nt_spatial_unit_sums(axes, &value.right);
        cross(value.backward, value.right, axes);
        (void)nt_spatial_unit_sums(axes, &value.up);
    }
    return finish_camera(&saved, &value, out);
}

static double view_coordinate(NtVec3 axis, NtVec3 point, NtVec3 position)
{
    NtScaledSum sum = {0};
    product(&sum, axis.x, point.x);
    product(&sum, axis.y, point.y);
    product(&sum, axis.z, point.z);
    product(&sum, axis.x, -position.x);
    product(&sum, axis.y, -position.y);
    product(&sum, axis.z, -position.z);
    return evaluate(&sum);
}

NtResult nt_spatial_camera_to_view(const NtSpatialCamera *camera, NtVec3 world_point, NtVec3 *out)
{
    if (!out || !finite_camera(camera) || !nt_spatial_finite_vec(world_point)) return NT_RESULT_INVALID_ARGUMENT;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_OUT_OF_RANGE;
    if (!camera_basis_valid(camera))
        return nt_finish_standard_fp(&saved) ? NT_RESULT_INVALID_ARGUMENT : NT_RESULT_OUT_OF_RANGE;
    const NtVec3 position = camera->position;
    const NtVec3 value = {view_coordinate(camera->right, world_point, position),
                          view_coordinate(camera->up, world_point, position),
                          view_coordinate(camera->backward, world_point, position)};
    return finish_vec(&saved, value, out);
}
