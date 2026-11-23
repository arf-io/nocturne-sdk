#include <nocturne/content.h>
#include <nocturne/present.h>
#include <nocturne/text_paint.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "line %d: %s\n", __LINE__, #e); exit(1); } } while (0)
#define POINT(x,y) ((NtCellPoint){x,y})
#define RECT(x,y,w,h) ((NtCellRect){{x,y},{w,h}})
#define BYTES(s) ((NtByteView){s,sizeof(s)-1})

static const NtCellStyle ink = {{255,255,255,255},{0}};

static NtFrame *make_frame(uint32_t columns, uint32_t rows)
{
    NtFrame *frame;
    CHECK(nt_frame_create(NULL,(NtCellSize){columns,rows},&frame) == NT_RESULT_OK);
    return frame;
}

static NtCellView cell(const NtFrame *frame, int32_t x, int32_t y)
{
    NtCellView result;
    CHECK(nt_frame_cell(frame,POINT(x,y),&result) == NT_RESULT_OK);
    return result;
}

static bool glyph(NtCellView cell, NtByteView expected)
{
    return cell.glyph.count == expected.count && (!expected.count ||
        memcmp(cell.glyph.data, expected.data, expected.count) == 0);
}

static void styled_text(void)
{
    static const char text[] = "e\xcc\x81\t\xe7\x95\x8c " "\xd7\x90\xd7\x91";
    const NtTextLayoutOptions layout_options = {
        .width={NT_WIDTH_ONE,NT_WIDTH_TWO}, .tab_stop=4, .base_direction=NT_DIRECTION_LTR
    };
    NtTextLayout *layout;
    CHECK(nt_text_layout_create_borrowed(NULL,BYTES(text),&layout_options,&layout) == NT_RESULT_OK);
    CHECK(nt_text_layout_source(layout).count == sizeof(text)-1);
    NtFrame *frame = make_frame(12,2);
    NtSurfaceView root;
    CHECK(nt_surface_root(frame,&root) == NT_RESULT_OK);

    const NtPaint tint = {.kind=NT_PAINT_SOLID,.content.solid={NT_COLOR_SRGB,{1,0,0},1},.opacity=1};
    NtTextPaintSpan spans[] = {
        {{0,3},{.color=ink,.owner=18}},
        {{0,1},{.color=ink,.owner=19,.foreground=&tint}},
        {{1,3},{.color=ink,.owner=20}},
        {{3,4},{.color={{255,255,255,255},{30,45,55,255}},.owner=21}},
        {{4,7},{.color={{0,255,0,255},{60,40,20,255}},.owner=22}},
        {{8,10},{.color={{0,0,255,255},{0}},.owner=23}}
    };
    const NtTextPaintOptions styles = {.base={.color=ink,.owner=7}, .spans=spans,
                                       .count=sizeof spans/sizeof spans[0]};
    CHECK(nt_text_paint(&root,POINT(0,0),layout,&styles) == NT_RESULT_OK);
    CHECK(glyph(cell(frame,0,0),BYTES("e\xcc\x81")) && cell(frame,0,0).owner == 19 &&
          cell(frame,0,0).style.foreground.red == 255 && cell(frame,0,0).style.foreground.green == 0);
    for (int x=1;x<4;++x) CHECK(glyph(cell(frame,x,0),BYTES(" ")) && cell(frame,x,0).owner == 21);
    CHECK(glyph(cell(frame,4,0),BYTES("\xe7\x95\x8c")) && cell(frame,5,0).lead.x == 4 &&
          cell(frame,5,0).owner == 22);
    CHECK(glyph(cell(frame,7,0),BYTES("\xd7\x91")) && cell(frame,7,0).owner == 7);
    CHECK(glyph(cell(frame,8,0),BYTES("\xd7\x90")) && cell(frame,8,0).owner == 23);

    NtSurfaceView clip;
    CHECK(nt_surface_child(&root,RECT(5,1,1,1),NULL,&clip) == NT_RESULT_OK);
    CHECK(nt_text_paint(&clip,POINT(-5,0),layout,&styles) == NT_RESULT_OK);
    CHECK(!cell(frame,5,1).glyph_columns && cell(frame,5,1).style.background.red == 60 &&
          cell(frame,5,1).owner == 22 && !cell(frame,4,1).style.background.alpha);

    const char *borrow = cell(frame,0,0).glyph.data;
    const NtPaint invalid = {.kind=NT_PAINT_SOLID,.opacity=2};
    spans[4].style.foreground = &invalid;
    CHECK(nt_text_paint(&root,POINT(1,0),layout,&styles) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(cell(frame,0,0).glyph.data == borrow && glyph(cell(frame,1,0),BYTES(" ")));
    spans[4].source.end = 100;
    CHECK(nt_text_paint(&root,POINT(0,0),layout,&styles) == NT_RESULT_OUT_OF_RANGE);
    CHECK(cell(frame,0,0).glyph.data == borrow);
    nt_frame_destroy(frame);
    nt_text_layout_destroy(layout);
}

static void cells_and_images(void)
{
    char original[] = "\xe7\x95\x8c";
    const NtContentCell cells[] = {
        {POINT(0,0),BYTES(original),2,{{255,0,0,255},{0}},31},
        {POINT(1,0),{0},0,{{0},{0,255,0,255}},32},
        {POINT(2,0),BYTES("Z"),1,{{0,0,255,255},{0}},33}
    };
    NtFrame *frame;
    CHECK(nt_frame_from_cells(NULL,(NtCellSize){3,1},cells,3,&frame) == NT_RESULT_OK);
    original[0] = 'X';
    CHECK(glyph(cell(frame,1,0),BYTES("\xe7\x95\x8c")) && cell(frame,1,0).owner == 31 &&
          cell(frame,1,0).style.background.green == 255);
    const NtContentCell bad = {POINT(0,0),BYTES("x"),0,{{0},{0}},0};
    CHECK(nt_frame_from_cells(NULL,(NtCellSize){1,1},&bad,1,&frame) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(nt_frame_size(frame).columns == 3);
    nt_frame_destroy(frame);

    const NtRgba pixels[] = {{255,0,0,255},{0,0,255,0},{0,0,0,0},
                             {2,2,2,255},{9,9,9,255}};
    NtImageView image = {pixels,{2,2},3,5};
    NtImageSampling sampling = {NT_IMAGE_BILINEAR,NT_PAINT_PAD,NT_PAINT_PAD};
    NtRgba color;
    CHECK(nt_image_sample(&image,sampling,.5,.25,&color) == NT_RESULT_OK &&
          color.red == 255 && color.blue == 0 && color.alpha == 128);
    CHECK(nt_image_sample(&image,sampling,.5,.75,&color) == NT_RESULT_OK && color.red == 6);
    sampling.horizontal = NT_PAINT_REPEAT;
    CHECK(nt_image_sample(&image,sampling,0,.75,&color) == NT_RESULT_OK && color.red == 6);
    sampling.horizontal = NT_PAINT_REFLECT;
    CHECK(nt_image_sample(&image,sampling,-.25,.75,&color) == NT_RESULT_OK && color.red == 2);
    sampling.filter = NT_IMAGE_NEAREST;
    CHECK(nt_image_sample(&image,sampling,nextafter(.5,0),.75,&color) == NT_RESULT_OK && color.red == 2);
    CHECK(nt_image_sample(&image,sampling,.5,.75,&color) == NT_RESULT_OK && color.red == 9);
    image.count = 4;
    CHECK(nt_image_sample(&image,sampling,.5,.75,&color) == NT_RESULT_INSUFFICIENT_BUFFER &&
          color.red == 9);
    image.count = 5;
    CHECK(nt_image_rasterize(&image,sampling,(NtCellSize){3,5},NT_SAMPLE_BRAILLE,42,NULL,&frame) ==
          NT_RESULT_OK);
    CHECK(nt_frame_size(frame).columns == 2 && nt_frame_size(frame).rows == 2);
    CHECK(cell(frame,1,1).owner == 42 && glyph(cell(frame,1,1),BYTES("\xe2\xa0\x81")));
    nt_frame_destroy(frame);
}

static void texture_edge_midpoints(void)
{
    const NtRgba pixels[] = {{0,0,0,8},{0,0,0,3},{0,0,0,255},{1,1,1,255}};
    const NtRgba alpha_plane[] = {{0,0,0,8},{0,0,0,3},{0,0,0,2},{0,0,0,9}};
    const NtRgba color_plane[] = {{0,0,0,255},{2,2,2,255},{5,5,5,255},{3,3,3,255}};
    const NtImageView alpha_image = {alpha_plane,{2,2},2,4};
    const NtImageView color_image = {color_plane,{2,2},2,4};
    const NtImageSampling sampling = {NT_IMAGE_BILINEAR,NT_PAINT_PAD,NT_PAINT_PAD};
    for (unsigned i = 0; i <= 100; ++i) {
        const double perpendicular = i / 100.0;
        NtImageView row = {pixels,{2,1},2,2};
        NtImageView column = {pixels,{1,2},1,2};
        NtRgba color;
        CHECK(nt_image_sample(&row,sampling,.5,perpendicular,&color) == NT_RESULT_OK &&
              color.alpha == 6);
        CHECK(nt_image_sample(&column,sampling,perpendicular,.5,&color) == NT_RESULT_OK &&
              color.alpha == 6);
        row.pixels = column.pixels = pixels + 2;
        CHECK(nt_image_sample(&row,sampling,.5,perpendicular,&color) == NT_RESULT_OK &&
              color.red == 1 && color.green == 1 && color.blue == 1);
        CHECK(nt_image_sample(&column,sampling,perpendicular,.5,&color) == NT_RESULT_OK &&
              color.red == 1 && color.green == 1 && color.blue == 1);
        CHECK(nt_image_sample(&alpha_image,sampling,.5,perpendicular,&color) == NT_RESULT_OK &&
              color.alpha == 6);
        CHECK(nt_image_sample(&color_image,sampling,perpendicular,.5,&color) == NT_RESULT_OK &&
              color.red == 3 && color.green == 3 && color.blue == 3);
    }
}

typedef struct Output {
    char bytes[1024];
    size_t count, calls, stop;
} Output;

static NtResult collect(void *context, NtByteView bytes)
{
    Output *output = context;
    if (++output->calls == output->stop) return NT_RESULT_TRANSPORT_FAILED;
    if (bytes.count > sizeof(output->bytes)-1-output->count) return NT_RESULT_INSUFFICIENT_BUFFER;
    memcpy(output->bytes+output->count,bytes.data,bytes.count);
    output->count += bytes.count;
    output->bytes[output->count] = 0;
    return NT_RESULT_OK;
}

static void sinks(void)
{
    NtFrame *frame = make_frame(5,2);
    CHECK(nt_frame_set_glyph(frame,POINT(0,0),BYTES("\xe7\x95\x8c"),2,ink,1) == NT_RESULT_OK);
    CHECK(nt_frame_set_glyph(frame,POINT(2,0),BYTES("\xe2\x96\x80"),1,ink,2) == NT_RESULT_OK);
    CHECK(nt_frame_set_glyph(frame,POINT(3,0),BYTES("\xe2\xa0\x81"),1,ink,3) == NT_RESULT_OK);
    CHECK(nt_frame_set_glyph(frame,POINT(4,0),BYTES("X"),1,ink,4) == NT_RESULT_OK);
    CHECK(nt_frame_set_glyph(frame,POINT(0,1),BYTES("\x1b"),1,ink,9) ==
          NT_RESULT_INVALID_ARGUMENT);
    Output output = {0};
    NtAnsiPresenter presenter = {
        .capabilities={.color=NT_COLOR_TRUECOLOR,.utf8=true,.width={NT_WIDTH_ONE,NT_WIDTH_TWO}},
        .matte={8,16,24,255},.write=collect,.write_context=&output
    };
    const NtFrameSink sink = {&presenter,nt_ansi_present};
    CHECK(nt_frame_present(&sink,frame,9) == NT_RESULT_OK);
    CHECK(!strcmp(output.bytes,"\xe7\x95\x8c" "??X\n     \n"));
    presenter.capabilities.utf8 = false;
    output = (Output){0};
    CHECK(nt_frame_present(&sink,frame,10) == NT_RESULT_OK && !strcmp(output.bytes,"????X\n     \n"));
    presenter.capabilities.interactive = true;
    presenter.capabilities.utf8 = presenter.capabilities.half_blocks = presenter.capabilities.braille = true;
    output = (Output){0};
    CHECK(nt_frame_present(&sink,frame,11) == NT_RESULT_OK &&
          strstr(output.bytes,"\x1b[1;1H") && strstr(output.bytes,"\x1b[2;1H") &&
          strstr(output.bytes,"\x1b[38;2;255;255;255;48;2;8;16;24m") &&
          strstr(output.bytes,"\xe7\x95\x8c\xe2\x96\x80\xe2\xa0\x81X"));
    const NtRgba palette[] = {{8,16,24,255},{255,255,255,255}};
    presenter.capabilities.color = NT_COLOR_INDEXED;
    presenter.palette = (NtPaletteView){palette,2};
    output = (Output){0};
    CHECK(nt_frame_present(&sink,frame,12) == NT_RESULT_OK &&
          strstr(output.bytes,"\x1b[38;5;1;48;5;0m"));
    presenter.capabilities.color = NT_COLOR_MONO;
    output = (Output){.stop=3};
    CHECK(nt_frame_present(&sink,frame,13) == NT_RESULT_TRANSPORT_FAILED && output.calls == 3);
    CHECK(nt_frame_present(NULL,frame,0) == NT_RESULT_INVALID_ARGUMENT);
    nt_frame_destroy(frame);
}

static void sink_grapheme_boundaries(void)
{
    const NtContentCell cells[] = {
        {POINT(0,0),BYTES("\xf0\x9f\x87\xba"),2,ink,1},
        {POINT(2,0),BYTES("\xf0\x9f\x87\xb8"),2,ink,1},
        {POINT(0,1),BYTES("\xf0\x9f\x91\xa9\xe2\x80\x8d"),2,ink,1},
        {POINT(2,1),BYTES("\xf0\x9f\x92\xbb"),2,ink,1},
        {POINT(0,2),BYTES("\xe1\x84\x80"),2,ink,1},
        {POINT(2,2),BYTES("\xe1\x85\xa1"),1,ink,1},
        {POINT(3,2),BYTES("D"),1,ink,1},
        {POINT(0,3),BYTES("\xf0\x9f\x87\xba\xf0\x9f\x87\xb8"),2,ink,1},
        {POINT(2,3),BYTES("X"),1,ink,1},
        {POINT(3,3),BYTES("\xe1\x86\xa8"),1,ink,1},
        {POINT(0,4),BYTES("A"),1,ink,1},
        {POINT(1,4),BYTES("\xe1\x85\x9f\xe1\x85\xa1"),1,ink,1},
        {POINT(2,4),BYTES("B"),1,ink,1}
    };
    NtFrame *frame;
    CHECK(nt_frame_from_cells(NULL,(NtCellSize){4,5},cells,sizeof(cells)/sizeof(*cells),&frame) ==
          NT_RESULT_OK);
    Output output = {0};
    NtAnsiPresenter presenter = {
        .capabilities={.utf8=true,.width={NT_WIDTH_ONE,NT_WIDTH_TWO}},
        .write=collect,.write_context=&output
    };
    const NtFrameSink sink = { &presenter, nt_ansi_present };
    CHECK(nt_frame_present(&sink,frame,0) == NT_RESULT_OK);
    const char expected[] = "??" "??\n" "\xf0\x9f\x91\xa9\xe2\x80\x8d" "??\n"
                            "\xe1\x84\x80" "?D\n" "\xf0\x9f\x87\xba\xf0\x9f\x87\xb8" "X?\n"
                            "A?B \n";
    CHECK(!strcmp(output.bytes,expected));
    nt_frame_destroy(frame);
}

static void indexed_reference_palette(void)
{
    NtRgba colors[NT_XTERM256_COLORS + 1] = {{1,2,3,4}};
    CHECK(nt_xterm256_palette(NULL, NT_XTERM256_COLORS) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(nt_xterm256_palette(colors, NT_XTERM256_COLORS - 1) == NT_RESULT_INSUFFICIENT_BUFFER);
    CHECK(colors[0].alpha == 4 && !colors[NT_XTERM256_COLORS - 1].alpha);
    CHECK(nt_xterm256_palette(colors, NT_XTERM256_COLORS + 1) == NT_RESULT_OK);
    CHECK(colors[74].red == 95 && colors[74].green == 175 && colors[74].blue == 215);
    CHECK(colors[232].red == 8 && colors[255].blue == 238 && !colors[256].alpha);
    for (size_t i = 0; i < NT_XTERM256_COLORS; ++i) CHECK(colors[i].alpha == 255);
}

int main(void)
{
    styled_text();
    cells_and_images();
    texture_edge_midpoints();
    sinks();
    sink_grapheme_boundaries();
    indexed_reference_palette();
    puts("Styled Unicode, borrowed content, textures, plain and ANSI sinks passed.");
    return EXIT_SUCCESS;
}
