#include <nocturne/component.h>
#include <nocturne/text_effect.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr,"text effect line %d: %s\n",__LINE__,#test); exit(EXIT_FAILURE); \
} } while (0)
#define BYTES(s) ((NtByteView){(s),sizeof(s)-1})

static const NtCellStyle red = {{255,0,0,255},{0,0,0,0}};
static const NtTextPaintOptions ink = {.base={{ {255,0,0,255},{0,0,0,0} },99,NULL,NULL}};
static const char combined[] = "e\xcc\x81";
static const char astronaut[] = "👩‍🚀";

static NtTextLayout *layout(const char *content, NtDirection direction)
{
    const NtTextLayoutOptions options = {.width={NT_WIDTH_ONE,NT_WIDTH_TWO},.align=NT_TEXT_ALIGN_START,
                                         .base_direction=direction,.tab_stop=4};
    NtTextLayout *out;
    CHECK(nt_text_layout_create_borrowed(NULL,(NtByteView){content,strlen(content)},&options,&out) == NT_RESULT_OK);
    return out;
}

static NtFrame *frame(uint32_t columns, uint32_t rows, NtSurfaceView *view)
{
    NtFrame *out;
    CHECK(nt_frame_create(NULL,(NtCellSize){columns,rows},&out) == NT_RESULT_OK);
    CHECK(nt_surface_root(out,view) == NT_RESULT_OK);
    return out;
}

static NtCellView cell(const NtFrame *frame, int32_t x, int32_t y)
{
    NtCellView view;
    CHECK(nt_frame_cell(frame,(NtCellPoint){x,y},&view) == NT_RESULT_OK);
    return view;
}

static void glyph(const NtFrame *frame, int32_t x, int32_t y, const char *text)
{
    const NtCellView view = cell(frame,x,y);
    const size_t bytes = strlen(text);
    CHECK(view.glyph.count == bytes && (!bytes || memcmp(view.glyph.data,text,bytes) == 0));
}

static void bidi_and_settle(void)
{
    NtTextLayout *text = layout("אב",NT_DIRECTION_RTL);
    NtSurfaceView view;
    NtFrame *f = frame(4,1,&view);
    NtTextPaintOptions options = ink;
    NtTextEffect effect = {.schedule={.stagger_us=10}};
    CHECK(nt_text_effect_paint(&view,(NtCellPoint){0,0},text,&options,&effect) == NT_RESULT_OK);
    glyph(f,0,0,"");
    glyph(f,1,0,"א");
    nt_frame_destroy(f);

    effect.order = NT_TEXT_EFFECT_VISUAL;
    f = frame(4,1,&view);
    CHECK(nt_text_effect_paint(&view,(NtCellPoint){0,0},text,&options,&effect) == NT_RESULT_OK);
    glyph(f,0,0,"ב");
    glyph(f,1,0,"");
    nt_frame_destroy(f);

    effect.order = NT_TEXT_EFFECT_REVERSE_VISUAL;
    options.elapsed_us = 10;
    f = frame(4,1,&view);
    CHECK(nt_text_effect_paint(&view,(NtCellPoint){0,0},text,&options,&effect) == NT_RESULT_OK);
    glyph(f,0,0,"ב");
    glyph(f,1,0,"א");
    nt_frame_destroy(f);

    effect.visibility = NT_TEXT_HIDE;
    options.elapsed_us = 0;
    options.motion = NT_MOTION_REDUCED;
    f = frame(4,1,&view);
    CHECK(nt_frame_set_glyph(f,(NtCellPoint){0,0},BYTES("."),1,red,88) == NT_RESULT_OK);
    CHECK(nt_text_effect_paint(&view,(NtCellPoint){0,0},text,&options,&effect) == NT_RESULT_OK);
    glyph(f,0,0,".");
    glyph(f,1,0,"");
    CHECK(cell(f,0,0).owner == 88);
    nt_frame_destroy(f);
    nt_text_layout_destroy(text);
    puts("logical and visual bidi reveal, boundary timing and reduced hide");
}

static void substitutions(void)
{
    NtTextLayout *text = layout("e\xcc\x81👩‍🚀",NT_DIRECTION_LTR);
    NtSurfaceView view;
    NtFrame *f = frame(6,1,&view);
    const NtByteView alph[] = {BYTES("*"),BYTES("界")};
    NtTextEffect effect = {.delay_us=4,.duration_us=20,.visibility=NT_TEXT_VISIBLE,
                           .glyphs={NT_TEXT_SCRAMBLE,alph,2,3,0},.seed=61};
    NtTextPaintOptions options = ink;
    options.elapsed_us = 3;
    CHECK(nt_text_effect_paint(&view,(NtCellPoint){0,0},text,&options,&effect) == NT_RESULT_OK);
    glyph(f,0,0,combined);
    glyph(f,1,0,astronaut);
    options.elapsed_us = 4;
    CHECK(nt_text_effect_paint(&view,(NtCellPoint){0,0},text,&options,&effect) == NT_RESULT_OK);
    glyph(f,0,0,"*");
    glyph(f,1,0,"界");
    const NtCellView tail = cell(f,2,0);
    CHECK(tail.lead.x == 1 && tail.glyph_columns == 2 && tail.owner == 99);
    options.elapsed_us = 24;
    CHECK(nt_text_effect_paint(&view,(NtCellPoint){0,0},text,&options,&effect) == NT_RESULT_OK);
    glyph(f,0,0,combined);
    glyph(f,1,0,astronaut);
    nt_frame_destroy(f);

    f = frame(2,1,&view);
    NtSurfaceView clipped;
    CHECK(nt_surface_child(&view,(NtCellRect){{0,0},{2,1}},NULL,&clipped) == NT_RESULT_OK);
    options.elapsed_us = 4;
    CHECK(nt_text_effect_paint(&clipped,(NtCellPoint){0,0},text,&options,&effect) == NT_RESULT_OK);
    glyph(f,0,0,"*");
    glyph(f,1,0,"");
    nt_frame_destroy(f);
    nt_text_layout_destroy(text);

    const NtByteView cycle[] = {BYTES("x"),BYTES("y"),BYTES("z")};
    text = layout("Q",NT_DIRECTION_LTR);
    effect = (NtTextEffect){.duration_us=20,.visibility=NT_TEXT_VISIBLE,
                            .glyphs={NT_TEXT_FLIPBOARD,cycle,3,2,0},.seed=318};
    f = frame(1,1,&view);
    char observed[3];
    for (size_t i = 0; i < 3; ++i) {
        options.elapsed_us = 2*i;
        CHECK(nt_text_effect_paint(&view,(NtCellPoint){0,0},text,&options,&effect) == NT_RESULT_OK);
        observed[i] = cell(f,0,0).glyph.data[0];
        CHECK(observed[i] >= 'x' && observed[i] <= 'z');
        if (i) CHECK(observed[i] == 'x' + (observed[i-1]-'x'+1)%3);
    }
    options.elapsed_us = 0;
    CHECK(nt_text_effect_paint(&view,(NtCellPoint){0,0},text,&options,&effect) == NT_RESULT_OK);
    CHECK(cell(f,0,0).glyph.data[0] == observed[0]);
    effect.glyphs.rotations = 3;
    options.elapsed_us = 2;
    CHECK(nt_text_effect_paint(&view,(NtCellPoint){0,0},text,&options,&effect) == NT_RESULT_OK);
    CHECK(cell(f,0,0).glyph.data[0] == observed[1]);
    const NtTimeUs durations[] = {120,UINT64_MAX-3};
    effect.glyphs.interval_us = 1;
    for (size_t i = 0; i < sizeof(durations)/sizeof(durations[0]); ++i) {
        effect.duration_us = durations[i];
        const NtTimeUs step = effect.duration_us/12;
        for (unsigned boundary = 1; boundary <= 12; ++boundary) {
            options.elapsed_us = step*boundary-1;
            CHECK(nt_text_effect_paint(&view,(NtCellPoint){0,0},text,&options,&effect) == NT_RESULT_OK);
            CHECK(cell(f,0,0).glyph.data[0] == 'x' + (observed[0]-'x'+boundary-1)%3);
            ++options.elapsed_us;
            CHECK(nt_text_effect_paint(&view,(NtCellPoint){0,0},text,&options,&effect) == NT_RESULT_OK);
            CHECK(cell(f,0,0).glyph.data[0] ==
                  (boundary == 12 ? 'Q' : 'x' + (observed[0]-'x'+boundary)%3));
        }
    }
    nt_frame_destroy(f);
    nt_text_layout_destroy(text);
    puts("clocked substitutions preserve width and hit exact small and full-width glyph steps");
}

typedef struct TransformState { bool fail, bad_width, bad_offset; size_t visits; } TransformState;

static NtResult transform(void *opaque, const NtTextEffectSample *sample, NtTextPose *pose)
{
    TransformState *state = opaque;
    ++state->visits;
    CHECK(sample->line_begin == 0 && sample->line_end > sample->cluster.position.x);
    if (state->fail && sample->logical_index == 1) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    if (state->bad_width) {
        pose->glyph = BYTES("界");
        return NT_RESULT_OK;
    }
    if (state->bad_offset) {
        pose->offset.x = 2147483648.0;
        return NT_RESULT_OK;
    }
    const NtTextMotion motion = {.travel={10,0},.easing=NT_LINEAR};
    NtResult result = nt_text_pose_motion(sample,&motion,pose);
    if (result == NT_RESULT_OK) result = nt_text_pose_noise(sample,(NtPoint){0,0},10,true,pose);
    pose->hue_degrees = 120;
    return result;
}

static void trails_and_atomic_errors(void)
{
    NtTextLayout *text = layout("A",NT_DIRECTION_LTR);
    NtSurfaceView view;
    NtFrame *f = frame(12,1,&view);
    TransformState state = {0};
    NtTextEffect effect = {.duration_us=100,.trails={2,20,.5},.transform=transform,.context=&state};
    NtTextPaintOptions options = ink;
    options.elapsed_us = 60;
    CHECK(nt_text_effect_paint(&view,(NtCellPoint){0,0},text,&options,&effect) == NT_RESULT_OK);
    CHECK(state.visits == 3);
    glyph(f,4,0,"A");
    glyph(f,6,0,"A");
    glyph(f,8,0,"A");
    const NtCellView current = cell(f,4,0);
    CHECK(current.style.foreground.green == 255 && current.style.foreground.red == 0);
    CHECK(current.style.foreground.alpha == 153 && cell(f,6,0).style.foreground.alpha == 51 &&
          cell(f,8,0).style.foreground.alpha == 13);
    nt_frame_destroy(f);

    f = frame(12,1,&view);
    options.motion = NT_MOTION_REDUCED;
    CHECK(nt_text_effect_paint(&view,(NtCellPoint){0,0},text,&options,&effect) == NT_RESULT_OK);
    CHECK(state.visits == 3 && cell(f,0,0).style.foreground.red == 255);
    glyph(f,4,0,"");
    nt_frame_destroy(f);
    nt_text_layout_destroy(text);

    f = frame(12,1,&view);
    text = layout("AB",NT_DIRECTION_LTR);
    CHECK(nt_frame_set_glyph(f,(NtCellPoint){0,0},BYTES("."),1,red,88) == NT_RESULT_OK);
    const NtCellView before = cell(f,0,0);
    options.motion = NT_MOTION_FULL;
    state.fail = true;
    effect.trails = (NtTextTrails){0};
    CHECK(nt_text_effect_paint(&view,(NtCellPoint){0,0},text,&options,&effect) ==
          NT_RESULT_UNSUPPORTED_CAPABILITY);
    CHECK(cell(f,0,0).glyph.data == before.glyph.data && cell(f,0,0).owner == 88);
    glyph(f,4,0,"");
    state.fail = false;
    state.bad_width = true;
    CHECK(nt_text_effect_paint(&view,(NtCellPoint){0,0},text,&options,&effect) ==
          NT_RESULT_INVALID_ARGUMENT);
    CHECK(cell(f,0,0).glyph.data == before.glyph.data);
    state.bad_width = false;
    state.bad_offset = true;
    CHECK(nt_text_effect_paint(&view,(NtCellPoint){0,0},text,&options,&effect) ==
          NT_RESULT_OUT_OF_RANGE);
    CHECK(cell(f,0,0).glyph.data == before.glyph.data);
    state.bad_offset = false;
    CHECK(nt_text_effect_paint(&view,(NtCellPoint){0,0},text,&options,&effect) == NT_RESULT_OK);
    glyph(f,4,0,"A");
    glyph(f,5,0,"B");
    nt_text_layout_destroy(text);
    nt_frame_destroy(f);
    puts("shared motion and hue, deterministic trail alpha, reduced settlement and callback rollback");
}

static void motion_sampling(void)
{
    NtTextEffectSample sample = {.progress=.25,.travel=.5,.seed=53,.identity=91,.local_us=17};
    const NtTextMotion motion = {.travel={2,2},.orbit={4,6},.cycles=1,.hue_degrees=180};
    NtTextPose pose = {.opacity=1};
    CHECK(nt_text_pose_motion(&sample,&motion,&pose) == NT_RESULT_OK);
    CHECK(fabs(pose.offset.x-1) < 1e-12 && fabs(pose.offset.y-4) < 1e-12 && pose.hue_degrees == 90);

    NtTextPose first = {.opacity=1}, again = first, later = first;
    const NtPoint extent = {4,6};
    CHECK(nt_text_pose_noise(&sample,extent,8,true,&first) == NT_RESULT_OK);
    CHECK(fabs(first.offset.x) <= 2 && fabs(first.offset.y) <= 3);
    sample.local_us = 68;
    CHECK(nt_text_pose_noise(&sample,extent,8,true,&later) == NT_RESULT_OK);
    CHECK(first.offset.x != later.offset.x && first.offset.y != later.offset.y);
    sample.local_us = 17;
    CHECK(nt_text_pose_noise(&sample,extent,8,true,&again) == NT_RESULT_OK);
    CHECK(first.offset.x == again.offset.x && first.offset.y == again.offset.y);
    puts("elliptical displacement and hue, bounded clock-independent seeded noise");
}

static NtResult step_travel(void *context, const NtTextEffectSample *sample, NtTextPose *pose)
{
    (void)context;
    const NtTextMotion motion = {.travel={4,0},.easing=NT_STEP_END};
    return nt_text_pose_motion(sample,&motion,pose);
}

static void reverse_travel_endpoint(void)
{
    NtTextLayout *text = layout("A",NT_DIRECTION_LTR);
    const NtTextEffect effect = {.delay_us=7,.duration_us=UINT64_MAX-7,.visibility=NT_TEXT_VISIBLE,
                                 .transform=step_travel};
    const NtTimeUs times[] = {7,8,UINT64_MAX-1,UINT64_MAX};
    for (size_t i = 0; i < sizeof(times)/sizeof(times[0]); ++i) {
        NtSurfaceView view;
        NtFrame *f = frame(5,1,&view);
        NtTextPaintOptions options = ink;
        options.elapsed_us = times[i];
        CHECK(nt_text_effect_paint(&view,(NtCellPoint){0,0},text,&options,&effect) == NT_RESULT_OK);
        glyph(f,i == 0 ? 4 : 0,0,"A");
        glyph(f,i == 0 ? 0 : 4,0,"");
        nt_frame_destroy(f);
    }
    nt_text_layout_destroy(text);
    puts("reveal travel preserves the first and last integer clock ticks for step easing");
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

static void release(void *context, void *address, size_t bytes, size_t alignment)
{
    AllocationLog *log = context;
    CHECK(log->live && address && bytes && alignment);
    --log->live;
    free(address);
}

static void allocation_refusal(void)
{
    NtTextLayout *text = layout("AB",NT_DIRECTION_LTR);
    AllocationLog log = {0};
    const NtAllocator allocator = {&log,allocate,release};
    NtFrame *f;
    NtSurfaceView surface;
    CHECK(nt_frame_create(&allocator,(NtCellSize){4,1},&f) == NT_RESULT_OK);
    CHECK(nt_surface_root(f,&surface) == NT_RESULT_OK);
    CHECK(nt_frame_set_glyph(f,(NtCellPoint){0,0},BYTES("."),1,red,88) == NT_RESULT_OK);
    const NtCellView before = cell(f,0,0);
    const size_t live = log.live;
    const NtByteView alphabet[] = {BYTES("x")};
    const NtTextEffect effect = {.duration_us=20,.visibility=NT_TEXT_VISIBLE,
        .glyphs={NT_TEXT_SCRAMBLE,alphabet,1,2,0}};
    size_t refusal = 1;
    for (;; ++refusal) {
        log.refuse = log.calls+refusal;
        const NtResult result = nt_text_effect_paint(&surface,(NtCellPoint){0,0},text,&ink,&effect);
        if (result == NT_RESULT_OK) break;
        CHECK(result == NT_RESULT_ALLOCATION_FAILED && refusal < 32 && log.live == live);
        CHECK(cell(f,0,0).glyph.data == before.glyph.data && cell(f,0,0).owner == 88);
        glyph(f,0,0,".");
        glyph(f,1,0,"");
    }
    CHECK(refusal > 3);
    glyph(f,0,0,"x");
    glyph(f,1,0,"x");
    nt_frame_destroy(f);
    nt_text_layout_destroy(text);
    CHECK(log.live == 0);
    puts("allocation refusal preserves published cells, owners and glyph borrows");
}

static NtResult measure(const void *state,NtMeasureContext *ctx,NtConstraints bounds,NtCellSize *out)
{
    (void)state;
    (void)ctx;
    *out = (NtCellSize){bounds.columns.min,bounds.rows.min};
    return NT_RESULT_OK;
}

static NtResult place(const void *state,NtLayoutContext *ctx,NtCellSize assigned)
{
    (void)state;
    (void)ctx;
    (void)assigned;
    return NT_RESULT_OK;
}

typedef struct Leaf { const NtTextLayout *text; NtTextPaintOptions options; } Leaf;

static NtResult update(void *state,NtUpdateInfo info,NtUpdateReply *reply)
{
    Leaf *leaf = state;
    leaf->options.elapsed_us = info.now;
    leaf->options.motion = info.motion;
    reply->invalidate = NT_INVALIDATE_PAINT;
    if (info.now < 10) reply->next = (NtDeadline){true,10};
    return NT_RESULT_OK;
}

static NtResult paint(const void *state,NtPaintContext *ctx,NtCellSize assigned)
{
    (void)assigned;
    const Leaf *leaf = state;
    const NtTextEffect effect = {.duration_us=10,.opacity_easing=NT_STEP_END};
    return nt_paint_text_effect(ctx,(NtCellPoint){0,0},leaf->text,&leaf->options,&effect);
}

static void component_effect(void)
{
    NtTree *tree;
    CHECK(nt_tree_create(NULL,&tree) == NT_RESULT_OK);
    NtTextLayout *text = layout("B",NT_DIRECTION_LTR);
    Leaf leaf = {text,ink};
    const NtComponentOps ops = {measure,place,update,paint,NULL,NULL};
    NtNodeId node;
    CHECK(nt_tree_mount(tree,(NtNodeId){0},&ops,&leaf,(NtMountOptions){true,false,false},&node) == NT_RESULT_OK);
    CHECK(nt_tree_tick(tree,0,NT_MOTION_FULL) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){2,1}) == NT_RESULT_OK);
    glyph(nt_tree_frame(tree),0,0,"");
    CHECK(nt_tree_tick(tree,10,NT_MOTION_FULL) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){2,1}) == NT_RESULT_OK);
    const NtFrame *f = nt_tree_frame(tree);
    glyph(f,0,0,"B");
    CHECK(cell(f,0,0).owner == nt_node_owner(node));
    nt_tree_destroy(tree);
    nt_text_layout_destroy(text);
    puts("mounted update supplies effect clock and paint derives its semantic owner");
}

int main(void)
{
    bidi_and_settle();
    substitutions();
    trails_and_atomic_errors();
    motion_sampling();
    reverse_travel_endpoint();
    allocation_refusal();
    component_effect();
    return 0;
}
