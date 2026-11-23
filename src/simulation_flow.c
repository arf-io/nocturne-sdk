#include "simulation_internal.h"

enum { FLOW_STATES = NT_FLOW_MAX_STAGES + 1, FLOW_SERIES_TERMS = 18 };

typedef struct FlowTransition {
    double column[FLOW_STATES][FLOW_STATES];
    double arrival[FLOW_STATES];
} FlowTransition;

/* State k retains water in reservoir k. The final state absorbs outflow.
 * Normalize each column before squaring again so roundoff cannot create
 * or destroy volume as the sampling interval grows. */
static void normalize(double *values, size_t first, size_t last)
{
    double total = 0;
    for (size_t k = first; k <= last; ++k) total += values[k];
    for (size_t k = first; k <= last; ++k) values[k] /= total;
}

static void square_transition(FlowTransition *transition, size_t last)
{
    FlowTransition next = {0};
    for (size_t from = 0; from <= last; ++from) {
        for (size_t via = from; via <= last; ++via) {
            const double probability = transition->column[from][via];
            for (size_t to = via; to <= last; ++to)
                next.column[from][to] += probability*transition->column[via][to];
        }
        normalize(next.column[from],from,last);
    }
    for (size_t to = 0; to <= last; ++to) {
        next.arrival[to] = transition->arrival[to];
        for (size_t from = 0; from <= to; ++from)
            next.arrival[to] += transition->arrival[from]*transition->column[from][to];
        next.arrival[to] *= .5;
    }
    normalize(next.arrival,0,last);
    *transition = next;
}

/* Uniformize each small interval with nonnegative Poisson weights.
 * Average the first column over injection time to handle a constant inlet. */
static void flow_transition(const double *tau, size_t count, double elapsed, FlowTransition *out)
{
    *out = (FlowTransition){0};
    double shortest = tau[0];
    for (size_t i = 1; i < count; ++i) shortest = fmin(shortest,tau[i]);
    double lambda = elapsed/shortest;
    unsigned squares = 0;
    while (lambda > .5) {
        lambda *= .5;
        ++squares;
    }
    double jump[FLOW_STATES] = {0};
    for (size_t i = 0; i < count; ++i) jump[i] = shortest/tau[i];
    for (size_t from = 0; from <= count; ++from) {
        double states[FLOW_STATES] = {0};
        states[from] = 1;
        double poisson = exp(-lambda);
        for (unsigned term = 0; term < FLOW_SERIES_TERMS; ++term) {
            double average = 0;
            if (from == 0) {
                double piece = poisson/(term+1);
                average = piece;
                for (unsigned j = term+2; j <= FLOW_SERIES_TERMS+1; ++j) {
                    piece *= lambda/j;
                    average += piece;
                }
            }
            for (size_t to = from; to <= count; ++to) {
                out->column[from][to] += poisson*states[to];
                if (!from) out->arrival[to] += average*states[to];
            }
            for (size_t to = count; to > from; --to)
                states[to] = states[to]*(1.0-jump[to]) + states[to-1]*jump[to-1];
            states[from] *= 1.0-jump[from];
            poisson *= lambda/(term+1);
        }
        normalize(out->column[from],from,count);
    }
    normalize(out->arrival,0,count);
    for (unsigned i = 0; i < squares; ++i) square_transition(out,count);
}

static double seconds(NtTimeUs time)
{
    return (double)(time/UINT64_C(1000000)) + (double)(time%UINT64_C(1000000))/1000000.0;
}

static NtFlowPort flow_port(const NtFlow *flow, NtTimeUs time, const double *tau, size_t count)
{
    NtFlowPort result = {0};
    for (size_t i = 0; i < flow->input_count; ++i) {
        const NtFlowInput input = flow->inputs[i];
        if (time < input.begin_us) break;
        if (!input.rate) continue;
        const bool feeding = time < input.end_us;
        const double duration = seconds((feeding ? time : input.end_us)-input.begin_us);
        if (!count) {
            result.volume += duration*input.rate;
            if (feeding) result.rate = input.rate;
            continue;
        }

        FlowTransition injection;
        flow_transition(tau,count,duration,&injection);
        double fraction = injection.arrival[count];
        if (feeding) {
            result.rate += input.rate*injection.column[0][count];
        } else {
            FlowTransition drain;
            flow_transition(tau,count,seconds(time-input.end_us),&drain);
            double retained = 0;
            for (size_t j = 0; j < count; ++j) {
                fraction += injection.arrival[j]*drain.column[j][count];
                retained += injection.arrival[j]*drain.column[j][count-1];
            }
            result.rate += input.rate*((duration*retained)/tau[count-1]);
        }
        result.volume += (duration*input.rate)*fmin(fraction,1.0);
    }
    result.rate = fmin(result.rate,NT_SIMULATION_COORDINATE_LIMIT);
    return result;
}

static bool valid_flow(const NtFlow *flow)
{
    for (size_t i = 0; i < flow->stage_count; ++i) {
        if (flow->stages[i].transit_us > NT_FLOW_STAGE_TIME_LIMIT ||
            flow->stages[i].residence_us > NT_FLOW_STAGE_TIME_LIMIT) return false;
    }
    for (size_t i = 0; i < flow->input_count; ++i) {
        const NtFlowInput input = flow->inputs[i];
        if (!nt_simulation_bounded(input.rate) || input.rate < 0 || input.end_us <= input.begin_us ||
            (i && input.begin_us < flow->inputs[i-1].end_us)) return false;
    }
    return true;
}

NtResult nt_flow_sample(const NtFlow *flow, NtTimeUs time, NtFlowSample *out)
{
    if (!flow || !out || (flow->stage_count && !flow->stages) ||
        (flow->input_count && !flow->inputs)) return NT_RESULT_INVALID_ARGUMENT;
    if (flow->stage_count > NT_FLOW_MAX_STAGES || flow->input_count > NT_FLOW_MAX_INPUTS)
        return NT_RESULT_OUT_OF_RANGE;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    if (!valid_flow(flow)) return nt_simulation_finish(&saved,NT_RESULT_INVALID_ARGUMENT);
    NtFlowSample result = {.stage_count=flow->stage_count};
    result.ports[0] = flow_port(flow,time,NULL,0);
    double tau[NT_FLOW_MAX_STAGES];
    size_t reservoirs = 0;
    NtTimeUs delay = 0;
    for (size_t i = 0; i < flow->stage_count; ++i) {
        const NtFlowStage stage = flow->stages[i];
        delay += stage.transit_us;
        if (stage.residence_us) tau[reservoirs++] = seconds(stage.residence_us);
        if (time >= delay) result.ports[i+1] = flow_port(flow,time-delay,tau,reservoirs);
        result.ports[i+1].volume = fmin(result.ports[i].volume,result.ports[i+1].volume);
        result.retained[i] = result.ports[i].volume-result.ports[i+1].volume;
    }
    const NtResult status = nt_simulation_finish(&saved,NT_RESULT_OK);
    if (status == NT_RESULT_OK) *out = result;
    return status;
}

NtResult nt_flow_section_sample(const NtFlowSection *section, double rate, NtFlowSectionSample *out)
{
    if (!section || !out) return NT_RESULT_INVALID_ARGUMENT;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    if (!nt_simulation_bounded(section->width) || section->width < 1e-6 ||
        !nt_simulation_bounded(section->speed) || section->speed < 1e-6 ||
        !nt_simulation_bounded(section->maximum_depth) || section->maximum_depth < 0 ||
        !nt_simulation_bounded(rate) || rate < 0)
        return nt_simulation_finish(&saved,NT_RESULT_INVALID_ARGUMENT);
    NtSpatialExact per_depth, cap, capacity, offered, one;
    nt_simulation_product(&per_depth,section->width,section->speed);
    nt_spatial_exact_number(&cap,section->maximum_depth,0);
    nt_spatial_exact_multiply(&capacity,&per_depth,&cap);
    nt_spatial_exact_number(&offered,rate,0);
    NtFlowSectionSample result = {0};
    if (nt_spatial_exact_compare(&offered,&capacity) > 0) {
        NtSpatialExact excess;
        nt_spatial_exact_number(&one,1,0);
        nt_spatial_exact_subtract(&excess,&offered,&capacity);
        result.depth = section->maximum_depth;
        result.carried_rate = nt_spatial_exact_quotient(&capacity,&one);
        result.excess_rate = nt_spatial_exact_quotient(&excess,&one);
    } else {
        result.depth = nt_spatial_exact_quotient(&offered,&per_depth);
        result.carried_rate = rate;
    }
    const NtResult status = nt_simulation_finish(&saved,NT_RESULT_OK);
    if (status == NT_RESULT_OK) *out = result;
    return status;
}

static bool section_direction_is_unit(NtVec3 direction)
{
    NtSpatialExact xx, yy, zz, xy, square, scale, scaled, lower, upper;
    nt_simulation_product(&xx,direction.x,direction.x);
    nt_simulation_product(&yy,direction.y,direction.y);
    nt_simulation_product(&zz,direction.z,direction.z);
    nt_spatial_exact_add(&xy,&xx,&yy);
    nt_spatial_exact_add(&square,&xy,&zz);
    /* Squared bounds on the exact length times 10^12. */
    nt_simulation_product(&scale,1e12,1e12);
    nt_spatial_exact_multiply(&scaled,&square,&scale);
    nt_simulation_product(&lower,1e12-1,1e12-1);
    nt_simulation_product(&upper,1e12+1,1e12+1);
    return nt_spatial_exact_compare(&scaled,&lower) >= 0 && nt_spatial_exact_compare(&scaled,&upper) <= 0;
}

static double displaced(double dry, double component, const NtSpatialExact *offset)
{
    NtSpatialExact point, direction, delta, result, one;
    nt_spatial_exact_number(&point,dry,0);
    nt_spatial_exact_number(&direction,component,0);
    nt_spatial_exact_multiply(&delta,&direction,offset);
    nt_spatial_exact_add(&result,&point,&delta);
    nt_spatial_exact_number(&one,1,0);
    return nt_spatial_exact_quotient(&result,&one);
}

NtResult nt_flow_section_point(NtVec3 dry, NtVec3 direction, double depth, double weight, NtVec3 *out)
{
    if (!out) return NT_RESULT_INVALID_ARGUMENT;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    if (!nt_simulation_vector(dry) || !nt_simulation_vector(direction) ||
        !nt_simulation_bounded(depth) || depth < 0 || !isfinite(weight) || weight < 0 || weight > 1)
        return nt_simulation_finish(&saved,NT_RESULT_INVALID_ARGUMENT);
    if (!section_direction_is_unit(direction))
        return nt_simulation_finish(&saved,NT_RESULT_INVALID_ARGUMENT);
    NtSpatialExact offset;
    nt_simulation_product(&offset,depth,weight);
    const NtVec3 result = {
        displaced(dry.x,direction.x,&offset),displaced(dry.y,direction.y,&offset),
        displaced(dry.z,direction.z,&offset)
    };
    const NtResult status = nt_simulation_finish(&saved,NT_RESULT_OK);
    if (status == NT_RESULT_OK) *out = result;
    return status;
}
