#ifndef NOCTURNE_PROPERTY_H
#define NOCTURNE_PROPERTY_H

#include <nocturne/component.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum NtValueKind {
    NT_VALUE_BOOL, NT_VALUE_I64, NT_VALUE_U64, NT_VALUE_REAL, NT_VALUE_TEXT, NT_VALUE_RGBA
} NtValueKind;

typedef struct NtValue {
    NtValueKind kind;
    union { bool flag; int64_t i64; uint64_t u64; double real; NtByteView text; NtRgba rgba; } as;
} NtValue;

/* REAL is finite and TEXT is counted, valid UTF-8. Values never coerce. */
NtResult nt_value_validate(NtValue value);

typedef uint32_t NtPropertyId;
enum {
    NT_PROPERTY_TEXT = 1, NT_PROPERTY_VALUE, NT_PROPERTY_CHECKED,
    NT_PROPERTY_SELECTED, NT_PROPERTY_OFFSET, NT_PROPERTY_FOREGROUND, NT_PROPERTY_BACKGROUND
};
typedef struct NtPropertyRef { NtNodeId node; NtPropertyId id; } NtPropertyRef;
typedef struct NtPropertyInfo {
    NtPropertyId id;
    NtValueKind kind;
    bool writable;
    NtInvalidation invalidation;
} NtPropertyInfo;

typedef NtResult (*NtPropertyRead)(const void *state, NtValue *out);
/* Prepare must preserve the component's observable state, even on success.
 * An unsuccessful prepare cleans its own temporary resources. A successful
 * prepare receives one matching finish, with the same borrowed value. */
typedef NtResult (*NtPropertyPrepare)(const void *state, NtValue value, void **prepared);
typedef void (*NtPropertyFinish)(void *state, NtValue value, void *prepared, bool commit);
typedef struct NtPropertyDescriptor {
    NtPropertyId id;
    NtValueKind kind;
    NtInvalidation invalidation;
    NtPropertyRead read;
    NtPropertyPrepare prepare;
    NtPropertyFinish finish;
} NtPropertyDescriptor;

/* Register one copied table on a live custom mount. IDs are nonzero and
 * unique per mount. read is required. Omit finish for read-only. An infallible
 * writer may omit prepare and receives a NULL preparation.
 * finish is infallible: on commit it changes only this property; on discard
 * it releases preparation resources while preserving component state.
 * Callbacks receive the mounted state. Tree mutations are guarded. */
NtResult nt_tree_define_properties(NtTree *tree, NtNodeId node,
                                   const NtPropertyDescriptor *properties, size_t count);
/* A property provider can dispatch by ID and carries one independent
 * context. The SDK copies the declarations and provider. A successful
 * registration releases the context once, before component disposal.
 * A failed registration leaves the context with the caller. */
typedef struct NtPropertyOutput NtPropertyOutput;
typedef NtResult (*NtPropertyProviderRead)(void *context, NtPropertyId id, NtPropertyOutput *out);
typedef NtResult (*NtPropertyProviderPrepare)(void *context, NtPropertyId id, NtValue value, void **prepared);
typedef void (*NtPropertyProviderFinish)(void *context, NtPropertyId id, NtValue value, void *prepared, bool commit);
typedef void (*NtPropertyProviderRelease)(void *context);
typedef struct NtPropertyProvider {
    void *context;
    NtPropertyProviderRead read;
    NtPropertyProviderPrepare prepare;
    NtPropertyProviderFinish finish;
    NtPropertyProviderRelease release;
} NtPropertyProvider;

/* Set one result during a provider read. The SDK validates the value and
 * owns a copy of returned text. The output is scoped to this callback. */
NtResult nt_property_output_set(NtPropertyOutput *out, NtValue value);
NtResult nt_tree_define_property_provider(NtTree *tree, NtNodeId node,
    const NtPropertyInfo *properties, size_t count, const NtPropertyProvider *provider);

NtResult nt_tree_property_count(const NtTree *tree, NtNodeId node, size_t *out);
NtResult nt_tree_property_at(const NtTree *tree, NtNodeId node, size_t index, NtPropertyInfo *out);
/* TEXT from a descriptor borrows from the mount until its state next changes.
 * Provider TEXT borrows until a later read replaces its bytes or the mount is
 * disposed. Set borrows its input for the call. A text-writing finish must
 * own its bytes. */
NtResult nt_tree_property_get(NtTree *tree, NtPropertyRef property, NtValue *out);
NtResult nt_tree_property_set(NtTree *tree, NtPropertyRef property, NtValue value);

typedef struct NtPropertyAssignment { NtPropertyRef property; NtValue value; } NtPropertyAssignment;
/* Validate unique live targets and copy text before preparing any write.
 * Commit in assignment order after every preparation succeeds. A failed
 * preparation discards earlier preparations in reverse order. A zero-count
 * batch succeeds on a tree that is outside a callback. */
NtResult nt_tree_properties_apply(NtTree *tree, const NtPropertyAssignment *assignments, size_t count);

#ifdef __cplusplus
}
#endif
#endif
