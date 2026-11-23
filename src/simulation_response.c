#include "simulation_internal.h"

/* A high and low binary64 pair keeps an extra term when a finite
 * relaxation ratio can affect a small endpoint or a range boundary. */

static NtSimulationDouble thermal_sum(double a, double b)
{
    const double sum = a+b;
    const double position = sum-a;
    return (NtSimulationDouble){sum,(a-(sum-position))+(b-position)};
}

static NtSimulationDouble thermal_add(NtSimulationDouble a, NtSimulationDouble b)
{
    const NtSimulationDouble sum = thermal_sum(a.high,b.high);
    return thermal_sum(sum.high,sum.low+a.low+b.low);
}

static NtSimulationDouble thermal_multiply(NtSimulationDouble a, NtSimulationDouble b)
{
    const double high = a.high*b.high;
    return thermal_sum(high,fma(a.high,b.high,-high)+a.high*b.low+a.low*b.high);
}

static NtSimulationDouble thermal_divide_integer(NtSimulationDouble value, unsigned divisor)
{
    const double q = value.high/divisor;
    return thermal_sum(q,(fma(-q,divisor,value.high)+value.low)/divisor);
}

NtSimulationDouble nt_simulation_ratio(const NtSpatialExact *n, const NtSpatialExact *d)
{
    const double high = nt_spatial_exact_quotient(n,d);
    NtSpatialExact h, multiple, remainder;
    nt_spatial_exact_number(&h,high,0);
    nt_spatial_exact_multiply(&multiple,&h,d);
    nt_spatial_exact_subtract(&remainder,n,&multiple);
    return (NtSimulationDouble){high,nt_spatial_exact_quotient(&remainder,d)};
}

void nt_simulation_double_exact(NtSpatialExact *out, NtSimulationDouble value, int power)
{
    NtSpatialExact h, l;
    nt_spatial_exact_number(&h,value.high,power);
    nt_spatial_exact_number(&l,value.low,power);
    nt_spatial_exact_add(out,&h,&l);
}

void nt_simulation_decay(NtSimulationDouble x, NtSpatialExact *out)
{
    unsigned squares = 0;
    while (x.high > .125) {
        x.high *= .5;
        x.low *= .5;
        ++squares;
    }
    const NtSimulationDouble neg = {-x.high,-x.low};
    NtSimulationDouble term = neg, difference = term;
    for (unsigned k=2;k<=24;++k) {
        term = thermal_divide_integer(thermal_multiply(term,neg),k);
        difference = thermal_add(difference,term);
    }
    if (!squares) {
        NtSpatialExact one, change;
        nt_spatial_exact_number(&one,1,0);
        nt_simulation_double_exact(&change,difference,0);
        nt_spatial_exact_add(out,&one,&change);
        return;
    }
    NtSimulationDouble decay = thermal_add((NtSimulationDouble){1,0},difference);
    int exponent = 0;
    while (squares--) {
        decay = thermal_multiply(decay,decay);
        int shift;
        decay.high = frexp(decay.high,&shift);
        decay.low = scalbn(decay.low,-shift);
        exponent = 2*exponent+shift;
    }
    nt_simulation_double_exact(out,decay,exponent);
}

void nt_simulation_small_response(const NtSpatialExact *n, const NtSpatialExact *d,
                           NtSpatialExact *numerator, NtSpatialExact *denominator)
{
    /* Evaluate through x^4 for response=(1-exp(-x))/x, with x=n/d. */
    const double coefficients[] = {120,-60,20,-5};
    NtSpatialExact polynomial, scale;
    nt_spatial_exact_number(&polynomial,1,0);
    nt_spatial_exact_number(&scale,1,0);
    for (unsigned i=4;i>0;--i) {
        NtSpatialExact next_scale, term, weighted, next;
        nt_spatial_exact_multiply(&next_scale,&scale,d);
        nt_spatial_exact_number(&term,coefficients[i-1],0);
        nt_spatial_exact_multiply(&weighted,&term,&next_scale);
        nt_spatial_exact_multiply(&term,&polynomial,n);
        nt_spatial_exact_add(&next,&term,&weighted);
        nt_spatial_exact_copy(&polynomial,&next);
        nt_spatial_exact_copy(&scale,&next_scale);
    }
    NtSpatialExact factor;
    nt_spatial_exact_number(&factor,120,0);
    nt_spatial_exact_multiply(denominator,&scale,&factor);
    nt_spatial_exact_copy(numerator,&polynomial);
}

void nt_simulation_loss(const NtSpatialExact *numerator, const NtSpatialExact *denominator,
                        NtSpatialExact *loss, NtSpatialExact *unit)
{
    const NtSimulationDouble x = nt_simulation_ratio(numerator,denominator);
    if (x.high <= 1e-6) {
        NtSpatialExact response, scale;
        nt_simulation_small_response(numerator,denominator,&response,&scale);
        nt_spatial_exact_multiply(loss,numerator,&response);
        nt_spatial_exact_multiply(unit,denominator,&scale);
        return;
    }
    nt_spatial_exact_number(unit,1,0);
    if (x.high > 4096) {
        nt_spatial_exact_copy(loss,unit);
        return;
    }
    NtSpatialExact decay;
    nt_simulation_decay(x,&decay);
    nt_spatial_exact_subtract(loss,unit,&decay);
}

void nt_simulation_exponential_loss(double rate, NtTimeUs elapsed, NtSpatialExact *loss, NtSpatialExact *unit)
{
    if (!rate) {
        nt_spatial_exact_number(loss,0,0);
        nt_spatial_exact_number(unit,1,0);
        return;
    }
    NtSpatialExact numerator, micros;
    nt_simulation_product(&numerator,rate,(double)elapsed);
    nt_spatial_exact_number(&micros,1000000,0);
    nt_simulation_loss(&numerator,&micros,loss,unit);
}
