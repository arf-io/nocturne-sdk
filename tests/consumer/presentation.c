#include <nocturne/canvas.h>
#include <nocturne/present.h>
#include <nocturne/text_paint.h>

#include <stdio.h>
#include <string.h>

static NtResult write_bytes(void *context, NtByteView bytes)
{
    return fwrite(bytes.data, 1, bytes.count, context) == bytes.count ?
           NT_RESULT_OK : NT_RESULT_TRANSPORT_FAILED;
}

static NtResult make_text(NtFrame **out)
{
    static const char source[] = "\xe7\x95\x8c" " caf\xc3\xa9 " "\xd7\x90\xd7\x91" "\n"
                                "Clipped text";
    NtTextLayout *layout = NULL;
    NtFrame *text = NULL;
    const NtTextLayoutOptions options = {
        .width = {NT_WIDTH_ONE, NT_WIDTH_TWO}, .base_direction = NT_DIRECTION_LTR, .tab_stop = 4
    };
    NtResult result = nt_text_layout_create_borrowed(NULL, (NtByteView){source, sizeof(source)-1},
                                                       &options, &layout);
    if (result != NT_RESULT_OK) goto done;
    result = nt_frame_create(NULL, nt_text_layout_size(layout), &text);
    if (result != NT_RESULT_OK) goto done;
    NtSurfaceView root_text;
    result = nt_surface_root(text, &root_text);
    if (result != NT_RESULT_OK) goto done;
    const NtTextPaintSpan spans[] = {
        {{0,3}, {.color={{255,178,88,255},{49,34,55,255}}, .owner=20}},
        {{4,9}, {.color={{118,220,180,255},{0}}, .owner=21}}
    };
    const NtTextPaintOptions styles = {
        .base={.color={{237,240,250,255},{0}},.owner=22}, .spans=spans, .count=2
    };
    result = nt_text_paint(&root_text, (NtCellPoint){0,0}, layout, &styles);
    if (result != NT_RESULT_OK) goto done;

done:
    nt_text_layout_destroy(layout);
    if (result == NT_RESULT_OK) *out = text;
    else nt_frame_destroy(text);
    return result;
}

static NtResult make_diagram(NtFrame **out)
{
    NtCanvas *canvas;
    NtResult result = nt_canvas_create(NULL, (NtCellSize){20,4}, &canvas);
    if (result != NT_RESULT_OK) return result;
    const NtCanvasInk ink = {{130,170,255,255},40,NULL};
    result = nt_canvas_line(canvas,(NtCellPoint){0,3},(NtCellPoint){19,0},ink);
    if (result == NT_RESULT_OK) result = nt_canvas_render(canvas, NT_SAMPLE_BRAILLE, NULL, out);
    nt_canvas_destroy(canvas);
    return result;
}

static NtResult paint_title(NtFrame *frame, const NtFrame *text, const NtFrame *diagram, int32_t offset)
{
    NtResult result;
    NtSurfaceView root, viewport;
    result = nt_surface_root(frame, &root);
    if (result != NT_RESULT_OK) return result;
    const NtCellRect bounds = {{2,1},{12,2}};
    result = nt_surface_child(&root, bounds, NULL, &viewport);
    if (result != NT_RESULT_OK) return result;
    result = nt_surface_fill(&viewport, (NtCellRect){{0,0},bounds.size},
                              (NtRgba){27,32,51,255}, 30);
    if (result != NT_RESULT_OK) return result;
    const NtSurfaceLayer text_layer = {
        .source=text, .region={{0,0},nt_frame_size(text)}, .position={offset,0},
        .opacity=1, .owners=NT_SURFACE_OWNERS_SHARED
    };
    result = nt_surface_compose(&viewport, &text_layer, 1);
    if (result != NT_RESULT_OK) return result;

    const NtCellStyle marker = {{140,162,200,255},{0}};
    result = nt_surface_glyph(&root,(NtCellPoint){1,1},(NtByteView){"|",1},1,marker,31);
    if (result != NT_RESULT_OK) return result;
    result = nt_surface_glyph(&root,(NtCellPoint){14,1},(NtByteView){"|",1},1,marker,31);
    if (result != NT_RESULT_OK) return result;

    const NtSurfaceLayer ruler = {
        .source=diagram,.region={{0,0},nt_frame_size(diagram)},.position={2,3},
        .opacity=1,.owners=NT_SURFACE_OWNERS_SHARED
    };
    return nt_surface_compose(&root,&ruler,1);
}

static NtResult show(const NtFrameSink *sink, const NtFrame *text, const NtFrame *diagram, bool shifted)
{
    NtFrame *frame;
    NtResult result = nt_frame_create(NULL,(NtCellSize){18,4},&frame);
    if (result != NT_RESULT_OK) return result;
    result = paint_title(frame,text,diagram,shifted ? -1 : 0);
    if (result == NT_RESULT_OK) result = nt_frame_present(sink,frame,shifted ? 0 : 100000);
    nt_frame_destroy(frame);
    if (result == NT_RESULT_OK && fflush(stdout) != 0) return NT_RESULT_TRANSPORT_FAILED;
    return result;
}

int main(int argc, char **argv)
{
    if (argc != 3 || (strcmp(argv[1],"0") && strcmp(argv[1],"1") && strcmp(argv[1],"move")) ||
        (strcmp(argv[2],"plain") && strcmp(argv[2],"ansi"))) return 2;
    const bool ansi = !strcmp(argv[2],"ansi"), move = !strcmp(argv[1],"move");
    const bool shifted = move || !strcmp(argv[1],"0");
    NtFrame *text = NULL, *diagram = NULL;
    NtResult result = make_text(&text);
    if (result == NT_RESULT_OK) result = make_diagram(&diagram);
    NtAnsiPresenter presenter = {
        .capabilities = {.color=NT_COLOR_TRUECOLOR,.interactive=ansi,.utf8=true,
                         .half_blocks=true,.braille=true,.width={NT_WIDTH_ONE,NT_WIDTH_TWO}},
        .matte={13,17,26,255}, .write=write_bytes,.write_context=stdout
    };
    const NtFrameSink sink = {&presenter, nt_ansi_present};
    if (result == NT_RESULT_OK) result = show(&sink,text,diagram,shifted);
    if (result == NT_RESULT_OK && move) {
        if (getchar() == EOF) result = NT_RESULT_TRANSPORT_FAILED;
        else result = show(&sink,text,diagram,false);
    }
    nt_frame_destroy(diagram);
    nt_frame_destroy(text);
    if (result == NT_RESULT_OK) return 0;
    fprintf(stderr,"presentation failed (%d)\n", result);
    return 4;
}
