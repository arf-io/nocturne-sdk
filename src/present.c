#include <nocturne/present.h>

#include <nocturne/blend.h>
#include <nocturne/text.h>

#include "grapheme_internal.h"
#include "present_internal.h"

#include <inttypes.h>
#include <stdio.h>

NtResult nt_frame_present(const NtFrameSink *sink, const NtFrame *frame, NtTimeUs now)
{
    if (!sink || !sink->present || !frame) return NT_RESULT_INVALID_ARGUMENT;
    return sink->present(sink->context, frame, now);
}

#define LITERAL(text) ((NtByteView){text, sizeof(text) - 1})

NtResult nt_xterm256_palette(NtRgba *colors, size_t count)
{
    if (!colors) return NT_RESULT_INVALID_ARGUMENT;
    if (count < NT_XTERM256_COLORS) return NT_RESULT_INSUFFICIENT_BUFFER;
    static const NtRgba basic[] = {
        {0,0,0,255}, {205,0,0,255}, {0,205,0,255}, {205,205,0,255},
        {0,0,238,255}, {205,0,205,255}, {0,205,205,255}, {229,229,229,255},
        {127,127,127,255}, {255,0,0,255}, {0,255,0,255}, {255,255,0,255},
        {92,92,255,255}, {255,0,255,255}, {0,255,255,255}, {255,255,255,255},
    };
    for (size_t i = 0; i < sizeof(basic) / sizeof(*basic); ++i) colors[i] = basic[i];
    static const uint8_t levels[] = {0, 95, 135, 175, 215, 255};
    for (size_t i = 0; i < 216; ++i)
        colors[16 + i] = (NtRgba){levels[i / 36], levels[i / 6 % 6], levels[i % 6], 255};
    for (size_t i = 232; i < NT_XTERM256_COLORS; ++i) {
        const uint8_t value = (uint8_t)(8 + 10 * (i - 232));
        colors[i] = (NtRgba){value, value, value, 255};
    }
    return NT_RESULT_OK;
}

static NtResult out(const NtAnsiPresenter *presenter, NtByteView bytes)
{
    return presenter->write(presenter->write_context, bytes);
}

static bool glyph_supported(NtByteView glyph, uint8_t columns, NtTerminalCapabilities caps)
{
    if (!glyph.count) return false;
    unsigned regional_indicators = 0;
    bool hangul_base = false, trailing_jamo = false;
    for (size_t offset = 0; offset < glyph.count;) {
        uint32_t scalar;
        if (nt_utf8_decode(glyph, &offset, &scalar) != NT_RESULT_OK || scalar < 0x20 ||
            (scalar >= 0x7f && scalar <= 0x9f) || (!caps.utf8 && scalar >= 0x80) ||
            (!caps.half_blocks && (scalar == 0x2580 || scalar == 0x2584)) ||
            (!caps.braille && scalar >= 0x2800 && scalar <= 0x28ff))
            return false;
        if (scalar >= 0x1f1e6 && scalar <= 0x1f1ff) ++regional_indicators;
        if ((scalar >= 0x1100 && scalar <= 0x115e) || (scalar >= 0xa960 && scalar <= 0xa97c) ||
            (scalar >= 0xac00 && scalar <= 0xd7a3)) hangul_base = true;
        if ((scalar >= 0x1160 && scalar <= 0x11ff) || (scalar >= 0xd7b0 && scalar <= 0xd7c6) ||
            (scalar >= 0xd7cb && scalar <= 0xd7fb)) trailing_jamo = true;
    }
    /* Terminals can advance only one column for a lone regional indicator,
     * absorb trailing Jamo into the preceding cell, or give those with only
     * a leading filler (U+115F) an extra column. */
    if ((columns == 2 && regional_indicators == 1) || (trailing_jamo && !hangul_base)) return false;
    NtGlyphMetrics metrics;
    return nt_glyph_measure(glyph, caps.width, &metrics) == NT_RESULT_OK &&
           (metrics.presentation == NT_GLYPH_TEXT || metrics.presentation == NT_GLYPH_EMOJI) &&
           metrics.columns == columns &&
           nt_grapheme_separates(LITERAL(" "), glyph) &&
           nt_grapheme_separates(glyph, LITERAL(" "));
}

static NtRgba flatten(NtRgba below, NtRgba above)
{
    NtRgba color = below;
    (void)nt_rgba_composite(below, above, NULL, &color);
    return color;
}

typedef struct AnsiStyle {
    uint32_t foreground, background;
    bool active;
} AnsiStyle;

static uint32_t rgb_code(NtRgba color)
{
    return (uint32_t)color.red << 16 | (uint32_t)color.green << 8 | color.blue;
}

static NtResult emit_style(const NtAnsiPresenter *presenter, NtCellStyle style, AnsiStyle *previous)
{
    if (presenter->capabilities.color == NT_COLOR_MONO) return NT_RESULT_OK;
    NtRgba matte = presenter->matte;
    matte.alpha = 255;
    const NtRgba bg = flatten(matte, style.background);
    const NtRgba fg = flatten(bg, style.foreground);
    uint32_t fg_code = rgb_code(fg), bg_code = rgb_code(bg);
    char sequence[48];
    int count;
    if (presenter->capabilities.color == NT_COLOR_INDEXED) {
        size_t near_fg, near_bg;
        NtResult result = nt_palette_nearest(presenter->palette, nt_color_from_rgba(fg),
                                             NT_GAMUT_CLIP, &near_fg);
        if (result != NT_RESULT_OK) return result;
        result = nt_palette_nearest(presenter->palette, nt_color_from_rgba(bg),
                                    NT_GAMUT_CLIP, &near_bg);
        if (result != NT_RESULT_OK) return result;
        fg_code = (uint32_t)near_fg;
        bg_code = (uint32_t)near_bg;
        count = snprintf(sequence, sizeof(sequence), "\x1b[38;5;%u;48;5;%um",
                         (unsigned)fg_code, (unsigned)bg_code);
    } else {
        count = snprintf(sequence, sizeof(sequence), "\x1b[38;2;%u;%u;%u;48;2;%u;%u;%um",
                         (unsigned)fg.red,(unsigned)fg.green,(unsigned)fg.blue,
                         (unsigned)bg.red,(unsigned)bg.green,(unsigned)bg.blue);
    }
    if (previous->active && previous->foreground == fg_code && previous->background == bg_code)
        return NT_RESULT_OK;
    if (count < 0 || (size_t)count >= sizeof(sequence)) return NT_RESULT_OUT_OF_RANGE;
    const NtResult result = out(presenter, (NtByteView){sequence, (size_t)count});
    if (result == NT_RESULT_OK) *previous = (AnsiStyle){fg_code, bg_code, true};
    return result;
}

static NtResult emit_row_position(const NtAnsiPresenter *presenter, uint32_t row)
{
    char position[24];
    const int count = snprintf(position, sizeof(position), "\x1b[%" PRIu32 ";1H", row + 1);
    if (count < 0 || (size_t)count >= sizeof(position)) return NT_RESULT_OUT_OF_RANGE;
    return out(presenter, (NtByteView){position, (size_t)count});
}

static NtResult present_frame(const NtAnsiPresenter *presenter, const NtFrame *frame, NtByteView clear)
{
    if (!frame || !presenter || !presenter->write ||
        !nt_ansi_capabilities_valid(presenter->capabilities, presenter->palette))
        return NT_RESULT_INVALID_ARGUMENT;
    NtResult result;
    const bool interactive = presenter->capabilities.interactive;
    if (interactive) {
        result = out(presenter, clear);
        if (result != NT_RESULT_OK) return result;
    }
    const NtCellSize size = nt_frame_size(frame);
    AnsiStyle previous = {0};
    for (uint32_t y = 0; size.columns && y < size.rows; ++y) {
        if (interactive) {
            result = emit_row_position(presenter, y);
            if (result != NT_RESULT_OK) return result;
        }
        NtByteView previous_glyph = {0};
        for (uint32_t x = 0; x < size.columns;) {
            NtCellView cell;
            result = nt_frame_cell(frame, (NtCellPoint){(int32_t)x,(int32_t)y}, &cell);
            if (result != NT_RESULT_OK) return result;
            const uint8_t columns = cell.glyph_columns ? cell.glyph_columns : 1;
            if (interactive) {
                result = emit_style(presenter, cell.style, &previous);
                if (result != NT_RESULT_OK) return result;
            }
            NtByteView bytes;
            if (!cell.glyph.count || !cell.style.foreground.alpha)
                bytes = columns == 2 ? LITERAL("  ") : LITERAL(" ");
            else if (glyph_supported(cell.glyph, columns, presenter->capabilities) &&
                     nt_grapheme_separates(previous_glyph, cell.glyph))
                bytes = cell.glyph;
            else
                bytes = columns == 2 ? LITERAL("??") : LITERAL("?");
            result = out(presenter, bytes);
            if (result != NT_RESULT_OK) return result;
            previous_glyph = bytes;
            x += columns;
        }
        if (!interactive) {
            result = out(presenter, LITERAL("\n"));
            if (result != NT_RESULT_OK) return result;
        }
    }
    return interactive ? out(presenter, LITERAL("\x1b[0m")) : NT_RESULT_OK;
}

NtResult nt_ansi_present(void *context, const NtFrame *frame, NtTimeUs now)
{
    (void)now;
    return present_frame(context, frame, LITERAL("\x1b[0m\x1b[2J"));
}

NtResult nt_ansi_present_in_place(const NtAnsiPresenter *presenter, const NtFrame *frame)
{
    /* Conhost's ED 2 can advance the viewport and add its contents to the scrollback. */
    return present_frame(presenter, frame, LITERAL("\x1b[0m\x1b[H\x1b[J"));
}
