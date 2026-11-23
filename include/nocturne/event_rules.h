#ifndef NOCTURNE_EVENT_RULES_H
#define NOCTURNE_EVENT_RULES_H

#include <nocturne/property.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum NtIntentPayloadKind { NT_INTENT_BYTES, NT_INTENT_FIELDS } NtIntentPayloadKind;
typedef struct NtIntentField { uint32_t id; NtValue value; } NtIntentField;
typedef struct NtEventIntentView {
    NtIntentView intent;
    NtIntentPayloadKind payload_kind;
    const NtIntentField *fields;
    size_t field_count;
} NtEventIntentView;

/* Stage one typed intent for this callback. Field IDs must be strictly
 * increasing and nonzero. The SDK copies fields and their counted text. */
NtResult nt_event_emit_fields(NtEventContext *context, uint32_t kind,
                              const NtIntentField *fields, size_t count);
/* Inspect either payload kind at the head of the same FIFO used by
 * nt_tree_peek_intent and nt_tree_pop_intent. Borrows end at pop or destroy. */
bool nt_tree_peek_event_intent(const NtTree *tree, NtEventIntentView *out);

typedef NtResult (*NtEventPredicate)(void *context, const NtEvent *event,
                                     const NtValue *properties, size_t count, bool *out);
typedef NtResult (*NtEventDerive)(void *context, const NtEvent *event,
                                  const NtValue *properties, size_t count, NtValue *out);
typedef struct NtEventFieldRule {
    uint32_t id;
    NtValueKind kind;
    NtEventDerive derive;
} NtEventFieldRule;
typedef struct NtEventRule {
    NtEventKind event;
    NtEventPhase phase;
    const NtPropertyRef *properties;
    size_t property_count;
    NtEventPredicate predicate; /* NULL matches unconditionally. */
    uint32_t intent_kind;
    const NtEventFieldRule *fields; /* Increasing, nonzero field IDs. */
    size_t field_count;
    void *context;
} NtEventRule;

/* Run in an event callback. Exact event/phase matches read and copy their
 * properties in declaration order. A true predicate stages one typed intent
 * per matching rule, in rule order. emitted is optional and written only on
 * success. A failure discards all of the enclosing callback's staged output.
 * Predicate and derive callbacks preserve state, may not emit or apply rules,
 * and can borrow derived text through the SDK's immediate copy. */
NtResult nt_event_apply_rules(NtEventContext *context, const NtEventRule *rules,
                              size_t count, size_t *emitted);

#ifdef __cplusplus
}
#endif
#endif
