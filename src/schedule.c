#include <nocturne/schedule.h>

#include "property_internal.h"

typedef struct ScheduledBatch {
    struct ScheduledBatch *next;
    NtNodeId owner;
    NtScheduleId id;
    NtTimeUs deadline;
    size_t count;
    NtPropertyAssignment assignments[];
} ScheduledBatch;

typedef struct Subscription {
    struct Subscription *next;
    NtNodeId owner;
    NtScheduleId id;
    uint32_t kind;
    NtIntentReceiver receive;
    void *context;
} Subscription;

struct NtSchedule {
    NtScheduleLimits limits, used;
    NtScheduleId latest;
    NtTimeUs now;
    ScheduledBatch *batches;
    Subscription *subscriptions;
};

NT_DECLARE_ALIGNMENT(NtSchedule);
NT_DECLARE_ALIGNMENT(Subscription);
NT_DECLARE_ALIGNMENT(NtPropertyAssignment);

static NtResult schedule_ready(const NtTree *tree)
{
    if (!tree) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    return tree->schedule ? NT_RESULT_OK : NT_RESULT_UNSUPPORTED_CAPABILITY;
}

NtResult nt_tree_schedule_configure(NtTree *tree, NtScheduleLimits limits)
{
    if (!tree) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    NtSchedule *schedule = tree->schedule;
    if (!schedule) {
        tree->busy = true;
        schedule = nt_allocate(&tree->allocator, sizeof(*schedule), NT_ALIGNMENT(NtSchedule));
        tree->busy = false;
        if (!schedule) return NT_RESULT_ALLOCATION_FAILED;
        *schedule = (NtSchedule){0};
        tree->schedule = schedule;
    }
    if (limits.batches < schedule->used.batches || limits.subscriptions < schedule->used.subscriptions)
        return NT_RESULT_OUT_OF_RANGE;
    schedule->limits = limits;
    return NT_RESULT_OK;
}

static size_t batch_bytes(size_t count)
{
    return sizeof(ScheduledBatch) + count * sizeof(NtPropertyAssignment);
}

static void release_batch(const NtTree *tree, ScheduledBatch *batch, size_t copied)
{
    for (size_t i = 0; i < copied; ++i) nt_value_release(&tree->allocator, &batch->assignments[i].value);
    nt_release(&tree->allocator, batch, batch_bytes(batch->count), NT_ALIGNMENT(NtPropertyAssignment));
}

static void drop_batch(NtTree *tree, ScheduledBatch **link)
{
    ScheduledBatch *batch = *link;
    *link = batch->next;
    --tree->schedule->used.batches;
    release_batch(tree, batch, batch->count);
}

static void drop_subscription(NtTree *tree, Subscription **link)
{
    Subscription *sub = *link;
    *link = sub->next;
    --tree->schedule->used.subscriptions;
    nt_release(&tree->allocator, sub, sizeof(*sub), NT_ALIGNMENT(Subscription));
}

NtResult nt_tree_schedule_batch(NtTree *tree, NtNodeId owner, NtTimeUs deadline,
                                const NtPropertyAssignment *assignments, size_t count,
                                NtScheduleId *out)
{
    NtResult result = schedule_ready(tree);
    if (result != NT_RESULT_OK) return result;
    if (!out || !count || !assignments) return NT_RESULT_INVALID_ARGUMENT;
    result = nt_node_validate(tree, owner);
    if (result != NT_RESULT_OK) return result;
    NtSchedule *schedule = tree->schedule;
    if (schedule->used.batches == schedule->limits.batches || schedule->latest == UINT64_MAX ||
        count > (SIZE_MAX - sizeof(ScheduledBatch)) / sizeof(*assignments)) return NT_RESULT_OUT_OF_RANGE;
    result = nt_property_check_assignments(tree, assignments, count, NULL);
    if (result != NT_RESULT_OK) return result;

    tree->busy = true;
    ScheduledBatch *batch = nt_allocate(&tree->allocator, batch_bytes(count), NT_ALIGNMENT(NtPropertyAssignment));
    if (!batch) result = NT_RESULT_ALLOCATION_FAILED;
    else {
        batch->count = count;
        batch->owner = owner;
        batch->deadline = deadline;
        size_t copied = 0;
        for (; copied < count; ++copied) {
            NtPropertyAssignment *entry = &batch->assignments[copied];
            entry->property = assignments[copied].property;
            result = nt_value_copy(&tree->allocator, assignments[copied].value, &entry->value);
            if (result != NT_RESULT_OK) break;
        }
        if (result == NT_RESULT_OK) {
            ScheduledBatch **link = &schedule->batches;
            while (*link && (*link)->deadline <= deadline) link = &(*link)->next;
            batch->id = ++schedule->latest;
            batch->next = *link;
            *link = batch;
            ++schedule->used.batches;
            *out = batch->id;
        } else release_batch(tree, batch, copied);
    }
    tree->busy = false;
    return result;
}

NtResult nt_tree_schedule_subscribe(NtTree *tree, NtNodeId owner, uint32_t kind,
                                    NtIntentReceiver receive, void *context, NtScheduleId *out)
{
    NtResult result = schedule_ready(tree);
    if (result != NT_RESULT_OK) return result;
    if (!receive || !out) return NT_RESULT_INVALID_ARGUMENT;
    result = nt_node_validate(tree, owner);
    if (result != NT_RESULT_OK) return result;
    NtSchedule *schedule = tree->schedule;
    if (schedule->used.subscriptions == schedule->limits.subscriptions || schedule->latest == UINT64_MAX)
        return NT_RESULT_OUT_OF_RANGE;
    tree->busy = true;
    Subscription *sub = nt_allocate(&tree->allocator, sizeof(*sub), NT_ALIGNMENT(Subscription));
    if (sub) {
        Subscription **link = &schedule->subscriptions;
        while (*link) link = &(*link)->next;
        *sub = (Subscription){.owner=owner,.id=++schedule->latest,.kind=kind,.receive=receive,.context=context};
        *link = sub;
        ++schedule->used.subscriptions;
        *out = sub->id;
    }
    tree->busy = false;
    return sub ? NT_RESULT_OK : NT_RESULT_ALLOCATION_FAILED;
}

NtResult nt_tree_schedule_cancel(NtTree *tree, NtScheduleId id)
{
    const NtResult ready = schedule_ready(tree);
    if (ready != NT_RESULT_OK) return ready;
    if (!id) return NT_RESULT_INVALID_ARGUMENT;
    ScheduledBatch **batch = &tree->schedule->batches;
    while (*batch && (*batch)->id != id) batch = &(*batch)->next;
    Subscription **sub = &tree->schedule->subscriptions;
    if (!*batch) while (*sub && (*sub)->id != id) sub = &(*sub)->next;
    if (!*batch && !*sub) return NT_RESULT_STALE_REFERENCE;
    tree->busy = true;
    if (*batch) drop_batch(tree, batch);
    else drop_subscription(tree, sub);
    tree->busy = false;
    return NT_RESULT_OK;
}

NtDeadline nt_tree_schedule_next(const NtTree *tree)
{
    const ScheduledBatch *head = tree && tree->schedule ? tree->schedule->batches : NULL;
    return head ? (NtDeadline){true,head->deadline} : (NtDeadline){0};
}

NtResult nt_tree_schedule_run(NtTree *tree, NtTimeUs now, size_t budget, NtScheduleRun *out)
{
    NtResult result = schedule_ready(tree);
    if (result != NT_RESULT_OK) return result;
    if (!out) return NT_RESULT_INVALID_ARGUMENT;
    NtSchedule *schedule = tree->schedule;
    if (now < schedule->now) return NT_RESULT_OUT_OF_RANGE;
    schedule->now = now;
    NtScheduleRun report = {0};
    tree->busy = true;
    while (report.attempted < budget && schedule->batches && schedule->batches->deadline <= now) {
        ScheduledBatch *batch = schedule->batches;
        result = nt_property_apply_batch(tree, batch->assignments, batch->count);
        ++report.attempted;
        if (result != NT_RESULT_OK) report.failed = batch->id;
        drop_batch(tree, &schedule->batches);
        if (result != NT_RESULT_OK) break;
    }
    tree->busy = false;
    *out = report;
    return result;
}

NtResult nt_tree_schedule_deliver(NtTree *tree, size_t budget, size_t *delivered)
{
    const NtResult result = schedule_ready(tree);
    if (result != NT_RESULT_OK) return result;
    if (!delivered) return NT_RESULT_INVALID_ARGUMENT;
    size_t count = 0;
    NtEventIntentView intent;
    tree->busy = true;
    while (count < budget && nt_tree_peek_event_intent(tree, &intent)) {
        for (const Subscription *sub = tree->schedule->subscriptions; sub; sub = sub->next)
            if (sub->kind == intent.intent.kind) sub->receive(sub->context, sub->owner, &intent);
        nt_tree_drop_intent(tree);
        ++count;
    }
    tree->busy = false;
    *delivered = count;
    return NT_RESULT_OK;
}

static bool batch_live(const NtTree *tree, const ScheduledBatch *batch)
{
    if (!nt_node_live(tree, batch->owner)) return false;
    for (size_t i = 0; i < batch->count; ++i)
        if (!nt_node_live(tree, batch->assignments[i].property.node)) return false;
    return true;
}

void nt_tree_prune_schedule(NtTree *tree)
{
    if (!tree->schedule) return;
    ScheduledBatch **batch = &tree->schedule->batches;
    while (*batch) {
        if (batch_live(tree, *batch)) batch = &(*batch)->next;
        else drop_batch(tree, batch);
    }
    Subscription **sub = &tree->schedule->subscriptions;
    while (*sub) {
        if (nt_node_live(tree, (*sub)->owner)) sub = &(*sub)->next;
        else drop_subscription(tree, sub);
    }
}

void nt_tree_release_schedule(NtTree *tree)
{
    NtSchedule *schedule = tree->schedule;
    if (!schedule) return;
    while (schedule->batches) drop_batch(tree, &schedule->batches);
    while (schedule->subscriptions) drop_subscription(tree, &schedule->subscriptions);
    tree->schedule = NULL;
    nt_release(&tree->allocator, schedule, sizeof(*schedule), NT_ALIGNMENT(NtSchedule));
}
