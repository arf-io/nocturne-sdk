#ifndef NOCTURNE_SIMULATION_INTERNAL_H
#define NOCTURNE_SIMULATION_INTERNAL_H

#include <nocturne/simulation.h>
#include <math.h>
#include "fp_environment_internal.h"
#include "spatial_exact_internal.h"

#if defined(__clang__) && !defined(__wasm__)
#pragma STDC FENV_ACCESS ON
#elif defined(_MSC_VER)
#pragma fenv_access(on)
#endif

#define NT_SIMULATION_COORDINATE_LIMIT 1000000.0
#define NT_FLOW_STAGE_TIME_LIMIT UINT64_C(86400000000)
#define NT_SIMULATION_STEP_LIMIT UINT64_C(1000000)

/* Keep exact-arithmetic phases in separate frames so they can reuse stack
 * storage in optimized builds. */
#if defined(_MSC_VER)
#define NT_SIMULATION_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define NT_SIMULATION_NOINLINE __attribute__((noinline))
#else
#define NT_SIMULATION_NOINLINE
#endif

typedef struct NtSimulationDouble { double high, low; } NtSimulationDouble;

NtSimulationDouble nt_simulation_ratio(const NtSpatialExact *n, const NtSpatialExact *d);
void nt_simulation_double_exact(NtSpatialExact *out, NtSimulationDouble value, int power);
void nt_simulation_decay(NtSimulationDouble exponent, NtSpatialExact *out);
void nt_simulation_small_response(const NtSpatialExact *n, const NtSpatialExact *d,
                                  NtSpatialExact *numerator, NtSpatialExact *denominator);
void nt_simulation_loss(const NtSpatialExact *numerator, const NtSpatialExact *denominator,
                        NtSpatialExact *loss, NtSpatialExact *unit);
void nt_simulation_exponential_loss(double rate, NtTimeUs elapsed, NtSpatialExact *loss, NtSpatialExact *unit);
bool nt_simulation_valid_body(const NtFloatingBody *body);

static inline bool nt_simulation_bounded(double value)
{
    return isfinite(value) && fabs(value) <= NT_SIMULATION_COORDINATE_LIMIT;
}

static inline bool nt_simulation_vector(NtVec3 value)
{
    return nt_simulation_bounded(value.x) && nt_simulation_bounded(value.y) &&
        nt_simulation_bounded(value.z);
}

static inline void nt_simulation_product(NtSpatialExact *out, double a, double b)
{
    NtSpatialExact left, right;
    nt_spatial_exact_number(&left,a,0);
    nt_spatial_exact_number(&right,b,0);
    nt_spatial_exact_multiply(out,&left,&right);
}

static inline void nt_simulation_exact_vec(NtVec3 point, NtSpatialExact out[3])
{
    nt_spatial_exact_number(&out[0],point.x,0);
    nt_spatial_exact_number(&out[1],point.y,0);
    nt_spatial_exact_number(&out[2],point.z,0);
}

static inline void nt_simulation_accumulate(NtSpatialExact *total, const NtSpatialExact *value)
{
    NtSpatialExact sum;
    nt_spatial_exact_add(&sum,total,value);
    nt_spatial_exact_copy(total,&sum);
}

static inline NtResult nt_simulation_finish(const fenv_t *saved, NtResult result)
{
    return nt_finish_standard_fp(saved) ? result : NT_RESULT_UNSUPPORTED_CAPABILITY;
}

#endif
