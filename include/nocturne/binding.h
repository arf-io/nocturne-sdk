#ifndef NOCTURNE_BINDING_H
#define NOCTURNE_BINDING_H

#include <nocturne/property.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum NtBindingSource { NT_BINDING_INPUT, NT_BINDING_PROPERTY, NT_BINDING_COMPUTED } NtBindingSource;
typedef NtResult (*NtBindingCompute)(void *context, const NtValue *inputs, size_t count, NtValue *out);
typedef struct NtBindingValue {
    NtValueKind kind;
    NtBindingSource source;
    union {
        NtPropertyRef property;
        struct {
            const size_t *dependencies;       /* Indices into the declared values. */
            size_t count;
            NtBindingCompute evaluate;
            void *context;
        } computed;
    } from;
} NtBindingValue;
typedef struct NtBindingTarget { NtPropertyRef property; size_t value; } NtBindingTarget;
typedef struct NtBindingGraph NtBindingGraph;

/* Copy descriptors and dependency arrays. Borrow compute contexts. Graphs
 * reject cycles and duplicate target properties. Remove/recycle a referenced
 * node or destroy the tree and the graph becomes stale. Caller owns the graph
 * and must destroy it; the tree allocator context must outlive the graph. */
NtResult nt_binding_graph_create(NtTree *tree, const NtBindingValue *values, size_t value_count,
                                 const NtBindingTarget *targets, size_t target_count,
                                 NtBindingGraph **out);
NtResult nt_binding_graph_destroy(NtBindingGraph *graph);
/* Supply INPUT values in declaration order. Every property source reads
 * pre-commit state. Compute functions have no state-changing side effects and
 * may return borrowed TEXT valid until its immediate SDK copy after return.
 * All values and property preparations succeed before any commit is called.
 * A stale endpoint fails before any property/compute callback runs. */
NtResult nt_binding_graph_apply(NtBindingGraph *graph, const NtValue *inputs, size_t count);

#ifdef __cplusplus
}
#endif
#endif
