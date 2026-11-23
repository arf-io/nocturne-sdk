#include <nocturne/plot.h>
#include <nocturne/simulation.h>
#include <nocturne/widgets.h>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRY(call) do { const NtResult status = (call); if (status!=NT_RESULT_OK) return status; } while (0)

enum { SAMPLE_COUNT = 41, STEP_US = 10000, FRAME_COLUMNS = 72, FRAME_ROWS = 16, MAX_STEPS = 160 };

typedef struct Lab {
    NtTree *tree;
    NtHull *hull;
    NtNodeId panel, plot, track;
    NtFloatingBody body;
    NtTimeUs time;
} Lab;

typedef struct FrameMarks {
    char title[FRAME_COLUMNS+1];
    uint32_t marker_column;
    size_t panel_glyphs, plot_glyphs, markers;
    uint64_t plot_map;
} FrameMarks;

static const NtCellSize frame_size = {FRAME_COLUMNS,FRAME_ROWS};
static const NtMountOptions visible = {.enabled=true};
static const NtCellStyle ink = {{205,225,233,255},{18,33,43,255}};
static const NtRgba tide = {93,206,190,255};
static const NtWaterWave waves[] = {
    {.amplitude=.115,.cycles_per_unit={.7,.25},.cycles_per_second=.62,.phase_turns=.125},
    {.amplitude=.025,.cycles_per_unit={1.4,-.4},.cycles_per_second=.9,.phase_turns=.18}
};
static const NtVec3 vertices[] = {
    {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
    {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}
};
static const NtHullTriangle faces[] = {
    {{0,2,1}},{{0,3,2}},{{4,5,6}},{{4,6,7}},{{0,1,5}},{{0,5,4}},
    {{3,7,6}},{{3,6,2}},{{0,4,7}},{{0,7,3}},{{1,2,6}},{{1,6,5}}
};

static double seconds(NtTimeUs time)
{
    return (double)time/1e6;
}

static NtResult wave_at(const Lab *lab, double x, double z, NtWaterWaveSample *out)
{
    return nt_water_wave_sample(waves,sizeof waves/sizeof waves[0],(NtPoint){x,z},seconds(lab->time),out);
}

static NtResult immersion(const Lab *lab, NtHullImmersion *out)
{
    const NtVec3 p = lab->body.position;
    NtWaterWaveSample wave;
    TRY(wave_at(lab,p.x,p.z,&wave));
    const NtWaterPlane plane = {{p.x,wave.height,p.z},wave.normal};
    return nt_hull_immerse_plane(lab->hull,&lab->body,&plane,out);
}

static NtResult advance(Lab *lab, unsigned steps)
{
    for (unsigned step = 0; step<steps; ++step) {
        NtHullImmersion wet;
        NtBuoyancy buoyancy;
        TRY(immersion(lab,&wet));
        TRY(nt_hull_buoyancy(&wet,&lab->body,1,10,&buoyancy));
        const NtBodyLoad load = {buoyancy.force,buoyancy.torque};
        TRY(nt_floating_body_step(&lab->body,&load,STEP_US,&lab->body));
        lab->time += STEP_US;
    }
    return NT_RESULT_OK;
}

static NtResult paint_track(const void *state, NtPaintContext *paint, NtCellSize size)
{
    const Lab *lab = state;
    if (!size.columns || !size.rows) return NT_RESULT_OK;
    /* Fixed world-domain projection onto the heave rail. */
    const double u = fmin(1,fmax(0,.5+lab->body.position.y));
    const int32_t at = (int32_t)lround(u*(size.columns-1));
    for (uint32_t x = 0; x<size.columns; ++x) {
        const char *glyph = (int32_t)x==at ? "B" : "-";
        const NtCellStyle color = (int32_t)x==at ? (NtCellStyle){{255,209,119,255},ink.background} : ink;
        TRY(nt_paint_glyph(paint,(NtCellPoint){(int32_t)x,0},(NtByteView){glyph,1},1,color));
    }
    return NT_RESULT_OK;
}

static NtResult measure_track(const void *state, NtMeasureContext *context, NtConstraints bounds, NtCellSize *out)
{
    (void)state;
    (void)context;
    *out = (NtCellSize){bounds.columns.min,bounds.rows.min};
    return NT_RESULT_OK;
}

static NtResult layout_track(const void *state, NtLayoutContext *context, NtCellSize size)
{
    (void)state;
    (void)context;
    (void)size;
    return NT_RESULT_OK;
}

static NtPlotOptions plot_options(const NtPlotDatum *data, size_t count)
{
    return (NtPlotOptions){.kind=NT_PLOT_LINE,.data=data,.count=count,.domain={{-1,1},{-.16,.16}},
        .ascii=true,.sampling=NT_SAMPLE_CELL,.foreground=tide,.background=ink.background};
}

static NtPanelOptions panel_options(const char *title)
{
    return (NtPanelOptions){.fill=ink.background,.padding={1,0,1,0},.framed=true,.ascii=true,.border=ink,
        .title={{title,strlen(title)},{.width={NT_WIDTH_ONE,NT_WIDTH_TWO},.tab_stop=4,.color=ink}}};
}

static NtResult mount(Lab *lab)
{
    const NtHullGeometry geometry = {vertices,sizeof vertices/sizeof vertices[0],faces,sizeof faces/sizeof faces[0]};
    TRY(nt_hull_create(NULL,&geometry,&lab->hull));
    lab->body = (NtFloatingBody){.mass=4,.principal_inertia={6,6,6}};
    TRY(nt_tree_create(NULL,&lab->tree));
    const NtPanelOptions panel = panel_options("Surface Lab");
    TRY(nt_panel_mount(lab->tree,(NtNodeId){0},visible,&panel,&lab->panel));
    const NtBoxItem items[] = {{.grow_columns=true,.grow_rows=true},
                              {.rows={1,1,true},.grow_columns=true}};
    const NtBoxLayout column = {.kind=NT_BOX_LAYOUT_COLUMN,.items=items,.item_count=2};
    NtNodeId content;
    TRY(nt_box_layout_mount(lab->tree,lab->panel,visible,&column,&content));
    const NtPlotOptions empty = plot_options(NULL,0);
    TRY(nt_plot_mount(lab->tree,content,visible,&empty,&lab->plot));
    static const NtComponentOps track = {.measure=measure_track,.layout=layout_track,.paint=paint_track};
    return nt_tree_mount(lab->tree,content,&track,lab,visible,&lab->track);
}

static NtResult render(Lab *lab)
{
    NtHullImmersion wet;
    TRY(immersion(lab,&wet));
    NtPlotDatum section[SAMPLE_COUNT];
    for (unsigned i = 0; i<SAMPLE_COUNT; ++i) {
        const double x = -1+2*(double)i/(SAMPLE_COUNT-1);
        NtWaterWaveSample wave;
        TRY(wave_at(lab,x,0,&wave));
        section[i] = (NtPlotDatum){.point={x,wave.height}};
    }
    char title[72];
    const int count = snprintf(title,sizeof title,"Surface Lab  %.2f s  Wet %.3f m^3  Heave %+.3f m",
                               seconds(lab->time),wet.displaced_volume,lab->body.position.y);
    if (count<0 || (size_t)count>=sizeof title) return NT_RESULT_OUT_OF_RANGE;
    const NtPanelOptions panel = panel_options(title);
    const NtPlotOptions plot = plot_options(section,SAMPLE_COUNT);
    TRY(nt_panel_set(lab->tree,lab->panel,&panel));
    TRY(nt_plot_set(lab->tree,lab->plot,&plot));
    return nt_tree_publish(lab->tree,frame_size);
}

static NtResult inspect_frame(const Lab *lab, FrameMarks *out)
{
    const NtFrame *frame = nt_tree_frame(lab->tree);
    const NtOwnerToken plot = nt_node_owner(lab->plot), panel = nt_node_owner(lab->panel);
    const NtOwnerToken track = nt_node_owner(lab->track);
    *out = (FrameMarks){0};
    memset(out->title,' ',FRAME_COLUMNS);
    for (int32_t y = 0; y<FRAME_ROWS; ++y) {
        for (int32_t x = 0; x<FRAME_COLUMNS; ++x) {
            NtCellView cell;
            TRY(nt_frame_cell(frame,(NtCellPoint){x,y},&cell));
            if (!cell.glyph.count) continue;
            if (cell.owner==panel) {
                ++out->panel_glyphs;
                if (!y && cell.glyph.count==1) out->title[x] = cell.glyph.data[0];
            }
            if (cell.owner==plot) {
                ++out->plot_glyphs;
                out->plot_map = out->plot_map*UINT64_C(1099511628211)+(uint32_t)(y*FRAME_COLUMNS+x+1);
            }
            if (cell.owner==track && cell.glyph.count==1 && cell.glyph.data[0]=='B') {
                ++out->markers;
                out->marker_column = (uint32_t)x;
            }
        }
    }
    return NT_RESULT_OK;
}

static NtResult verify(Lab *lab)
{
    FrameMarks first, later, removed;
    TRY(render(lab));
    TRY(inspect_frame(lab,&first));
    TRY(advance(lab,80));
    TRY(render(lab));
    TRY(inspect_frame(lab,&later));
    if (!first.panel_glyphs || !later.panel_glyphs || !first.plot_glyphs || !later.plot_glyphs ||
        first.markers!=1 || later.markers!=1 || first.marker_column==later.marker_column ||
        first.plot_map==later.plot_map || !strstr(first.title,"Surface Lab  0.00 s") ||
        !strstr(later.title,"Surface Lab  0.80 s")) return NT_RESULT_INVALID_ARGUMENT;

    TRY(nt_tree_remove(lab->tree,lab->panel));
    TRY(nt_tree_publish(lab->tree,frame_size));
    TRY(inspect_frame(lab,&removed));
    if (removed.panel_glyphs || removed.plot_glyphs || removed.markers)
        return NT_RESULT_INVALID_ARGUMENT;
    return NT_RESULT_OK;
}

static NtResult write_frame(void *context, NtByteView bytes)
{
    return fwrite(bytes.data,1,bytes.count,context)==bytes.count ? NT_RESULT_OK : NT_RESULT_TRANSPORT_FAILED;
}

int main(int argc, char **argv)
{
    bool check = false;
    unsigned steps = 80;
    if (argc==2 && strcmp(argv[1],"--verify")==0) check = true;
    else if (argc==2) {
        char *end;
        errno = 0;
        const unsigned long count = strtoul(argv[1],&end,10);
        if (argv[1][0]<'0' || argv[1][0]>'9' || *end || errno || count>MAX_STEPS) {
            fprintf(stderr,"Usage: %s [steps 0-%u | --verify]\n",argv[0],(unsigned)MAX_STEPS);
            return EXIT_FAILURE;
        }
        steps = (unsigned)count;
    } else if (argc!=1) {
        fprintf(stderr,"Usage: %s [steps 0-%u | --verify]\n",argv[0],(unsigned)MAX_STEPS);
        return EXIT_FAILURE;
    }

    Lab lab = {0};
    NtResult status = mount(&lab);
    if (status==NT_RESULT_OK) status = check ? verify(&lab) : advance(&lab,steps);
    if (!check && status==NT_RESULT_OK) status = render(&lab);
    if (!check && status==NT_RESULT_OK) {
        NtAnsiPresenter presenter = {.capabilities={.width={NT_WIDTH_ONE,NT_WIDTH_TWO}},
            .matte=ink.background,.write=write_frame,.write_context=stdout};
        const NtFrameSink sink = {&presenter,nt_ansi_present};
        status = nt_frame_present(&sink,nt_tree_frame(lab.tree),lab.time);
        if (fflush(stdout)!=0) status = NT_RESULT_TRANSPORT_FAILED;
    }
    nt_tree_destroy(lab.tree);
    nt_hull_destroy(lab.hull);
    if (status!=NT_RESULT_OK) fprintf(stderr,"Surface Lab ended with result %d\n",(int)status);
    return status==NT_RESULT_OK ? EXIT_SUCCESS : EXIT_FAILURE;
}
