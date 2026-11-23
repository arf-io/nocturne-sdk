#include "event_internal.h"
#include "property_internal.h"

NT_DECLARE_ALIGNMENT(NtValue);

static bool rule_matches(const NtEventContext *context, const NtEventRule *rule)
{
    return rule->event == context->event->kind && rule->phase == context->phase;
}

static NtResult validate_rule(const NtEventContext *context, const NtEventRule *rule)
{
    if (rule->event < NT_EVENT_KEY || rule->event > NT_EVENT_RESIZE ||
        rule->phase < NT_EVENT_PREVIEW || rule->phase > NT_EVENT_BUBBLE ||
        (rule->property_count && !rule->properties) || (rule->field_count && !rule->fields))
        return NT_RESULT_INVALID_ARGUMENT;
    if (rule->property_count > SIZE_MAX / sizeof(NtValue) ||
        rule->property_count > SIZE_MAX / sizeof(*rule->properties) ||
        rule->field_count > SIZE_MAX / sizeof(*rule->fields) ||
        !nt_intent_field_count_valid(rule->field_count)) return NT_RESULT_OUT_OF_RANGE;
    uint32_t previous = 0;
    for (size_t i = 0; i < rule->field_count; ++i) {
        const NtEventFieldRule *field = &rule->fields[i];
        if (field->id <= previous || !nt_value_kind_valid(field->kind) || !field->derive)
            return NT_RESULT_INVALID_ARGUMENT;
        previous = field->id;
    }
    if (rule_matches(context, rule)) {
        for (size_t i = 0; i < rule->property_count; ++i) {
            NtAttachedProperty property;
            const NtResult result = nt_property_lookup(context->tree, rule->properties[i], false, &property);
            if (result != NT_RESULT_OK) return result;
        }
    }
    return NT_RESULT_OK;
}

static NtResult read_input(NtTree *tree, NtPropertyRef ref, NtValue *out)
{
    NtAttachedProperty property;
    NtValue value;
    NtResult result = nt_property_lookup(tree, ref, false, &property);
    if (result == NT_RESULT_OK) result = nt_property_read(property, &value);
    if (result == NT_RESULT_OK) result = nt_value_copy(&tree->allocator, value, out);
    return result;
}

static NtResult derive_intent(NtEventContext *context, const NtEventRule *rule, const NtValue *inputs)
{
    NtIntent *intent = nt_intent_create_fields(context, rule->intent_kind, rule->field_count);
    if (!intent) return NT_RESULT_ALLOCATION_FAILED;
    for (size_t i = 0; i < rule->field_count; ++i) {
        const NtEventFieldRule *field = &rule->fields[i];
        NtValue value = {.kind=NT_VALUE_RGBA + 1};
        NtResult result = field->derive(rule->context, context->event, inputs, rule->property_count, &value);
        if (result == NT_RESULT_OK) result = nt_value_require_kind(value, field->kind);
        if (result == NT_RESULT_OK)
            result = nt_intent_append_field(context, intent, (NtIntentField){field->id, value});
        if (result != NT_RESULT_OK) {
            nt_intent_release(context->tree, intent);
            return result;
        }
    }
    nt_intent_stage(context, intent);
    return NT_RESULT_OK;
}

static NtResult apply_rule(NtEventContext *context, const NtEventRule *rule, size_t *emissions)
{
    const NtAllocator *allocator = &context->tree->allocator;
    const size_t bytes = rule->property_count * sizeof(NtValue);
    NtValue *inputs = bytes ? nt_allocate(allocator, bytes, NT_ALIGNMENT(NtValue)) : NULL;
    if (bytes && !inputs) return NT_RESULT_ALLOCATION_FAILED;
    size_t initialized = 0;
    NtResult result = NT_RESULT_OK;
    while (initialized < rule->property_count) {
        result = read_input(context->tree, rule->properties[initialized], &inputs[initialized]);
        if (result != NT_RESULT_OK) break;
        ++initialized;
    }
    bool matches = !rule->predicate;
    if (result == NT_RESULT_OK && rule->predicate)
        result = rule->predicate(rule->context, context->event, inputs, rule->property_count, &matches);
    if (result == NT_RESULT_OK && matches) {
        result = derive_intent(context, rule, inputs);
        if (result == NT_RESULT_OK) ++*emissions;
    }
    for (size_t i = 0; i < initialized; ++i) nt_value_release(allocator, &inputs[i]);
    nt_release(allocator, inputs, bytes, NT_ALIGNMENT(NtValue));
    return result;
}

NtResult nt_event_apply_rules(NtEventContext *context, const NtEventRule *rules,
                              size_t count, size_t *emitted)
{
    const NtResult ready = nt_event_output_ready(context);
    if (ready != NT_RESULT_OK) return ready;
    if (count && !rules) return context->failure = NT_RESULT_INVALID_ARGUMENT;
    if (count > SIZE_MAX / sizeof(*rules)) return context->failure = NT_RESULT_OUT_OF_RANGE;
    for (size_t i = 0; i < count; ++i) {
        const NtResult result = validate_rule(context, &rules[i]);
        if (result != NT_RESULT_OK) return context->failure = result;
    }
    size_t emissions = 0;
    context->evaluating = true;
    for (size_t i = 0; i < count; ++i) {
        if (!rule_matches(context, &rules[i])) continue;
        const NtResult result = apply_rule(context, &rules[i], &emissions);
        if (result != NT_RESULT_OK) {
            context->failure = result;
            break;
        }
    }
    context->evaluating = false;
    if (context->failure == NT_RESULT_OK && emitted) *emitted = emissions;
    return context->failure;
}
