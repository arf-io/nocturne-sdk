#include <nocturne/animation.h>
#include <nocturne/frame.h>
#include <nocturne/motion.h>
#include <nocturne/paint_sample.h>
#include <nocturne/surface.h>
#include <nocturne/canvas.h>
#include <nocturne/text_layout.h>
#include <stdio.h>

static int place_cluster(NtByteView input)
{
    NtText *text = NULL;
    NtFrame *frame = NULL;
    int status = 4;
    if (nt_text_create(NULL, input, &text) != NT_RESULT_OK ||
        nt_frame_create(NULL, (NtCellSize){2, 1}, &frame) != NT_RESULT_OK)
        goto cleanup;
    NtTextCluster cluster;
    if (nt_text_cluster(text, 1, &cluster) != NT_RESULT_OK) goto cleanup;
    const NtByteView bytes = nt_text_bytes(text);
    const NtByteView glyph = {bytes.data + cluster.source.begin,
                             cluster.source.end - cluster.source.begin};
    NtGlyphMetrics metrics;
    const NtWidthPolicy policy = {NT_WIDTH_ONE, NT_WIDTH_TWO};
    const NtCellStyle style = {{250, 250, 250, 255}, {0, 0, 0, 255}};
    if (nt_glyph_measure(glyph, policy, &metrics) != NT_RESULT_OK ||
        metrics.presentation != NT_GLYPH_EMOJI || metrics.columns != 2 ||
        nt_frame_set_glyph(frame, (NtCellPoint){0, 0}, glyph, metrics.columns, style, 11) !=
            NT_RESULT_OK)
        goto cleanup;

    nt_text_destroy(text);
    text = NULL;
    NtCellView cell;
    if (nt_frame_cell(frame, (NtCellPoint){1, 0}, &cell) == NT_RESULT_OK &&
        cell.owner == 11 && cell.lead.x == 0 && cell.glyph_columns == 2 &&
        cell.glyph.count == 11 && (unsigned char)cell.glyph.data[0] == 0xf0)
        status = 0;
cleanup:
    nt_frame_destroy(frame);
    nt_text_destroy(text);
    return status;
}

static bool analyze_text(void)
{
    static const char mixed[] = "A \xd7\x90\xd7\x91";
    const NtByteView source = {mixed, sizeof(mixed) - 1};
    NtLineBreak breaks[2];
    NtScriptRun runs[2];
    size_t count;
    return nt_line_breaks(NULL, source, breaks, 2, &count) == NT_RESULT_OK &&
        count == 2 && breaks[0].offset == 2 && !breaks[0].mandatory &&
        breaks[1].offset == source.count && breaks[1].mandatory &&
        nt_script_runs(NULL, source, runs, 2, &count) == NT_RESULT_OK &&
        count == 2 && runs[0].source.end == 2 && runs[1].source.end == source.count &&
        runs[1].script[0] == 'H' && runs[1].script[3] == 'r';
}

static bool layout_unicode(void)
{
    const NtByteView mixed = {"e\xcc\x81\xe4\xb8\xad\n\xd7\x90\xd7\x91", 11};
    const NtTextLayoutOptions options = {
        .width = {NT_WIDTH_ONE, NT_WIDTH_TWO}, .wrap = NT_WRAP_WORD,
        .align = NT_TEXT_ALIGN_START, .base_direction = NT_DIRECTION_AUTO, .tab_stop = 4
    };
    NtTextLayout *layout;
    if (nt_text_layout_create_borrowed(NULL, mixed, &options, &layout) != NT_RESULT_OK)
        return false;
    const NtCellSize size = nt_text_layout_size(layout);
    NtClusterView glyph;
    NtCaret caret;
    NtCellPoint edge;
    const bool ok = size.columns == 3 && size.rows == 2 &&
        nt_text_layout_cluster_count(layout) == 4 &&
        nt_text_layout_cluster(layout, 1, &glyph) == NT_RESULT_OK &&
        glyph.source.begin == 3 && glyph.source.end == 6 && glyph.columns == 2 &&
        nt_text_layout_cluster(layout, 2, &glyph) == NT_RESULT_OK &&
        glyph.source.begin == 9 && glyph.position.x == 1 && glyph.position.y == 1 &&
        nt_text_layout_hit(layout, (NtCellPoint){2, 0}, &caret) == NT_RESULT_OK &&
        caret.offset == 6 && caret.affinity == NT_CARET_TRAILING &&
        nt_text_layout_caret_position(layout, caret, &edge) == NT_RESULT_OK &&
        edge.x == 3 && edge.y == 0;
    nt_text_layout_destroy(layout);
    return ok;
}

static bool sample_gradient(void)
{
    const NtGradientStop stops[] = {
        {0, nt_color_from_rgba((NtRgba){0, 0, 0, 255}), NT_LINEAR},
        {1, nt_color_from_rgba((NtRgba){255, 255, 255, 255}), NT_LINEAR}
    };
    const NtPaint paint = {
        .kind = NT_PAINT_GRADIENT,
        .field = {.pattern = NT_PAINT_LINEAR, .scale_x = 10, .scale_y = 1},
        .content.gradient = {stops, 2, NT_PAINT_PAD,
                             {NT_COLOR_LINEAR_SRGB, NT_GAMUT_REJECT,
                              NT_HUE_SHORTER, NT_ALPHA_PREMULTIPLIED}},
        .opacity = .5
    };
    NtRgba rgba;
    return nt_paint_sample_rgba(&paint, (NtPaintSample){5, 0, 0, NT_MOTION_FULL},
                                NT_GAMUT_REJECT, &rgba) == NT_RESULT_OK &&
        rgba.red == 188 && rgba.green == 188 && rgba.blue == 188 && rgba.alpha == 128;
}

static bool place_offscreen_text(void)
{
    NtFrame *frame = NULL, *source = NULL;
    NtSurfaceView root, viewport;
    bool ok = false;
    const NtByteView glyph = {"\xe7\x95\x8c", 3};
    const NtCellStyle style = {{240, 250, 250, 255}, {0, 0, 255, 255}};
    if (nt_frame_create(NULL, (NtCellSize){4, 1}, &frame) != NT_RESULT_OK ||
        nt_frame_create(NULL, (NtCellSize){2, 1}, &source) != NT_RESULT_OK ||
        nt_frame_set_glyph(source, (NtCellPoint){0, 0}, glyph, 2, style, 7) != NT_RESULT_OK ||
        nt_surface_root(frame, &root) != NT_RESULT_OK ||
        nt_surface_child(&root, (NtCellRect){{1, 0}, {2, 1}}, NULL, &viewport) != NT_RESULT_OK)
        goto cleanup;

    NtSurfaceLayer layer = {.source = source, .region = {{0, 0}, {2, 1}}, .position = {-1, 0},
                            .opacity = 1, .owners = NT_SURFACE_OWNERS_SHARED};
    NtCellView visible;
    if (nt_surface_compose(&viewport, &layer, 1) != NT_RESULT_OK ||
        nt_frame_cell(frame, (NtCellPoint){1, 0}, &visible) != NT_RESULT_OK ||
        visible.glyph_columns != 0 || visible.style.background.blue != 255 || visible.owner != 7)
        goto cleanup;

    layer.position.x = 0;
    ok = nt_surface_compose(&viewport, &layer, 1) == NT_RESULT_OK &&
        nt_frame_cell(frame, (NtCellPoint){2, 0}, &visible) == NT_RESULT_OK &&
        visible.glyph_columns == 2 && visible.glyph.count == 3 &&
        visible.lead.x == 1 && visible.owner == 7;
cleanup:
    nt_frame_destroy(source);
    nt_frame_destroy(frame);
    return ok;
}

static bool draw_subcells(void)
{
    NtCanvas *canvas = NULL;
    NtFrame *frame = NULL;
    const NtCanvasInk ink = {{130, 170, 255, 255}, 42, NULL};
    NtCellView cell;
    const bool ok = nt_canvas_create(NULL, (NtCellSize){2, 4}, &canvas) == NT_RESULT_OK &&
        nt_canvas_line(canvas, (NtCellPoint){0, 0}, (NtCellPoint){1, 3}, ink) == NT_RESULT_OK &&
        nt_canvas_render(canvas, NT_SAMPLE_BRAILLE, NULL, &frame) == NT_RESULT_OK &&
        nt_frame_cell(frame, (NtCellPoint){0, 0}, &cell) == NT_RESULT_OK &&
        cell.glyph.count == 3 && (unsigned char)cell.glyph.data[0] == 0xe2 &&
        (unsigned char)cell.glyph.data[1] == 0xa2 && (unsigned char)cell.glyph.data[2] == 0xa3 &&
        cell.style.foreground.blue == 255 && cell.owner == 42;
    nt_frame_destroy(frame);
    nt_canvas_destroy(canvas);
    return ok;
}

int main(void) {
    const NtPoint points[] = {{0, 0}, {30, 0}, {30, 10}};
    NtPath path = {points, 3, 0, NT_ONCE};
    NtTween movement = {0, 40, 1000000, 0, NT_LINEAR, NT_ONCE};
    NtAnimationSample distance; NtPathSample position;
    if (nt_tween_sample(&movement, 875000, &distance) != NT_OK ||
        nt_path_sample(&path, distance.value, &position) != NT_OK) return 1;
    if (position.position.x != 30 || position.position.y != 5) return 2;

    static const char mixed[] = "e\xcc\x81" "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x9a\x80";
    const NtByteView text = { mixed, sizeof(mixed) - 1 };
    NtGraphemeIterator iterator;
    NtByteSpan cluster;
    bool available;
    if (nt_grapheme_iterator_init(&iterator, text) != NT_RESULT_OK ||
        nt_grapheme_iterator_next(&iterator, &cluster, &available) != NT_RESULT_OK ||
        !available || cluster.begin != 0 || cluster.end != 3 ||
        nt_grapheme_iterator_next(&iterator, &cluster, &available) != NT_RESULT_OK ||
        !available || cluster.begin != 3 || cluster.end != sizeof(mixed) - 1 ||
        nt_grapheme_iterator_next(&iterator, &cluster, &available) != NT_RESULT_OK ||
        available) return 3;
    if (place_cluster(text)) return 4;
    if (!analyze_text()) return 5;
    if (!layout_unicode()) return 6;
    if (!sample_gradient()) return 7;
    if (!place_offscreen_text()) return 8;
    if (!draw_subcells()) return 9;
    puts("Installed C consumer: Unicode layout, paint, clipped text and subcell drawing");
    return 0;
}
