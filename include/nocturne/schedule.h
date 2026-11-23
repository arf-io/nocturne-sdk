#ifndef NOCTURNE_SCHEDULE_H
#define NOCTURNE_SCHEDULE_H

#include <nocturne/event_rules.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t NtScheduleId;
typedef struct NtScheduleLimits { size_t batches, subscriptions; } NtScheduleLimits;
typedef struct NtScheduleRun { size_t attempted; NtScheduleId failed; } NtScheduleRun;
typedef void (*NtIntentReceiver)(void *context, NtNodeId owner, const NtEventIntentView *intent);

/* Set capacities. Reducing either below its current use fails. */
NtResult nt_tree_schedule_configure(NtTree *tree, NtScheduleLimits limits);
/* Queue an owned copy of a nonempty assignment list. Removal of the owner
 * or any target cancels the batch. IDs are scoped to this tree. */
NtResult nt_tree_schedule_batch(NtTree *tree, NtNodeId owner, NtTimeUs deadline,
                                const NtPropertyAssignment *assignments, size_t count,
                                NtScheduleId *out);
/* Subscribe to an exact intent kind. The borrowed context must live until
 * cancellation or owner removal. Deliveries borrow payloads for the call. */
NtResult nt_tree_schedule_subscribe(NtTree *tree, NtNodeId owner, uint32_t kind,
                                    NtIntentReceiver receive, void *context, NtScheduleId *out);
NtResult nt_tree_schedule_cancel(NtTree *tree, NtScheduleId id);
NtDeadline nt_tree_schedule_next(const NtTree *tree);

/* Attempt at most budget due batches in deadline and registration order.
 * Time is monotonic across admitted runs. A failed batch is consumed and
 * stops the run. The result describes attempted work after admission. */
NtResult nt_tree_schedule_run(NtTree *tree, NtTimeUs now, size_t budget, NtScheduleRun *out);
/* Consume at most budget committed intents. Each matching subscription
 * receives each item once in registration order. Receivers run under the
 * tree mutation guard. A budget of zero consumes nothing. */
NtResult nt_tree_schedule_deliver(NtTree *tree, size_t budget, size_t *delivered);

#ifdef __cplusplus
}
#endif
#endif
