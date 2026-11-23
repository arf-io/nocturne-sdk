#include <nocturne/canvas.h>
#include <nocturne/surface.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "line %d: %s\n", __LINE__, #e); exit(1); } } while (0)
#define POINT(x,y) ((NtCellPoint){x,y})
#define RECT(x,y,w,h) ((NtCellRect){{x,y},{w,h}})

static const NtRgba white = {255,255,255,255};
static const NtRgba black = {0,0,0,255};
static const NtCanvasInk ink = {{255,255,255,255},22,NULL};

static NtCanvas *canvas_new(uint32_t columns, uint32_t rows)
{
    NtCanvas *canvas;
    CHECK(nt_canvas_create(NULL,(NtCellSize){columns,rows},&canvas) == NT_RESULT_OK);
    return canvas;
}

static NtCanvasPixel pixel(NtCanvas *canvas,int x,int y)
{
    NtCanvasPixel result;
    CHECK(nt_canvas_pixel(canvas,POINT(x,y),&result) == NT_RESULT_OK);
    return result;
}

static unsigned glyph(NtSampledCell cell)
{
    const unsigned char *b = (const unsigned char *)cell.glyph;
    return cell.glyph_size == 1 ? b[0] : ((b[0]&15u)<<12) | ((b[1]&63u)<<6) | (b[2]&63u);
}

static void reducers(void)
{
    const char *names[] = {"cell","half-block","braille","hybrid"};
    NtSamplingMode mode = NT_SAMPLE_CELL;
    for (unsigned i = 0; i < 4; ++i)
        CHECK(nt_sampling_mode_parse(names[i],&mode) == NT_RESULT_OK && mode == (NtSamplingMode)i);
    CHECK(nt_sampling_mode_parse("halves",&mode) == NT_RESULT_UNSUPPORTED_CAPABILITY &&
          mode == NT_SAMPLE_HYBRID);

    const unsigned dots[] = {1,8,2,16,4,32,64,128};
    NtCellSamples samples = {0};
    for (unsigned i = 0; i < 8; ++i) {
        samples.pixels[i].color = white;
        samples.pixels[i].owner = 34+i;
        samples.background[i] = black;
    }
    NtSampledCell out;
    for (unsigned mask = 1; mask <= 255; ++mask) {
        unsigned scalar = 0x2800;
        for (unsigned i = 0; i < 8; ++i) {
            samples.pixels[i].covered = (mask & (1u<<i)) != 0;
            if (samples.pixels[i].covered) scalar |= dots[i];
        }
        CHECK(nt_cell_sample(&samples,NT_SAMPLE_BRAILLE,&out) == NT_RESULT_OK && glyph(out) == scalar);
        CHECK(out.style.foreground.red == 255 && out.style.background.red == 0);
        CHECK(nt_cell_sample(&samples,NT_SAMPLE_HYBRID,&out) == NT_RESULT_OK && glyph(out) == scalar);
    }
    CHECK(out.owner == 34 && glyph(out) == 0x28ff);
    for (unsigned i = 0; i < 8; ++i) {
        samples.pixels[i].covered = false;
        samples.pixels[i].color = i < 4 ? white : black;
    }
    CHECK(nt_cell_sample(&samples,NT_SAMPLE_CELL,&out) == NT_RESULT_OK &&
          glyph(out) == ' ' && out.style.background.red == 188 && out.owner == 0);
    CHECK(nt_cell_sample(&samples,NT_SAMPLE_HALF_BLOCK,&out) == NT_RESULT_OK &&
          glyph(out) == 0x2580 && out.style.foreground.red == 255 && out.style.background.red == 0);
    CHECK(nt_cell_sample(&samples,NT_SAMPLE_BRAILLE,&out) == NT_RESULT_OK && glyph(out) == 0x281b);
    for (unsigned i = 0; i < 8; ++i) samples.pixels[i].color = black;
    CHECK(nt_cell_sample(&samples,NT_SAMPLE_BRAILLE,&out) == NT_RESULT_OK && glyph(out) == ' ');
    CHECK(nt_cell_sample(&samples,NT_SAMPLE_HYBRID,&out) == NT_RESULT_OK && glyph(out) == 0x2580);
    for (unsigned i = 0; i < 8; ++i) samples.pixels[i].color.red = i % 2 ? 9 : 2;
    CHECK(nt_cell_sample(&samples,NT_SAMPLE_CELL,&out) == NT_RESULT_OK && out.style.background.red == 6);
}

static void shapes(void)
{
    NtCanvas *canvas = canvas_new(5,5);
    CHECK(nt_canvas_point(canvas,POINT(-1,0),ink) == NT_RESULT_OK);
    CHECK(nt_canvas_point(canvas,POINT(0,0),ink) == NT_RESULT_OK && pixel(canvas,0,0).owner == 22);
    CHECK(nt_canvas_clear(canvas,(NtRgba){0}) == NT_RESULT_OK);
    CHECK(nt_canvas_line(canvas,POINT(INT32_MIN,INT32_MIN),POINT(INT32_MAX,INT32_MAX),ink) ==
          NT_RESULT_OK);
    for (int y=0;y<5;++y)
        for (int x=0;x<5;++x) CHECK(pixel(canvas,x,y).covered == (x==y));
    CHECK(nt_canvas_clear(canvas,(NtRgba){0}) == NT_RESULT_OK);
    CHECK(nt_canvas_line(canvas,POINT(4,2),POINT(0,0),ink) == NT_RESULT_OK);
    CHECK(pixel(canvas,1,1).covered && pixel(canvas,3,2).covered && !pixel(canvas,1,0).covered);
    CHECK(nt_canvas_clear(canvas,(NtRgba){0}) == NT_RESULT_OK);
    CHECK(nt_canvas_triangle(canvas,POINT(INT32_MIN,INT32_MIN),POINT(INT32_MAX,INT32_MIN),
                             POINT(INT32_MAX,INT32_MAX),ink) == NT_RESULT_OK);
    for (int y=0;y<5;++y)
        for (int x=0;x<5;++x) CHECK(pixel(canvas,x,y).covered == (x>=y));
    CHECK(nt_canvas_clear(canvas,(NtRgba){0}) == NT_RESULT_OK);
    const NtCanvasInk half = {{5,5,5,128},17,NULL};
    CHECK(nt_canvas_triangle(canvas,POINT(0,0),POINT(2,2),POINT(4,4),half) == NT_RESULT_OK);
    for (int i=0;i<5;++i) CHECK(pixel(canvas,i,i).color.alpha == 128);

    CHECK(nt_canvas_clear(canvas,(NtRgba){0}) == NT_RESULT_OK);
    CHECK(nt_canvas_border(canvas,RECT(-1,-1,5,5),half) == NT_RESULT_OK);
    CHECK(!pixel(canvas,0,0).covered && pixel(canvas,3,3).color.alpha == 128);
    CHECK(nt_canvas_border(canvas,RECT(4,0,1,5),half) == NT_RESULT_OK &&
          pixel(canvas,4,4).color.alpha == 128);
    CHECK(nt_canvas_fill(canvas,RECT(-1,-1,3,3),ink) == NT_RESULT_OK);
    CHECK(pixel(canvas,1,1).owner == 22 && !pixel(canvas,2,2).covered);
    const NtCompositeOptions remove = {.operation=NT_COMPOSITE_CLEAR,.space=NT_COLOR_SRGB};
    CHECK(nt_canvas_point(canvas,POINT(1,1),(NtCanvasInk){white,24,&remove}) == NT_RESULT_OK &&
          !pixel(canvas,1,1).covered && pixel(canvas,1,1).owner == 0);
    NtCanvasPixel unchanged = {.owner=99};
    CHECK(nt_canvas_pixel(canvas,POINT(-1,0),&unchanged) == NT_RESULT_OUT_OF_RANGE &&
          unchanged.owner == 99);
    nt_canvas_destroy(canvas);
}

static void render_and_dissolve(void)
{
    NtCanvas *canvas = canvas_new(3,5);
    CHECK(nt_canvas_fill(canvas,RECT(0,0,2,4),ink) == NT_RESULT_OK);
    CHECK(nt_canvas_point(canvas,POINT(2,4),ink) == NT_RESULT_OK);
    NtFrame *frame = NULL;
    CHECK(nt_canvas_render(canvas,NT_SAMPLE_HYBRID,NULL,&frame) == NT_RESULT_OK);
    CHECK(nt_frame_size(frame).columns == 2 && nt_frame_size(frame).rows == 2);
    NtCellView cell;
    CHECK(nt_frame_cell(frame,POINT(0,0),&cell) == NT_RESULT_OK &&
          cell.glyph.count == 3 && !memcmp(cell.glyph.data,"\xe2\xa3\xbf",3) && cell.owner == 22);
    CHECK(nt_frame_cell(frame,POINT(1,1),&cell) == NT_RESULT_OK &&
          cell.glyph.count == 3 && !memcmp(cell.glyph.data,"\xe2\xa0\x81",3) && cell.owner == 22);
    nt_frame_destroy(frame);
    nt_canvas_destroy(canvas);

    canvas = canvas_new(0,200);
    CHECK(nt_canvas_line(canvas,POINT(0,INT32_MIN),POINT(0,INT32_MAX),ink) == NT_RESULT_OK);
    CHECK(nt_canvas_render(canvas,NT_SAMPLE_BRAILLE,NULL,&frame) == NT_RESULT_OK &&
          nt_frame_size(frame).columns == 0 && nt_frame_size(frame).rows == 50);
    nt_frame_destroy(frame);
    nt_canvas_destroy(canvas);

    canvas = canvas_new(8,8);
    NtCanvas *points = canvas_new(8,8);
    const NtCompositeOptions dissolve = {.blend=NT_BLEND_DISSOLVE,.space=NT_COLOR_SRGB,
                                         .seed=8765,.sample_index=12};
    const NtCanvasInk spray = {{40,50,60,175},7,&dissolve};
    CHECK(nt_canvas_fill(canvas,RECT(0,0,8,8),spray) == NT_RESULT_OK);
    unsigned covered = 0;
    for (int y=0;y<8;++y) for (int x=0;x<8;++x) {
        CHECK(nt_canvas_point(points,POINT(x,y),spray) == NT_RESULT_OK);
        const NtCanvasPixel q=pixel(canvas,x,y), p=pixel(points,x,y);
        CHECK(q.covered==p.covered && q.owner==p.owner && q.color.alpha==p.color.alpha);
        covered += q.covered;
    }
    CHECK(covered > 0 && covered < 64);
    nt_canvas_destroy(points);
    nt_canvas_destroy(canvas);
}

typedef struct FailureProbe { unsigned seen, fail, live; } FailureProbe;

static void *allocate(void *context, size_t bytes, size_t alignment)
{
    (void)alignment;
    FailureProbe *probe = context;
    if (++probe->seen == probe->fail) return NULL;
    void *data = malloc(bytes);
    if (data) ++probe->live;
    return data;
}

static void release(void *context, void *data, size_t bytes, size_t alignment)
{
    (void)bytes;
    (void)alignment;
    --((FailureProbe *)context)->live;
    free(data);
}

static void rollback(void)
{
    FailureProbe probe = {0};
    const NtAllocator allocator = {&probe,allocate,release};
    NtCanvas *canvas = NULL;
    for (unsigned fail = 1; fail <= 2; ++fail) {
        probe = (FailureProbe){.fail=fail};
        CHECK(nt_canvas_create(&allocator,(NtCellSize){4,4},&canvas) ==
              NT_RESULT_ALLOCATION_FAILED && canvas == NULL && !probe.live);
    }
    canvas = canvas_new(4,4);
    CHECK(nt_canvas_point(canvas,POINT(1,0),(NtCanvasInk){{255,0,0,255},23,NULL}) ==
          NT_RESULT_OK);
    const NtCompositeOptions reject = {.space=NT_COLOR_OKLCH,.gamut=NT_GAMUT_REJECT,
                                        .hue=NT_HUE_LONGER};
    const NtCanvasInk paint = {{255,0,0,128},99,&reject};
    CHECK(nt_canvas_fill(canvas,RECT(0,0,2,1),paint) == NT_RESULT_OUT_OF_RANGE);
    CHECK(!pixel(canvas,0,0).covered && pixel(canvas,1,0).owner == 23);

    CHECK(nt_canvas_fill(canvas,RECT(0,0,4,4),ink) == NT_RESULT_OK);
    for (unsigned fail = 1; fail <= 4; ++fail) {
        probe = (FailureProbe){.fail=fail};
        NtFrame *frame = NULL;
        CHECK(nt_canvas_render(canvas,NT_SAMPLE_BRAILLE,&allocator,&frame) ==
              NT_RESULT_ALLOCATION_FAILED && frame == NULL && !probe.live);
    }
    nt_canvas_destroy(canvas);
}

int main(void)
{
    reducers();
    shapes();
    render_and_dissolve();
    rollback();
    puts("256 masks, contrast fallback, clipped primitives, semantic owners and rasterization passed.");
    return EXIT_SUCCESS;
}
