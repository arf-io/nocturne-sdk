#include <nocturne/style.h>
#include "property_internal.h"
#include "text_paint_internal.h"

#include <string.h>

typedef struct StyleRule {
    NtStyleRule rule;
    unsigned specificity;
} StyleRule;

struct NtStyleSheet {
    NtAllocator allocator;
    StyleRule *rules;
    char *text;
    size_t count, text_bytes;
};

NT_DECLARE_ALIGNMENT(NtStyleSheet);
NT_DECLARE_ALIGNMENT(StyleRule);
NT_DECLARE_ALIGNMENT(NtStyleElement);
NT_DECLARE_ALIGNMENT(NtPropertyWrite);

static unsigned state_count(uint64_t mask)
{
    unsigned count = 0;
    for (; mask; mask &= mask - 1) ++count;
    return count;
}

static unsigned specificity(NtStyleCondition condition)
{
    return (condition.role != 0) + (condition.region != 0) +
           state_count(condition.require | condition.forbid);
}

static bool scalar(uint32_t codepoint)
{
    return codepoint <= 0x10ffff && (codepoint < 0xd800 || codepoint > 0xdfff);
}

static NtResult check_rule(const NtStyleRule *rule)
{
    const NtStyleSelector *selector = &rule->selector;
    if (!rule->property || selector->relation < NT_STYLE_SELF || selector->relation > NT_STYLE_ANCESTOR ||
        (selector->subject.require & selector->subject.forbid) ||
        (selector->ancestor.require & selector->ancestor.forbid) ||
        (selector->relation == NT_STYLE_SELF && specificity(selector->ancestor)))
        return NT_RESULT_INVALID_ARGUMENT;
    if (selector->match_bytes && selector->bytes.begin > selector->bytes.end)
        return NT_RESULT_OUT_OF_RANGE;
    if (selector->match_characters &&
        (!scalar(selector->first_scalar) || !scalar(selector->last_scalar) ||
         selector->first_scalar > selector->last_scalar))
        return NT_RESULT_INVALID_ARGUMENT;
    return nt_value_validate(rule->value);
}

void nt_style_sheet_destroy(NtStyleSheet *sheet)
{
    if (!sheet) return;
    const NtAllocator allocator = sheet->allocator;
    nt_release(&allocator, sheet->text, sheet->text_bytes, 1);
    nt_release(&allocator, sheet->rules, sheet->count * sizeof(*sheet->rules), NT_ALIGNMENT(StyleRule));
    nt_release(&allocator, sheet, sizeof(*sheet), NT_ALIGNMENT(NtStyleSheet));
}

NtResult nt_style_sheet_create(const NtAllocator *allocator, const NtStyleRule *rules,
                                size_t count, NtStyleSheet **out)
{
    NtAllocator selected;
    if (!out || (count && !rules) || !nt_allocator_get(allocator, &selected))
        return NT_RESULT_INVALID_ARGUMENT;
    if (count > SIZE_MAX / sizeof(StyleRule) || count > SIZE_MAX / sizeof(*rules))
        return NT_RESULT_OUT_OF_RANGE;
    size_t text_bytes = 0;
    for (size_t i = 0; i < count; ++i) {
        const NtResult result = check_rule(&rules[i]);
        if (result != NT_RESULT_OK) return result;
        if (rules[i].value.kind == NT_VALUE_TEXT) {
            const size_t length = rules[i].value.as.text.count;
            if (length > SIZE_MAX - text_bytes) return NT_RESULT_OUT_OF_RANGE;
            text_bytes += length;
        }
    }
    NtStyleSheet *sheet = nt_allocate(&selected, sizeof(*sheet), NT_ALIGNMENT(NtStyleSheet));
    if (!sheet) return NT_RESULT_ALLOCATION_FAILED;
    *sheet = (NtStyleSheet){.allocator=selected, .count=count, .text_bytes=text_bytes};
    if (count) sheet->rules = nt_allocate(&selected, count * sizeof(*sheet->rules), NT_ALIGNMENT(StyleRule));
    if (text_bytes) sheet->text = nt_allocate(&selected, text_bytes, 1);
    if ((count && !sheet->rules) || (text_bytes && !sheet->text)) {
        nt_style_sheet_destroy(sheet);
        return NT_RESULT_ALLOCATION_FAILED;
    }
    size_t offset = 0;
    for (size_t i = 0; i < count; ++i) {
        NtStyleRule rule = rules[i];
        NtValue *value = &rule.value;
        if (value->kind == NT_VALUE_TEXT) {
            const size_t length = value->as.text.count;
            if (length) {
                memcpy(sheet->text + offset, value->as.text.data, length);
                value->as.text.data = sheet->text + offset;
                offset += length;
            } else value->as.text.data = NULL;
        }
        const NtStyleSelector selector = rule.selector;
        unsigned score = specificity(selector.subject) + (selector.text_region != 0) +
            (selector.cluster_id != 0) + selector.match_bytes + selector.match_characters;
        if (selector.relation != NT_STYLE_SELF)
            score += 1 + (selector.relation == NT_STYLE_PARENT) + specificity(selector.ancestor);
        sheet->rules[i] = (StyleRule){rule, score};
    }
    *out = sheet;
    return NT_RESULT_OK;
}

static NtResult check_sheets(NtStyleSheets sheets)
{
    if (sheets.count && !sheets.items) return NT_RESULT_INVALID_ARGUMENT;
    if (sheets.count > SIZE_MAX / sizeof(*sheets.items)) return NT_RESULT_OUT_OF_RANGE;
    for (size_t i = 0; i < sheets.count; ++i)
        if (!sheets.items[i]) return NT_RESULT_INVALID_ARGUMENT;
    return NT_RESULT_OK;
}

static NtResult check_path(NtStylePath path)
{
    if (path.ancestor_count && !path.ancestors) return NT_RESULT_INVALID_ARGUMENT;
    if (path.ancestor_count > SIZE_MAX / sizeof(*path.ancestors)) return NT_RESULT_OUT_OF_RANGE;
    return NT_RESULT_OK;
}

static NtResult check_regions(const NtStyleRegion *regions, size_t count, size_t bytes)
{
    if (count && !regions) return NT_RESULT_INVALID_ARGUMENT;
    if (count > SIZE_MAX / sizeof(*regions)) return NT_RESULT_OUT_OF_RANGE;
    for (size_t i = 0; i < count; ++i) {
        if (!regions[i].tag) return NT_RESULT_INVALID_ARGUMENT;
        if (regions[i].bytes.begin > regions[i].bytes.end || regions[i].bytes.end > bytes)
            return NT_RESULT_OUT_OF_RANGE;
    }
    return NT_RESULT_OK;
}

static bool condition(NtStyleCondition test, NtStyleElement element)
{
    return (!test.role || test.role == element.role) &&
           (!test.region || test.region == element.region) &&
           (test.require & element.states) == test.require && !(test.forbid & element.states);
}

static bool in_span(NtByteSpan span, size_t byte)
{
    return span.begin <= byte && byte < span.end;
}

static bool region_matches(const NtStyleSubject *subject, NtStyleTag tag)
{
    for (size_t i = 0; i < subject->region_count; ++i) {
        const NtStyleRegion region = subject->regions[i];
        if (region.tag == tag && in_span(region.bytes, subject->text.begin)) return true;
    }
    return false;
}

static bool character_matches(const NtStyleSubject *subject, const NtStyleSelector *selector)
{
    const NtByteView slice = {subject->source.data + subject->text.begin,
                              subject->text.end - subject->text.begin};
    size_t offset = 0;
    while (offset < slice.count) {
        uint32_t cp;
        if (nt_utf8_decode(slice, &offset, &cp) != NT_RESULT_OK) return false;
        if (cp >= selector->first_scalar && cp <= selector->last_scalar) return true;
    }
    return false;
}

static bool selector_matches(const NtStyleSubject *subject, const NtStyleSelector *selector)
{
    const NtStylePath path = subject->path;
    if (!condition(selector->subject, path.element)) return false;
    if (selector->relation != NT_STYLE_SELF) {
        bool matched = false;
        for (size_t i = 0; i < path.ancestor_count; ++i) {
            if (condition(selector->ancestor, path.ancestors[i])) {
                matched = true;
                break;
            }
            if (selector->relation == NT_STYLE_PARENT) break;
        }
        if (!matched) return false;
    }
    if (!selector->text_region && !selector->cluster_id &&
        !selector->match_bytes && !selector->match_characters) return true;
    if (subject->text.begin == subject->text.end) return false;
    if (selector->cluster_id && selector->cluster_id != subject->cluster_id) return false;
    if (selector->match_bytes && !in_span(selector->bytes, subject->text.begin)) return false;
    if (selector->text_region && !region_matches(subject, selector->text_region)) return false;
    return !selector->match_characters || character_matches(subject, selector);
}

static const StyleRule *resolve(NtStyleSheets sheets, const NtStyleSubject *subject, NtPropertyId property)
{
    const StyleRule *winner = NULL;
    for (size_t i = 0; i < sheets.count; ++i) {
        const NtStyleSheet *sheet = sheets.items[i];
        for (size_t j = 0; j < sheet->count; ++j) {
            const StyleRule *rule = &sheet->rules[j];
            if (rule->rule.property != property) continue;
            if (winner && (winner->rule.override > rule->rule.override ||
                (winner->rule.override == rule->rule.override && winner->specificity > rule->specificity)))
                continue;
            if (selector_matches(subject, &rule->rule.selector)) winner = rule;
        }
    }
    return winner;
}

NtResult nt_style_resolve(NtStyleSheets sheets, const NtStyleSubject *subject, NtPropertyId property,
                           bool *matched, NtValue *out)
{
    if (!subject || !property || !matched || !out || (subject->source.count && !subject->source.data))
        return NT_RESULT_INVALID_ARGUMENT;
    if (subject->text.begin > subject->text.end || subject->text.end > subject->source.count)
        return NT_RESULT_OUT_OF_RANGE;
    NtResult result = check_sheets(sheets);
    if (result == NT_RESULT_OK) result = check_path(subject->path);
    if (result == NT_RESULT_OK)
        result = check_regions(subject->regions, subject->region_count, subject->source.count);
    if (result != NT_RESULT_OK) return result;
    if (subject->text.begin != subject->text.end) {
        const NtByteView text = {subject->source.data + subject->text.begin,
                                subject->text.end - subject->text.begin};
        result = nt_utf8_validate(text, NULL);
        if (result != NT_RESULT_OK) return result;
    }
    const StyleRule *rule = resolve(sheets, subject, property);
    *matched = rule != NULL;
    if (rule) *out = rule->rule.value;
    return NT_RESULT_OK;
}

static NtStyleElement element_for(const NtStyleMount *mounts, size_t count, uint32_t slot)
{
    for (size_t i = 0; i < count; ++i)
        if (mounts[i].node.slot == slot) return mounts[i].element;
    return (NtStyleElement){0};
}

static size_t ancestor_count(const NtTree *tree, uint32_t slot)
{
    size_t count = 0;
    for (slot = tree->nodes[slot].parent; slot != NT_NO_SLOT; slot = tree->nodes[slot].parent)
        ++count;
    return count;
}

static NtStylePath tree_path(const NtTree *tree, uint32_t slot, const NtStyleMount *mounts,
                             size_t mount_count, NtStyleElement *ancestors)
{
    NtStylePath path = {.element=element_for(mounts, mount_count, slot), .ancestors=ancestors};
    for (slot = tree->nodes[slot].parent; slot != NT_NO_SLOT; slot = tree->nodes[slot].parent)
        ancestors[path.ancestor_count++] = element_for(mounts, mount_count, slot);
    return path;
}

NtResult nt_tree_apply_styles(NtTree *tree, NtStyleSheets sheets, const NtStyleMount *mounts,
                               size_t mount_count, const NtPropertyRef *targets, size_t target_count)
{
    if (!tree || (mount_count && !mounts) || (target_count && !targets))
        return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    if (mount_count > SIZE_MAX / sizeof(*mounts) || target_count > SIZE_MAX / sizeof(*targets) ||
        target_count > SIZE_MAX / sizeof(NtPropertyWrite)) return NT_RESULT_OUT_OF_RANGE;
    NtResult result = check_sheets(sheets);
    if (result != NT_RESULT_OK) return result;
    for (size_t i = 0; i < mount_count; ++i) {
        result = nt_node_validate(tree, mounts[i].node);
        if (result != NT_RESULT_OK) return result;
        for (size_t j = 0; j < i; ++j)
            if (mounts[i].node.slot == mounts[j].node.slot) return NT_RESULT_INVALID_ARGUMENT;
    }
    size_t depth = 0;
    for (size_t i = 0; i < target_count; ++i) {
        NtAttachedProperty property;
        result = nt_property_lookup(tree, targets[i], true, &property);
        if (result != NT_RESULT_OK) return result;
        for (size_t j = 0; j < i; ++j)
            if (targets[i].node.slot == targets[j].node.slot && targets[i].id == targets[j].id)
                return NT_RESULT_INVALID_ARGUMENT;
        const size_t length = ancestor_count(tree, targets[i].node.slot);
        if (length > depth) depth = length;
    }
    if (!target_count || !sheets.count) return NT_RESULT_OK;
    if (depth > SIZE_MAX / sizeof(NtStyleElement)) return NT_RESULT_OUT_OF_RANGE;
    const size_t path_bytes = depth * sizeof(NtStyleElement);
    const size_t write_bytes = target_count * sizeof(NtPropertyWrite);
    tree->busy = true;
    NtStyleElement *ancestors = depth ?
        nt_allocate(&tree->allocator, path_bytes, NT_ALIGNMENT(NtStyleElement)) : NULL;
    NtPropertyWrite *writes = nt_allocate(&tree->allocator, write_bytes, NT_ALIGNMENT(NtPropertyWrite));
    if (!writes || (depth && !ancestors)) result = NT_RESULT_ALLOCATION_FAILED;
    size_t count = 0;
    for (size_t i = 0; i < target_count && result == NT_RESULT_OK; ++i) {
        const NtPropertyRef target = targets[i];
        const NtStyleSubject subject = {.path=tree_path(tree, target.node.slot, mounts, mount_count, ancestors)};
        const StyleRule *rule = resolve(sheets, &subject, target.id);
        if (!rule) continue;
        NtAttachedProperty property;
        result = nt_property_lookup(tree, target, true, &property);
        if (result == NT_RESULT_OK && property.info.kind != rule->rule.value.kind)
            result = NT_RESULT_INVALID_ARGUMENT;
        if (result == NT_RESULT_OK)
            writes[count++] = (NtPropertyWrite){.property=property, .value=rule->rule.value};
    }
    if (result == NT_RESULT_OK) result = nt_property_write_batch(tree, writes, count);
    nt_release(&tree->allocator, writes, write_bytes, NT_ALIGNMENT(NtPropertyWrite));
    nt_release(&tree->allocator, ancestors, path_bytes, NT_ALIGNMENT(NtStyleElement));
    tree->busy = false;
    return result;
}

typedef struct StyleTextDrawing {
    const NtTextStyles *styles;
    NtByteView source;
} StyleTextDrawing;

static NtResult resolve_text(void *context, NtClusterView cluster, NtTextPaintStyle *style)
{
    const StyleTextDrawing *drawing = context;
    const NtTextStyles *styles = drawing->styles;
    NtStyleSubject subject = {.path=styles->path, .source=drawing->source,
        .text=cluster.id ? cluster.source : (NtByteSpan){0}, .cluster_id=cluster.id,
        .regions=styles->regions, .region_count=styles->region_count};
    const StyleRule *foreground = resolve(styles->sheets, &subject, NT_PROPERTY_FOREGROUND);
    const StyleRule *background = resolve(styles->sheets, &subject, NT_PROPERTY_BACKGROUND);
    if ((foreground && foreground->rule.value.kind != NT_VALUE_RGBA) ||
        (background && background->rule.value.kind != NT_VALUE_RGBA))
        return NT_RESULT_INVALID_ARGUMENT;
    if (foreground) {
        style->color.foreground = foreground->rule.value.as.rgba;
        style->foreground = NULL;
    }
    if (background) {
        style->color.background = background->rule.value.as.rgba;
        style->background = NULL;
    }
    return NT_RESULT_OK;
}

static NtResult styled_text(const NtSurfaceView *view, NtCellPoint position, const NtTextLayout *layout,
                             const NtTextPaintOptions *options, const NtTextStyles *styles,
                             bool mounted, NtOwnerToken owner)
{
    if (!layout || !styles) return NT_RESULT_INVALID_ARGUMENT;
    const NtByteView source = nt_text_layout_source(layout);
    NtResult result = check_sheets(styles->sheets);
    if (result == NT_RESULT_OK) result = check_path(styles->path);
    if (result == NT_RESULT_OK) result = check_regions(styles->regions, styles->region_count, source.count);
    if (result != NT_RESULT_OK) return result;
    StyleTextDrawing drawing = {styles, source};
    return nt_text_paint_resolved(view, position, layout, options, resolve_text, &drawing, mounted, owner);
}

NtResult nt_style_text_paint(const NtSurfaceView *view, NtCellPoint position, const NtTextLayout *layout,
                              const NtTextPaintOptions *paint, const NtTextStyles *styles)
{
    return styled_text(view, position, layout, paint, styles, false, 0);
}

NtResult nt_paint_styled_text(NtPaintContext *paint, NtCellPoint position, const NtTextLayout *layout,
                              const NtTextPaintOptions *options, const NtTextStyles *styles)
{
    return paint ? styled_text(paint->surface, position, layout, options, styles, true, paint->owner) :
        NT_RESULT_INVALID_ARGUMENT;
}
