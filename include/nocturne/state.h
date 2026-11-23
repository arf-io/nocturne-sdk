#ifndef NOCTURNE_STATE_H
#define NOCTURNE_STATE_H

#include <nocturne/property.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NtStateSnapshot NtStateSnapshot;
enum { NT_STATE_SCHEMA_VERSION = 1 };

/* Capture explicitly listed writable properties and own their encoded
 * values. The supplied allocator must outlive the snapshot. */
NtResult nt_tree_state_capture(NtTree *tree, const NtPropertyRef *properties, size_t count,
                                const NtAllocator *allocator, NtStateSnapshot **out);
/* Validate and copy a versioned snapshot. Its bytes may contain NULs. */
NtResult nt_state_snapshot_import(const NtAllocator *allocator, NtByteView bytes, NtStateSnapshot **out);
NtByteView nt_state_snapshot_bytes(const NtStateSnapshot *snapshot);
void nt_state_snapshot_destroy(NtStateSnapshot *snapshot);

/* Restore onto corresponding live writable properties in snapshot order.
 * IDs and types must agree. The caller chooses each destination mount. */
NtResult nt_tree_state_restore(NtTree *tree, const NtPropertyRef *properties, size_t count,
                                const NtStateSnapshot *snapshot);

#ifdef __cplusplus
}
#endif
#endif
