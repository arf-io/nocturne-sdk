#include "event_internal.h"
#include "property_internal.h"

#include <string.h>

struct NtIntent {
    NtIntent *next;
    NtIntentView view;
    NtIntentPayloadKind payload_kind;
    size_t allocation_bytes, field_count;
    NtIntentField fields[];
};

/* offsetof cannot define an alignment wrapper for a flexible-array type. */
NT_DECLARE_ALIGNMENT(NtIntentView);
NT_DECLARE_ALIGNMENT(NtIntentField);

static size_t intent_alignment(void)
{
    const size_t view = NT_ALIGNMENT(NtIntentView);
    const size_t field = NT_ALIGNMENT(NtIntentField);
    return view > field ? view : field;
}

static NtIntent *allocate_intent(NtEventContext *context, uint32_t kind,
                                 size_t bytes, NtIntentPayloadKind payload_kind)
{
    NtIntent *intent = nt_allocate(&context->tree->allocator, bytes, intent_alignment());
    if (intent) {
        intent->next = NULL;
        intent->view = (NtIntentView){.source=context->source, .kind=kind};
        intent->payload_kind = payload_kind;
        intent->allocation_bytes = bytes;
        intent->field_count = 0;
    }
    return intent;
}

bool nt_intent_field_count_valid(size_t count)
{
    return count <= (SIZE_MAX - sizeof(NtIntent)) / sizeof(NtIntentField);
}

NtIntent *nt_intent_create_fields(NtEventContext *context, uint32_t kind, size_t count)
{
    return allocate_intent(context, kind, sizeof(NtIntent) + count * sizeof(NtIntentField),
                           NT_INTENT_FIELDS);
}

NtResult nt_intent_append_field(NtEventContext *context, NtIntent *intent, NtIntentField field)
{
    NtValue copy;
    const NtResult result = nt_value_copy(&context->tree->allocator, field.value, &copy);
    if (result == NT_RESULT_OK)
        intent->fields[intent->field_count++] = (NtIntentField){field.id, copy};
    return result;
}

void nt_intent_release(const NtTree *tree, NtIntent *intent)
{
    if (!intent) return;
    for (size_t i = 0; i < intent->field_count; ++i)
        nt_value_release(&tree->allocator, &intent->fields[i].value);
    nt_release(&tree->allocator, intent, intent->allocation_bytes, intent_alignment());
}

static void release_intents(const NtTree *tree, NtIntent *first)
{
    while (first) {
        NtIntent *next = first->next;
        nt_intent_release(tree, first);
        first = next;
    }
}

void nt_tree_release_intents(NtTree *tree)
{
    release_intents(tree, tree->intent_first);
    tree->intent_first = tree->intent_last = NULL;
}

NtResult nt_event_output_ready(const NtEventContext *context)
{
    if (!context) return NT_RESULT_INVALID_ARGUMENT;
    return context->evaluating ? NT_RESULT_WRONG_PHASE : context->failure;
}

void nt_intent_stage(NtEventContext *context, NtIntent *intent)
{
    if (context->last) context->last->next = intent;
    else context->first = intent;
    context->last = intent;
}

NtResult nt_event_emit(NtEventContext *context, uint32_t kind, NtByteView payload)
{
    const NtResult ready = nt_event_output_ready(context);
    if (ready != NT_RESULT_OK) return ready;
    if (payload.count && !payload.data) return context->failure = NT_RESULT_INVALID_ARGUMENT;
    if (payload.count > SIZE_MAX - sizeof(NtIntent))
        return context->failure = NT_RESULT_OUT_OF_RANGE;
    NtIntent *intent = allocate_intent(context, kind, sizeof(NtIntent) + payload.count,
                                       NT_INTENT_BYTES);
    if (!intent) return context->failure = NT_RESULT_ALLOCATION_FAILED;
    if (payload.count) {
        char *copy = (char *)(intent + 1);
        memcpy(copy, payload.data, payload.count);
        intent->view.payload = (NtByteView){copy, payload.count};
    }
    nt_intent_stage(context, intent);
    return NT_RESULT_OK;
}

NtResult nt_event_emit_fields(NtEventContext *context, uint32_t kind,
                              const NtIntentField *fields, size_t count)
{
    const NtResult ready = nt_event_output_ready(context);
    if (ready != NT_RESULT_OK) return ready;
    if (count && !fields) return context->failure = NT_RESULT_INVALID_ARGUMENT;
    if (!nt_intent_field_count_valid(count)) return context->failure = NT_RESULT_OUT_OF_RANGE;
    uint32_t previous = 0;
    for (size_t i = 0; i < count; ++i) {
        if (fields[i].id <= previous) return context->failure = NT_RESULT_INVALID_ARGUMENT;
        const NtResult valid = nt_value_validate(fields[i].value);
        if (valid != NT_RESULT_OK) return context->failure = valid;
        previous = fields[i].id;
    }
    NtIntent *intent = nt_intent_create_fields(context, kind, count);
    if (!intent) return context->failure = NT_RESULT_ALLOCATION_FAILED;
    for (size_t i = 0; i < count; ++i) {
        const NtResult result = nt_intent_append_field(context, intent, fields[i]);
        if (result != NT_RESULT_OK) {
            nt_intent_release(context->tree, intent);
            return context->failure = result;
        }
    }
    nt_intent_stage(context, intent);
    return NT_RESULT_OK;
}

void nt_component_discard_pending_intents(NtEventContext *context)
{
    release_intents(context->tree, context->first);
    context->first = context->last = NULL;
}

void nt_component_commit_pending_intents(NtEventContext *context)
{
    if (!context->first) return;
    NtTree *tree = context->tree;
    if (tree->intent_last) tree->intent_last->next = context->first;
    else tree->intent_first = context->first;
    tree->intent_last = context->last;
    context->first = context->last = NULL;
}

bool nt_tree_peek_intent(const NtTree *tree, NtIntentView *out)
{
    if (!tree || !out || !tree->intent_first) return false;
    *out = tree->intent_first->view;
    return true;
}

bool nt_tree_peek_event_intent(const NtTree *tree, NtEventIntentView *out)
{
    if (!tree || !out || !tree->intent_first) return false;
    const NtIntent *head = tree->intent_first;
    *out = (NtEventIntentView){head->view, head->payload_kind,
        head->field_count ? head->fields : NULL, head->field_count};
    return true;
}

void nt_tree_drop_intent(NtTree *tree)
{
    NtIntent *intent = tree->intent_first;
    tree->intent_first = intent->next;
    if (!tree->intent_first) tree->intent_last = NULL;
    nt_intent_release(tree, intent);
}

void nt_tree_pop_intent(NtTree *tree)
{
    if (tree && !tree->busy && tree->intent_first) nt_tree_drop_intent(tree);
}
