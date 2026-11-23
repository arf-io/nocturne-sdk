#ifndef NOCTURNE_SIMULATION_H
#define NOCTURNE_SIMULATION_H

#include <nocturne/motion.h>
#include <nocturne/spatial.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { NT_WATER_MAX_WAVES = 32, NT_FLOW_MAX_STAGES = 8,
       NT_FLOW_MAX_INPUTS = 64, NT_SPRAY_MAX_DROPLETS = 4096 };

typedef struct NtWaterWave {
    double amplitude;
    NtPoint cycles_per_unit;       /* World x and z. */
    double cycles_per_second;      /* Positive frequency subtracts phase. */
    double phase_turns;
} NtWaterWave;

typedef struct NtWaterWaveSample {
    double height;
    NtPoint gradient;
    NtVec3 normal;
    double vertical_velocity, laplacian;
} NtWaterWaveSample;

NtResult nt_water_wave_sample(const NtWaterWave *waves, size_t count, NtPoint xz,
                               double seconds, NtWaterWaveSample *out);

typedef struct NtFlowStage {
    NtTimeUs transit_us, residence_us;
} NtFlowStage;

typedef struct NtFlowInput {
    NtTimeUs begin_us, end_us;     /* Half-open interval. Gaps supply zero. */
    double rate;
} NtFlowInput;

typedef struct NtFlow {
    const NtFlowStage *stages;
    size_t stage_count;
    const NtFlowInput *inputs;
    size_t input_count;
} NtFlow;

typedef struct NtFlowPort {
    double volume, rate;
} NtFlowPort;

typedef struct NtFlowSample {
    size_t stage_count;
    NtFlowPort ports[NT_FLOW_MAX_STAGES + 1]; /* Inlet, then successive outlets. */
    double retained[NT_FLOW_MAX_STAGES];      /* Transit plus reservoir per stage. */
} NtFlowSample;

NtResult nt_flow_sample(const NtFlow *flow, NtTimeUs time, NtFlowSample *out);

typedef struct NtFlowSection {
    double width, speed, maximum_depth;
} NtFlowSection;

typedef struct NtFlowSectionSample {
    double depth, carried_rate, excess_rate;
} NtFlowSectionSample;

NtResult nt_flow_section_sample(const NtFlowSection *section, double rate,
                                 NtFlowSectionSample *out);
/* Interpolate selected parts of a mesh between a dry point and the full depth. */
NtResult nt_flow_section_point(NtVec3 dry, NtVec3 depth_direction, double depth,
                                double weight, NtVec3 *out);

typedef struct NtSprayOptions {
    NtVec3 origin, velocity, velocity_spread, acceleration;
    NtTimeUs lifetime_us;
    double droplet_volume;
    uint64_t seed, stream;
} NtSprayOptions;

typedef struct NtSprayBatch {
    NtSprayOptions options;
    NtTimeUs birth_us;
    uint64_t first_id;
    size_t count;
    double emitted_volume, remaining_volume;
} NtSprayBatch;

typedef struct NtSpraySample {
    uint64_t id;
    NtVec3 position, velocity;
    double volume, progress;
} NtSpraySample;

/* Exchange available volume for a bounded batch. The batch owns its options by value. */
NtResult nt_spray_emit(const NtSprayOptions *options, double available_volume, size_t capacity,
                      uint64_t first_id, NtTimeUs birth, NtSprayBatch *out);
/* An inactive droplet writes only active=false. */
NtResult nt_spray_sample(const NtSprayBatch *batch, size_t index, NtTimeUs now,
                         bool *active, NtSpraySample *out);

enum { NT_TRANSPORT_MAX_CELLS = 128, NT_TRANSPORT_MAX_EDGES = 512,
       NT_TRANSPORT_DRAIN = NT_TRANSPORT_MAX_CELLS };

typedef struct NtThermalStore {
    double heat_capacity, energy;
} NtThermalStore;

typedef struct NtThermalPair {
    NtThermalStore first, second;
} NtThermalPair;

/* Conductive contact between two isolated stores of constant heat capacity. */
NtResult nt_thermal_exchange(const NtThermalPair *stores, double conductance,
                               NtTimeUs elapsed, NtThermalPair *out);

typedef struct NtTransportCell {
    double water, foam, energy;
} NtTransportCell;

typedef struct NtTransportCellOptions {
    double water_capacity, foam_capacity, substrate_heat_capacity;
    double inlet_rate, inlet_temperature;
    double foam_generation, foam_decay;
    double evaporation, latent_heat;
    double ambient_temperature, ambient_conductance, power;
} NtTransportCellOptions;

typedef struct NtTransportEdge {
    size_t source, destination;  /* Destination may be NT_TRANSPORT_DRAIN. */
    double water_rate, foam_rate, conductance;
} NtTransportEdge;

typedef struct NtTransportOptions {
    const NtTransportCellOptions *cells;
    size_t cell_count;
    const NtTransportEdge *edges;
    size_t edge_count;
    double water_heat_capacity; /* Per unit of volume. */
} NtTransportOptions;

typedef struct NtTransportBudget {
    double water_inlet, water_drained, water_spilled, water_evaporated;
    double foam_generated, foam_drained, foam_spilled, foam_decayed;
    double heat_inlet, heat_drained, heat_spilled, heat_evaporated, heat_latent;
    double heat_applied, heat_ambient; /* Signed positive into the network. */
} NtTransportBudget;

/* The caller owns all arrays. A failed step preserves cells and budget.
 * A nonempty step with positive elapsed time may fail to allocate scratch storage.
 * Elapsed time is caller supplied. Budget may be NULL. */
NtResult nt_surface_transport_step(const NtTransportOptions *options, NtTransportCell *cells,
                                     NtTimeUs elapsed, NtTransportBudget *budget);

enum { NT_HULL_MAX_VERTICES = 128, NT_HULL_MAX_TRIANGLES = 252,
       NT_WATER_MAX_TRIANGLES = 64 };

typedef struct NtHull NtHull;
typedef struct NtHullTriangle { uint32_t indices[3]; } NtHullTriangle;
typedef struct NtHullGeometry {
    const NtVec3 *vertices;
    size_t vertex_count;
    const NtHullTriangle *triangles;
    size_t triangle_count;
} NtHullGeometry;

/* The hull copies one closed, outward-wound convex boundary in body space. */
NtResult nt_hull_create(const NtAllocator *allocator, const NtHullGeometry *geometry, NtHull **out);
void nt_hull_destroy(NtHull *hull);

typedef struct NtFloatingBody {
    NtVec3 position;              /* World center of mass, whose local position is zero. */
    NtVec3 rotation_axis;         /* World axis of the body-to-world orientation. */
    double rotation_radians;
    double mass;
    NtVec3 principal_inertia;     /* Positive moments along the local axes. */
    NtVec3 linear_velocity, angular_velocity; /* World axes, per second. */
} NtFloatingBody;

NtResult nt_floating_body_transform(const NtFloatingBody *body, NtSpatialTransform *out);

typedef struct NtWaterPlane { NtVec3 point, normal; } NtWaterPlane;
typedef struct NtWaterTriangle { NtVec3 vertices[3]; } NtWaterTriangle;
typedef struct NtHullImmersion {
    double full_volume, displaced_volume;
    NtVec3 center;                /* World center of displaced water, zero when dry. */
    NtVec3 center_offset;         /* World displacement from the body's center of mass. */
} NtHullImmersion;

/* Either immersion query may allocate temporary clipping storage and use
 * the hull allocator to resolve a rounding midpoint. */
/* Water lies below the plane, whose normal must have positive y. */
NtResult nt_hull_immerse_plane(const NtHull *hull, const NtFloatingBody *body,
                               const NtWaterPlane *plane, NtHullImmersion *out);
/* Water exists below finite XZ triangles with disjoint projected interiors. */
NtResult nt_hull_immerse_water(const NtHull *hull, const NtFloatingBody *body,
                               const NtWaterTriangle *triangles, size_t count,
                               NtHullImmersion *out);

typedef struct NtBuoyancy {
    NtVec3 lift, weight, force, torque; /* Torque is about the body's center of mass. */
} NtBuoyancy;

/* Use the immersion from the same body placement. Gravity acts along -Y. */
NtResult nt_hull_buoyancy(const NtHullImmersion *immersion, const NtFloatingBody *body,
                          double density, double gravity, NtBuoyancy *out);

typedef struct NtLinearStore { double mass; NtVec3 velocity; } NtLinearStore;
typedef struct NtLinearPair { NtLinearStore first, second; } NtLinearPair;

typedef struct NtAngularStore { NtVec3 inertia, velocity; } NtAngularStore;
typedef struct NtAngularPair { NtAngularStore first, second; } NtAngularPair;

typedef struct NtDragResult {
    NtVec3 first_velocity, second_velocity;
    NtVec3 impulse_on_second;
    double dissipated_energy;
} NtDragResult;

/* Isolated exchange in one shared frame. The impulse on the first store
 * is the negative of impulse_on_second. */
NtResult nt_linear_drag_exchange(const NtLinearPair *stores, double conductance, NtTimeUs elapsed,
                                  NtDragResult *out);
/* Diagonal inertias and conductances use the same explicit principal frame. */
NtResult nt_angular_drag_exchange(const NtAngularPair *stores, NtVec3 conductance, NtTimeUs elapsed,
                                   NtDragResult *out);

typedef struct NtBodyLoad { NtVec3 force, torque; } NtBodyLoad;

/* Advance under a world-frame load about the center of mass. The input and
 * output snapshots may alias. Time or range failures preserve the output. */
NtResult nt_floating_body_step(const NtFloatingBody *body, const NtBodyLoad *load,
                                NtTimeUs elapsed, NtFloatingBody *out);

#ifdef __cplusplus
}
#endif
#endif
