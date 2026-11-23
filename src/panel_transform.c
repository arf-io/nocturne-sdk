#include "panel_transform_internal.h"

#include "animation_internal.h"
#include "color_internal.h"
#include "fp_environment_internal.h"

#include <fenv.h>
#include <float.h>
#include <math.h>

#if defined(__clang__) && !defined(__wasm__)
#pragma STDC FENV_ACCESS ON
#elif defined(_MSC_VER)
#pragma fenv_access(on)
#endif

bool nt_panel_begin_geometry(fenv_t *saved)
{
    return nt_begin_standard_fp(saved);
}

bool nt_panel_finish_geometry(const fenv_t *saved)
{
    return nt_finish_standard_fp(saved);
}

static bool point_finite(NtPoint point)
{
    return isfinite(point.x) && isfinite(point.y);
}

static bool pose_valid(const NtPanelPose *pose)
{
    return point_finite(pose->anchor) && point_finite(pose->offset) &&
        point_finite(pose->scale) && isfinite(pose->turns) && nt_unit_interval(pose->opacity);
}

static NtPoint rotation(double turns)
{
    turns = remainder(turns,1);
    const double absolute = fabs(turns);
    const bool behind = absolute > .25;
    const double quadrant = behind ? .5-absolute : absolute;
    const bool swap = quadrant > .125;
    const double octant = swap ? .25-quadrant : quadrant;
    const double angle = octant * 6.28318530717958647692;
    NtPoint unit = {1,0};
    if (octant == .125) {
        const double diagonal = sqrt(.5);
        unit = (NtPoint){diagonal,diagonal};
    } else if (octant) {
        unit = (NtPoint){cos(angle),sin(angle)};
    }
    const double cosine = swap ? unit.y : unit.x;
    const double sine = swap ? unit.x : unit.y;
    return (NtPoint){behind ? -cosine : cosine,copysign(sine,turns)};
}

static void add_product(NtScaledSum *sum, double a, double b)
{
    if (a && b) nt_scaled_add_product(sum,nt_scaled_term(a,0),nt_scaled_term(b,0));
}

static void add_triple(NtScaledSum *sum, double a, double b, double c)
{
    if (!a || !b || !c) return;
    const NtScaledTerm first = nt_scaled_term(a,0), second = nt_scaled_term(b,0), third = nt_scaled_term(c,0);
    const int exponent = first.exponent+second.exponent;
    const double high = first.fraction*second.fraction;
    nt_scaled_add_product(sum,nt_scaled_term(high,exponent),third);
    nt_scaled_add_product(sum,nt_scaled_term(fma(first.fraction,second.fraction,-high),exponent),third);
}

static double sum_double(const NtScaledSum *sum)
{
    static const NtScaledSum unit = {.partials={{.5,1}},.count=1};
    return nt_scaled_quotient(sum,&unit,unit.partials[0]);
}

static double divide_for_cell(const NtScaledSum *sum, const NtPanelAxis *axis)
{
    const double value = nt_scaled_quotient(sum,&axis->divisor,axis->approximate_divisor);
    /* Nearest rounding can cross a representable integer boundary
     * only by landing on that boundary. Preserve the source cell. */
    if (isfinite(value) && floor(value) == value &&
        nt_scaled_compare_boundary(sum,&axis->divisor,value) < 0) return nextafter(value,-INFINITY);
    return value;
}

static void prepare_axis(NtPanelAxis *axis, NtPoint rotation, double scale, double anchor)
{
    /* Invert the same binary rotation matrix used for forward projection.
     * Its determinant is cosine²+sine², which may differ from one. */
    add_triple(&axis->divisor,scale,rotation.x,rotation.x);
    add_triple(&axis->divisor,scale,rotation.y,rotation.y);
    axis->approximate_divisor = nt_scaled_value(&axis->divisor);
    nt_scaled_add_sum(&axis->origin,&axis->divisor,nt_scaled_term(anchor,0));
}

NtResult nt_panel_basis(const NtPanelPose *pose, NtPanelBasis *out)
{
    fenv_t saved;
    if (!nt_panel_begin_geometry(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    if (!pose_valid(pose)) {
        (void)nt_panel_finish_geometry(&saved);
        return NT_RESULT_INVALID_ARGUMENT;
    }
    if (!pose->opacity || !pose->scale.x || !pose->scale.y) {
        if (!nt_panel_finish_geometry(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
        *out = (NtPanelBasis){0};
        return NT_RESULT_OK;
    }
    const NtPoint rot = rotation(pose->turns);
    NtPanelBasis basis = {.cosine=rot.x,.sine=rot.y,.drawable=true};
    prepare_axis(&basis.x,rot,pose->scale.x,pose->anchor.x);
    prepare_axis(&basis.y,rot,pose->scale.y,pose->anchor.y);
    NtScaledSum *x = &basis.x.origin, *y = &basis.y.origin;
    /* Rotate destination minus the placed anchor before scaling. The
     * precomputed numerator also contains the source anchor term. */
    add_product(x,-rot.x,pose->anchor.x);
    add_product(x,-rot.x,pose->offset.x);
    add_product(x,-rot.y,pose->anchor.y);
    add_product(x,-rot.y,pose->offset.y);
    add_product(y,rot.y,pose->anchor.x);
    add_product(y,rot.y,pose->offset.x);
    add_product(y,-rot.x,pose->anchor.y);
    add_product(y,-rot.x,pose->offset.y);
    if (!nt_panel_finish_geometry(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    *out = basis;
    return NT_RESULT_OK;
}

NtResult nt_panel_inverse_point(const NtPanelBasis *basis, NtPoint destination, NtPoint *out)
{
    NtScaledSum x = basis->x.origin, y = basis->y.origin;
    add_product(&x,basis->cosine,destination.x);
    add_product(&x,basis->sine,destination.y);
    add_product(&y,basis->cosine,destination.y);
    add_product(&y,-basis->sine,destination.x);
    const NtPoint source = {divide_for_cell(&x,&basis->x),divide_for_cell(&y,&basis->y)};
    if (!point_finite(source)) return NT_RESULT_OUT_OF_RANGE;
    *out = source;
    return NT_RESULT_OK;
}

NtResult nt_panel_source_cell(NtPanelPixel pixel, NtCellRect region, NtCellPoint *out, bool *inside)
{
    fenv_t saved;
    if (!nt_panel_begin_geometry(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    const bool valid = point_finite(pixel.source) && nt_unit_interval(pixel.opacity);
    const bool covered = valid && pixel.opacity > 0 &&
        pixel.source.x >= 0 && pixel.source.y >= 0 &&
        pixel.source.x < region.size.columns && pixel.source.y < region.size.rows;
    NtCellPoint source = {0};
    if (covered) {
        /* Force the conversions inside the geometry scope. AArch64 GCC
         * otherwise moves them past the restore, where they raise FE_INEXACT. */
        const volatile int32_t column = (int32_t)pixel.source.x;
        const volatile int32_t row = (int32_t)pixel.source.y;
        source = (NtCellPoint){region.origin.x + column, region.origin.y + row};
    }
    if (!nt_panel_finish_geometry(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    if (!valid) return NT_RESULT_INVALID_ARGUMENT;
    *inside = covered;
    if (covered) *out = source;
    return NT_RESULT_OK;
}

NtResult nt_panel_project_point(const NtPanelPose *pose, NtPoint source, NtPoint *out)
{
    if (!pose || !out) return NT_RESULT_INVALID_ARGUMENT;
    fenv_t saved;
    if (!nt_panel_begin_geometry(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    if (!pose_valid(pose) || !point_finite(source)) {
        (void)nt_panel_finish_geometry(&saved);
        return NT_RESULT_INVALID_ARGUMENT;
    }
    const NtPoint rot = rotation(pose->turns);
    NtScaledSum x = {0}, y = {0};
    add_triple(&x,rot.x,pose->scale.x,source.x);
    add_triple(&x,-rot.x,pose->scale.x,pose->anchor.x);
    add_triple(&x,-rot.y,pose->scale.y,source.y);
    add_triple(&x,rot.y,pose->scale.y,pose->anchor.y);
    nt_scaled_add(&x,nt_scaled_term(pose->anchor.x,0));
    nt_scaled_add(&x,nt_scaled_term(pose->offset.x,0));
    add_triple(&y,rot.y,pose->scale.x,source.x);
    add_triple(&y,-rot.y,pose->scale.x,pose->anchor.x);
    add_triple(&y,rot.x,pose->scale.y,source.y);
    add_triple(&y,-rot.x,pose->scale.y,pose->anchor.y);
    nt_scaled_add(&y,nt_scaled_term(pose->anchor.y,0));
    nt_scaled_add(&y,nt_scaled_term(pose->offset.y,0));
    const NtPoint point = {sum_double(&x),sum_double(&y)};
    const bool finite = point_finite(point);
    if (!nt_panel_finish_geometry(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    if (!finite) return NT_RESULT_OUT_OF_RANGE;
    *out = point;
    return NT_RESULT_OK;
}

static NtPoint mix_point(NtPoint a, NtPoint b, double weight)
{
    return (NtPoint){nt_animation_mix(a.x,b.x,weight),nt_animation_mix(a.y,b.y,weight)};
}

NtResult nt_panel_pose_at(const NtPanelTransition *transition, NtTimeUs elapsed_us,
                         NtMotionPolicy motion, NtPanelPose *out)
{
    if (!transition || !out ||
        transition->easing < NT_LINEAR || transition->easing > NT_STEP_END ||
        (motion != NT_MOTION_FULL && motion != NT_MOTION_REDUCED))
        return NT_RESULT_INVALID_ARGUMENT;
    if (transition->duration_us > UINT64_MAX-transition->delay_us) return NT_RESULT_OUT_OF_RANGE;
    fenv_t saved;
    if (!nt_panel_begin_geometry(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    if (!pose_valid(&transition->from) || !pose_valid(&transition->to)) {
        (void)nt_panel_finish_geometry(&saved);
        return NT_RESULT_INVALID_ARGUMENT;
    }
    NtAnimationSample sample;
    nt_animation_timing(elapsed_us,transition->delay_us,transition->duration_us,NT_ONCE,&sample);
    double weight;
    const NtStatus result = nt_ease(transition->easing,
                                    motion == NT_MOTION_REDUCED ? 1 : sample.progress, &weight);
    if (result != NT_OK) {
        (void)nt_panel_finish_geometry(&saved);
        return NT_RESULT_INVALID_ARGUMENT;
    }
    const NtPanelPose a = transition->from, b = transition->to;
    const NtPanelPose pose = {
        .anchor=mix_point(a.anchor,b.anchor,weight),
        .offset=mix_point(a.offset,b.offset,weight),
        .scale=mix_point(a.scale,b.scale,weight),
        .turns=nt_animation_mix(a.turns,b.turns,weight),
        .opacity=nt_animation_mix(a.opacity,b.opacity,weight)
    };
    const bool valid = pose_valid(&pose);
    if (!nt_panel_finish_geometry(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    if (!valid) return NT_RESULT_OUT_OF_RANGE;
    *out = pose;
    return NT_RESULT_OK;
}
