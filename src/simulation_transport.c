#include "simulation_internal.h"
#include <assert.h>
#include <float.h>
#include <stdlib.h>
#include <string.h>

#define TRANSPORT_CAPACITY_MIN 1e-6
#define TRANSPORT_CAPACITY_MAX 1e12
#define TRANSPORT_ENERGY_MAX 1e24
#define TRANSPORT_POWER_MAX 1e18

typedef struct ScaledAmount {
    NtSimulationDouble fraction;
    int exponent;
} ScaledAmount;

typedef struct RoutedInventory { ScaledAmount water, foam, energy; } RoutedInventory;
typedef struct ExactInventory { NtSpatialExact water, foam, energy; } ExactInventory;
typedef struct RouteErrors { NtSpatialExact water, energy; } RouteErrors;

typedef struct Route {
    RoutedInventory amount;
    size_t next;
} Route;

typedef struct Routes {
    NtTransportCell base[NT_TRANSPORT_MAX_CELLS];
    RoutedInventory retained[NT_TRANSPORT_MAX_CELLS];
    size_t first[NT_TRANSPORT_MAX_CELLS+1];
    Route incoming[NT_TRANSPORT_MAX_EDGES];
    size_t count;
} Routes;

typedef struct RouteScale { NtSpatialExact numerator, denominator; } RouteScale;
typedef struct TransportScratch TransportScratch;

typedef struct RouteInputs {
    const NtTransportOptions *options;
    const NtTransportCell *cells;
    NtTimeUs elapsed;
    TransportScratch *scratch;
} RouteInputs;

static bool amount(double value, double maximum)
{
    return isfinite(value) && value >= 0 && value <= maximum;
}

static bool valid_store(NtThermalStore store)
{
    return amount(store.heat_capacity,TRANSPORT_CAPACITY_MAX) &&
        store.heat_capacity >= TRANSPORT_CAPACITY_MIN &&
        amount(store.energy,TRANSPORT_ENERGY_MAX);
}

static double add_scaled(double initial, const NtSpatialExact *increment,
                          const NtSpatialExact *denominator)
{
    NtSpatialExact base, scaled, total;
    nt_spatial_exact_number(&base,initial,0);
    nt_spatial_exact_multiply(&scaled,&base,denominator);
    nt_spatial_exact_add(&total,&scaled,increment);
    return nt_spatial_exact_quotient(&total,denominator);
}

static void contact_loss(NtSpatialExact *out, double conductance, NtTimeUs elapsed,
                          const NtSpatialExact *ca, const NtSpatialExact *cb,
                          const NtSpatialExact *sum, double exponent);

static void conduct(double first_capacity, double second_capacity, double conductance,
                    NtTimeUs elapsed, double *first_energy, double *second_energy)
{
    if (!conductance) return;
    const double seconds = (double)elapsed/1000000.0;
    const double exponent = (conductance*seconds)*(1.0/first_capacity+1.0/second_capacity);
    const double fraction = -expm1(-exponent);
    const double first = *first_energy, second = *second_energy;
    NtSpatialExact first_product, second_product, difference, ca, cb, capacity;
    nt_simulation_product(&first_product,first,second_capacity);
    nt_simulation_product(&second_product,second,first_capacity);
    nt_spatial_exact_subtract(&difference,&second_product,&first_product);
    if (!nt_spatial_exact_sign(&difference)) return;
    nt_spatial_exact_number(&ca,first_capacity,0);
    nt_spatial_exact_number(&cb,second_capacity,0);
    if (exponent <= DBL_EPSILON*.25) {
        /* The first-order transfer cancels the sum of heat capacities.
         * Keep the tiny conductance in the rational product until rounding. */
        NtSpatialExact time, interval, impulse, product, unit;
        nt_simulation_product(&time,conductance,(double)elapsed);
        nt_spatial_exact_multiply(&impulse,&difference,&time);
        nt_spatial_exact_multiply(&product,&ca,&cb);
        nt_spatial_exact_number(&unit,1000000,0);
        nt_spatial_exact_multiply(&interval,&product,&unit);
        *first_energy = add_scaled(first,&impulse,&interval);
        nt_spatial_exact_negate(&impulse);
        *second_energy = add_scaled(second,&impulse,&interval);
        return;
    }
    nt_spatial_exact_add(&capacity,&ca,&cb);
    const double equilibrium_transfer = nt_spatial_exact_quotient(&difference,&capacity);
    const double transfer = fraction*equilibrium_transfer;
    NtSpatialExact loss, impulse;
    if (first >= .5*TRANSPORT_ENERGY_MAX || second >= .5*TRANSPORT_ENERGY_MAX)
        contact_loss(&loss,conductance,elapsed,&ca,&cb,&capacity,exponent);
    else nt_spatial_exact_number(&loss,fraction,0);
    nt_spatial_exact_multiply(&impulse,&difference,&loss);
    *first_energy = add_scaled(first,&impulse,&capacity);
    nt_spatial_exact_negate(&impulse);
    *second_energy = add_scaled(second,&impulse,&capacity);

    /* A store losing most of its energy needs the two surviving positive
     * contributions. An increment is more accurate for the other store. */
    const bool first_loses_most = transfer < -.5*first;
    if (first_loses_most || transfer > .5*second) {
        NtSpatialExact ea, eb, energy, weighted, refined_loss, one, retained;
        NtSpatialExact mixed, original, original_scaled, saved, result;
        nt_spatial_exact_number(&ea,first,0);
        nt_spatial_exact_number(&eb,second,0);
        nt_spatial_exact_add(&energy,&ea,&eb);
        nt_spatial_exact_multiply(&weighted,&energy,first_loses_most ? &ca : &cb);
        contact_loss(&refined_loss,conductance,elapsed,&ca,&cb,&capacity,exponent);
        nt_spatial_exact_number(&one,1,0);
        nt_spatial_exact_subtract(&retained,&one,&refined_loss);
        nt_spatial_exact_multiply(&mixed,&weighted,&refined_loss);
        nt_spatial_exact_number(&original,first_loses_most ? first : second,0);
        nt_spatial_exact_multiply(&original_scaled,&original,&capacity);
        nt_spatial_exact_multiply(&saved,&original_scaled,&retained);
        nt_spatial_exact_add(&result,&mixed,&saved);
        double *destination = first_loses_most ? first_energy : second_energy;
        *destination = nt_spatial_exact_quotient(&result,&capacity);
    }
}

NtResult nt_thermal_exchange(const NtThermalPair *stores, double conductance,
                               NtTimeUs elapsed, NtThermalPair *out)
{
    if (!stores || !out) return NT_RESULT_INVALID_ARGUMENT;
    if (elapsed > NT_SIMULATION_STEP_LIMIT) return NT_RESULT_OUT_OF_RANGE;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    if (!valid_store(stores->first) || !valid_store(stores->second) ||
        !amount(conductance,NT_SIMULATION_COORDINATE_LIMIT))
        return nt_simulation_finish(&saved,NT_RESULT_INVALID_ARGUMENT);
    NtThermalPair result = *stores;
    if (elapsed && conductance) {
        conduct(result.first.heat_capacity,result.second.heat_capacity,
                conductance,elapsed,&result.first.energy,&result.second.energy);
    }
    const bool valid = valid_store(result.first) && valid_store(result.second);
    const NtResult status = nt_simulation_finish(&saved,valid ? NT_RESULT_OK : NT_RESULT_OUT_OF_RANGE);
    if (status == NT_RESULT_OK) *out = result;
    return status;
}

static double cell_capacity(const NtTransportCellOptions *options, double water, double specific_heat)
{
    return fma(water,specific_heat,options->substrate_heat_capacity);
}

static bool valid_cell(const NtTransportCellOptions *o, NtTransportCell cell, double specific_heat)
{
    const double limit = NT_SIMULATION_COORDINATE_LIMIT;
    return amount(o->water_capacity,limit) && amount(o->foam_capacity,limit) &&
        amount(o->substrate_heat_capacity,limit) && o->substrate_heat_capacity >= TRANSPORT_CAPACITY_MIN &&
        cell_capacity(o,o->water_capacity,specific_heat) <= TRANSPORT_CAPACITY_MAX &&
        amount(cell.water,o->water_capacity) && amount(cell.foam,o->foam_capacity) &&
        amount(cell.energy,TRANSPORT_ENERGY_MAX) &&
        amount(o->inlet_rate,limit) && amount(o->inlet_temperature,limit) &&
        amount(o->foam_generation,limit) && amount(o->foam_decay,limit) &&
        amount(o->evaporation,limit) && amount(o->latent_heat,TRANSPORT_CAPACITY_MAX) &&
        amount(o->ambient_temperature,limit) && amount(o->ambient_conductance,limit) &&
        isfinite(o->power) && fabs(o->power) <= TRANSPORT_POWER_MAX;
}

static bool valid_edge(const NtTransportEdge *edge, size_t cell_count)
{
    const double limit = NT_SIMULATION_COORDINATE_LIMIT;
    if (edge->source >= cell_count || edge->source == edge->destination ||
        !amount(edge->water_rate,limit) || !amount(edge->foam_rate,limit) ||
        !amount(edge->conductance,limit)) return false;
    if (edge->destination == NT_TRANSPORT_DRAIN) return edge->conductance == 0;
    return edge->destination < cell_count;
}

/* Normalize the quotient before rounding the two binary64 components.
 * Its exponent can be smaller than the binary64 subnormal range. */
static ScaledAmount routed_amount(const NtSpatialExact *n, const NtSpatialExact *d)
{
    if (!n->count) return (ScaledAmount){{0,0},0};
    const int shift = n->offset+(int)n->count-d->offset-(int)d->count;
    NtSpatialExact scaled;
    nt_spatial_exact_copy(&scaled,n);
    scaled.offset -= shift;
    return (ScaledAmount){nt_simulation_ratio(&scaled,d),32*shift};
}

static void sum_rate(NtSpatialExact *total, double rate)
{
    if (!rate) return;
    NtSpatialExact contribution, sum;
    nt_spatial_exact_number(&contribution,rate,0);
    nt_spatial_exact_add(&sum,total,&contribution);
    nt_spatial_exact_copy(total,&sum);
}

static void route_scale(RouteScale *scale, const NtSpatialExact *rate, double limit,
                        double multiplier, NtTimeUs elapsed, const NtSpatialExact *micros)
{
    NtSpatialExact requested, time, maximum;
    nt_spatial_exact_number(&time,(double)elapsed,0);
    nt_spatial_exact_multiply(&requested,&time,rate);
    nt_simulation_product(&maximum,limit,1000000);
    if (nt_spatial_exact_compare(&requested,&maximum)>0) {
        nt_simulation_product(&scale->numerator,limit,multiplier);
        nt_spatial_exact_copy(&scale->denominator,rate);
    } else {
        nt_simulation_product(&scale->numerator,(double)elapsed,multiplier);
        nt_spatial_exact_copy(&scale->denominator,micros);
    }
}

static void exact_cell_capacity(NtSpatialExact *out, const NtTransportCellOptions *local,
                                double volume, double specific_heat)
{
    NtSpatialExact wet, substrate;
    nt_simulation_product(&wet,volume,specific_heat);
    nt_spatial_exact_number(&substrate,local->substrate_heat_capacity,0);
    nt_spatial_exact_add(out,&wet,&substrate);
}

static void route_heat_scale(RouteScale *scale, const RouteScale *water, NtTransportCell start,
                             const NtTransportCellOptions *local, double specific_heat)
{
    NtSpatialExact energy_capacity, capacity;
    nt_simulation_product(&energy_capacity,start.energy,specific_heat);
    exact_cell_capacity(&capacity,local,start.water,specific_heat);
    nt_spatial_exact_multiply(&scale->numerator,&water->numerator,&energy_capacity);
    nt_spatial_exact_multiply(&scale->denominator,&water->denominator,&capacity);
}

static ScaledAmount transferred_amount(const NtSpatialExact *rate, const RouteScale *scale)
{
    NtSpatialExact amount;
    nt_spatial_exact_multiply(&amount,rate,&scale->numerator);
    return routed_amount(&amount,&scale->denominator);
}

static ScaledAmount retained_amount(double *base, const NtSpatialExact *rate, const RouteScale *scale)
{
    NtSpatialExact outgoing, initial, available, remaining;
    nt_spatial_exact_multiply(&outgoing,rate,&scale->numerator);
    nt_spatial_exact_number(&initial,*base,0);
    nt_spatial_exact_multiply(&available,&initial,&scale->denominator);
    /* Preserve a small removal from a large store or a small remainder
     * after a large removal, whichever is better conditioned. */
    if (nt_spatial_exact_compare_scaled(&outgoing,&available,.5)>0) {
        nt_spatial_exact_subtract(&remaining,&available,&outgoing);
        *base = 0;
        return routed_amount(&remaining,&scale->denominator);
    }
    nt_spatial_exact_negate(&outgoing);
    return routed_amount(&outgoing,&scale->denominator);
}

static void plan_donor(const NtTransportOptions *o, NtTransportCell start, size_t donor,
                       NtTimeUs elapsed, size_t first, const size_t *next, Routes *routes)
{
    NtTransportCell *base = routes->base+donor;
    *base = start;
    if (!first) return;

    /* Group parallel rates before dividing by the donor total. This leaves
     * the original edge order available for the separate conductive step. */
    size_t grouped[NT_TRANSPORT_MAX_CELLS+1] = {0};
    size_t next_in_group[NT_TRANSPORT_MAX_EDGES];
    NtSpatialExact total_water, total_foam;
    nt_spatial_exact_number(&total_water,0,0);
    nt_spatial_exact_number(&total_foam,0,0);
    for (size_t node=first;node;node=next[node-1]) {
        const NtTransportEdge *edge = o->edges+node-1;
        sum_rate(&total_water,edge->water_rate);
        sum_rate(&total_foam,edge->foam_rate);
        next_in_group[node-1] = grouped[edge->destination];
        grouped[edge->destination] = node;
    }

    NtSpatialExact micros;
    nt_spatial_exact_number(&micros,1000000,0);
    RouteScale water, foam, energy;
    route_scale(&water,&total_water,start.water,1,elapsed,&micros);
    route_scale(&foam,&total_foam,1,start.foam,elapsed,&micros);
    route_heat_scale(&energy,&water,start,o->cells+donor,o->water_heat_capacity);
    routes->retained[donor] = (RoutedInventory){
        retained_amount(&base->water,&total_water,&water),
        retained_amount(&base->foam,&total_foam,&foam),
        retained_amount(&base->energy,&total_water,&energy)
    };

    for (size_t destination=0;destination<=NT_TRANSPORT_MAX_CELLS;++destination) {
        if (!grouped[destination]) continue;
        NtSpatialExact water_rate, foam_rate;
        nt_spatial_exact_number(&water_rate,0,0);
        nt_spatial_exact_number(&foam_rate,0,0);
        for (size_t node=grouped[destination];node;node=next_in_group[node-1]) {
            const NtTransportEdge *edge = o->edges+node-1;
            sum_rate(&water_rate,edge->water_rate);
            sum_rate(&foam_rate,edge->foam_rate);
        }
        Route *route = routes->incoming+routes->count++;
        route->amount = (RoutedInventory){
            transferred_amount(&water_rate,&water),
            transferred_amount(&foam_rate,&foam),
            transferred_amount(&water_rate,&energy)
        };
        route->next = routes->first[destination];
        routes->first[destination] = routes->count;
    }
}

static NT_SIMULATION_NOINLINE void plan_transfers(const NtTransportOptions *o, const NtTransportCell *cells,
                           NtTimeUs elapsed, Routes *routes)
{
    size_t first[NT_TRANSPORT_MAX_CELLS] = {0}, next[NT_TRANSPORT_MAX_EDGES];
    for (size_t i=0;i<o->edge_count;++i) {
        const NtTransportEdge *edge = o->edges+i;
        if (!edge->water_rate && !edge->foam_rate) continue;
        next[i] = first[edge->source];
        first[edge->source] = i+1;
    }
    for (size_t i=0;i<o->cell_count;++i)
        plan_donor(o,cells[i],i,elapsed,first[i],next,routes);
}

static void add_routed_amount(NtSpatialExact *total, ScaledAmount amount)
{
    if (!amount.fraction.high) return;
    NtSpatialExact value, sum;
    nt_simulation_double_exact(&value,amount.fraction,amount.exponent);
    nt_spatial_exact_add(&sum,total,&value);
    nt_spatial_exact_copy(total,&sum);
}

static void retain_inventory(NtSpatialExact *total, double initial, ScaledAmount adjustment)
{
    nt_spatial_exact_number(total,initial,0);
    add_routed_amount(total,adjustment);
}

static void include_route_error(NtSpatialExact *error, ScaledAmount amount)
{
    if (!amount.fraction.high) return;
    NtSpatialExact bound, sum;
    /* The second rounded quotient represents the exact first residual.
     * Its error is less than 2^-104 of the magnitude of the first. */
    nt_spatial_exact_number(&bound,fabs(amount.fraction.high),amount.exponent-104);
    nt_spatial_exact_add(&sum,error,&bound);
    nt_spatial_exact_copy(error,&sum);
}

static void collect_transfers(const Routes *routes, size_t destination, ExactInventory *inventory,
                              RouteErrors *errors)
{
    const bool drain = destination == NT_TRANSPORT_DRAIN;
    const NtTransportCell base = drain ? (NtTransportCell){0} : routes->base[destination];
    const RoutedInventory retained = drain ? (RoutedInventory){0} : routes->retained[destination];
    retain_inventory(&inventory->water,base.water,retained.water);
    retain_inventory(&inventory->foam,base.foam,retained.foam);
    retain_inventory(&inventory->energy,base.energy,retained.energy);
    if (errors) {
        nt_spatial_exact_number(&errors->energy,0,0);
        nt_spatial_exact_number(&errors->water,0,0);
        include_route_error(&errors->energy,retained.energy);
        include_route_error(&errors->water,retained.water);
    }
    for (size_t node=routes->first[destination];node;node=routes->incoming[node-1].next) {
        const RoutedInventory *amount = &routes->incoming[node-1].amount;
        add_routed_amount(&inventory->water,amount->water);
        add_routed_amount(&inventory->foam,amount->foam);
        add_routed_amount(&inventory->energy,amount->energy);
        if (errors) {
            include_route_error(&errors->energy,amount->energy);
            include_route_error(&errors->water,amount->water);
        }
    }
}

static void drain_transfers(const Routes *routes, NtTransportBudget *budget)
{
    if (!routes->first[NT_TRANSPORT_DRAIN]) return;
    ExactInventory drained;
    NtSpatialExact one;
    collect_transfers(routes,NT_TRANSPORT_DRAIN,&drained,NULL);
    nt_spatial_exact_number(&one,1,0);
    budget->water_drained = nt_spatial_exact_quotient(&drained.water,&one);
    budget->foam_drained = nt_spatial_exact_quotient(&drained.foam,&one);
    budget->heat_drained = nt_spatial_exact_quotient(&drained.energy,&one);
}

/* An individual denominator is a sum of rates times one donor capacity.
 * Its bits range from -3222 through 69. The remainder also spans terms
 * weighted by a heat capacity or a rounding midpoint and the initial
 * 160-bit downshift. The public bounds put its span below 7000 bits. */
enum { ROUTE_DENOMINATOR_LIMBS = 112, ROUTE_REMAINDER_LIMBS = 224 };

typedef struct RouteFraction {
    uint32_t remainder[ROUTE_REMAINDER_LIMBS], denominator[ROUTE_DENOMINATOR_LIMBS];
    int remainder_offset, denominator_offset;
    unsigned remainder_count, denominator_count;
    bool negative;
} RouteFraction;

typedef struct RouteSum {
    RouteFraction terms[NT_TRANSPORT_MAX_CELLS+1];
    size_t count;
    int denominator_bits;
} RouteSum;

typedef struct WaterMixScratch {
    NtSpatialExact micros, inlet, mixed_water, limit, excess;
    NtSpatialExact water_heat, inlet_heat, inlet_density, temperature, mixed_energy;
    RouteErrors errors;
} WaterMixScratch;

struct TransportScratch {
    Routes routes;
    RouteSum sum;
    WaterMixScratch water;
};

static void append_route_fraction(RouteSum *sum, const NtSpatialExact *numerator,
                                  const NtSpatialExact *denominator)
{
    if (!numerator->count) return;
    RouteFraction *term = sum->terms+sum->count++;
    assert(numerator->count <= ROUTE_REMAINDER_LIMBS && denominator->count <= ROUTE_DENOMINATOR_LIMBS);
    term->remainder_count = numerator->count;
    term->denominator_count = denominator->count;
    memcpy(term->remainder,numerator->limbs,numerator->count*sizeof(numerator->limbs[0]));
    memcpy(term->denominator,denominator->limbs,denominator->count*sizeof(denominator->limbs[0]));
    term->negative = numerator->negative;
    term->denominator_offset = denominator->offset;
    const int extra = denominator->offset-numerator->offset;
    sum->denominator_bits += 32*((int)denominator->count+(extra > 0 ? extra : 0));
    /* The magnitude of any signed term fits strictly below 2^160. */
    term->remainder_offset = numerator->offset-5;
}

static uint64_t route_fraction_digit(RouteFraction *term)
{
    if (!term->remainder_count) return 0;
    NtSpatialExact remainder, denominator;
    remainder.offset = term->remainder_offset+1;
    remainder.count = term->remainder_count;
    remainder.negative = false;
    denominator.offset = term->denominator_offset;
    denominator.count = term->denominator_count;
    denominator.negative = false;
    memcpy(remainder.limbs,term->remainder,remainder.count*sizeof(remainder.limbs[0]));
    memcpy(denominator.limbs,term->denominator,denominator.count*sizeof(denominator.limbs[0]));
    uint64_t digit = (uint64_t)nt_spatial_exact_quotient(&remainder,&denominator);
    NtSpatialExact amount, product, next;
    nt_spatial_exact_number(&amount,(double)digit,0);
    nt_spatial_exact_multiply(&product,&amount,&denominator);
    if (nt_spatial_exact_compare(&product,&remainder)>0) {
        --digit;
        nt_spatial_exact_number(&amount,(double)digit,0);
        nt_spatial_exact_multiply(&product,&amount,&denominator);
    }
    nt_spatial_exact_subtract(&next,&remainder,&product);
    assert(next.count <= ROUTE_REMAINDER_LIMBS);
    term->remainder_count = next.count;
    term->remainder_offset = next.offset;
    memcpy(term->remainder,next.limbs,next.count*sizeof(next.limbs[0]));
    return digit;
}

static NT_SIMULATION_NOINLINE int route_sum_sign(RouteSum *sum)
{
    /* Long division keeps only a remainder per term. It avoids forming
     * a common denominator that could span hundreds of thousands of bits. */
    int64_t difference = 0;
    for (int power=128;;power-=32) {
        difference *= INT64_C(4294967296);
        int positive = 0, negative = 0;
        for (size_t i=0;i<sum->count;++i) {
            RouteFraction *term = sum->terms+i;
            const int64_t digit = (int64_t)route_fraction_digit(term);
            difference += term->negative ? -digit : digit;
            if (term->remainder_count) {
                if (term->negative) ++negative;
                else ++positive;
            }
        }
        if (!positive && !negative) return (difference>0)-(difference<0);
        if (difference >= negative) return 1;
        if (difference <= -positive) return -1;

        /* An unresolved residual is smaller than one unit per term.
         * A nonzero exact sum is at least the inverse of the product
         * of integer denominators, so this also detects exact equality. */
        if (power+sum->denominator_bits+8 <= 0) return 0;
    }
}

typedef struct RouteMix {
    const RouteInputs *input;
    size_t destination;
    const NtSpatialExact *micros, *inlet, *inlet_heat;
} RouteMix;

typedef struct InventoryComparison {
    /* Compare water*water_weight + energy*energy_weight + constant to zero. */
    const NtSpatialExact *water_weight, *energy_weight, *constant;
} InventoryComparison;

static NT_SIMULATION_NOINLINE void append_donor_fraction(RouteSum *sum, const RouteMix *mix,
                                  const InventoryComparison *comparison, size_t donor)
{
    const RouteInputs *input = mix->input;
    NtSpatialExact total, selected;
    nt_spatial_exact_number(&total,0,0);
    nt_spatial_exact_number(&selected,0,0);
    for (size_t i=0;i<input->options->edge_count;++i) {
        const NtTransportEdge *edge = input->options->edges+i;
        if (edge->source != donor) continue;
        sum_rate(&total,edge->water_rate);
        if (edge->destination == mix->destination) sum_rate(&selected,edge->water_rate);
    }
    const bool outgoing = donor == mix->destination;
    const NtSpatialExact *rate = outgoing ? &total : &selected;
    if (!rate->count) return;

    const NtTransportCell start = input->cells[donor];
    const double specific_heat = input->options->water_heat_capacity;
    RouteScale water;
    route_scale(&water,&total,start.water,1,input->elapsed,mix->micros);
    NtSpatialExact coefficient, volume, numerator, denominator;
    if (comparison->energy_weight->count) {
        /* Water and heat here share the donor's water quotient. Use its
         * capacity as the only additional factor in the denominator. */
        NtSpatialExact capacity, specific_energy, energy_term, water_term;
        exact_cell_capacity(&capacity,input->options->cells+donor,start.water,specific_heat);
        nt_simulation_product(&specific_energy,start.energy,specific_heat);
        nt_spatial_exact_multiply(&energy_term,comparison->energy_weight,&specific_energy);
        nt_spatial_exact_multiply(&water_term,comparison->water_weight,&capacity);
        nt_spatial_exact_add(&coefficient,&energy_term,&water_term);
        nt_spatial_exact_multiply(&denominator,&water.denominator,&capacity);
    } else {
        nt_spatial_exact_copy(&coefficient,comparison->water_weight);
        nt_spatial_exact_copy(&denominator,&water.denominator);
    }
    nt_spatial_exact_multiply(&volume,&water.numerator,rate);
    nt_spatial_exact_multiply(&numerator,&volume,&coefficient);
    if (outgoing) nt_spatial_exact_negate(&numerator);
    append_route_fraction(sum,&numerator,&denominator);
}

static void base_with_inlet(NtSpatialExact *out, double initial,
                            const NtSpatialExact *inlet, const NtSpatialExact *micros)
{
    NtSpatialExact value, base;
    nt_spatial_exact_number(&value,initial,0);
    nt_spatial_exact_multiply(&base,&value,micros);
    nt_spatial_exact_add(out,&base,inlet);
}

static int compare_mixed_inventory(const RouteMix *mix, const InventoryComparison *comparison)
{
    RouteSum *sum = &mix->input->scratch->sum;
    sum->count = 0;
    sum->denominator_bits = 0;
    const NtTransportCell start = mix->input->cells[mix->destination];
    NtSpatialExact initial, water, energy, constant, combined, numerator;
    base_with_inlet(&initial,start.water,mix->inlet,mix->micros);
    nt_spatial_exact_multiply(&water,&initial,comparison->water_weight);
    base_with_inlet(&initial,start.energy,mix->inlet_heat,mix->micros);
    nt_spatial_exact_multiply(&energy,&initial,comparison->energy_weight);
    nt_spatial_exact_multiply(&constant,comparison->constant,mix->micros);
    nt_spatial_exact_add(&combined,&water,&energy);
    nt_spatial_exact_add(&numerator,&combined,&constant);
    append_route_fraction(sum,&numerator,mix->micros);
    for (size_t donor=0;donor<mix->input->options->cell_count;++donor)
        append_donor_fraction(sum,mix,comparison,donor);
    return route_sum_sign(sum);
}

static void rounding_midpoint(NtSpatialExact *out, double lower, double upper)
{
    /* The interval width is much less than one ULP. Endpoints which
     * round differently bracket a single rounding midpoint. */
    NtSpatialExact a, b;
    nt_spatial_exact_number(&a,lower,-1);
    nt_spatial_exact_number(&b,upper,-1);
    nt_spatial_exact_add(out,&a,&b);
}

static double select_rounding(double lower, double upper, int order, const NtSpatialExact *midpoint)
{
    if (order) return order < 0 ? lower : upper;
    NtSpatialExact one;
    nt_spatial_exact_number(&one,1,0);
    return nt_spatial_exact_quotient(midpoint,&one);
}

static double round_mixed_inventory(const RouteMix *mix, const NtSpatialExact *value,
                                    const NtSpatialExact *error, bool water)
{
    if (!error->count) return nt_spatial_exact_quotient(value,mix->micros);
    NtSpatialExact lo, hi;
    nt_spatial_exact_subtract(&lo,value,error);
    nt_spatial_exact_add(&hi,value,error);
    const double lower = nt_spatial_exact_quotient(&lo,mix->micros);
    const double upper = nt_spatial_exact_quotient(&hi,mix->micros);
    if (lower == upper) return lower;

    NtSpatialExact midpoint, constant, one, zero;
    rounding_midpoint(&midpoint,lower,upper);
    nt_spatial_exact_copy(&constant,&midpoint);
    nt_spatial_exact_negate(&constant);
    nt_spatial_exact_number(&one,1,0);
    nt_spatial_exact_number(&zero,0,0);
    const InventoryComparison check = {water ? &one : &zero,water ? &zero : &one,&constant};
    const int order = compare_mixed_inventory(mix,&check);
    return select_rounding(lower,upper,order,&midpoint);
}

static double drain_evaporative_energy(NtTransportCell *cell, const NtSpatialExact *capacity,
                                        const NtSpatialExact *water_heat, const NtSpatialExact *latent,
                                        NtTransportBudget *budget)
{
    NtSpatialExact sensible, latent_capacity, divisor, requested, water, total_volume, retained;
    NtSpatialExact initial, numerator;
    nt_spatial_exact_number(&initial,cell->energy,0);
    nt_spatial_exact_multiply(&sensible,&initial,water_heat);
    nt_spatial_exact_multiply(&latent_capacity,latent,capacity);
    nt_spatial_exact_add(&divisor,&sensible,&latent_capacity);
    nt_spatial_exact_multiply(&requested,&initial,capacity);
    nt_spatial_exact_number(&water,cell->water,0);
    nt_spatial_exact_multiply(&total_volume,&water,&divisor);
    nt_spatial_exact_subtract(&retained,&total_volume,&requested);
    nt_spatial_exact_multiply(&numerator,&initial,&latent_capacity);
    budget->heat_latent += nt_spatial_exact_quotient(&numerator,&divisor);
    nt_spatial_exact_multiply(&numerator,&initial,&sensible);
    budget->heat_evaporated += nt_spatial_exact_quotient(&numerator,&divisor);
    cell->water = nt_spatial_exact_sign(&retained)>0 ? nt_spatial_exact_quotient(&retained,&divisor) : 0;
    cell->energy = 0;
    return nt_spatial_exact_quotient(&requested,&divisor);
}

static NT_SIMULATION_NOINLINE void evaporate(NtTransportCell *state, const NtTransportCellOptions *local,
                      double specific_heat, NtTimeUs elapsed, NtTransportBudget *budget)
{
    NtSpatialExact lost, time_unit, volume, remaining, removed;
    nt_simulation_exponential_loss(local->evaporation,elapsed,&lost,&time_unit);
    nt_spatial_exact_number(&volume,state->water,0);
    nt_spatial_exact_multiply(&removed,&volume,&lost);
    nt_spatial_exact_subtract(&remaining,&time_unit,&lost);

    NtSpatialExact water_heat, substrate, wet, capacity, e, latent;
    nt_spatial_exact_number(&water_heat,specific_heat,0);
    nt_spatial_exact_number(&substrate,local->substrate_heat_capacity,0);
    nt_spatial_exact_multiply(&wet,&water_heat,&volume);
    nt_spatial_exact_add(&capacity,&substrate,&wet);
    nt_spatial_exact_number(&e,state->energy,0);
    nt_spatial_exact_number(&latent,local->latent_heat,0);

    NtSpatialExact sensible_scale, latent_scale, sensible, latent_cost, spent, available, remaining_heat, unit;
    nt_spatial_exact_multiply(&sensible_scale,&e,&water_heat);
    nt_spatial_exact_multiply(&latent_scale,&capacity,&latent);
    nt_spatial_exact_multiply(&sensible,&removed,&sensible_scale);
    nt_spatial_exact_multiply(&latent_cost,&removed,&latent_scale);
    nt_spatial_exact_add(&spent,&sensible,&latent_cost);
    nt_spatial_exact_multiply(&unit,&capacity,&time_unit);
    nt_spatial_exact_multiply(&available,&e,&unit);
    nt_spatial_exact_subtract(&remaining_heat,&available,&spent);
    if (nt_spatial_exact_sign(&remaining_heat)<0) {
        budget->water_evaporated += drain_evaporative_energy(state,&capacity,&water_heat,&latent,budget);
        return;
    }
    state->energy = nt_spatial_exact_quotient(&remaining_heat,&unit);
    nt_spatial_exact_multiply(&lost,&volume,&remaining);
    state->water = nt_spatial_exact_quotient(&lost,&time_unit);
    budget->water_evaporated += nt_spatial_exact_quotient(&removed,&time_unit);
    budget->heat_evaporated += nt_spatial_exact_quotient(&sensible,&unit);
    budget->heat_latent += nt_spatial_exact_quotient(&latent_cost,&unit);
}

static void add_inventory(NtSpatialExact *out, const NtSpatialExact *initial,
                          const NtSpatialExact *added, const NtSpatialExact *unit)
{
    NtSpatialExact scaled;
    nt_spatial_exact_multiply(&scaled,initial,unit);
    nt_spatial_exact_add(out,&scaled,added);
}

static NT_SIMULATION_NOINLINE double retained_spill_energy(const RouteMix *mix, const NtSpatialExact *energy,
                                    const NtSpatialExact *capacity, const NtSpatialExact *kept,
                                    const NtSpatialExact *water_heat, const RouteErrors *errors)
{
    NtSpatialExact scaled_kept, error, lo, hi, numerator, denominator;
    nt_spatial_exact_multiply(&scaled_kept,kept,mix->micros);
    nt_spatial_exact_multiply(&error,&errors->water,water_heat);
    nt_spatial_exact_add(&hi,capacity,&error);
    nt_spatial_exact_multiply(&denominator,&hi,mix->micros);
    nt_spatial_exact_subtract(&lo,energy,&errors->energy);
    nt_spatial_exact_multiply(&numerator,&lo,&scaled_kept);
    const double lower = nt_spatial_exact_quotient(&numerator,&denominator);

    nt_spatial_exact_subtract(&lo,capacity,&error);
    nt_spatial_exact_multiply(&denominator,&lo,mix->micros);
    nt_spatial_exact_add(&hi,energy,&errors->energy);
    nt_spatial_exact_multiply(&numerator,&hi,&scaled_kept);
    const double upper = nt_spatial_exact_quotient(&numerator,&denominator);
    if (lower == upper) return lower;

    /* Compare E*Ckept to midpoint*(Csubstrate + W*Cv). */
    const NtTransportCellOptions *local = mix->input->options->cells+mix->destination;
    NtSpatialExact midpoint, negative_midpoint, substrate, water_weight, constant;
    rounding_midpoint(&midpoint,lower,upper);
    nt_spatial_exact_copy(&negative_midpoint,&midpoint);
    nt_spatial_exact_negate(&negative_midpoint);
    nt_spatial_exact_number(&substrate,local->substrate_heat_capacity,0);
    nt_spatial_exact_multiply(&constant,&negative_midpoint,&substrate);
    nt_spatial_exact_multiply(&water_weight,&negative_midpoint,water_heat);
    const InventoryComparison check = {&water_weight,kept,&constant};
    const int order = compare_mixed_inventory(mix,&check);
    return select_rounding(lower,upper,order,&midpoint);
}

static void spill_water(NtTransportCell *state, const RouteMix *mix, const NtSpatialExact *excess,
                        const NtSpatialExact *mixed_energy, const NtSpatialExact *water_heat,
                        const RouteErrors *errors, NtTransportBudget *budget)
{
    const NtTransportCellOptions *local = mix->input->options->cells+mix->destination;
    NtSpatialExact kept, kept_scaled, removed, mixed, unit, energy;
    exact_cell_capacity(&kept,local,local->water_capacity,mix->input->options->water_heat_capacity);
    nt_spatial_exact_multiply(&kept_scaled,&kept,mix->micros);
    nt_spatial_exact_multiply(&removed,excess,water_heat);
    nt_spatial_exact_add(&mixed,&kept_scaled,&removed);
    state->water = local->water_capacity;
    state->energy = retained_spill_energy(mix,mixed_energy,&mixed,&kept,water_heat,errors);
    if (nt_spatial_exact_sign(excess)>0) {
        nt_spatial_exact_multiply(&unit,&mixed,mix->micros);
        nt_spatial_exact_multiply(&energy,mixed_energy,&removed);
        budget->heat_spilled += nt_spatial_exact_quotient(&energy,&unit);
        budget->water_spilled += nt_spatial_exact_quotient(excess,mix->micros);
    }
}

static bool exceeds_capacity(const RouteMix *mix, const NtSpatialExact *excess,
                             const NtSpatialExact *error, double capacity)
{
    const int sign = nt_spatial_exact_sign(excess);
    NtSpatialExact magnitude;
    nt_spatial_exact_copy(&magnitude,excess);
    magnitude.negative = false;
    if (!error->count || nt_spatial_exact_compare(&magnitude,error)>0) return sign>0;

    NtSpatialExact one, zero, constant;
    nt_spatial_exact_number(&one,1,0);
    nt_spatial_exact_number(&zero,0,0);
    nt_spatial_exact_number(&constant,-capacity,0);
    const InventoryComparison check = {&one,&zero,&constant};
    return compare_mixed_inventory(mix,&check)>0;
}

static NT_SIMULATION_NOINLINE void mix_water(NtTransportCell *state, const ExactInventory *inventory,
                      const RouteErrors *errors, const RouteInputs *input,
                      size_t destination, NtTransportBudget *budget)
{
    const NtTransportCellOptions *local = input->options->cells+destination;
    const double specific_heat = input->options->water_heat_capacity;
    WaterMixScratch *work = &input->scratch->water;
    nt_spatial_exact_number(&work->micros,1000000,0);
    nt_simulation_product(&work->inlet,local->inlet_rate,(double)input->elapsed);
    add_inventory(&work->mixed_water,&inventory->water,&work->inlet,&work->micros);
    nt_simulation_product(&work->limit,local->water_capacity,1000000);
    nt_spatial_exact_subtract(&work->excess,&work->mixed_water,&work->limit);

    nt_spatial_exact_number(&work->water_heat,specific_heat,0);
    nt_spatial_exact_number(&work->temperature,local->inlet_temperature,0);
    nt_spatial_exact_multiply(&work->inlet_density,&work->water_heat,&work->temperature);
    nt_spatial_exact_multiply(&work->inlet_heat,&work->inlet,&work->inlet_density);
    add_inventory(&work->mixed_energy,&inventory->energy,&work->inlet_heat,&work->micros);
    nt_spatial_exact_multiply(&work->errors.water,&errors->water,&work->micros);
    nt_spatial_exact_multiply(&work->errors.energy,&errors->energy,&work->micros);
    budget->water_inlet += nt_spatial_exact_quotient(&work->inlet,&work->micros);
    budget->heat_inlet += nt_spatial_exact_quotient(&work->inlet_heat,&work->micros);
    const RouteMix mix = {input,destination,&work->micros,&work->inlet,&work->inlet_heat};
    if (exceeds_capacity(&mix,&work->excess,&work->errors.water,local->water_capacity)) {
        spill_water(state,&mix,&work->excess,&work->mixed_energy,&work->water_heat,&work->errors,budget);
    } else {
        state->water = round_mixed_inventory(&mix,&work->mixed_water,&work->errors.water,true);
        state->energy = round_mixed_inventory(&mix,&work->mixed_energy,&work->errors.energy,false);
    }
}

static NT_SIMULATION_NOINLINE void update_foam(NtTransportCell *state, const NtSpatialExact *foam, const NtTransportCellOptions *local,
                        NtTimeUs elapsed, NtTransportBudget *budget)
{
    NtSpatialExact micros, generated, total, loss, unit, decayed, before, surviving;
    nt_spatial_exact_number(&micros,1000000,0);
    nt_simulation_product(&generated,local->foam_generation,(double)elapsed);
    add_inventory(&total,foam,&generated,&micros);
    budget->foam_generated += nt_spatial_exact_quotient(&generated,&micros);
    nt_simulation_exponential_loss(local->foam_decay,elapsed,&loss,&unit);
    nt_spatial_exact_multiply(&decayed,&total,&loss);
    nt_spatial_exact_multiply(&before,&total,&unit);
    nt_spatial_exact_subtract(&surviving,&before,&decayed);
    NtSpatialExact denominator, capacity, limit, excess;
    nt_spatial_exact_multiply(&denominator,&unit,&micros);
    nt_spatial_exact_number(&capacity,local->foam_capacity,0);
    nt_spatial_exact_multiply(&limit,&capacity,&denominator);
    nt_spatial_exact_subtract(&excess,&surviving,&limit);
    budget->foam_decayed += nt_spatial_exact_quotient(&decayed,&denominator);
    if (nt_spatial_exact_sign(&excess)>0) {
        state->foam = local->foam_capacity;
        budget->foam_spilled += nt_spatial_exact_quotient(&excess,&denominator);
    } else state->foam = nt_spatial_exact_quotient(&surviving,&denominator);
}

/* Fraction of constant input that leaves through ambient conductance. */
static double thermal_response_deficit(double loss, double exponent)
{
    if (exponent >= 1e-3) return 1.0-loss/exponent;
    return exponent*(.5-exponent*(1.0/6.0-exponent*(1.0/24.0-exponent/120.0)));
}

static bool add_energy(NtTransportCell *state, const NtSpatialExact *change,
                       const NtSpatialExact *denominator)
{
    NtSpatialExact initial, stored, total;
    nt_spatial_exact_number(&initial,state->energy,0);
    nt_spatial_exact_multiply(&stored,&initial,denominator);
    nt_spatial_exact_add(&total,&stored,change);
    if (nt_spatial_exact_sign(&total) < 0) return false;
    state->energy = nt_spatial_exact_quotient(&total,denominator);
    return true;
}

static double ambient_budget(const NtSpatialExact *gap, const NtSpatialExact *work,
                             const NtSpatialExact *time_unit, double loss, double deficit)
{
    NtSpatialExact decay, remaining, heat, scaled_heat, carried, result;
    nt_spatial_exact_number(&decay,loss,0);
    nt_spatial_exact_number(&remaining,deficit,0);
    nt_spatial_exact_multiply(&heat,gap,&decay);
    nt_spatial_exact_multiply(&scaled_heat,&heat,time_unit);
    nt_spatial_exact_multiply(&carried,work,&remaining);
    nt_spatial_exact_subtract(&result,&scaled_heat,&carried);
    return nt_spatial_exact_quotient(&result,time_unit);
}

static bool integrate_weak_heat(NtTransportCell *state, const NtSpatialExact *drive,
                                double capacity, NtTimeUs elapsed, double deficit)
{
    /* E_new = E + drive * elapsed_seconds * response / C. Compute the
     * signed drive before integrating so opposing large rates can cancel. */
    NtSpatialExact time, impulse, lost, scaled_loss, net, denominator;
    nt_spatial_exact_number(&time,(double)elapsed,0);
    nt_spatial_exact_number(&lost,deficit,0);
    nt_spatial_exact_multiply(&impulse,drive,&time);
    nt_spatial_exact_multiply(&scaled_loss,&impulse,&lost);
    nt_spatial_exact_subtract(&net,&impulse,&scaled_loss);
    nt_simulation_product(&denominator,capacity,1000000);
    return add_energy(state,&net,&denominator);
}

static void contact_loss(NtSpatialExact *out, double conductance, NtTimeUs elapsed,
                          const NtSpatialExact *ca, const NtSpatialExact *cb,
                          const NtSpatialExact *sum, double exponent)
{
    if (exponent > 4096) {
        nt_spatial_exact_number(out,1,0);
        return;
    }
    NtSpatialExact t, numerator, product, unit, denominator, decay, one;
    nt_simulation_product(&t,conductance,(double)elapsed);
    nt_spatial_exact_multiply(&numerator,&t,sum);
    nt_spatial_exact_multiply(&product,ca,cb);
    nt_spatial_exact_number(&unit,1000000,0);
    nt_spatial_exact_multiply(&denominator,&product,&unit);
    nt_simulation_decay(nt_simulation_ratio(&numerator,&denominator),&decay);
    nt_spatial_exact_number(&one,1,0);
    nt_spatial_exact_subtract(out,&one,&decay);
}

static bool integrate_small_heat(NtTransportCell *state, const NtSpatialExact *drive,
                                  const NtSpatialExact *gap, const NtSpatialExact *work,
                                  const NtSpatialExact *unit, double capacity, double conductance,
                                  NtTimeUs elapsed, double *ambient)
{
    NtSpatialExact n, d, response, denominator, impulse, net;
    NtSpatialExact result, time, volume, heat, lost, heater, exhaust;
    nt_simulation_product(&n,conductance,(double)elapsed);
    nt_simulation_product(&d,capacity,1000000);
    nt_simulation_small_response(&n,&d,&response,&denominator);
    nt_spatial_exact_number(&time,(double)elapsed,0);
    nt_spatial_exact_multiply(&impulse,drive,&time);
    nt_spatial_exact_multiply(&net,&impulse,&response);
    nt_spatial_exact_multiply(&result,&d,&denominator);
    if (!add_energy(state,&net,&result)) return false;

    nt_spatial_exact_multiply(&volume,gap,unit);
    nt_spatial_exact_multiply(&heat,&volume,&n);
    nt_spatial_exact_multiply(&net,&heat,&response);
    nt_spatial_exact_subtract(&lost,&denominator,&response);
    nt_spatial_exact_multiply(&heater,work,&lost);
    nt_spatial_exact_multiply(&exhaust,&heater,&d);
    nt_spatial_exact_subtract(&impulse,&net,&exhaust);
    nt_spatial_exact_multiply(&time,&result,unit);
    *ambient = nt_spatial_exact_quotient(&impulse,&time);
    return true;
}

static bool integrate_ambient_endpoint(NtTransportCell *state, const NtSpatialExact *steady,
                                     const NtSpatialExact *initial, const NtSpatialExact *g,
                                     double conductance, double capacity, NtTimeUs elapsed)
{
    NtSpatialExact time, magnitude;
    nt_simulation_product(&time,conductance,(double)elapsed);
    nt_simulation_product(&magnitude,capacity,1000000);
    const NtSimulationDouble exponent = nt_simulation_ratio(&time,&magnitude);
    /* At this bound the decayed initial term is below the least
     * nonzero negative steady term allowed by the binary64 inputs. */
    if (exponent.high > 4096) {
        if (nt_spatial_exact_sign(steady) < 0) return false;
        state->energy = nt_spatial_exact_quotient(steady,g);
        return true;
    }
    NtSpatialExact decay, one, lost, keep, equilibrium, result;
    nt_simulation_decay(exponent,&decay);
    nt_spatial_exact_number(&one,1,0);
    nt_spatial_exact_subtract(&lost,&one,&decay);
    nt_spatial_exact_multiply(&keep,initial,&decay);
    nt_spatial_exact_multiply(&equilibrium,steady,&lost);
    nt_spatial_exact_add(&result,&keep,&equilibrium);
    if (nt_spatial_exact_sign(&result) < 0) return false;
    state->energy = nt_spatial_exact_quotient(&result,g);
    return true;
}

static NT_SIMULATION_NOINLINE bool update_ambient(NtTransportCell *state, const NtTransportCellOptions *local,
                           double specific_heat, NtTimeUs elapsed, NtTransportBudget *budget)
{
    if (!local->ambient_conductance && !local->power) return true;
    NtSpatialExact work, unit;
    nt_simulation_product(&work,local->power,(double)elapsed);
    nt_spatial_exact_number(&unit,1000000,0);
    const double power = nt_spatial_exact_quotient(&work,&unit);
    budget->heat_applied += power;
    if (!local->ambient_conductance) return add_energy(state,&work,&unit);

    const double seconds = (double)elapsed/1000000.0;
    const double capacity = cell_capacity(local,state->water,specific_heat);
    const double conductance = local->ambient_conductance;
    NtSpatialExact at_ambient, energy, gap, g, heat, applied, drive;
    nt_simulation_product(&at_ambient,capacity,local->ambient_temperature);
    nt_spatial_exact_number(&energy,state->energy,0);
    nt_spatial_exact_subtract(&gap,&at_ambient,&energy);
    nt_spatial_exact_number(&g,conductance,0);
    nt_spatial_exact_multiply(&heat,&gap,&g);
    nt_simulation_product(&applied,local->power,capacity);
    nt_spatial_exact_add(&drive,&heat,&applied);
    if (!nt_spatial_exact_sign(&drive)) {
        budget->heat_ambient -= power;
        return true;
    }

    const double exponent = (conductance*seconds)/capacity;
    if (exponent <= 1e-6) {
        double ambient;
        if (!integrate_small_heat(state,&drive,&gap,&work,&unit,capacity,conductance,elapsed,&ambient))
            return false;
        budget->heat_ambient += ambient;
        return true;
    }
    const double loss = -expm1(-exponent);
    const double deficit = thermal_response_deficit(loss,exponent);
    budget->heat_ambient += ambient_budget(&gap,&work,&unit,loss,deficit);
    NtSpatialExact stored_rate, steady;
    nt_spatial_exact_multiply(&stored_rate,&energy,&g);
    nt_spatial_exact_add(&steady,&stored_rate,&drive);
    if (exponent <= .125 && nt_spatial_exact_sign(&steady) >= 0)
        return integrate_weak_heat(state,&drive,capacity,elapsed,deficit);
    return integrate_ambient_endpoint(state,&steady,&stored_rate,&g,conductance,capacity,elapsed);
}

static bool evolve(const RouteInputs *input, NtTransportCell *next, NtTransportBudget *budget)
{
    Routes *routes = &input->scratch->routes;
    const NtTransportOptions *options = input->options;
    const NtTimeUs elapsed = input->elapsed;
    plan_transfers(options,input->cells,elapsed,routes);
    drain_transfers(routes,budget);
    const double specific_heat = options->water_heat_capacity;
    for (size_t i = 0; i < options->cell_count; ++i) {
        ExactInventory inventory;
        RouteErrors errors;
        collect_transfers(routes,i,&inventory,&errors);
        NtTransportCell updated = {0};
        const NtTransportCellOptions *local = &options->cells[i];
        mix_water(&updated,&inventory,&errors,input,i,budget);
        if (updated.water && local->evaporation) evaporate(&updated,local,specific_heat,elapsed,budget);
        update_foam(&updated,&inventory.foam,local,elapsed,budget);
        if (!update_ambient(&updated,local,specific_heat,elapsed,budget)) return false;
        next[i] = updated;
    }

    for (size_t i = 0; i < options->edge_count; ++i) {
        const NtTransportEdge edge = options->edges[i];
        if (!edge.conductance) continue;
        NtTransportCell *first = &next[edge.source], *second = &next[edge.destination];
        const double a = cell_capacity(options->cells+edge.source,first->water,specific_heat);
        const double b = cell_capacity(options->cells+edge.destination,second->water,specific_heat);
        conduct(a,b,edge.conductance,elapsed,&first->energy,&second->energy);
    }
    for (size_t i = 0; i < options->cell_count; ++i) {
        if (!amount(next[i].energy,TRANSPORT_ENERGY_MAX)) return false;
    }
    return true;
}

NtResult nt_surface_transport_step(const NtTransportOptions *options, NtTransportCell *cells,
                                     NtTimeUs elapsed, NtTransportBudget *budget)
{
    if (!options || (options->cell_count && (!cells || !options->cells)) ||
        (options->edge_count && !options->edges)) return NT_RESULT_INVALID_ARGUMENT;
    if (options->cell_count > NT_TRANSPORT_MAX_CELLS || options->edge_count > NT_TRANSPORT_MAX_EDGES ||
        elapsed > NT_SIMULATION_STEP_LIMIT) return NT_RESULT_OUT_OF_RANGE;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    if (!amount(options->water_heat_capacity,NT_SIMULATION_COORDINATE_LIMIT))
        return nt_simulation_finish(&saved,NT_RESULT_INVALID_ARGUMENT);
    for (size_t i = 0; i < options->cell_count; ++i) {
        if (!valid_cell(&options->cells[i],cells[i],options->water_heat_capacity))
            return nt_simulation_finish(&saved,NT_RESULT_INVALID_ARGUMENT);
    }
    for (size_t i = 0; i < options->edge_count; ++i) {
        if (!valid_edge(&options->edges[i],options->cell_count))
            return nt_simulation_finish(&saved,NT_RESULT_INVALID_ARGUMENT);
    }

    NtTransportBudget report = {0};
    NtTransportCell next[NT_TRANSPORT_MAX_CELLS];
    bool valid = true;
    if (elapsed && options->cell_count) {
        /* Each call owns its routing tables and exact comparison workspace.
         * Both can exceed the default C stack on an ordinary worker thread. */
        TransportScratch *scratch = malloc(sizeof(*scratch));
        if (!scratch) return nt_simulation_finish(&saved,NT_RESULT_ALLOCATION_FAILED);
        memset(&scratch->routes,0,sizeof(scratch->routes));
        const RouteInputs input = {options,cells,elapsed,scratch};
        valid = evolve(&input,next,&report);
        free(scratch);
    }
    const NtResult status = nt_simulation_finish(&saved,valid ? NT_RESULT_OK : NT_RESULT_OUT_OF_RANGE);
    if (status == NT_RESULT_OK) {
        if (elapsed && options->cell_count) memcpy(cells,next,options->cell_count*sizeof(*cells));
        if (budget) *budget = report;
    }
    return status;
}
