#ifndef NOCTURNE_INTERNAL_ALLOCATOR_H
#define NOCTURNE_INTERNAL_ALLOCATOR_H

#include <nocturne/base.h>
#include <stdlib.h>

#define NT_DECLARE_ALIGNMENT(type) \
    typedef struct NtAligned##type { char padding; type value; } NtAligned##type
#define NT_ALIGNMENT(type) offsetof(NtAligned##type, value)

static inline bool nt_allocator_get(const NtAllocator *provided, NtAllocator *out)
{
    if (provided && (!provided->allocate || !provided->release)) return false;
    *out = provided ? *provided : (NtAllocator){0};
    return true;
}

static inline void *nt_allocate(const NtAllocator *allocator, size_t bytes, size_t alignment)
{
    return allocator->allocate ? allocator->allocate(allocator->context, bytes, alignment)
                               : malloc(bytes);
}

static inline void nt_release(const NtAllocator *allocator, void *address,
                              size_t bytes, size_t alignment)
{
    if (!address) return;
    if (allocator->release)
        allocator->release(allocator->context, address, bytes, alignment);
    else
        free(address);
}

#endif
