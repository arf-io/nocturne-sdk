#include "plot_internal.h"
#include "fp_environment_internal.h"
#include "scaled_sum_internal.h"

#include <math.h>

typedef struct Scaled { double fraction; int exponent; } Scaled;

static Scaled scale_of(double n)
{
    Scaled scaled;
    scaled.fraction = frexp(n, &scaled.exponent);
    return scaled;
}

static Scaled span_of(double a, double b)
{
    const double difference = b - a;
    if (isfinite(difference)) return scale_of(difference);
    Scaled scale = scale_of(b * .5 - a * .5);
    ++scale.exponent;
    return scale;
}

static Scaled divide(Scaled a, Scaled b)
{
    Scaled result = scale_of(a.fraction / b.fraction);
    result.exponent += a.exponent - b.exponent;
    return result;
}

static bool smaller(Scaled a, Scaled b)
{
    return a.exponent == b.exponent ? a.fraction < b.fraction : a.exponent < b.exponent;
}

static double factor(Scaled a, Scaled b)
{
    const Scaled ratio = divide(a, b);
    return scalbn(ratio.fraction, ratio.exponent);
}

bool nt_plot_domain_valid(NtPlotDomain domain)
{
    return isfinite(domain.min) && isfinite(domain.max) && domain.min <= domain.max;
}

bool nt_plot_aspect_valid(double aspect)
{
    return isfinite(aspect) && aspect >= 0;
}

static bool finite_point(NtPoint point)
{
    return isfinite(point.x) && isfinite(point.y);
}

static NtPoint fit_spans(NtPoint extents, double aspect, Scaled width, Scaled height)
{
    const Scaled physical = scale_of(aspect ? aspect : .5);
    const Scaled x = divide(divide(width, scale_of(extents.x)), physical);
    const Scaled y = divide(height, scale_of(extents.y));
    if (smaller(x,y)) extents.x *= factor(x,y);
    else extents.y *= factor(y,x);
    return extents;
}

NtPoint nt_plot_fit(NtPoint extents, double aspect, double width, double height)
{
    return fit_spans(extents, aspect, scale_of(width), scale_of(height));
}

static double fraction(NtPlotDomain domain, double value)
{
    if (domain.min == domain.max) return .5;
    if (value == domain.min) return 0;
    if (value == domain.max) return 1;
    return factor(span_of(domain.min, value), span_of(domain.min, domain.max));
}

NtPoint nt_plot_project_inside(const NtPlotViewport *viewport, NtPoint value)
{
    const NtPlotBounds domain = viewport->domain;
    const NtPoint area = {viewport->cells.size.columns, viewport->cells.size.rows};
    NtPoint extent = area;
    if (viewport->equal_units && domain.x.min < domain.x.max && domain.y.min < domain.y.max)
        extent = fit_spans(area, viewport->cell_aspect,
                           span_of(domain.x.min, domain.x.max), span_of(domain.y.min, domain.y.max));
    const double x = viewport->cells.origin.x + (area.x - extent.x) * .5;
    const double y = viewport->cells.origin.y + (area.y - extent.y) * .5;
    return (NtPoint){fma(fraction(domain.x, value.x), extent.x, x),
                     fma(fraction(domain.y, value.y), -extent.y, y + extent.y)};
}

enum { LEFT=1, RIGHT=2, BOTTOM=4, TOP=8 };

static unsigned region(NtPlotBounds b, NtPoint p)
{
    return (p.x < b.x.min ? LEFT : p.x > b.x.max ? RIGHT : 0) |
           (p.y < b.y.min ? BOTTOM : p.y > b.y.max ? TOP : 0);
}

NtResult nt_plot_project(const NtPlotViewport *viewport, NtPoint value, NtPoint *out)
{
    if (!viewport || !out) return NT_RESULT_INVALID_ARGUMENT;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    NtResult result = NT_RESULT_OK;
    NtPoint projected = {0};
    if (!finite_point(value) || !nt_plot_domain_valid(viewport->domain.x) ||
        !nt_plot_domain_valid(viewport->domain.y) || !nt_plot_aspect_valid(viewport->cell_aspect) ||
        !viewport->cells.size.columns || !viewport->cells.size.rows) result = NT_RESULT_INVALID_ARGUMENT;
    else if (region(viewport->domain,value)) result = NT_RESULT_OUT_OF_RANGE;
    else projected = nt_plot_project_inside(viewport,value);
    if (!nt_finish_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    if (result == NT_RESULT_OK) *out = projected;
    return result;
}

typedef struct ClipEdge { double value; bool x; } ClipEdge;
typedef struct ClipRatio { double n1, n0, d1, d0; } ClipRatio;

static ClipRatio edge_ratio(ClipEdge edge, NtPoint a, NtPoint b)
{
    const double from = edge.x ? a.x : a.y, to = edge.x ? b.x : b.y;
    return from < to ? (ClipRatio){edge.value,from,to,from} : (ClipRatio){from,edge.value,from,to};
}

static void product(NtScaledSum *sum, double first, double second)
{
    nt_scaled_add_product(sum,nt_scaled_term(first,0),nt_scaled_term(second,0));
}

/* Compare the directed parameters without dividing. Both denominators are
 * positive. At most sixteen partials represent the eight product terms. */
static int compare_ratios(ClipRatio a, ClipRatio b)
{
    NtScaledSum difference = {0};
    product(&difference,a.n1,b.d1);
    product(&difference,-a.n1,b.d0);
    product(&difference,-a.n0,b.d1);
    product(&difference,a.n0,b.d0);
    product(&difference,-b.n1,a.d1);
    product(&difference,b.n1,a.d0);
    product(&difference,b.n0,a.d1);
    product(&difference,-b.n0,a.d0);
    if (!difference.count) return 0;
    return difference.partials[difference.count-1].fraction < 0 ? -1 : 1;
}

static ClipEdge select_edge(NtPlotBounds box, unsigned outside, NtPoint a, NtPoint b, bool entering)
{
    const ClipEdge x = {outside & LEFT ? box.x.min : box.x.max,true};
    const ClipEdge y = {outside & BOTTOM ? box.y.min : box.y.max,false};
    if (!(outside & (LEFT | RIGHT))) return y;
    if (!(outside & (BOTTOM | TOP))) return x;
    const int order = compare_ratios(edge_ratio(x,a,b),edge_ratio(y,a,b));
    return (entering ? order >= 0 : order <= 0) ? x : y;
}

static double cross_at(double x0, double y0, double x1, double y1, double x)
{
    if (x == x0 || y0 == y1) return y0;
    if (x == x1) return y1;
    NtScaledSum sum = {0}, denominator = {0};
    product(&sum,y0,x1);
    product(&sum,-y0,x);
    product(&sum,y1,x);
    product(&sum,-y1,x0);
    nt_scaled_add(&denominator,nt_scaled_term(x1,0));
    nt_scaled_add(&denominator,nt_scaled_term(-x0,0));
    return nt_scaled_quotient(&sum,&denominator,nt_scaled_value(&denominator));
}

static NtPoint intersect(ClipEdge edge, NtPoint a, NtPoint b)
{
    return edge.x ? (NtPoint){edge.value,cross_at(a.x,a.y,b.x,b.y,edge.value)} :
                    (NtPoint){cross_at(a.y,a.x,b.y,b.x,edge.value),edge.value};
}

static bool clip_line(NtPlotBounds domain, NtPoint a, NtPoint b, NtPoint *first, NtPoint *last)
{
    const unsigned ca = region(domain,a), cb = region(domain,b);
    if (ca & cb) return false;
    const ClipEdge enter = ca ? select_edge(domain,ca,a,b,true) : (ClipEdge){0};
    const ClipEdge exit = cb ? select_edge(domain,cb,a,b,false) : (ClipEdge){0};
    if (ca && cb && compare_ratios(edge_ratio(enter,a,b),edge_ratio(exit,a,b)) > 0) return false;
    *first = ca ? intersect(enter,a,b) : a;
    *last = cb ? intersect(exit,a,b) : b;
    return true;
}

NtResult nt_plot_clip_segment(NtPlotBounds domain, NtPoint a, NtPoint b,
                              bool *visible, NtPoint *first, NtPoint *last)
{
    if (!visible || !first || !last || first == last) return NT_RESULT_INVALID_ARGUMENT;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    const bool valid = finite_point(a) && finite_point(b) && nt_plot_domain_valid(domain.x) &&
                       nt_plot_domain_valid(domain.y);
    NtPoint entered, exited;
    const bool hit = valid && clip_line(domain,a,b,&entered,&exited);
    if (!nt_finish_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    if (!valid) return NT_RESULT_INVALID_ARGUMENT;
    *visible = hit;
    if (hit) {
        *first = entered;
        *last = exited;
    }
    return NT_RESULT_OK;
}
