#ifndef NOCTURNE_INTERNAL_EVENT_H
#define NOCTURNE_INTERNAL_EVENT_H

#include <nocturne/event_rules.h>
#include "component_internal.h"

struct NtEventContext {
    NtTree *tree;
    NtNodeId source;
    const NtEvent *event;
    NtEventPhase phase;
    NtLocalPosition local;
    bool has_position, evaluating;
    NtResult failure;
    NtIntent *first, *last;
};

NtResult nt_event_output_ready(const NtEventContext *context);
/* Pass NULL for both input and reserved to validate before reading a key. */
NtResult nt_key_policy_check(const NtDispatchPolicy *policy, const NtKeyInput *input, bool *reserved);
bool nt_intent_field_count_valid(size_t count);
NtIntent *nt_intent_create_fields(NtEventContext *context, uint32_t kind, size_t count);
NtResult nt_intent_append_field(NtEventContext *context, NtIntent *intent, NtIntentField field);
void nt_intent_stage(NtEventContext *context, NtIntent *intent);
void nt_intent_release(const NtTree *tree, NtIntent *intent);
void nt_component_commit_pending_intents(NtEventContext *context);

#endif
