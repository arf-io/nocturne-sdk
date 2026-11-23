#include <nocturne/component.h>
#include <nocturne/panel_transform.h>

#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr,"%s:%d: %s\n",__func__,__LINE__,#test); exit(EXIT_FAILURE); \
} } while (0)
#define OK(expr) CHECK((expr) == NT_RESULT_OK)
#define P(x,y) ((NtCellPoint){(x),(y)})
#define SIZE(w,h) ((NtCellSize){(w),(h)})
#define RECT(x,y,w,h) ((NtCellRect){P(x,y),SIZE(w,h)})
#define BYTES(s) ((NtByteView){(s),sizeof(s)-1})

static const NtCellStyle white = {{255,255,255,255},{0,0,0,0}};
static const NtPanelPose identity = {.scale={1,1},.opacity=1};
static const char astronaut[] = "👩‍🚀";
static const char accent[] = "e\xcc\x81";

static NtFrame *frame(const NtAllocator *allocator, NtCellSize size, NtSurfaceView *root)
{
    NtFrame *out;
    OK(nt_frame_create(allocator,size,&out));
    if (root) OK(nt_surface_root(out,root));
    return out;
}

static NtCellView cell(const NtFrame *frame, int32_t x, int32_t y)
{
    NtCellView out;
    OK(nt_frame_cell(frame,P(x,y),&out));
    return out;
}

static NtCellBackground background_at(const NtFrame *frame, int32_t x, int32_t y)
{
    NtCellBackground out;
    OK(nt_frame_background(frame,P(x,y),&out));
    return out;
}

static void glyph(const NtFrame *frame, int32_t x, int32_t y, const char *expected)
{
    const NtCellView actual = cell(frame,x,y);
    CHECK(actual.glyph.count == strlen(expected));
    CHECK(!actual.glyph.count || !memcmp(actual.glyph.data,expected,actual.glyph.count));
}

static NtPanelLayer layer(const NtFrame *source, NtCellRect region, NtCellRect viewport)
{
    return (NtPanelLayer){.source=source,.region=region,.viewport=viewport,
        .pose=identity,.owners=NT_SURFACE_OWNERS_SHARED};
}

static void pose_clock(void)
{
    NtPanelPose pose = {.anchor={2,3},.offset={-3,4},.scale={2,.5},.turns=.25,.opacity=.5};
    NtPoint projected;
    OK(nt_panel_project_point(&pose,(NtPoint){3,5},&projected));
    CHECK(projected.x == -2 && projected.y == 9);
    pose = (NtPanelPose){.anchor={1,0},.offset={-.5,0},.scale={0x1p53,1},.opacity=1};
    OK(nt_panel_project_point(&pose,pose.anchor,&projected));
    CHECK(projected.x == .5 && projected.y == 0);
    pose = identity;
    pose.anchor = (NtPoint){DBL_MAX,DBL_MAX};
    OK(nt_panel_project_point(&pose,(NtPoint){.5,2.5},&projected));
    CHECK(projected.x == .5 && projected.y == 2.5);
    pose.scale.x = -DBL_MAX;
    CHECK(nt_panel_project_point(&pose,(NtPoint){.5,2.5},&projected) == NT_RESULT_OUT_OF_RANGE);
    CHECK(projected.x == .5 && projected.y == 2.5);
    pose.scale = (NtPoint){DBL_MAX,-DBL_MAX};
    OK(nt_panel_project_point(&pose,pose.anchor,&projected));
    CHECK(projected.x == DBL_MAX && projected.y == DBL_MAX);
    pose = identity;
    for (int sign = -1; sign <= 1; sign += 2) {
        pose.offset.x = sign*0x1p969;
        const NtPoint saved = projected;
        CHECK(nt_panel_project_point(&pose,(NtPoint){sign*DBL_MAX,0},&projected) ==
              NT_RESULT_OUT_OF_RANGE);
        CHECK(projected.x == saved.x && projected.y == saved.y);
        OK(nt_panel_project_point(&pose,(NtPoint){sign*nextafter(DBL_MAX,0),0},&projected));
    }
    pose = identity;
    for (int sign = -1; sign <= 1; sign += 2) {
        for (int side = -1; side <= 1; ++side) {
            pose.anchor.x = -side*0x1p-107;
            pose.offset.x = -pose.anchor.x;
            pose.scale.x = sign*0x1p-1022;
            OK(nt_panel_project_point(&pose,(NtPoint){0x1p-53,.5},&projected));
            CHECK(projected.x == (side > 0 ? sign*0x1p-1074 : 0) && projected.y == .5);
        }
    }

    NtPanelTransition transition = {.from=identity,.to=identity,.delay_us=9,.duration_us=10,.easing=NT_LINEAR};
    transition.from.scale = (NtPoint){0,0};
    transition.from.opacity = 0;
    transition.to.offset = (NtPoint){8,4};
    transition.to.turns = 1.5;
    OK(nt_panel_pose_at(&transition,14,NT_MOTION_FULL,&pose));
    CHECK(pose.offset.x == 4 && pose.offset.y == 2 && pose.turns == .75 &&
          pose.scale.x == .5 && pose.opacity == .5);
    OK(nt_panel_pose_at(&transition,0,NT_MOTION_REDUCED,&pose));
    CHECK(pose.opacity == 1 && pose.offset.x == 8 && pose.turns == 1.5);
    transition.duration_us = UINT64_MAX-9;
    transition.easing = NT_STEP_END;
    OK(nt_panel_pose_at(&transition,UINT64_MAX-1,NT_MOTION_FULL,&pose));
    CHECK(pose.scale.x == 0 && pose.opacity == 0);
    OK(nt_panel_pose_at(&transition,UINT64_MAX,NT_MOTION_FULL,&pose));
    CHECK(pose.scale.x == 1 && pose.opacity == 1);
    ++transition.duration_us;
    CHECK(nt_panel_pose_at(&transition,UINT64_MAX,NT_MOTION_FULL,&pose) == NT_RESULT_OUT_OF_RANGE);
    CHECK(pose.scale.x == 1 && pose.opacity == 1);
    puts("anchored anisotropic rotation, explicit progress, reduced motion and full-width clock endpoints");
}

static void affine_cancellation(void)
{
    NtFrame *source = frame(NULL,SIZE(2,1),NULL);
    NtSurfaceView root;
    NtFrame *destination = frame(NULL,SIZE(1,1),&root);
    OK(nt_frame_set_glyph(source,P(1,0),BYTES("X"),1,white,7));
    NtPanelLayer projected = layer(source,RECT(0,0,2,1),RECT(0,0,1,1));
    projected.pose = (NtPanelPose){.anchor={.5,0x1p53},.offset={-0x1p53,-0x1p53},
                                   .scale={1,1},.turns=.25,.opacity=1};
    NtPoint location;
    OK(nt_panel_project_point(&projected.pose,(NtPoint){1,0},&location));
    CHECK(location.x == .5 && location.y == .5);
    OK(nt_surface_project_panels(&root,&projected,1));
    glyph(destination,0,0,"X");
    CHECK(cell(destination,0,0).owner == 7);
    nt_frame_destroy(destination);
    nt_frame_destroy(source);
    puts("large anchor and translation cancellation preserve exact glyph and half-cell placement");
}

static void inverse_cell_boundary(void)
{
    NtFrame *source = frame(NULL,SIZE(2,1),NULL);
    NtSurfaceView root;
    NtFrame *destination = frame(NULL,SIZE(1,1),&root);
    OK(nt_frame_set_glyph(source,P(0,0),BYTES("A"),1,white,1));
    OK(nt_frame_set_glyph(source,P(1,0),BYTES("B"),1,white,2));
    NtPanelLayer projected = layer(source,RECT(0,0,2,1),RECT(0,0,1,1));
    projected.pose.offset.x = -nextafter(.5,0);
    OK(nt_surface_project_panels(&root,&projected,1));
    glyph(destination,0,0,"A");
    projected.pose.offset.x = -.5;
    OK(nt_surface_project_panels(&root,&projected,1));
    glyph(destination,0,0,"B");

    projected.region.size.columns = 1;
    OK(nt_frame_clear_glyph(destination,P(0,0)));
    projected.pose.offset.x = -nextafter(.5,0);
    OK(nt_surface_project_panels(&root,&projected,1));
    glyph(destination,0,0,"A");
    OK(nt_frame_clear_glyph(destination,P(0,0)));
    projected.pose.offset.x = -.5;
    OK(nt_surface_project_panels(&root,&projected,1));
    glyph(destination,0,0,"");

    projected.pose.offset.x = nextafter(.5,1);
    OK(nt_surface_project_panels(&root,&projected,1));
    glyph(destination,0,0,"");
    projected.pose.offset.x = .5;
    OK(nt_surface_project_panels(&root,&projected,1));
    glyph(destination,0,0,"A");
    nt_frame_destroy(destination);
    nt_frame_destroy(source);
    puts("inverse rounding preserves the correct side of half-open source cell and crop boundaries");
}

static NtResult tile(void *context, const NtPanelSample *sample, NtPanelPixel *pixel)
{
    const double boundary = *(const double *)context;
    const double column = floor(sample->source.x);
    CHECK(column == boundary || column == boundary-1);
    pixel->source.x = column-2*floor(column/2)+.5;
    return NT_RESULT_OK;
}

static void wrapped_boundaries(void)
{
    NtFrame *source = frame(NULL,SIZE(2,1),NULL);
    NtSurfaceView root;
    NtFrame *destination = frame(NULL,SIZE(1,1),&root);
    OK(nt_frame_set_glyph(source,P(0,0),BYTES("A"),1,white,1));
    OK(nt_frame_set_glyph(source,P(1,0),BYTES("B"),1,white,2));
    NtPanelLayer projected = layer(source,RECT(0,0,2,1),RECT(0,0,1,1));
    projected.pose.scale.x = -1;
    projected.remap = tile;
    double boundary;
    projected.remap_context = &boundary;
    const double boundaries[] = {-0x1p32,-1,0,1,0x1p32};
    const double offsets[] = {nextafter(.5,0),.5,nextafter(.5,1)};
    for (size_t i = 0; i < sizeof(boundaries)/sizeof(boundaries[0]); ++i) {
        boundary = boundaries[i];
        projected.pose.anchor.x = boundary/2;
        for (size_t j = 0; j < sizeof(offsets)/sizeof(offsets[0]); ++j) {
            projected.pose.offset.x = offsets[j];
            OK(nt_surface_project_panels(&root,&projected,1));
            const double expected = j == 0 ? boundary-1 : boundary;
            const bool odd = expected-2*floor(expected/2) != 0;
            glyph(destination,0,0,odd ? "B" : "A");
            CHECK(cell(destination,0,0).owner == (odd ? 2 : 1));
        }
    }
    boundary = -4503599627370490.0;
    projected.pose.anchor.x = 0x1.4p+44;
    projected.pose.scale.x = -0x1.6666666666666p+0;
    projected.pose.offset.x = -0x1.696666666665dp+52;
    OK(nt_surface_project_panels(&root,&projected,1));
    glyph(destination,0,0,"A");

    boundary = 0x1.8000000000006p+52;
    projected.pose.anchor.x = 0x1.8000000000003p+52;
    projected.pose.scale.x = 0x1.5555555555556p-3;
    projected.pose.offset.x = -projected.pose.anchor.x;
    OK(nt_surface_project_panels(&root,&projected,1));
    glyph(destination,0,0,"B");
    nt_frame_destroy(destination);
    nt_frame_destroy(source);
    puts("tiled remaps preserve signed boundaries inside and outside the frame coordinate range");
}

static NtResult check_anchor(void *context, const NtPanelSample *sample, NtPanelPixel *pixel)
{
    (void)pixel;
    const NtPoint *anchor = context;
    CHECK(sample->source.x == anchor->x && sample->source.y == anchor->y);
    return NT_RESULT_OK;
}

static void inverse_anchor(void)
{
    NtFrame *source = frame(NULL,SIZE(2,1),NULL);
    NtSurfaceView root;
    NtFrame *destination = frame(NULL,SIZE(1,1),&root);
    OK(nt_frame_set_glyph(source,P(0,0),BYTES("A"),1,white,1));
    OK(nt_frame_set_glyph(source,P(1,0),BYTES("B"),1,white,2));
    NtPanelLayer projected = layer(source,RECT(0,0,2,1),RECT(0,0,1,1));
    projected.pose.anchor = (NtPoint){1,.5};
    projected.pose.offset.x = -.5;
    projected.pose.scale = (NtPoint){.75,1.25};
    projected.remap = check_anchor;
    projected.remap_context = &projected.pose.anchor;
    for (unsigned i = 0; i < 64; ++i) {
        projected.pose.turns = i/64.0;
        NtPoint center;
        OK(nt_panel_project_point(&projected.pose,projected.pose.anchor,&center));
        CHECK(center.x == .5 && center.y == .5);
        OK(nt_frame_clear_glyph(destination,P(0,0)));
        OK(nt_surface_project_panels(&root,&projected,1));
        glyph(destination,0,0,"B");
        CHECK(cell(destination,0,0).owner == 2);
    }
    projected.pose.anchor = (NtPoint){.75,.25};
    projected.pose.offset = (NtPoint){-.25,.25};
    projected.pose.scale = (NtPoint){.1,-.2};
    OK(nt_surface_project_panels(&root,&projected,1));
    glyph(destination,0,0,"A");

    projected.pose.anchor = (NtPoint){1.5,0};
    projected.pose.offset = (NtPoint){-1,0};
    projected.pose.turns = 0;
    projected.pose.scale.y = 1;
    const NtPoint along_x_anchor = {1.5,.5};
    projected.remap_context = (void *)&along_x_anchor;
    const double tiny = nextafter(0,1);
    const double scales[] = {1,-1,DBL_MIN,-DBL_MIN,tiny,-tiny,2*tiny,3*tiny,4*tiny};
    for (size_t i = 0; i < sizeof(scales)/sizeof(*scales); ++i) {
        projected.pose.scale.x = scales[i];
        OK(nt_surface_project_panels(&root,&projected,1));
        glyph(destination,0,0,"B");
    }
    nt_frame_destroy(destination);
    nt_frame_destroy(source);
    puts("forward and inverse projection agree at the anchor across a full turn and subnormal scales");
}

static void diagonal_boundaries(void)
{
    NtFrame *source = frame(NULL,SIZE(1,2),NULL);
    NtSurfaceView root;
    NtFrame *destination = frame(NULL,SIZE(1,1),&root);
    OK(nt_frame_set_glyph(source,P(0,0),BYTES("A"),1,white,1));
    OK(nt_frame_set_glyph(source,P(0,1),BYTES("B"),1,white,2));
    NtPanelLayer projected = layer(source,RECT(0,0,1,2),RECT(0,0,1,1));
    projected.pose.anchor = (NtPoint){0,1};
    projected.pose.scale.y = -1;
    const NtPoint offsets[] = {{0,-1},{1,-1},{1,0},{0,0}};
    for (size_t i = 0; i < sizeof(offsets)/sizeof(offsets[0]); ++i) {
        projected.pose.offset = offsets[i];
        projected.pose.turns = (2*i+1)/8.0;
        OK(nt_surface_project_panels(&root,&projected,1));
        glyph(destination,0,0,"B");
        CHECK(cell(destination,0,0).owner == 2);
    }
    projected.pose.turns = .125;
    projected.pose.offset = (NtPoint){0,-1-0x1p-44};
    OK(nt_surface_project_panels(&root,&projected,1));
    glyph(destination,0,0,"A");
    projected.pose.offset.y = -1+0x1p-44;
    OK(nt_surface_project_panels(&root,&projected,1));
    glyph(destination,0,0,"B");
    nt_frame_destroy(destination);
    nt_frame_destroy(source);
    puts("all four exact diagonals and adjacent samples select their correct source cells");
}

static void inverse_rotation_scale(void)
{
    NtFrame *source = frame(NULL,SIZE(2,1),NULL);
    NtSurfaceView root;
    NtFrame *destination = frame(NULL,SIZE(1,1),&root);
    OK(nt_frame_set_glyph(source,P(0,0),BYTES("A"),1,white,1));
    OK(nt_frame_set_glyph(source,P(1,0),BYTES("B"),1,white,2));
    NtPanelLayer projected = layer(source,RECT(0,0,2,1),RECT(0,0,1,1));
    projected.pose.turns = .125;
    const double diagonal = sqrt(.5);
    const double scales[] = {nextafter(diagonal,0),diagonal,nextafter(diagonal,1)};
    for (size_t i = 0; i < sizeof(scales)/sizeof(scales[0]); ++i) {
        projected.pose.scale.x = scales[i];
        OK(nt_surface_project_panels(&root,&projected,1));
        glyph(destination,0,0,i == 0 ? "B" : "A");
        CHECK(cell(destination,0,0).owner == (i == 0 ? 2 : 1));
    }
    nt_frame_destroy(destination);
    nt_frame_destroy(source);
    puts("inverse rotation uses the same scaled matrix as forward projection");
}

static NtResult select_cell(void *context, const NtPanelSample *sample, NtPanelPixel *pixel)
{
    size_t *calls = context;
    CHECK(isfinite(sample->source.x));
    ++*calls;
    pixel->source.x = .5;
    return NT_RESULT_OK;
}

static void finite_boundaries(void)
{
    NtFrame *source = frame(NULL,SIZE(1,1),NULL);
    NtSurfaceView root;
    NtFrame *destination = frame(NULL,SIZE(1,1),&root);
    OK(nt_frame_set_glyph(source,P(0,0),BYTES("A"),1,white,1));
    NtPanelLayer projected = layer(source,RECT(0,0,1,1),RECT(0,0,1,1));
    size_t calls = 0;
    projected.remap = select_cell;
    projected.remap_context = &calls;
    for (int sign = -1; sign <= 1; sign += 2) {
        projected.pose.scale.x = sign*0x1p-970;
        projected.pose.anchor.x = sign*DBL_MAX;
        projected.pose.offset.x = -projected.pose.anchor.x;
        OK(nt_frame_set_glyph(destination,P(0,0),BYTES("X"),1,white,9));
        const NtCellView before = cell(destination,0,0);
        CHECK(nt_surface_project_panels(&root,&projected,1) == NT_RESULT_OUT_OF_RANGE);
        CHECK(!calls && cell(destination,0,0).owner == 9 &&
              cell(destination,0,0).glyph.data == before.glyph.data);

        projected.pose.anchor.x = sign*nextafter(DBL_MAX,0);
        projected.pose.offset.x = -projected.pose.anchor.x;
        OK(nt_surface_project_panels(&root,&projected,1));
        CHECK(calls == 1 && cell(destination,0,0).owner == 1);
        calls = 0;
    }
    nt_frame_destroy(destination);
    nt_frame_destroy(source);
    puts("unrounded coordinates outside the finite range fail before callbacks or publication");
}

#if defined(FE_UPWARD) && defined(FE_DOWNWARD) && defined(FE_TOWARDZERO) && !defined(__wasm__)
typedef struct GeometryEnvironment { int mode, flags; } GeometryEnvironment;

static NtResult observe_environment(void *context, const NtPanelSample *sample, NtPanelPixel *pixel)
{
    const GeometryEnvironment *expected = context;
    CHECK(fegetround() == expected->mode && fetestexcept(FE_ALL_EXCEPT) == expected->flags);
    CHECK(sample->source.x < 1);
    pixel->source.x = .5;
    return NT_RESULT_OK;
}

static void rounding_environment(void)
{
    fenv_t original;
    CHECK(fegetenv(&original) == 0);
    NtFrame *source = frame(NULL,SIZE(1,1),NULL);
    NtSurfaceView root;
    NtFrame *destination = frame(NULL,SIZE(1,1),&root);
    OK(nt_frame_set_glyph(source,P(0,0),BYTES("A"),1,white,1));
    NtPanelLayer projected = layer(source,RECT(0,0,1,1),RECT(0,0,1,1));
    const int modes[] = {FE_TONEAREST,FE_UPWARD,FE_DOWNWARD,FE_TOWARDZERO};
    const double offsets[] = {0x1p-54,0x1p-53,0x1.8p-53};
    const double expected[] = {1,1,0x1.0000000000001p0};
    const double just_inside = -nextafter(.5,0);
    projected.remap = observe_environment;
    GeometryEnvironment environment;
    projected.remap_context = &environment;
    for (size_t i = 0; i < sizeof(modes)/sizeof(modes[0]); ++i) {
        CHECK(fesetround(modes[i]) == 0);
        CHECK(feclearexcept(FE_ALL_EXCEPT) == 0 && feraiseexcept(FE_DIVBYZERO) == 0);
        environment = (GeometryEnvironment){modes[i],fetestexcept(FE_ALL_EXCEPT)};
        for (size_t j = 0; j < sizeof(offsets)/sizeof(offsets[0]); ++j) {
            projected.pose.offset.x = offsets[j];
            NtPoint point;
            OK(nt_panel_project_point(&projected.pose,(NtPoint){1,.5},&point));
            CHECK(point.x == expected[j] && point.y == .5 && fegetround() == environment.mode);
            CHECK(fetestexcept(FE_ALL_EXCEPT) == environment.flags);
        }
        NtPanelTransition transition = {.from=identity,.to=identity,.duration_us=2};
        transition.from.offset.x = 1;
        transition.to.offset.x = expected[2];
        OK(nt_panel_pose_at(&transition,1,NT_MOTION_FULL,&projected.pose));
        CHECK(projected.pose.offset.x == 1 && fegetround() == environment.mode);
        CHECK(fetestexcept(FE_ALL_EXCEPT) == environment.flags);
        projected.pose.offset.x = just_inside;
        OK(nt_surface_project_panels(&root,&projected,1));
        glyph(destination,0,0,"A");
        CHECK(fegetround() == environment.mode);
    }
    CHECK(fesetenv(&original) == 0);
    nt_frame_destroy(destination);
    nt_frame_destroy(source);
    puts("geometry uses nearest rounding and preserves the caller environment before callbacks");
}

typedef struct CompositeEnvironment {
    GeometryEnvironment saved;
    size_t samples, resolutions;
    bool refuse;
} CompositeEnvironment;

static NtResult composite_sample(void *context, const NtPanelSample *sample, NtPanelPixel *pixel)
{
    (void)sample;
    (void)pixel;
    CompositeEnvironment *state = context;
    CHECK(fegetround() == state->saved.mode && fetestexcept(FE_ALL_EXCEPT) == state->saved.flags);
    ++state->samples;
    return state->refuse && state->samples == 2 ? NT_RESULT_OUT_OF_RANGE : NT_RESULT_OK;
}

static NtResult composite_owner(void *context, NtOwnerToken source, NtOwnerToken *out)
{
    CompositeEnvironment *state = context;
    CHECK(fegetround() == state->saved.mode && fetestexcept(FE_ALL_EXCEPT) == state->saved.flags);
    ++state->resolutions;
    *out = source+100;
    return NT_RESULT_OK;
}

static void compositing_environment(void)
{
    fenv_t original;
    CHECK(fegetenv(&original) == 0);
    NtFrame *source = frame(NULL,SIZE(1,2),NULL);
    NtSurfaceView root, child;
    NtFrame *destination = frame(NULL,SIZE(1,2),&root);
    const NtCellStyle ink = {{190,30,78,169},{6,110,220,199}};
    for (int32_t y = 0; y < 2; ++y) {
        OK(nt_frame_set_glyph(source,P(0,y),BYTES("A"),1,ink,1));
        OK(nt_frame_set_glyph(destination,P(0,y),BYTES("X"),1,white,9));
    }
    const uint8_t alpha[] = {128,43};
    const NtSurfaceMask mask = {.kind=NT_MASK_ALPHA,.bounds=RECT(0,0,1,2),
                               .alpha=alpha,.stride=1,.count=2};
    OK(nt_surface_child(&root,RECT(0,0,1,2),&mask,&child));
    NtPanelLayer projected = layer(source,RECT(0,0,1,2),RECT(0,0,1,2));
    CompositeEnvironment state = {0};
    projected.remap = composite_sample;
    projected.remap_context = &state;
    projected.owners = NT_SURFACE_OWNERS_MAP;
    projected.map_owner = composite_owner;
    projected.owner_context = &state;
    for (size_t attempt = 0; attempt < 2; ++attempt) {
        CHECK(fesetround(FE_DOWNWARD) == 0);
        CHECK(feclearexcept(FE_ALL_EXCEPT) == 0 && feraiseexcept(FE_DIVBYZERO) == 0);
        state = (CompositeEnvironment){.saved={FE_DOWNWARD,fetestexcept(FE_ALL_EXCEPT)},
                                       .refuse=attempt == 0};
        projected.mask = attempt == 0 ? &mask : NULL;
        const NtCellView prior = cell(destination,0,0);
        const NtResult result = nt_surface_project_panels(attempt == 0 ? &root : &child,&projected,1);
        CHECK(fegetround() == state.saved.mode && fetestexcept(FE_ALL_EXCEPT) == state.saved.flags);
        CHECK(state.samples == 2);
        if (attempt == 0) {
            CHECK(result == NT_RESULT_OUT_OF_RANGE && state.resolutions == 2);
            CHECK(cell(destination,0,0).glyph.data == prior.glyph.data);
            glyph(destination,0,0,"X");
        } else {
            OK(result);
            CHECK(state.resolutions == 4 && cell(destination,0,0).owner == 101);
            glyph(destination,0,0,"A");
            glyph(destination,0,1,"A");
        }
    }
    CHECK(fesetenv(&original) == 0);
    nt_frame_destroy(destination);
    nt_frame_destroy(source);
    puts("mask weights and blends preserve caller flags across remap and owner callbacks");
}
#endif

#if defined(FE_DFL_DISABLE_DENORMS_ENV) && defined(__APPLE__) && defined(__aarch64__)
static NtResult observe_denormal_flags(void *context, const NtPanelSample *sample, NtPanelPixel *pixel)
{
    (void)sample;
    (void)pixel;
    const fenv_t *expected = context;
    fenv_t observed;
    CHECK(fegetenv(&observed) == 0);
    CHECK(observed.__fpcr == expected->__fpcr && observed.__fpsr == expected->__fpsr);
    return NT_RESULT_OK;
}

static void denormal_environment(void)
{
    fenv_t original;
    CHECK(fegetenv(&original) == 0);
    NtSurfaceView root;
    NtFrame *source = frame(NULL,SIZE(2,1),NULL);
    NtFrame *destination = frame(NULL,SIZE(1,1),&root);
    OK(nt_frame_set_glyph(source,P(0,0),BYTES("A"),1,white,1));
    OK(nt_frame_set_glyph(source,P(1,0),BYTES("B"),1,white,2));
    const double tiny = 0x1p-1074;
    volatile double caller_input = tiny, caller_multiplier = 0x1p1023;
    CHECK(fesetenv(FE_DFL_DISABLE_DENORMS_ENV) == 0);
    CHECK(caller_input*caller_multiplier == 0);

    NtPanelPose pose = identity;
    pose.scale.x = 0x1p1023;
    NtPoint point;
    OK(nt_panel_project_point(&pose,(NtPoint){tiny,.5},&point));
    CHECK(point.x == 0x1p-51 && point.y == .5);
    CHECK(caller_input*caller_multiplier == 0);

    NtPanelLayer projected = layer(source,RECT(0,0,2,1),RECT(0,0,1,1));
    projected.pose.anchor.x = 1.5;
    projected.pose.offset.x = -1;
    projected.pose.scale.x = tiny;
    OK(nt_surface_project_panels(&root,&projected,1));
    glyph(destination,0,0,"B");

    projected.pose.anchor.x = -.5;
    projected.pose.offset.x = tiny;
    projected.pose.scale.x = 2;
    OK(nt_frame_set_glyph(destination,P(0,0),BYTES("T"),1,white,9));
    OK(nt_surface_project_panels(&root,&projected,1));
    glyph(destination,0,0,"T");
    projected.pose.opacity = -tiny;
    CHECK(nt_surface_project_panels(&root,&projected,1) == NT_RESULT_INVALID_ARGUMENT);
    glyph(destination,0,0,"T");
    CHECK(caller_input*caller_multiplier == 0);

    CHECK(fesetenv(FE_DFL_DISABLE_DENORMS_ENV) == 0 && feraiseexcept(FE_DIVBYZERO) == 0);
    fenv_t before, after;
    CHECK(fegetenv(&before) == 0);
    pose.scale.x = .5;
    OK(nt_panel_project_point(&pose,(NtPoint){0x1p-1022,.5},&point));
    CHECK(fegetenv(&after) == 0);
    CHECK(before.__fpcr == after.__fpcr && before.__fpsr == after.__fpsr);

    projected.pose = identity;
    projected.pose.anchor.x = -tiny;
    projected.pose.offset.x = .5;
    projected.pose.scale.x = 4;
    projected.remap = observe_denormal_flags;
    projected.remap_context = &before;
    OK(nt_surface_project_panels(&root,&projected,1));
    glyph(destination,0,0,"T");
    CHECK(fesetenv(FE_DFL_ENV) == 0);
    CHECK(point.x == 0x1p-1023 && point.y == .5);
    CHECK(fesetenv(&original) == 0);
    nt_frame_destroy(destination);
    nt_frame_destroy(source);
    puts("geometry supports gradual underflow and restores the caller's denormal mode");
}
#endif

static NtResult mapped(void *context, NtOwnerToken source, NtOwnerToken *out)
{
    const NtOwnerToken stop = *(const NtOwnerToken *)context;
    if (source == stop) return NT_RESULT_WRONG_PHASE;
    *out = source+100;
    return NT_RESULT_OK;
}

static void affine_projection(void)
{
    NtFrame *source = frame(NULL,SIZE(3,2),NULL);
    const char input[] = "ABCDEF";
    for (int32_t i = 0; i < 6; ++i) {
        OK(nt_frame_set_glyph(source,P(i%3,i/3),(NtByteView){input+i,1},1,white,(NtOwnerToken)(i+1)));
    }
    NtSurfaceView root, child;
    NtFrame *destination = frame(NULL,SIZE(8,5),&root);
    OK(nt_surface_child(&root,RECT(1,1,6,4),NULL,&child));
    NtPanelLayer projected = layer(source,RECT(0,0,3,2),RECT(1,0,2,3));
    projected.pose.turns = .25;
    projected.pose.offset.x = 2;
    const NtOwnerToken stop = 0;
    projected.owners = NT_SURFACE_OWNERS_MAP;
    projected.map_owner = mapped;
    projected.owner_context = (void *)&stop;
    OK(nt_surface_project_panels(&child,&projected,1));
    const char rotated[] = "DAEBFC";
    for (int32_t i = 0; i < 6; ++i) {
        const NtCellView actual = cell(destination,2+i%2,1+i/2);
        CHECK(actual.glyph.count == 1 && actual.glyph.data[0] == rotated[i]);
        CHECK(actual.owner == 101+(NtOwnerToken)(rotated[i]-'A'));
    }
    glyph(destination,1,1,"");

    projected.viewport = RECT(4,0,3,3);
    projected.pose = identity;
    projected.pose.scale = (NtPoint){2,1};
    projected.region = RECT(1,0,2,2);
    OK(nt_surface_project_panels(&child,&projected,1));
    glyph(destination,5,1,"B");
    glyph(destination,6,1,"B");
    glyph(destination,7,1,"");
    glyph(destination,5,2,"E");
    glyph(destination,6,2,"E");

    projected = layer(source,RECT(0,0,3,1),RECT(0,0,3,1));
    projected.pose.scale.x = -1;
    projected.pose.offset.x = 3;
    OK(nt_surface_project_panels(&root,&projected,1));
    glyph(destination,0,0,"C");
    glyph(destination,1,0,"B");
    glyph(destination,2,0,"A");
    nt_frame_destroy(destination);
    nt_frame_destroy(source);
    puts("quarter-turn raster, anchored scale, reflection, nested clips and mapped owners");
}

/* Two viewport bands slip in opposite directions behind a moving aperture. */
static NtResult striped_iris(void *context, const NtPanelSample *sample, NtPanelPixel *pixel)
{
    const bool fail_late = *(const bool *)context;
    CHECK(sample->viewport_size.columns == 6 && sample->region_size.columns == 5);
    const double row = sample->destination.y;
    if (fail_late && row > 1) return NT_RESULT_WRONG_PHASE;
    const double time = (double)sample->elapsed_us / 100;
    const double side = row < 1 ? -1 : 1;
    pixel->source.x += side*(1-time);
    const double distance = hypot(sample->destination.x-3,row-1);
    pixel->opacity = distance < 2+time ? (row < 1 ? .5 : 1) : 0;
    return NT_RESULT_OK;
}

static void consumer_effect(void)
{
    NtFrame *source = frame(NULL,SIZE(5,2),NULL);
    for (int32_t y = 0; y < 2; ++y) {
        for (int32_t x = 0; x < 5; ++x)
            OK(nt_frame_set_glyph(source,P(x,y),BYTES("x"),1,white,(NtOwnerToken)(10+x)));
    }
    NtSurfaceView root;
    NtFrame *destination = frame(NULL,SIZE(7,3),&root);
    OK(nt_frame_set_glyph(destination,P(6,2),BYTES("!"),1,white,99));
    const NtCellView before = cell(destination,6,2);
    NtPanelLayer projected = layer(source,RECT(0,0,5,2),RECT(0,0,6,2));
    bool fail_late = true;
    projected.remap = striped_iris;
    projected.remap_context = &fail_late;
    projected.pose.offset = (NtPoint){1,0};
    CHECK(nt_surface_project_panels(&root,&projected,1) == NT_RESULT_WRONG_PHASE);
    CHECK(cell(destination,6,2).glyph.data == before.glyph.data);
    glyph(destination,2,0,"");
    fail_late = false;
    OK(nt_surface_project_panels(&root,&projected,1));
    glyph(destination,0,0,"");
    CHECK(cell(destination,2,0).owner == 10);
    CHECK(cell(destination,2,0).style.foreground.alpha == 128);
    CHECK(cell(destination,2,1).owner == 12);
    CHECK(cell(destination,2,1).style.foreground.alpha == 255);
    glyph(destination,5,1,"");
    nt_frame_destroy(destination);
    nt_frame_destroy(source);
    puts("installed consumer composes a striped iris and rolls back on a later callback row");
}

static NtResult cut(void *context, const NtPanelSample *sample, NtPanelPixel *pixel)
{
    const int column = *(const int *)context;
    if ((int)sample->destination.x == column) pixel->opacity = 0;
    return NT_RESULT_OK;
}

static NtResult dim(void *context, const NtPanelSample *sample, NtPanelPixel *pixel)
{
    (void)sample;
    pixel->opacity = *(const double *)context;
    return NT_RESULT_OK;
}

static void contribution_threshold(void)
{
    NtSurfaceView offscreen, root;
    NtFrame *source = frame(NULL,SIZE(1,1),&offscreen);
    NtFrame *destination = frame(NULL,SIZE(1,1),&root);
    OK(nt_frame_set_glyph(destination,P(0,0),BYTES("I"),1,white,14));
    NtCellStyle faint = white;
    faint.foreground.alpha = 1;
    OK(nt_frame_set_glyph(source,P(0,0),BYTES("N"),1,faint,41));
    OK(nt_surface_fill(&offscreen,RECT(0,0,1,1),(NtRgba){32,16,8,1},42));
    NtPanelLayer projected = layer(source,RECT(0,0,1,1),RECT(0,0,1,1));
    double procedural = 0x1.5555555555555p-1;
    projected.pose.opacity = .75;
    projected.remap = dim;
    projected.remap_context = &procedural;
    const NtCellView before = cell(destination,0,0);
    OK(nt_surface_project_panels(&root,&projected,1));
    CHECK(cell(destination,0,0).glyph.data == before.glyph.data);
    CHECK(background_at(destination,0,0).color.alpha == 0);

    procedural = nextafter(procedural,1);
    OK(nt_surface_project_panels(&root,&projected,1));
    glyph(destination,0,0,"N");
    CHECK(cell(destination,0,0).owner == 41);
    CHECK(background_at(destination,0,0).color.alpha == 1 &&
          background_at(destination,0,0).owner == 42);
    nt_frame_destroy(destination);

    destination = frame(NULL,SIZE(1,1),&root);
    faint.foreground.alpha = 2;
    OK(nt_frame_set_glyph(source,P(0,0),BYTES("N"),1,faint,41));
    OK(nt_surface_fill(&offscreen,RECT(0,0,1,1),(NtRgba){32,16,8,2},42));
    projected.pose.opacity = procedural = .5;
    OK(nt_surface_project_panels(&root,&projected,1));
    glyph(destination,0,0,"N");
    CHECK(cell(destination,0,0).style.foreground.alpha == 1);
    CHECK(background_at(destination,0,0).color.alpha == 1);
    nt_frame_destroy(destination);
    nt_frame_destroy(source);
    puts("separate binary opacity factors preserve the exact half-byte contribution threshold");
}

static void whole_glyph(void)
{
    NtSurfaceView source_view;
    NtFrame *source = frame(NULL,SIZE(4,2),&source_view);
    const NtRgba red = {220,20,10,255}, blue = {20,40,200,255};
    OK(nt_frame_set_glyph(source,P(0,0),BYTES(astronaut),2,white,71));
    OK(nt_frame_set_glyph(source,P(2,0),BYTES(accent),1,white,72));
    OK(nt_surface_fill(&source_view,RECT(0,0,1,1),red,81));
    OK(nt_surface_fill(&source_view,RECT(1,0,1,1),blue,82));
    NtSurfaceView root;
    NtFrame *destination = frame(NULL,SIZE(4,3),&root);
    NtPanelLayer projected = layer(source,RECT(0,0,4,2),RECT(0,0,4,2));
    int masked_column = 1;
    projected.remap = cut;
    projected.remap_context = &masked_column;
    OK(nt_surface_project_panels(&root,&projected,1));
    glyph(destination,0,0,"");
    glyph(destination,1,0,"");
    glyph(destination,2,0,accent);
    CHECK(background_at(destination,0,0).owner == 81);
    CHECK(background_at(destination,1,0).color.alpha == 0);

    projected.remap = NULL;
    const uint8_t opacity[] = {255,128,255,255};
    const NtSurfaceMask mask = {.kind=NT_MASK_ALPHA,.bounds=RECT(0,0,4,1),.alpha=opacity,.stride=4,.count=4};
    projected.mask = &mask;
    OK(nt_surface_project_panels(&root,&projected,1));
    glyph(destination,0,0,astronaut);
    const NtCellView tail = cell(destination,1,0);
    CHECK(tail.glyph_columns == 2 && tail.lead.x == 0 && tail.owner == 71);
    CHECK(tail.style.foreground.alpha == 128 && background_at(destination,1,0).owner == 82);

    projected.region = RECT(2,0,1,1);
    projected.viewport = RECT(1,0,1,1);
    projected.mask = NULL;
    const NtByteView prior = cell(destination,0,0).glyph;
    OK(nt_surface_project_panels(&root,&projected,1));
    CHECK(cell(destination,0,0).glyph.data == prior.data);
    CHECK(cell(destination,1,0).lead.x == 0);

    projected = layer(source,RECT(0,0,1,1),RECT(2,2,2,1));
    projected.pose.scale.x = 2;
    OK(nt_surface_project_panels(&root,&projected,1));
    glyph(destination,2,2,"");
    CHECK(background_at(destination,2,2).owner == 81);

    projected = layer(source,RECT(0,0,2,2),RECT(0,1,2,2));
    projected.pose.turns = .25;
    projected.pose.offset.x = 2;
    OK(nt_surface_project_panels(&root,&projected,1));
    glyph(destination,1,1,"");
    nt_frame_destroy(destination);
    destination = frame(NULL,SIZE(3,3),&root);
    projected.viewport = RECT(0,0,3,2);
    projected.pose.scale.y = 1.5;
    projected.pose.offset.x = 2.75;
    OK(nt_surface_project_panels(&root,&projected,1));
    glyph(destination,1,0,astronaut);
    CHECK(cell(destination,2,0).lead.x == 1);
    nt_frame_destroy(destination);

    destination = frame(NULL,SIZE(4,1),&root);
    projected = layer(source,RECT(0,0,4,1),RECT(0,0,4,1));
    projected.pose.scale.x = -1;
    projected.pose.offset.x = 4;
    OK(nt_surface_project_panels(&root,&projected,1));
    glyph(destination,1,0,accent);
    glyph(destination,2,0,astronaut);
    CHECK(cell(destination,3,0).lead.x == 2);
    CHECK(background_at(destination,2,0).owner == 82 && background_at(destination,3,0).owner == 81);
    nt_frame_destroy(destination);
    nt_frame_destroy(source);
    puts("complete ZWJ and combining glyphs across masks, rotation and cropped footprints");
}

typedef struct AllocationLog { size_t calls, live, refuse; } AllocationLog;

static void *allocate(void *context, size_t bytes, size_t alignment)
{
    AllocationLog *log = context;
    CHECK(bytes && alignment);
    if (++log->calls == log->refuse) return NULL;
    void *value = malloc(bytes);
    CHECK(value);
    ++log->live;
    return value;
}

static void release(void *context, void *memory, size_t bytes, size_t alignment)
{
    AllocationLog *log = context;
    CHECK(memory && bytes && alignment && log->live);
    --log->live;
    free(memory);
}

static void transactions(void)
{
    AllocationLog log = {0};
    const NtAllocator allocator = {&log,allocate,release};
    NtSurfaceView root;
    NtFrame *destination = frame(&allocator,SIZE(3,3),&root);
    OK(nt_frame_set_glyph(destination,P(0,0),BYTES("A"),1,white,11));
    OK(nt_frame_set_glyph(destination,P(1,0),BYTES("B"),1,white,12));
    NtPanelLayer layers[] = {
        layer(destination,RECT(0,0,1,1),RECT(1,0,1,1)),
        layer(destination,RECT(1,0,1,1),RECT(0,0,1,1))
    };
    const NtCellView before = cell(destination,0,0);
    const size_t live = log.live;
    layers[1].owners = NT_SURFACE_OWNERS_MAP;
    layers[1].map_owner = mapped;
    NtOwnerToken stop = 12;
    layers[1].owner_context = &stop;
    CHECK(nt_surface_project_panels(&root,layers,2) == NT_RESULT_WRONG_PHASE);
    CHECK(cell(destination,0,0).glyph.data == before.glyph.data && log.live == live);
    glyph(destination,1,0,"B");
    layers[1].pose.scale.x = NAN;
    CHECK(nt_surface_project_panels(&root,layers,2) == NT_RESULT_INVALID_ARGUMENT);
    glyph(destination,1,0,"B");
    layers[1].pose = identity;
    stop = 0;
    size_t attempt = 1;
    for (;; ++attempt) {
        log.refuse = log.calls+attempt;
        const NtResult result = nt_surface_project_panels(&root,layers,2);
        if (result == NT_RESULT_OK) break;
        CHECK(result == NT_RESULT_ALLOCATION_FAILED && attempt < 25 && log.live == live);
        CHECK(cell(destination,0,0).glyph.data == before.glyph.data);
        glyph(destination,1,0,"B");
    }
    CHECK(attempt >= 6);
    glyph(destination,0,0,"B");
    glyph(destination,1,0,"A");
    CHECK(cell(destination,0,0).owner == 112 && cell(destination,1,0).owner == 11);
    nt_frame_destroy(destination);
    CHECK(log.live == 0);
    puts("all layers read the pre-call source, with rollback on mapper, pose and allocator errors");
}

typedef struct MountedPanel {
    NtPanelLayer layer;
    NtPanelTransition transition;
} MountedPanel;

static NtResult measure(const void *state, NtMeasureContext *ctx, NtConstraints limits, NtCellSize *out)
{
    (void)state;
    (void)ctx;
    *out = SIZE(limits.columns.min,limits.rows.min);
    return NT_RESULT_OK;
}

static NtResult layout(const void *state, NtLayoutContext *ctx, NtCellSize size)
{
    (void)state;
    (void)ctx;
    (void)size;
    return NT_RESULT_OK;
}

static NtResult update(void *state, NtUpdateInfo info, NtUpdateReply *reply)
{
    MountedPanel *panel = state;
    panel->layer.elapsed_us = info.now;
    reply->invalidate = NT_INVALIDATE_PAINT;
    if (info.now < 10) reply->next = (NtDeadline){true,10};
    return nt_panel_pose_at(&panel->transition,info.now,info.motion,&panel->layer.pose);
}

static NtResult paint(const void *state, NtPaintContext *ctx, NtCellSize size)
{
    const MountedPanel *panel = state;
    NtPanelLayer viewport = panel->layer;
    viewport.viewport.size = size;
    return nt_paint_project_panels(ctx,&viewport,1);
}

static void mounted(void)
{
    NtSurfaceView source_view;
    NtFrame *source = frame(NULL,SIZE(1,1),&source_view);
    OK(nt_frame_set_glyph(source,P(0,0),BYTES("M"),1,white,17));
    const NtRgba background = {0,0,255,255};
    OK(nt_surface_fill(&source_view,RECT(0,0,1,1),background,0));
    MountedPanel panel = {.layer=layer(source,RECT(0,0,1,1),RECT(0,0,5,1)),
                          .transition={.from=identity,.to=identity,.duration_us=10}};
    panel.transition.from.offset.x = 3;
    NtTree *tree;
    OK(nt_tree_create(NULL,&tree));
    NtNodeId node;
    const NtComponentOps ops = {.measure=measure,.layout=layout,.update=update,.paint=paint};
    OK(nt_tree_mount(tree,(NtNodeId){0},&ops,&panel,(NtMountOptions){true,false,false},&node));
    OK(nt_tree_tick(tree,0,NT_MOTION_FULL));
    OK(nt_tree_publish(tree,SIZE(5,1)));
    const NtFrame *visible = nt_tree_frame(tree);
    glyph(visible,3,0,"M");
    CHECK(cell(visible,3,0).owner == nt_node_owner(node));
    CHECK(background_at(visible,3,0).owner == nt_node_owner(node));
    OK(nt_tree_tick(tree,10,NT_MOTION_REDUCED));
    OK(nt_tree_publish(tree,SIZE(2,1)));
    glyph(nt_tree_frame(tree),0,0,"M");
    CHECK(cell(nt_tree_frame(tree),0,0).owner == nt_node_owner(node));
    nt_tree_destroy(tree);
    nt_frame_destroy(source);
    puts("mounted paint owns transformed glyphs and backgrounds through update and resize");
}

int main(void)
{
    pose_clock();
    affine_cancellation();
    inverse_cell_boundary();
    wrapped_boundaries();
    inverse_anchor();
    diagonal_boundaries();
    inverse_rotation_scale();
    finite_boundaries();
#if defined(FE_UPWARD) && defined(FE_DOWNWARD) && defined(FE_TOWARDZERO) && !defined(__wasm__)
    rounding_environment();
    compositing_environment();
#endif
#if defined(FE_DFL_DISABLE_DENORMS_ENV) && defined(__APPLE__) && defined(__aarch64__)
    denormal_environment();
#endif
    affine_projection();
    consumer_effect();
    contribution_threshold();
    whole_glyph();
    transactions();
    mounted();
    return 0;
}
