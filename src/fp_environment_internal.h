#ifndef NOCTURNE_INTERNAL_FP_ENVIRONMENT_H
#define NOCTURNE_INTERNAL_FP_ENVIRONMENT_H

#include <stdbool.h>
#include <fenv.h>

static inline bool nt_begin_standard_fp(fenv_t *saved)
{
#if defined(__wasm__)
    /* WebAssembly scalar operations have a fixed nearest-even mode. */
    (void)saved;
    return true;
#else
    if (fegetenv(saved) != 0) return false;
    if (fesetenv(FE_DFL_ENV) == 0 &&
        (fegetround() == FE_TONEAREST || fesetround(FE_TONEAREST) == 0)) return true;
    (void)fesetenv(saved);
    return false;
#endif
}

static inline bool nt_finish_standard_fp(const fenv_t *saved)
{
#if defined(__wasm__)
    (void)saved;
    return true;
#else
    return fesetenv(saved) == 0;
#endif
}

#endif
