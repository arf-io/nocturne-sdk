#include <nocturne/simulation.h>
#include <fenv.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "simulation_worker.h"

#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr,"surface transport line %d: %s\n",__LINE__,#test); return 1; \
} } while (0)
#define OK(call) CHECK((call) == NT_RESULT_OK)
#define SECOND UINT64_C(1000000)

static bool near(double value, double expected)
{
    return fabs(value-expected) <= 5e-12*fmax(1.0,fabs(expected));
}

static bool same(NtTransportCell a, NtTransportCell b)
{
    return a.water == b.water && a.foam == b.foam && a.energy == b.energy;
}

static int exchange(void)
{
    NtThermalPair pair = {{2,600},{6,600}}, result;
    OK(nt_thermal_exchange(&pair,3,SECOND,&result));
    const double difference = 200*exp(-2);
    CHECK(near(result.first.energy,2*(150+.75*difference)));
    CHECK(near(result.second.energy,6*(150-.25*difference)));
    CHECK(near(result.first.energy+result.second.energy,1200));
    OK(nt_thermal_exchange(&pair,3,0,&pair));
    CHECK(pair.first.energy==600 && pair.second.energy==600);
    CHECK(nt_thermal_exchange(&pair,3,SECOND+1,&result)==NT_RESULT_OUT_OF_RANGE);
    CHECK(near(result.first.energy,2*(150+.75*difference)));
    pair = (NtThermalPair){{1e-6,1e24},{1e12,0}};
    OK(nt_thermal_exchange(&pair,1e6,SECOND,&result));
    CHECK(near(result.first.energy,1e6) && result.second.energy==1e24);
    pair = (NtThermalPair){{1e-6,1e24},{1e-6,1e24}};
    OK(nt_thermal_exchange(&pair,1e-6,SECOND,&result));
    CHECK(result.first.energy==1e24 && result.second.energy==1e24);
    pair = (NtThermalPair){{2,1e24},{1,5e23}};
    OK(nt_thermal_exchange(&pair,.25,SECOND,&result));
    CHECK(result.first.energy==pair.first.energy && result.second.energy==pair.second.energy);
    pair = (NtThermalPair){{1,1e24},{1,.9999*1e24}};
    OK(nt_thermal_exchange(&pair,1e-7,1,&result));
    CHECK(result.first.energy==pair.first.energy && result.second.energy==pair.second.energy);
    pair = (NtThermalPair){{98943258681.00027,1e24},{2.257004654260098e-5,186396561.61701056}};
    OK(nt_thermal_exchange(&pair,348.8634777583258,483723,&result));
    CHECK(result.first.energy<=pair.first.energy && result.second.energy>pair.second.energy);
    pair = (NtThermalPair){{1e-6,1e24},{1e-6,0}};
    OK(nt_thermal_exchange(&pair,0x1p-1074,1,&result));
    CHECK(result.first.energy==1e24 && fabs(result.second.energy/0x1.a784379d99db4p-995-1) < 1e-12);
    const NtThermalPair previous = result;
    pair = (NtThermalPair){{0x1.a784379d99db4p-1,1e24},{0x1.a784379d99db3p-1,1e24}};
    CHECK(nt_thermal_exchange(&pair,1000,SECOND,&result)==NT_RESULT_OUT_OF_RANGE);
    CHECK(result.first.energy==previous.first.energy && result.second.energy==previous.second.energy);
    pair = (NtThermalPair){{1,0x1p-1074},{4,0}};
    OK(nt_thermal_exchange(&pair,.64,SECOND,&result));
    CHECK(result.first.energy==0x1p-1074 && result.second.energy==0);
    return 0;
}

static int transport(void)
{
    const double ln2 = 0x1.62e42fefa39efp-1;
    const NtTransportCellOptions locals[] = {
        {.water_capacity=8,.foam_capacity=4,.substrate_heat_capacity=2,.foam_generation=2},
        {.water_capacity=3,.foam_capacity=1,.substrate_heat_capacity=2,
         .inlet_rate=2,.inlet_temperature=300,.foam_decay=ln2},
        {.water_capacity=8,.foam_capacity=8,.substrate_heat_capacity=2},
    };
    const NtTransportEdge edges[] = {
        {0,1,6,1,0},{0,NT_TRANSPORT_DRAIN,2,1,0},{1,2,9,1,0}
    };
    const NtTransportOptions o = {locals,3,edges,3,2};
    const NtTransportCell original[] = {{4,4,2000},{0,0,200},{3,1,3200}};
    NtTransportCell cells[3];
    memcpy(cells,original,sizeof cells);
    NtTransportBudget budget;
    OK(nt_surface_transport_step(&o,cells,SECOND,&budget));
    CHECK(cells[0].water==0 && cells[0].foam==2 && near(cells[0].energy,400));
    CHECK(cells[1].water==3 && cells[1].foam==1 && near(cells[1].energy,5200.0/3));
    CHECK(same(cells[2],original[2]));
    CHECK(budget.water_drained==1 && budget.water_spilled==2 && budget.water_inlet==2);
    CHECK(budget.foam_drained==2 && budget.foam_generated==2 && near(budget.foam_decayed,1));
    CHECK(near(budget.heat_inlet,1200) && near(budget.heat_drained,400) &&
          near(budget.heat_spilled,2600.0/3));
    CHECK(near(cells[0].energy+cells[1].energy+cells[2].energy,
               5400+budget.heat_inlet-budget.heat_drained-budget.heat_spilled));

    NtTransportCell restored[3];
    memcpy(restored,original,sizeof restored);
    OK(nt_surface_transport_step(&o,restored,SECOND,NULL));
    for (unsigned i=0;i<3;++i) CHECK(same(cells[i],restored[i]));
    /* A caller can remove a cell by supplying a new topology and snapshot. */
    NtTransportOptions cropped = {locals,1,NULL,0,2};
    OK(nt_surface_transport_step(&cropped,restored,SECOND,NULL));
    CHECK(same(cells[1],restored[1]) && same(cells[2],restored[2]));
    return 0;
}

static int atmosphere(void)
{
    const double ln2 = 0x1.62e42fefa39efp-1;
    NtTransportCellOptions local = {
        .water_capacity=4,.foam_capacity=2,.substrate_heat_capacity=2,.evaporation=ln2,
        .latent_heat=5,.ambient_temperature=50,.ambient_conductance=3*ln2,.power=4
    };
    const NtTransportOptions o = {&local,1,NULL,0,1};
    NtTransportCell cell = {2,0,400};
    NtTransportBudget b;
    OK(nt_surface_transport_step(&o,&cell,SECOND,&b));
    const double pre = 295, expected = pre*.5+150*.5+2/ln2;
    CHECK(near(cell.water,1) && near(cell.energy,expected));
    CHECK(near(b.heat_evaporated,100) && near(b.heat_latent,5) && b.heat_applied==4);
    CHECK(near(cell.energy,400-b.heat_evaporated-b.heat_latent+b.heat_applied+b.heat_ambient));

    local.latent_heat=1000;
    local.ambient_conductance=0;
    local.power=0;
    cell = (NtTransportCell){2,0,44};
    OK(nt_surface_transport_step(&o,&cell,SECOND,&b));
    CHECK(near(b.water_evaporated,44.0/1011) && cell.energy==0);
    CHECK(near(b.heat_evaporated+b.heat_latent,44));

    local.evaporation=40;
    local.latent_heat=0;
    local.foam_decay=40;
    cell = (NtTransportCell){2,1,400};
    OK(nt_surface_transport_step(&o,&cell,SECOND,&b));
    CHECK(fabs(cell.water/(2*exp(-40))-1) < 1e-12);
    CHECK(fabs(cell.foam/exp(-40)-1) < 1e-12);
    CHECK(near(cell.energy,200));
    local.evaporation=50;
    local.latent_heat=1e-30;
    local.water_capacity=100000;
    local.substrate_heat_capacity=1e-6;
    const NtTransportOptions high_specific_heat = {&local,1,NULL,0,1e6};
    cell = (NtTransportCell){100000,0,1e20};
    OK(nt_surface_transport_step(&high_specific_heat,&cell,SECOND,&b));
    CHECK(fabs(cell.water/(100000*exp(-50))-1) < 1e-12);
    CHECK(near(cell.energy,(1e-6+cell.water*1e6)*1e9));
    local = (NtTransportCellOptions){.substrate_heat_capacity=1,.power=-1e18};
    cell = (NtTransportCell){0,0,33e12};
    const NtTransportOptions dry = {&local,1,NULL,0,0};
    OK(nt_surface_transport_step(&dry,&cell,33,&b));
    CHECK(cell.energy==0 && b.heat_applied==-33e12);

    local = (NtTransportCellOptions){.substrate_heat_capacity=1e6,.water_capacity=999999,
                                     .power=1e18,.ambient_conductance=1e-5};
    cell = (NtTransportCell){999999,0,0};
    OK(nt_surface_transport_step(&high_specific_heat,&cell,SECOND,&b));
    CHECK(cell.energy==1e18 && fabs(b.heat_ambient+5) < 1e-10);
    local.power=-1e18;
    cell = (NtTransportCell){999999,0,1e18};
    CHECK(nt_surface_transport_step(&high_specific_heat,&cell,SECOND,&b)==NT_RESULT_OUT_OF_RANGE);
    CHECK(cell.energy==1e18 && fabs(b.heat_ambient+5) < 1e-10);
    local = (NtTransportCellOptions){.substrate_heat_capacity=1,.ambient_temperature=1,
                                     .ambient_conductance=6,.power=-6};
    cell = (NtTransportCell){0,0,0};
    OK(nt_surface_transport_step(&dry,&cell,SECOND,&b));
    CHECK(cell.energy==0 && b.heat_applied==-6 && b.heat_ambient==6);
    local = (NtTransportCellOptions){.substrate_heat_capacity=1e6,.ambient_temperature=1e6,
                                     .ambient_conductance=20000,.power=-2e10};
    cell = (NtTransportCell){0,0,1e-10};
    OK(nt_surface_transport_step(&dry,&cell,SECOND,&b));
    CHECK(fabs(cell.energy/(1e-10*exp(-.02))-1) < 1e-12);
    local = (NtTransportCellOptions){.substrate_heat_capacity=1,.ambient_temperature=1,
                                     .ambient_conductance=750,.power=-750};
    cell = (NtTransportCell){0,0,1e24};
    OK(nt_surface_transport_step(&dry,&cell,SECOND,&b));
    CHECK(fabs(cell.energy/0x1.a150b00730349p-1003-1) < 1e-12);
    local = (NtTransportCellOptions){.substrate_heat_capacity=1e-6,.ambient_conductance=0x1p-1074};
    cell = (NtTransportCell){0,0,1e24};
    OK(nt_surface_transport_step(&dry,&cell,1,&b));
    CHECK(cell.energy==1e24 && fabs(b.heat_ambient/-0x1.a784379d99db4p-995-1) < 1e-12);
    local = (NtTransportCellOptions){.water_capacity=1e6,.foam_capacity=1e6,
                                     .substrate_heat_capacity=1,.foam_decay=750,.evaporation=750};
    const NtTransportOptions wet = {&local,1,NULL,0,1};
    cell = (NtTransportCell){1e6,1e6,0};
    OK(nt_surface_transport_step(&wet,&cell,SECOND,&b));
    CHECK(cell.water==0x0.0000000000f09p-1022 && cell.foam==cell.water);

    local = (NtTransportCellOptions){.substrate_heat_capacity=1,.ambient_conductance=4,
                                     .power=-0x1.53601abd13d8fp+49};
    cell = (NtTransportCell){0,0,1e16};
    OK(nt_surface_transport_step(&dry,&cell,SECOND,&b));
    CHECK(fabs(cell.energy/0x1.563c329e7b0c9p-7-1) < 1e-10);
    local = (NtTransportCellOptions){.substrate_heat_capacity=1,.ambient_temperature=.5,
                                     .ambient_conductance=0x1p-60,.power=-1};
    cell = (NtTransportCell){0,0,1};
    OK(nt_surface_transport_step(&dry,&cell,SECOND,&b));
    CHECK(fabs(cell.energy/0x1.5555555555555p-124-1) < 1e-12 && cell.energy==b.heat_ambient);
    local.substrate_heat_capacity = 0x1.0000000000001p+0;
    local.ambient_temperature = 0x1.ffffffffffffep-2;
    local.ambient_conductance = 0x1p-102;
    cell = (NtTransportCell){0,0,1};
    CHECK(nt_surface_transport_step(&dry,&cell,SECOND,&b)==NT_RESULT_OUT_OF_RANGE);
    CHECK(cell.energy==1);
    local = (NtTransportCellOptions){.water_capacity=1,.substrate_heat_capacity=1,
                                     .latent_heat=0x1p-1074,.evaporation=.25};
    cell = (NtTransportCell){1,0,0};
    OK(nt_surface_transport_step(&dry,&cell,SECOND,&b));
    CHECK(cell.water==1 && b.water_evaporated==0 && b.heat_latent==0);
    local.latent_heat = 0x1p-1073;
    local.evaporation = 1;
    cell = (NtTransportCell){1,0,0x1p-1074};
    OK(nt_surface_transport_step(&dry,&cell,SECOND,&b));
    CHECK(cell.water==.5 && b.water_evaporated==.5 && b.heat_latent==0x1p-1074);
    local.evaporation = .6;
    cell = (NtTransportCell){1,0,0x1p-1074};
    OK(nt_surface_transport_step(&dry,&cell,SECOND,&b));
    CHECK(near(cell.water,exp(-.6)) && b.water_evaporated<.5);
    local = (NtTransportCellOptions){.water_capacity=1e6,.substrate_heat_capacity=1,
                                     .latent_heat=1e12,.evaporation=ln2,.power=-10};
    cell = (NtTransportCell){1e6,0,5e17};
    OK(nt_surface_transport_step(&dry,&cell,SECOND,&b));
    CHECK(near(cell.energy,1.595234069231497) && b.heat_applied==-10);
    local.evaporation=40;
    local.power=-1;
    cell = (NtTransportCell){1e6,0,1e18};
    OK(nt_surface_transport_step(&dry,&cell,SECOND,&b));
    CHECK(near(cell.energy,1e18*exp(-40)-1) && b.heat_applied==-1);
    return 0;
}

static int small_transfers(void)
{
    const double capacity = 0x1p19, added = 0x1p-34, energy = 1e18;
    NtTransportCellOptions local = {
        .water_capacity=capacity,.foam_capacity=capacity,.substrate_heat_capacity=1,
        .inlet_rate=added,.foam_generation=added
    };
    const NtTransportOptions one = {&local,1,NULL,0,1};
    const NtTransportCell full = {capacity,capacity,energy};
    NtTransportCell cell = full;
    NtTransportBudget b;
    OK(nt_surface_transport_step(&one,&cell,SECOND,&b));
    CHECK(cell.water==capacity && cell.foam==capacity && cell.energy==nextafter(energy,0));
    CHECK(b.water_spilled==added && b.foam_spilled==added);
    CHECK(near(b.heat_spilled,energy*added/(capacity+1+added)));

    local.power = -energy;
    cell = full;
    b = (NtTransportBudget){.water_spilled=17,.heat_applied=93};
    CHECK(nt_surface_transport_step(&one,&cell,SECOND,&b)==NT_RESULT_OUT_OF_RANGE);
    CHECK(same(cell,full) && b.water_spilled==17 && b.heat_applied==93);
    cell.water = nextafter(capacity,0);
    OK(nt_surface_transport_step(&one,&cell,SECOND,&b));
    CHECK(cell.energy==0 && b.water_spilled==0);

    const double tiny = 0x1p-1074;
    const NtTransportCellOptions cells[] = {
        {.water_capacity=1,.foam_capacity=1,.substrate_heat_capacity=1},
        {.water_capacity=1,.foam_capacity=1,.substrate_heat_capacity=1},
        {.water_capacity=1,.foam_capacity=1,.substrate_heat_capacity=1,.power=-4*tiny}
    };
    const NtTransportEdge edges[] = {
        {0,2,tiny,tiny,0},{0,2,tiny,tiny,0},{1,2,tiny,tiny,0},{1,2,tiny,tiny,0}
    };
    const NtTransportOptions network = {cells,3,edges,4,1};
    NtTransportCell state[] = {{1,1,4},{1,1,4},{0}};
    OK(nt_surface_transport_step(&network,state,SECOND/4,&b));
    CHECK(same(state[2],(NtTransportCell){tiny,tiny,tiny}) && b.heat_applied==-tiny);
    return 0;
}

static int routed_midpoints(void)
{
    NtTransportCellOptions local[] = {
        {.water_capacity=1,.substrate_heat_capacity=1},
        {.water_capacity=1,.substrate_heat_capacity=1},
        {.water_capacity=2,.substrate_heat_capacity=1}
    };
    NtTransportEdge edges[] = {
        {0,2,1,0,0},{0,NT_TRANSPORT_DRAIN,5,0,0},{1,2,5,0,0},{1,NT_TRANSPORT_DRAIN,1,0,0},
        {0,NT_TRANSPORT_DRAIN,0x1p-150,0,0}
    };
    NtTransportOptions network = {local,3,edges,4,1};
    const NtTransportCell warm[] = {{1,0,0x1p27},{1,0,0x1p27},{0,0,1e24}};
    NtTransportCell cells[3];
    NtTransportBudget b;
    for (size_t count=4;count<=5;++count) {
        network.edge_count = count;
        memcpy(cells,warm,sizeof cells);
        OK(nt_surface_transport_step(&network,cells,SECOND,&b));
        CHECK(cells[2].water==1 && cells[2].energy==1e24 && b.heat_drained==0x1p26);
    }
    edges[4].destination = 2;
    memcpy(cells,warm,sizeof cells);
    CHECK(nt_surface_transport_step(&network,cells,SECOND,NULL)==NT_RESULT_OUT_OF_RANGE);
    CHECK(same(cells[2],warm[2]));

    network.edge_count = 4;
    local[2].power = -0x1p-1074;
    const NtTransportCell cold[] = {{1,0,0x1p-1074},{1,0,0x1p-1074},{0,0,0}};
    memcpy(cells,cold,sizeof cells);
    b = (NtTransportBudget){.heat_applied=19};
    CHECK(nt_surface_transport_step(&network,cells,SECOND,&b)==NT_RESULT_OUT_OF_RANGE);
    CHECK(same(cells[0],cold[0]) && same(cells[2],cold[2]) && b.heat_applied==19);

    /* At the receiver cap of zero, the exact mixed heat is halved by
     * spilling one unit of water. The retained heat still ties down. */
    local[2].water_capacity = 0;
    local[2].power = -nextafter(5e16,INFINITY);
    const NtTransportCell spill[] = {{1,0,16},{1,0,16},{0,0,1e17}};
    memcpy(cells,spill,sizeof cells);
    CHECK(nt_surface_transport_step(&network,cells,SECOND,NULL)==NT_RESULT_OUT_OF_RANGE);
    CHECK(same(cells[0],spill[0]) && same(cells[2],spill[2]));
    return 0;
}

static int failures(void)
{
    NtTransportCellOptions local = {.water_capacity=4,.foam_capacity=3,.substrate_heat_capacity=2,
                                    .inlet_rate=1,.power=-1e10};
    NtTransportEdge edge = {0,NT_TRANSPORT_DRAIN,1,1,0};
    const NtTransportOptions o = {&local,1,&edge,1,1};
    const NtTransportCell original = {3,2,100};
    NtTransportCell cell = original;
    NtTransportBudget b = {.water_inlet=91,.heat_ambient=92};
    CHECK(nt_surface_transport_step(&o,&cell,SECOND+1,&b)==NT_RESULT_OUT_OF_RANGE);
    CHECK(nt_surface_transport_step(&o,&cell,SECOND,&b)==NT_RESULT_OUT_OF_RANGE);
    CHECK(same(cell,original) && b.water_inlet==91 && b.heat_ambient==92);
    edge.conductance=1;
    CHECK(nt_surface_transport_step(&o,&cell,0,&b)==NT_RESULT_INVALID_ARGUMENT);
    CHECK(same(cell,original) && b.water_inlet==91 && b.heat_ambient==92);
    edge.conductance=0;
    OK(nt_surface_transport_step(&o,&cell,0,&b));
    CHECK(same(cell,original) && b.water_inlet==0 && b.heat_ambient==0);
    local.power=0;
#if !defined(__wasm__)
    fenv_t save;
    CHECK(fegetenv(&save)==0 && fesetround(FE_UPWARD)==0 && feclearexcept(FE_ALL_EXCEPT)==0 &&
          feraiseexcept(FE_DIVBYZERO)==0);
    const int earlier = fetestexcept(FE_ALL_EXCEPT);
    const NtResult status = nt_surface_transport_step(&o,&cell,SECOND,&b);
    const int rounding = fegetround(), later = fetestexcept(FE_ALL_EXCEPT);
    CHECK(fesetenv(&save)==0 && status==NT_RESULT_OK && rounding==FE_UPWARD && earlier==later);
#else
    OK(nt_surface_transport_step(&o,&cell,SECOND,&b));
#endif
    CHECK(near(b.water_drained,1) && near(b.foam_drained,2));
    return 0;
}

static NtTransportCell inventory(const NtTransportCell *cells, size_t count)
{
    NtTransportCell total = {0};
    for (size_t i=0;i<count;++i) {
        total.water += cells[i].water;
        total.foam += cells[i].foam;
        total.energy += cells[i].energy;
    }
    return total;
}

static int stress(void)
{
    NtTransportCellOptions local[NT_TRANSPORT_MAX_CELLS] = {{0}};
    NtTransportCell current[NT_TRANSPORT_MAX_CELLS], original[NT_TRANSPORT_MAX_CELLS];
    NtTransportEdge edges[NT_TRANSPORT_MAX_EDGES];
    for (size_t i=0;i<NT_TRANSPORT_MAX_CELLS;++i) {
        local[i] = (NtTransportCellOptions){.water_capacity=10,.foam_capacity=4,.substrate_heat_capacity=2,
                   .inlet_rate=1,.inlet_temperature=4,.foam_generation=.2,.foam_decay=.3,
                   .evaporation=.2,.latent_heat=3,.ambient_temperature=5,.ambient_conductance=2,.power=1};
        original[i] = (NtTransportCell){(double)(i%9),(double)(i%4),(double)(10+i)};
        for (unsigned j=0;j<4;++j) {
            const size_t destination = j==3 ? NT_TRANSPORT_DRAIN : (i+1+17*j)%NT_TRANSPORT_MAX_CELLS;
            edges[4*i+j] = (NtTransportEdge){i,destination,.25*(j+1),.125*(j+1),j<3 ? .75 : 0};
        }
    }
    const NtTransportOptions o = {local,NT_TRANSPORT_MAX_CELLS,edges,NT_TRANSPORT_MAX_EDGES,1.5};
    memcpy(current,original,sizeof current);
    for (unsigned j=1;j<=128;++j) {
        const NtTransportCell before = inventory(current,o.cell_count);
        NtTransportBudget b;
        OK(nt_surface_transport_step(&o,current,1000*j,&b));
        const NtTransportCell after = inventory(current,o.cell_count);
        CHECK(near(after.water,before.water+b.water_inlet-b.water_drained-b.water_spilled-b.water_evaporated));
        CHECK(near(after.foam,before.foam+b.foam_generated-b.foam_drained-b.foam_spilled-b.foam_decayed));
        CHECK(near(after.energy,before.energy+b.heat_inlet-b.heat_drained-b.heat_spilled-b.heat_evaporated-
                               b.heat_latent+b.heat_applied+b.heat_ambient));
    }
    for (unsigned j=1;j<=128;++j) OK(nt_surface_transport_step(&o,original,1000*j,NULL));
    for (size_t i=0;i<o.cell_count;++i) CHECK(same(current[i],original[i]));
    return 0;
}

static int run_contract(void)
{
    return exchange() || transport() || atmosphere() || small_transfers() || routed_midpoints() ||
           failures() || stress();
}
