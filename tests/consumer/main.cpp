#include <nocturne/animation.h>
#include <nocturne/binding.h>
#include <nocturne/event_rules.h>
#include <nocturne/schedule.h>
#include <nocturne/state.h>
#include <nocturne/plot.h>
#include <nocturne/style.h>
#include <nocturne/frame.h>
#include <nocturne/field.h>
#include <nocturne/motion.h>
#include <nocturne/panel_transform.h>
#include <nocturne/particle_effect.h>
#include <nocturne/particle_deposits.h>
#include <nocturne/paint_sample.h>
#include <nocturne/surface.h>
#include <nocturne/blend.h>
#include <nocturne/text_layout.h>
#include <nocturne/text_effect.h>
#include <nocturne/controls.h>
#include <nocturne/timeline.h>

static bool layout_snapshot()
{
    NtText *text = nullptr;
    if (nt_text_create(nullptr, {"a\xe4\xb8\xad\tb", 6}, &text) != NT_RESULT_OK)
        return false;
    NtTextLayoutOptions options{};
    options.width = {NT_WIDTH_ONE, NT_WIDTH_TWO};
    options.wrap = NT_WRAP_CLUSTER;
    options.bounds.columns = {0, 4, true};
    options.tab_stop = 4;
    NtTextLayout *layout = nullptr;
    const auto created = nt_text_layout_create(nullptr, text, &options, &layout);
    nt_text_destroy(text);
    if (created != NT_RESULT_OK) return false;
    NtClusterView tab{}, final{};
    const auto size = nt_text_layout_size(layout);
    const bool ok = size.columns == 4 && size.rows == 2 &&
        nt_text_layout_cluster(layout, 2, &tab) == NT_RESULT_OK &&
        tab.glyph.count == 1 && tab.glyph.data[0] == ' ' &&
        tab.source.begin == 4 && tab.position.x == 3 &&
        nt_text_layout_cluster(layout, 3, &final) == NT_RESULT_OK &&
        final.source.begin == 5 && final.position.x == 0 &&
        final.position.y == 1 && final.glyph.data[0] == 'b';
    nt_text_layout_destroy(layout);
    return ok;
}

static bool sample_palette()
{
    const NtRgba colors[]{{255, 0, 0, 0}, {0, 0, 255, 255}};
    const NtPaletteRamp ramp{{colors, 2}, NT_PALETTE_LINEAR, NT_PAINT_REPEAT,
                             {NT_COLOR_SRGB, NT_GAMUT_REJECT,
                              NT_HUE_SHORTER, NT_ALPHA_PREMULTIPLIED}, NT_LINEAR};
    NtColor color{};
    NtRgba rgba{};
    return nt_palette_sample(&ramp, -.25, &color) == NT_RESULT_OK &&
        nt_color_to_rgba(color, NT_GAMUT_REJECT, &rgba) == NT_RESULT_OK &&
        rgba.red == 0 && rgba.green == 0 && rgba.blue == 255 && rgba.alpha == 128;
}

static bool masked_fill()
{
    NtFrame *frame = nullptr;
    if (nt_frame_create(nullptr, {2, 1}, &frame) != NT_RESULT_OK) return false;
    const uint8_t alpha[] = {0, 128};
    const NtSurfaceMask mask{NT_MASK_ALPHA, {{0, 0}, {2, 1}}, 0, alpha, 2, sizeof(alpha)};
    NtSurfaceView root{}, child{};
    NtCellBackground outside{}, covered{};
    const bool ok = nt_surface_root(frame, &root) == NT_RESULT_OK &&
        nt_surface_child(&root, {{0, 0}, {2, 1}}, &mask, &child) == NT_RESULT_OK &&
        nt_surface_fill(&child, {{0, 0}, {2, 1}}, {70, 100, 200, 255}, 8) == NT_RESULT_OK &&
        nt_frame_background(frame, {0, 0}, &outside) == NT_RESULT_OK &&
        nt_frame_background(frame, {1, 0}, &covered) == NT_RESULT_OK &&
        outside.color.alpha == 0 && outside.owner == 0 &&
        covered.color.blue == 200 && covered.color.alpha == 128 && covered.owner == 8;
    nt_frame_destroy(frame);
    return ok;
}

static bool blend_colors()
{
    NtCompositeOptions options{};
    NtRgba result{};
    return nt_blend_mode_parse("screen", &options.blend) == NT_RESULT_OK &&
        nt_composite_operator_parse("source-over", &options.operation) == NT_RESULT_OK &&
        nt_rgba_composite({0, 128, 255, 255}, {128, 64, 0, 255}, &options, &result) ==
            NT_RESULT_OK &&
        result.red == 128 && result.green == 160 && result.blue == 255 && result.alpha == 255;
}

int main() {
    NtTween tween = {0, 1, 100, 0, NT_LINEAR, NT_ONCE};
    NtAnimationSample sample = {};
    if (nt_tween_sample(&tween, 50, &sample) != NT_OK || sample.value != .5) return 1;

    const NtByteView text{"\xf0\x9f\x8c\x99", 4};
    size_t offset = 0;
    uint32_t scalar = 0;
    if (nt_utf8_decode(text, &offset, &scalar) != NT_RESULT_OK ||
        offset != 4 || scalar != 0x1f319) return 2;

    NtText *owned = nullptr;
    if (nt_text_create(nullptr, text, &owned) != NT_RESULT_OK) return 3;
    NtTextCluster before{}, after{};
    const bool stable = nt_text_cluster(owned, 0, &before) == NT_RESULT_OK &&
        nt_text_replace(owned, {0, 0}, {"!", 1}) == NT_RESULT_OK &&
        nt_text_cluster(owned, 1, &after) == NT_RESULT_OK &&
        before.id == after.id && after.source.begin == 1 && after.source.end == 5;
    nt_text_destroy(owned);
    if (!stable) return 4;

    NtFrame *frame = nullptr;
    if (nt_frame_create(nullptr, {2, 1}, &frame) != NT_RESULT_OK) return 5;
    const NtCellStyle style{{1, 2, 3, 255}, {0, 0, 0, 255}};
    NtCellView cell{};
    const bool shrank = nt_frame_set_glyph(frame, {0, 0}, text, 2, style, 7) == NT_RESULT_OK &&
        nt_frame_resize(frame, {1, 1}) == NT_RESULT_OK &&
        nt_frame_cell(frame, {0, 0}, &cell) == NT_RESULT_OK &&
        cell.owner == 0 && cell.glyph.data == nullptr && cell.glyph_columns == 0;
    nt_frame_destroy(frame);
    if (!shrank) return 6;

    const NtByteView rtl{"(" "\xd7\x90" ")", 4};
    NtBidi *bidi = nullptr;
    if (nt_bidi_create(nullptr, rtl, NT_DIRECTION_AUTO, &bidi) != NT_RESULT_OK) return 7;
    NtBidiVisual visual[3]{};
    size_t count = 0;
    const bool mirrored = nt_bidi_reorder_line(bidi, {0, rtl.count}, visual, 3, &count) ==
        NT_RESULT_OK && count == 3 && visual[0].logical_index == 2 &&
        visual[0].level == 1 && visual[0].display_scalar == '(' &&
        visual[2].logical_index == 0 && visual[2].display_scalar == ')';
    nt_bidi_destroy(bidi);
    if (!mirrored) return 8;
    if (!layout_snapshot()) return 9;
    if (!sample_palette()) return 10;
    if (!masked_fill()) return 11;
    if (!blend_colors()) return 12;
    if (nt_control_style_default().width.emoji != NT_WIDTH_TWO) return 13;
    NtValue typed{};
    typed.kind = NT_VALUE_REAL;
    typed.as.real = .25;
    if (nt_value_validate(typed) != NT_RESULT_OK) return 15;
    NtStyleRule rule{};
    rule.property = NT_PROPERTY_VALUE;
    rule.value = typed;
    rule.override = true;
    NtStyleSheet *sheet = nullptr;
    if (nt_style_sheet_create(nullptr, &rule, 1, &sheet) != NT_RESULT_OK) return 16;
    const NtStyleSheet *sheets[] = {sheet};
    NtStyleSubject subject{};
    bool matched = false;
    NtValue styled_value{};
    const bool resolved = nt_style_resolve({sheets, 1}, &subject, NT_PROPERTY_VALUE, &matched, &styled_value) ==
        NT_RESULT_OK && matched && styled_value.kind == NT_VALUE_REAL && styled_value.as.real == .25;
    nt_style_sheet_destroy(sheet);
    if (!resolved) return 17;
    NtTree *empty_tree = nullptr;
    if (nt_tree_create(nullptr, &empty_tree) != NT_RESULT_OK) return 18;
    NtEventIntentView event_intent{};
    const bool empty_queue = !nt_tree_peek_event_intent(empty_tree, &event_intent);
    NtStateSnapshot *snapshot = nullptr;
    const bool state_ready = nt_tree_schedule_configure(empty_tree, {1, 1}) == NT_RESULT_OK &&
        nt_tree_state_capture(empty_tree, nullptr, 0, nullptr, &snapshot) == NT_RESULT_OK &&
        nt_state_snapshot_bytes(snapshot).count == 16 &&
        nt_tree_state_restore(empty_tree, nullptr, 0, snapshot) == NT_RESULT_OK;
    nt_state_snapshot_destroy(snapshot);
    nt_tree_destroy(empty_tree);
    if (!empty_queue) return 19;
    if (!state_ready) return 20;
    NtPlotViewport chart_view{};
    chart_view.domain = {{-1, 1}, {0, 2}};
    chart_view.cells = {{2, 3}, {8, 4}};
    NtPoint projected{};
    if (nt_plot_project(&chart_view, {0, 1}, &projected) != NT_RESULT_OK ||
        projected.x != 6 || projected.y != 5) return 21;
    NtParticleEffectOptions particle_effect{};
    if (nt_particle_effect_recipe(NT_PARTICLE_RECIPE_NOVA, &particle_effect) != NT_RESULT_OK ||
        !particle_effect.motion.radial || particle_effect.view.emitter.period_us != 0) return 22;
    NtField wave_field{};
    wave_field.kind = NT_FIELD_WAVE;
    wave_field.as.wave = nt_wave_field_default();
    NtFieldSample field_sample{};
    if (nt_field_sample(&wave_field, {{2, 0}, 0, NT_MOTION_FULL}, &field_sample) != NT_RESULT_OK ||
        field_sample.value != 1 || field_sample.coverage != 1) return 23;
    const auto deposition = nt_particle_deposits_options_default();
    if (!deposition.capacity || !deposition.hold_us) return 24;
    NtTimeUs source_time = 0;
    return nt_track_time(nullptr, 31, &source_time) == NT_RESULT_OK && source_time == 31 ? 0 : 14;
}
