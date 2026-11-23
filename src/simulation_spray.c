#include "simulation_internal.h"
#include "seed_internal.h"

#define NT_SPRAY_VOLUME_LIMIT 1e18
#define NT_DROPLET_VOLUME_MIN 1e-18

static bool valid_spray(const NtSprayOptions *options)
{
    return nt_simulation_vector(options->origin) && nt_simulation_vector(options->velocity) &&
        nt_simulation_vector(options->velocity_spread) && nt_simulation_vector(options->acceleration) &&
        options->velocity_spread.x >= 0 && options->velocity_spread.y >= 0 && options->velocity_spread.z >= 0 &&
        isfinite(options->droplet_volume) && options->droplet_volume >= NT_DROPLET_VOLUME_MIN &&
        options->droplet_volume <= NT_SPRAY_VOLUME_LIMIT &&
        options->lifetime_us > 0 && options->lifetime_us <= NT_FLOW_STAGE_TIME_LIMIT;
}

NtResult nt_spray_emit(const NtSprayOptions *options, double available, size_t capacity,
                      uint64_t first_id, NtTimeUs birth, NtSprayBatch *out)
{
    if (!options || !out) return NT_RESULT_INVALID_ARGUMENT;
    if (capacity > NT_SPRAY_MAX_DROPLETS) return NT_RESULT_OUT_OF_RANGE;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    if (!valid_spray(options) || !isfinite(available) || available < 0 || available > NT_SPRAY_VOLUME_LIMIT)
        return nt_simulation_finish(&saved,NT_RESULT_INVALID_ARGUMENT);
    size_t low = 0, high = capacity + 1;
    while (high-low > 1) {
        const size_t mid = low + (high-low)/2;
        if (fma(-(double)mid,options->droplet_volume,available) >= 0) low = mid;
        else high = mid;
    }
    if (low && (uint64_t)(low-1) > UINT64_MAX-first_id)
        return nt_simulation_finish(&saved,NT_RESULT_OUT_OF_RANGE);
    const NtSprayBatch batch = {*options,birth,first_id,low,low*options->droplet_volume,
                               fma(-(double)low,options->droplet_volume,available)};
    const NtResult status = nt_simulation_finish(&saved,NT_RESULT_OK);
    if (status == NT_RESULT_OK) *out = batch;
    return status;
}

static double speed(const NtSprayOptions *options, uint64_t id, uint64_t axis,
                    double nominal, double spread)
{
    const uint64_t seed = nt_seed_bits(options->seed,options->stream,id);
    return fma(2*nt_seed_unit(seed,axis,0)-1.0,spread,nominal);
}

static double position(double origin, double velocity, double acceleration, double seconds)
{
    return fma(fma(.5*seconds,acceleration,velocity),seconds,origin);
}

NtResult nt_spray_sample(const NtSprayBatch *batch, size_t index, NtTimeUs now,
                         bool *active, NtSpraySample *out)
{
    if (!batch || !active || !out) return NT_RESULT_INVALID_ARGUMENT;
    if (batch->count > NT_SPRAY_MAX_DROPLETS ||
        (batch->count && (uint64_t)(batch->count-1) > UINT64_MAX-batch->first_id) ||
        index >= batch->count) return NT_RESULT_OUT_OF_RANGE;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    const NtSprayOptions *options = &batch->options;
    if (!valid_spray(options)) return nt_simulation_finish(&saved,NT_RESULT_INVALID_ARGUMENT);
    if (now < batch->birth_us || now-batch->birth_us >= options->lifetime_us) {
        const NtResult status = nt_simulation_finish(&saved,NT_RESULT_OK);
        if (status == NT_RESULT_OK) *active = false;
        return status;
    }
    const NtTimeUs elapsed = now-batch->birth_us;
    const double seconds = (double)elapsed/1000000.0;
    const uint64_t id = batch->first_id+index;
    const NtVec3 v = {speed(options,id,0,options->velocity.x,options->velocity_spread.x),
                      speed(options,id,1,options->velocity.y,options->velocity_spread.y),
                      speed(options,id,2,options->velocity.z,options->velocity_spread.z)};
    const NtVec3 p = options->origin, a = options->acceleration;
    const NtSpraySample sample = {
        id,
        {position(p.x,v.x,a.x,seconds),position(p.y,v.y,a.y,seconds),position(p.z,v.z,a.z,seconds)},
        {fma(a.x,seconds,v.x),fma(a.y,seconds,v.y),fma(a.z,seconds,v.z)},
        options->droplet_volume,(double)elapsed/(double)options->lifetime_us
    };
    const NtResult status = nt_simulation_finish(&saved,NT_RESULT_OK);
    if (status == NT_RESULT_OK) {
        *out = sample;
        *active = true;
    }
    return status;
}
