#include "plot_internal.h"
#include "property_internal.h"

#include <limits.h>
#include <math.h>
#include <string.h>

typedef struct Plot {
    NtAllocator allocator;
    NtPlotOptions options;
    NtPlotDatum *data;
    double *cumulative;
    double progress;
    NtMotionPolicy motion;
} Plot;

typedef struct ChartRaster {
    NtCanvas *canvas;
    NtPlotViewport viewport;
    NtCellRect clip;
    NtCellSize grid;
} ChartRaster;

NT_DECLARE_ALIGNMENT(Plot);
NT_DECLARE_ALIGNMENT(NtPlotDatum);
typedef double PlotWeight;
NT_DECLARE_ALIGNMENT(PlotWeight);

static bool radial(NtPlotKind kind)
{
    return kind == NT_PLOT_PIE || kind == NT_PLOT_DONUT;
}

static NtResult validate(const NtPlotOptions *options)
{
    if (!options || options->kind < NT_PLOT_BARS || options->kind > NT_PLOT_EDGES ||
        options->sampling < NT_SAMPLE_CELL || options->sampling > NT_SAMPLE_HYBRID ||
        options->reveal.easing < NT_LINEAR || options->reveal.easing > NT_STEP_END ||
        (options->count && !options->data) || (options->kind == NT_PLOT_EDGES && options->count % 2) ||
        !nt_plot_aspect_valid(options->cell_aspect)) return NT_RESULT_INVALID_ARGUMENT;
    const NtPlotKind kind = options->kind;
    if ((kind == NT_PLOT_BARS && !isfinite(options->baseline)) ||
        (kind == NT_PLOT_DONUT && (!isfinite(options->hole_ratio) || options->hole_ratio < 0 ||
                                  options->hole_ratio >= 1)) ||
        (!radial(kind) && ((!options->auto_y && !nt_plot_domain_valid(options->domain.y)) ||
         (kind != NT_PLOT_BARS && !options->auto_x && !nt_plot_domain_valid(options->domain.x)))))
        return NT_RESULT_INVALID_ARGUMENT;
    if ((options->data_limit && options->count > options->data_limit) ||
        options->count > SIZE_MAX / sizeof(NtPlotDatum) || options->count > SIZE_MAX / sizeof(PlotWeight) ||
        options->reveal.duration_us > UINT64_MAX - options->reveal.start_us) return NT_RESULT_OUT_OF_RANGE;
    for (size_t i = 0; i < options->count; ++i) {
        const NtPoint point = options->data[i].point;
        if (!isfinite(point.y) || (kind >= NT_PLOT_SCATTER && !isfinite(point.x)) ||
            (radial(kind) && point.y < 0)) return NT_RESULT_INVALID_ARGUMENT;
    }
    return NT_RESULT_OK;
}

static NtPlotDomain automatic_domain(const NtPlotOptions *options, bool vertical)
{
    NtPlotDomain domain = {0,1};
    if (!options->count) return domain;
    const NtPoint first = options->data[0].point;
    const double seed = vertical ? first.y : first.x;
    domain = (NtPlotDomain){seed,seed};
    if (options->kind == NT_PLOT_BARS) {
        domain.min = fmin(seed,options->baseline);
        domain.max = fmax(seed,options->baseline);
    }
    for (size_t i = 1; i < options->count; ++i) {
        const NtPoint point = options->data[i].point;
        const double value = vertical ? point.y : point.x;
        domain.min = fmin(domain.min,value);
        domain.max = fmax(domain.max,value);
    }
    return domain;
}

static void release_data(Plot *state)
{
    nt_release(&state->allocator,state->cumulative,state->options.count * sizeof(PlotWeight),NT_ALIGNMENT(PlotWeight));
    nt_release(&state->allocator,state->data,state->options.count * sizeof(NtPlotDatum),NT_ALIGNMENT(NtPlotDatum));
}

static NtResult configure(Plot *state, const NtPlotOptions *options)
{
    state->options = *options;
    state->data = NULL;
    state->cumulative = NULL;
    const size_t count = options->count;
    if (count) {
        state->data = nt_allocate(&state->allocator,count * sizeof(NtPlotDatum),NT_ALIGNMENT(NtPlotDatum));
        if (!state->data) return NT_RESULT_ALLOCATION_FAILED;
        memcpy(state->data,options->data,count * sizeof(NtPlotDatum));
    }
    state->options.data = state->data;
    if (radial(options->kind)) {
        if (!count) return NT_RESULT_OK;
        state->cumulative = nt_allocate(&state->allocator,count * sizeof(PlotWeight),NT_ALIGNMENT(PlotWeight));
        if (!state->cumulative) {
            release_data(state);
            return NT_RESULT_ALLOCATION_FAILED;
        }
        double largest = 0;
        for (size_t i = 0; i < count; ++i) largest = fmax(largest,state->data[i].point.y);
        double sum = 0;
        for (size_t i = 0; i < count; ++i) {
            if (largest > 0) sum += state->data[i].point.y / largest;
            state->cumulative[i] = sum;
        }
    } else {
        if (options->kind == NT_PLOT_BARS) state->options.domain.x = (NtPlotDomain){0,1};
        else if (options->auto_x) state->options.domain.x = automatic_domain(&state->options,false);
        if (options->auto_y) state->options.domain.y = automatic_domain(&state->options,true);
    }
    return NT_RESULT_OK;
}

static double reveal_at(NtPlotReveal reveal, NtTimeUs now, NtMotionPolicy motion)
{
    if (motion == NT_MOTION_REDUCED || !reveal.duration_us) return 1;
    const NtTween tween = {0,1,reveal.duration_us,reveal.start_us,reveal.easing,NT_ONCE};
    NtAnimationSample sample;
    return nt_tween_sample(&tween,now,&sample) == NT_OK ? sample.value : 0;
}

static NtResult update(void *value, NtUpdateInfo info, NtUpdateReply *reply)
{
    Plot *state = value;
    const NtPlotReveal reveal = state->options.reveal;
    const double fraction = reveal_at(reveal,info.now,info.motion);
    if (fraction != state->progress) reply->invalidate = NT_INVALIDATE_PAINT;
    state->progress = fraction;
    state->motion = info.motion;
    const NtTimeUs end = reveal.start_us + reveal.duration_us;
    if (info.motion == NT_MOTION_FULL && reveal.duration_us && info.now < end) {
        const NtTimeUs interval = reveal.duration_us / 60;
        const NtTimeUs quantum = info.now < reveal.start_us || interval > 16667 ? 16667 :
                                 interval ? interval : 1;
        NtTimeUs next = end - info.now <= quantum ? end : info.now + quantum;
        if (info.now < reveal.start_us && reveal.start_us < next) next = reveal.start_us;
        reply->next = (NtDeadline){true,next};
    }
    return NT_RESULT_OK;
}

static uint32_t fit_extent(uint32_t value, NtExtentRange range)
{
    if (value < range.min) return range.min;
    return range.bounded && value > range.max ? range.max : value;
}

static NtResult measure(const void *value, NtMeasureContext *ctx, NtConstraints bounds, NtCellSize *preferred)
{
    const Plot *chart = value;
    if (nt_measure_child_count(ctx)) return NT_RESULT_INVALID_ARGUMENT;
    const NtCellSize desired = chart->options.preferred;
    *preferred = (NtCellSize){fit_extent(desired.columns ? desired.columns : 16,bounds.columns),
                              fit_extent(desired.rows ? desired.rows : 6,bounds.rows)};
    return NT_RESULT_OK;
}

static NtResult layout(const void *value, NtLayoutContext *ctx, NtCellSize size)
{
    (void)value;
    (void)size;
    return nt_layout_child_count(ctx) ? NT_RESULT_INVALID_ARGUMENT : NT_RESULT_OK;
}

static NtRgba ink_at(const Plot *state, size_t index)
{
    return state->options.sample_colors ? state->data[index].color : state->options.foreground;
}

static int32_t sample_axis(double cells, unsigned pitch, uint32_t extent)
{
    const double sample = cells * pitch;
    if (sample <= 0) return 0;
    return sample < extent ? (int32_t)sample : (int32_t)(extent - 1);
}

static NtCellPoint sample_position(const ChartRaster *raster, NtPoint data)
{
    const NtPoint p = nt_plot_project_inside(&raster->viewport,data);
    return (NtCellPoint){sample_axis(p.x,2,raster->grid.columns) - 2*raster->clip.origin.x,
                         sample_axis(p.y,4,raster->grid.rows) - 4*raster->clip.origin.y};
}

static NtResult segment(const ChartRaster *raster, NtPoint a, NtPoint b, NtRgba color)
{
    if (!color.alpha) return NT_RESULT_OK;
    bool visible;
    const NtResult result = nt_plot_clip_segment(raster->viewport.domain,a,b,&visible,&a,&b);
    if (result != NT_RESULT_OK || !visible) return result;
    return nt_canvas_line(raster->canvas,sample_position(raster,a),sample_position(raster,b),
                          (NtCanvasInk){.color=color});
}

static NtResult scatter_point(const ChartRaster *raster, NtPoint p, NtRgba color)
{
    const NtPlotBounds b = raster->viewport.domain;
    if (!color.alpha || p.x < b.x.min || p.x > b.x.max || p.y < b.y.min || p.y > b.y.max) return NT_RESULT_OK;
    return nt_canvas_point(raster->canvas,sample_position(raster,p),(NtCanvasInk){.color=color});
}

static int64_t grid_edge(double cells, unsigned pitch, uint32_t extent)
{
    return (int64_t)fmin(extent,fmax(0,ceil(cells * pitch - .5)));
}

static NtResult bars(const Plot *chart, const ChartRaster *raster)
{
    const NtPlotDomain domain = raster->viewport.domain.y;
    const double baseline = fmin(domain.max,fmax(domain.min,chart->options.baseline));
    const size_t count = chart->options.count;
    const double x0 = 2.0*raster->clip.origin.x, y0 = 4.0*raster->clip.origin.y;
    const double width = raster->viewport.cells.size.columns;
    const int64_t base = grid_edge(nt_plot_project_inside(&raster->viewport,(NtPoint){0,baseline}).y,
                                    4,raster->grid.rows);
    for (size_t i = 0; i < count; ++i) {
        const NtRgba color = ink_at(chart,i);
        if (!color.alpha) continue;
        const double end = fmin(domain.max,fmax(domain.min,chart->data[i].point.y));
        if (end == baseline) continue;
        const int64_t y = grid_edge(nt_plot_project_inside(&raster->viewport,(NtPoint){0,end}).y,
                                     4,raster->grid.rows);
        const double first = ((double)i + .10) * (width / (double)count);
        const double last = ((double)i + .90) * (width / (double)count);
        const int64_t l = grid_edge(first,2,raster->grid.columns), r = grid_edge(last,2,raster->grid.columns);
        const int64_t top = y < base ? y : base, bottom = y < base ? base : y;
        const NtCellRect rect = {{(int32_t)(l - x0),(int32_t)(top - y0)},
                                 {(uint32_t)(r-l),(uint32_t)(bottom-top)}};
        const NtResult result = nt_canvas_fill(raster->canvas,rect,(NtCanvasInk){.color=color});
        if (result != NT_RESULT_OK) return result;
    }
    return NT_RESULT_OK;
}

static NtResult plot_series(const Plot *chart, const ChartRaster *raster)
{
    const size_t count = chart->options.count;
    const size_t stride = chart->options.sample_stride ? chart->options.sample_stride : 1;
    const NtPlotKind kind = chart->options.kind;
    if (kind == NT_PLOT_BARS) return bars(chart,raster);
    if (kind == NT_PLOT_EDGES) {
        for (size_t i = 0; i < count/2;) {
            const NtResult result = segment(raster,chart->data[2*i].point,chart->data[2*i+1].point,ink_at(chart,2*i));
            if (result != NT_RESULT_OK) return result;
            if (stride >= count/2-i) break;
            i += stride;
        }
        return NT_RESULT_OK;
    }
    for (size_t i = 0, previous = 0; i < count;) {
        NtResult result = NT_RESULT_OK;
        if (kind == NT_PLOT_LINE && i)
            result = segment(raster,chart->data[previous].point,chart->data[i].point,ink_at(chart,i));
        else if (kind != NT_PLOT_LINE || count == 1)
            result = scatter_point(raster,chart->data[i].point,ink_at(chart,i));
        if (result != NT_RESULT_OK) return result;
        if (i == count-1) break;
        previous = i;
        i = stride < count-1-i ? i+stride : count-1;
    }
    return NT_RESULT_OK;
}

static NtCanvasPixel radial_pixel(const Plot *chart, NtPoint size, NtPoint fitted, NtPoint sample)
{
    const size_t count = chart->options.count;
    const double total = chart->cumulative[count-1];
    if (total <= 0 || fitted.x <= 0 || fitted.y <= 0) return (NtCanvasPixel){0};
    const double x = (sample.x - size.x*.5) / (fitted.x*.5);
    const double y = (size.y*.5 - sample.y) / (fitted.y*.5);
    const double radius = fma(x,x,y*y);
    const double hole = chart->options.hole_ratio ? chart->options.hole_ratio : .5;
    if (radius > 1 || (chart->options.kind == NT_PLOT_DONUT && radius < hole*hole)) return (NtCanvasPixel){0};
    double turn = atan2(x,y) / 6.283185307179586476925286766559;
    if (turn < 0) turn += 1;
    if (turn >= chart->progress && chart->progress < 1) return (NtCanvasPixel){0};
    const double weight = fmin(turn * total,nextafter(total,0));
    size_t left = 0, right = count;
    while (left < right) {
        const size_t middle = left + (right-left)/2;
        if (chart->cumulative[middle] > weight) right = middle;
        else left = middle+1;
    }
    const NtRgba ink = ink_at(chart,left);
    return (NtCanvasPixel){.color=ink,.covered=ink.alpha!=0};
}

static NtResult paint_cells(const Plot *chart, NtPaintContext *paint, const ChartRaster *raster)
{
    const NtCellRect clip = raster->clip;
    const NtPoint size = {raster->viewport.cells.size.columns,raster->viewport.cells.size.rows};
    const bool circle = radial(chart->options.kind);
    const NtPoint fitted = circle ? nt_plot_fit(size,chart->options.cell_aspect,1,1) : size;
    for (uint32_t y = 0; y < clip.size.rows; ++y) {
        for (uint32_t x = 0; x < clip.size.columns; ++x) {
            NtCellSamples samples = {0};
            bool covered = false;
            for (unsigned i = 0; i < 8; ++i) {
                const int32_t sx = (int32_t)(2*x + i%2), sy = (int32_t)(4*y + i/2);
                const double px = clip.origin.x + (sx + .5)/2, py = clip.origin.y + (sy + .5)/4;
                NtCanvasPixel *pixel = &samples.pixels[i];
                if (circle) *pixel = radial_pixel(chart,size,fitted,(NtPoint){px,py});
                else if (px <= chart->progress * size.x) {
                    const NtResult read = nt_canvas_pixel(raster->canvas,(NtCellPoint){sx,sy},pixel);
                    if (read != NT_RESULT_OK) return read;
                }
                covered |= pixel->covered;
            }
            if (!covered) continue;
            NtSampledCell cell;
            const NtSamplingMode mode = chart->options.ascii ? NT_SAMPLE_BRAILLE : chart->options.sampling;
            NtResult result = nt_cell_sample(&samples,mode,&cell);
            if (result != NT_RESULT_OK) return result;
            const NtByteView glyph = chart->options.ascii ? (NtByteView){"#",1} :
                                    (NtByteView){cell.glyph,cell.glyph_size};
            const NtCellPoint point = {clip.origin.x + (int32_t)x,clip.origin.y + (int32_t)y};
            result = nt_paint_glyph(paint,point,glyph,1,cell.style);
            if (result != NT_RESULT_OK) return result;
        }
    }
    return NT_RESULT_OK;
}

static NtResult paint_plot(const void *value, NtPaintContext *paint, NtCellSize assigned)
{
    const Plot *chart = value;
    const NtCellRect clip = nt_paint_clip(paint);
    if (!clip.size.columns || !clip.size.rows) return NT_RESULT_OK;
    if (assigned.columns > INT32_MAX/2 || assigned.rows > INT32_MAX/4) return NT_RESULT_OUT_OF_RANGE;
    NtResult result = nt_paint_fill(paint,clip,chart->options.background);
    if (result != NT_RESULT_OK || !chart->options.count || chart->progress <= 0) return result;
    const NtPlotOptions *options = &chart->options;
    const NtPlotViewport viewport = {.domain=options->domain,.cells={.size=assigned},
        .cell_aspect=options->cell_aspect,.equal_units=options->equal_units && options->kind!=NT_PLOT_BARS};
    ChartRaster raster = {.viewport=viewport,.clip=clip,.grid={2*assigned.columns,4*assigned.rows}};
    if (!radial(options->kind)) {
        const NtAllocator *allocator = chart->allocator.allocate ? &chart->allocator : NULL;
        result = nt_canvas_create(allocator,(NtCellSize){2*clip.size.columns,4*clip.size.rows},&raster.canvas);
        if (result == NT_RESULT_OK) result = plot_series(chart,&raster);
    }
    if (result == NT_RESULT_OK) result = paint_cells(chart,paint,&raster);
    nt_canvas_destroy(raster.canvas);
    return result;
}

static void dispose(void *value)
{
    Plot *state = value;
    const NtAllocator allocator = state->allocator;
    release_data(state);
    nt_release(&allocator,state,sizeof(*state),NT_ALIGNMENT(Plot));
}

static NtResult read_foreground(const void *value, NtValue *out)
{
    *out = (NtValue){.kind=NT_VALUE_RGBA,.as.rgba=((const Plot *)value)->options.foreground};
    return NT_RESULT_OK;
}

static NtResult read_background(const void *value, NtValue *out)
{
    *out = (NtValue){.kind=NT_VALUE_RGBA,.as.rgba=((const Plot *)value)->options.background};
    return NT_RESULT_OK;
}

static void write_foreground(void *state, NtValue value, void *prepared, bool commit)
{
    (void)prepared;
    if (commit) ((Plot *)state)->options.foreground = value.as.rgba;
}

static void write_background(void *state, NtValue value, void *prepared, bool commit)
{
    (void)prepared;
    if (commit) ((Plot *)state)->options.background = value.as.rgba;
}

static const NtPropertyDescriptor properties[] = {
    {NT_PROPERTY_FOREGROUND,NT_VALUE_RGBA,NT_INVALIDATE_PAINT,read_foreground,NULL,write_foreground},
    {NT_PROPERTY_BACKGROUND,NT_VALUE_RGBA,NT_INVALIDATE_PAINT,read_background,NULL,write_background}
};

static const NtComponentOps plot_ops = {measure,layout,update,paint_plot,NULL,dispose};

NtResult nt_plot_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount,
                       const NtPlotOptions *options, NtNodeId *out)
{
    if (!tree || !out) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    NtResult result = nt_node_null(parent) ? NT_RESULT_OK : nt_node_validate(tree,parent);
    if (result == NT_RESULT_OK) result = validate(options);
    if (result != NT_RESULT_OK) return result;
    tree->busy = true;
    Plot *state = nt_allocate(&tree->allocator,sizeof(*state),NT_ALIGNMENT(Plot));
    if (state) {
        *state = (Plot){.allocator=tree->allocator,.motion=NT_MOTION_FULL};
        result = configure(state,options);
        if (result != NT_RESULT_OK) nt_release(&tree->allocator,state,sizeof(*state),NT_ALIGNMENT(Plot));
    } else result = NT_RESULT_ALLOCATION_FAILED;
    tree->busy = false;
    if (result != NT_RESULT_OK) return result;
    state->progress = reveal_at(options->reveal,tree->watermark,state->motion);
    NtNodeId node;
    result = nt_tree_mount(tree,parent,&plot_ops,state,mount,&node);
    if (result == NT_RESULT_OK) {
        nt_property_stock_table(tree,node,properties,sizeof(properties)/sizeof(*properties));
        *out = node;
    } else {
        tree->busy = true;
        dispose(state);
        tree->busy = false;
    }
    return result;
}

NtResult nt_plot_set(NtTree *tree, NtNodeId node, const NtPlotOptions *options)
{
    if (!tree) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    NtResult result = nt_node_validate(tree,node);
    if (result != NT_RESULT_OK) return result;
    NtNode *mount = &tree->nodes[node.slot];
    if (mount->ops.paint != paint_plot) return NT_RESULT_INVALID_ARGUMENT;
    result = validate(options);
    if (result != NT_RESULT_OK) return result;
    Plot *state = mount->state;
    Plot replacement = {.allocator=state->allocator,.motion=state->motion};
    tree->busy = true;
    result = configure(&replacement,options);
    if (result == NT_RESULT_OK) {
        replacement.progress = reveal_at(options->reveal,tree->watermark,replacement.motion);
        const NtCellSize before = state->options.preferred, after = options->preferred;
        release_data(state);
        *state = replacement;
        mount->updated = false;
        nt_tree_dirty(tree,before.columns == after.columns && before.rows == after.rows ?
                      NT_INVALIDATE_PAINT : NT_INVALIDATE_LAYOUT);
    }
    tree->busy = false;
    return result;
}
