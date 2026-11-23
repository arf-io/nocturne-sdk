#ifndef NOCTURNE_CONSUMER_INDICATOR_H
#define NOCTURNE_CONSUMER_INDICATOR_H

#include <nocturne/component.h>

typedef struct IndicatorStats {
    bool defer_paint;
    unsigned disposed, updates, focus_gains, focus_losses;
} IndicatorStats;

NtResult indicator_mount(NtTree *tree, NtNodeId parent, IndicatorStats *stats, NtNodeId *out);

#endif
