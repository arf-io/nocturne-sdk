#ifndef NOCTURNE_ABI_H
#define NOCTURNE_ABI_H

#include <nocturne/base.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NT_ABI_VERSION UINT32_C(1)

/* Version of the C ABI implemented by the linked portable core. */
uint32_t nt_abi_version(void);

/* Static C identifier for a known NtResult value, or NULL for an unknown
 * number. Bindings can preserve the number independently of this lookup. */
const char *nt_result_name(int32_t result);

#ifdef __cplusplus
}
#endif
#endif
