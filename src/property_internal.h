#ifndef NOCTURNE_INTERNAL_PROPERTY_H
#define NOCTURNE_INTERNAL_PROPERTY_H

#include <nocturne/property.h>
#include "component_internal.h"

static inline void nt_property_stock_table(NtTree *tree, NtNodeId id,
                                              const NtPropertyDescriptor *properties, size_t count)
{
    tree->nodes[id.slot].properties = properties;
    tree->nodes[id.slot].property_count = count;
}

void nt_property_release_table(const NtTree *tree, NtNode *node);
void nt_property_release_provider(const NtTree *tree, NtNode *node);
typedef struct NtAttachedProperty {
    NtPropertyInfo info;
    const NtPropertyDescriptor *descriptor;
    void *state;
    const NtPropertyProvider *provider;
    NtByteView *text;
} NtAttachedProperty;

typedef struct NtPropertyWrite {
    NtAttachedProperty property;
    NtValue value;
    void *prepared;
} NtPropertyWrite;

/* The tree is guarded, and all attached properties, types and values are valid. */
NtResult nt_property_write_batch(NtTree *tree, NtPropertyWrite *writes, size_t count);
/* The tree is guarded. Validate and copy a caller's full assignment list. */
NtResult nt_property_apply_batch(NtTree *tree, const NtPropertyAssignment *assignments, size_t count);
NtResult nt_property_check_assignments(const NtTree *tree, const NtPropertyAssignment *assignments,
                                       size_t count, NtPropertyWrite *writes);

NtResult nt_property_lookup(const NtTree *tree, NtPropertyRef ref, bool write, NtAttachedProperty *out);
NtResult nt_property_read(NtAttachedProperty property, NtValue *out);
bool nt_value_kind_valid(NtValueKind kind);
NtResult nt_value_require_kind(NtValue value, NtValueKind kind);
/* The source is validated. These helpers own and release copied TEXT bytes. */
NtResult nt_value_copy(const NtAllocator *allocator, NtValue value, NtValue *out);
void nt_value_release(const NtAllocator *allocator, NtValue *value);

#endif
