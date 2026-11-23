#include "simulation_internal.h"
#include "spatial_geometry_internal.h"

#define DRAG_INERTIA_MIN 1e-6
#define DRAG_INERTIA_MAX 1e12

typedef struct DragAxis {
    double first, second, impulse;
    NtSpatialExact dissipated, unit;
} DragAxis;

static bool valid_axis(double inertia, double conductance)
{
    return inertia>=DRAG_INERTIA_MIN && inertia<=DRAG_INERTIA_MAX &&
        conductance>=0 && conductance<=NT_SIMULATION_COORDINATE_LIMIT;
}

static void drag_velocity(DragAxis *result, const NtSpatialExact *equilibrium,
                          const NtSpatialExact *relative, const NtSpatialExact *first_mass,
                          const NtSpatialExact *second_mass, const NtSpatialExact *total_mass,
                          const NtSpatialExact *loss, const NtSpatialExact *unit)
{
    NtSpatialExact retained, tail, mean, denominator, weighted, endpoint;
    nt_spatial_exact_subtract(&retained,unit,loss);
    nt_spatial_exact_multiply(&tail,relative,&retained);
    nt_spatial_exact_multiply(&mean,equilibrium,unit);
    nt_spatial_exact_multiply(&denominator,total_mass,unit);
    nt_spatial_exact_multiply(&weighted,&tail,second_mass);
    nt_spatial_exact_add(&endpoint,&mean,&weighted);
    result->first = nt_spatial_exact_quotient(&endpoint,&denominator);
    nt_spatial_exact_multiply(&weighted,&tail,first_mass);
    nt_spatial_exact_subtract(&endpoint,&mean,&weighted);
    result->second = nt_spatial_exact_quotient(&endpoint,&denominator);
}

static void drag_budgets(DragAxis *result, const NtSpatialExact *relative, const NtSpatialExact *product,
                         const NtSpatialExact *mass, const NtSpatialExact *n, const NtSpatialExact *d,
                         const NtSpatialExact *loss, const NtSpatialExact *unit)
{
    NtSpatialExact relative_mass, moved, denominator;
    nt_spatial_exact_multiply(&relative_mass,relative,product);
    nt_spatial_exact_multiply(&moved,&relative_mass,loss);
    nt_spatial_exact_multiply(&denominator,mass,unit);
    result->impulse = nt_spatial_exact_quotient(&moved,&denominator);

    NtSpatialExact twice, exponent, energy_loss, energy_unit, energy;
    nt_spatial_exact_number(&twice,2,0);
    nt_spatial_exact_multiply(&exponent,n,&twice);
    nt_simulation_loss(&exponent,d,&energy_loss,&energy_unit);
    nt_spatial_exact_multiply(&energy,&relative_mass,relative);
    nt_spatial_exact_multiply(&result->dissipated,&energy,&energy_loss);
    nt_spatial_exact_multiply(&denominator,mass,&energy_unit);
    nt_spatial_exact_multiply(&result->unit,&denominator,&twice);
}

static void drag_axis(double a, double b, double conductance, NtTimeUs elapsed,
                      double u, double v, DragAxis *out)
{
    if (!conductance || !elapsed || u==v) {
        out->first = u;
        out->second = v;
        out->impulse = 0;
        nt_spatial_exact_number(&out->dissipated,0,0);
        nt_spatial_exact_number(&out->unit,1,0);
        return;
    }
    NtSpatialExact first_mass, second_mass, mass, product, time, micros, n, d, loss, unit;
    nt_spatial_exact_number(&first_mass,a,0);
    nt_spatial_exact_number(&second_mass,b,0);
    nt_spatial_exact_add(&mass,&first_mass,&second_mass);
    nt_spatial_exact_multiply(&product,&first_mass,&second_mass);
    nt_simulation_product(&time,conductance,(double)elapsed);
    nt_spatial_exact_multiply(&n,&time,&mass);
    nt_spatial_exact_number(&micros,1000000,0);
    nt_spatial_exact_multiply(&d,&product,&micros);
    nt_simulation_loss(&n,&d,&loss,&unit);

    NtSpatialExact first, second, equilibrium, relative;
    nt_simulation_product(&first,a,u);
    nt_simulation_product(&second,b,v);
    nt_spatial_exact_add(&equilibrium,&first,&second);
    nt_exact_difference(u,v,&relative);
    drag_velocity(out,&equilibrium,&relative,&first_mass,&second_mass,&mass,&loss,&unit);
    drag_budgets(out,&relative,&product,&mass,&n,&d,&loss,&unit);
}

static void add_energy(NtSpatialExact *total, NtSpatialExact *unit, const DragAxis *axis)
{
    if (!axis->dissipated.count) return;
    NtSpatialExact a, b, sum, denominator;
    nt_spatial_exact_multiply(&a,total,&axis->unit);
    nt_spatial_exact_multiply(&b,&axis->dissipated,unit);
    nt_spatial_exact_add(&sum,&a,&b);
    nt_spatial_exact_multiply(&denominator,unit,&axis->unit);
    nt_spatial_exact_copy(total,&sum);
    nt_spatial_exact_copy(unit,&denominator);
}

static NtResult drag_exchange(NtVec3 first_inertia, NtVec3 second_inertia, NtVec3 conductance,
                              NtVec3 first, NtVec3 second, NtTimeUs elapsed, NtDragResult *out)
{
    if (elapsed>NT_SIMULATION_STEP_LIMIT) return NT_RESULT_OUT_OF_RANGE;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    const double a[3] = {first_inertia.x,first_inertia.y,first_inertia.z};
    const double b[3] = {second_inertia.x,second_inertia.y,second_inertia.z};
    const double g[3] = {conductance.x,conductance.y,conductance.z};
    if (!nt_simulation_vector(first) || !nt_simulation_vector(second))
        return nt_simulation_finish(&saved,NT_RESULT_INVALID_ARGUMENT);
    for (unsigned i=0;i<3;++i)
        if (!valid_axis(a[i],g[i]) || !valid_axis(b[i],g[i]))
            return nt_simulation_finish(&saved,NT_RESULT_INVALID_ARGUMENT);

    const double u[3] = {first.x,first.y,first.z}, v[3] = {second.x,second.y,second.z};
    double updated_a[3], updated_b[3], impulse[3];
    NtSpatialExact dissipated, unit;
    nt_spatial_exact_number(&dissipated,0,0);
    nt_spatial_exact_number(&unit,1,0);
    for (unsigned i=0;i<3;++i) {
        DragAxis axis;
        drag_axis(a[i],b[i],g[i],elapsed,u[i],v[i],&axis);
        updated_a[i] = axis.first;
        updated_b[i] = axis.second;
        impulse[i] = axis.impulse;
        add_energy(&dissipated,&unit,&axis);
    }
    const NtDragResult result = {
        {updated_a[0],updated_a[1],updated_a[2]},
        {updated_b[0],updated_b[1],updated_b[2]},
        {impulse[0],impulse[1],impulse[2]},
        nt_spatial_exact_quotient(&dissipated,&unit)
    };
    const NtResult status = nt_simulation_finish(&saved,NT_RESULT_OK);
    if (status==NT_RESULT_OK) *out = result;
    return status;
}

NtResult nt_linear_drag_exchange(const NtLinearPair *stores, double conductance, NtTimeUs elapsed,
                                  NtDragResult *out)
{
    if (!stores || !out) return NT_RESULT_INVALID_ARGUMENT;
    const double a = stores->first.mass, b = stores->second.mass;
    return drag_exchange((NtVec3){a,a,a},(NtVec3){b,b,b},(NtVec3){conductance,conductance,conductance},
                         stores->first.velocity,stores->second.velocity,elapsed,out);
}

NtResult nt_angular_drag_exchange(const NtAngularPair *stores, NtVec3 conductance, NtTimeUs elapsed,
                                   NtDragResult *out)
{
    if (!stores || !out) return NT_RESULT_INVALID_ARGUMENT;
    return drag_exchange(stores->first.inertia,stores->second.inertia,conductance,
                         stores->first.velocity,stores->second.velocity,elapsed,out);
}
