#include <nocturne/spatial_mesh.h>
#include <nocturne/component.h>

#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr,"spatial mesh line %d: %s\n",__LINE__,#condition); return 1; \
} } while (0)
#define OK(call) CHECK((call) == NT_RESULT_OK)
#define BYTES(value) (NtByteView){value,sizeof(value)-1}

static const char model[] =
    "# concave face with relative coordinates and UVs\r\n"
    "mtllib untouched-reference.mtl\n"
    "v -2 -2 -3\nv 2 -2 -3\nv 2 2 -3\nv -0e999 0 -3\nv -2 2 -3\n"
    "vt 0 0\nvt 1 0\nvt 1 1\nvt .5 .5\nvt 0 1\nvn 0 0 1\n"
    "f -5/-5/1 -4/-4/1 -3/-3/1 -2/-2/1 -1/-1/1 # next line optional";

static const NtSpatialVertex triangle[] = {
    {{-2,-2,-2},{0,0}}, {{2,-2,-2},{1,0}}, {{0,2,-4},{1,1}}
};
static const NtSpatialFace faces[] = {{{0,1,2},123456}};

static int ownership_and_models(void)
{
    NtSpatialVertex mutable_vertices[3];
    memcpy(mutable_vertices,triangle,sizeof(triangle));
    NtSpatialMesh *mesh;
    NtSpatialMeshData data = {mutable_vertices,3,faces,1};
    OK(nt_spatial_mesh_create(NULL,&data,&mesh));
    mutable_vertices[1].position.x = 100;
    OK(nt_spatial_mesh_data(mesh,&data));
    CHECK(data.vertices != mutable_vertices && data.vertices[1].position.x == 2);
    CHECK(data.faces[0].primitive_id == 123456);
    const NtSpatialFace bad = {{0,1,3},5};
    data.faces = &bad;
    NtSpatialMesh *prior = mesh;
    CHECK(nt_spatial_mesh_create(NULL,&data,&mesh) == NT_RESULT_INVALID_ARGUMENT && mesh == prior);
    nt_spatial_mesh_destroy(mesh);

    OK(nt_spatial_mesh_decode_obj(NULL,BYTES(model),&mesh));
    OK(nt_spatial_mesh_data(mesh,&data));
    CHECK(data.vertex_count == 5 && data.face_count == 3);
    CHECK(data.vertices[3].position.x == 0 && signbit(data.vertices[3].position.x));
    CHECK(data.vertices[3].uv.x == .5 && data.vertices[3].uv.y == .5);
    double twice_area = 0;
    for (size_t i = 0; i < data.face_count; ++i) {
        const NtSpatialFace face = data.faces[i];
        const NtVec3 a = data.vertices[face.indices[0]].position;
        const NtVec3 b = data.vertices[face.indices[1]].position;
        const NtVec3 c = data.vertices[face.indices[2]].position;
        const double area = (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
        CHECK(area > 0 && face.primitive_id == i);
        twice_area += area;
    }
    CHECK(twice_area == 24);
    prior = mesh;
    CHECK(nt_spatial_mesh_decode_obj(NULL,BYTES("v 0 0 0\nf 1 2 3"),&mesh) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(nt_spatial_mesh_decode_obj(NULL,BYTES("v 1e1000 0 0"),&mesh) == NT_RESULT_OUT_OF_RANGE);
    CHECK(nt_spatial_mesh_decode_obj(NULL,BYTES("curv 1 2"),&mesh) == NT_RESULT_UNSUPPORTED_CAPABILITY);
    CHECK(nt_spatial_mesh_decode_obj(NULL,BYTES("v 0 0 0\nv 1 1 0\nv 1 0 0\nv 0 1 0\nf 1 2 3 4"),&mesh)
          == NT_RESULT_INVALID_ARGUMENT);
    CHECK(nt_spatial_mesh_decode_obj(NULL,BYTES("v 0 0 0\nv 1 0 0\nv 1 1 1\nv 0 1 0\nf 1 2 3 4"),&mesh)
          == NT_RESULT_INVALID_ARGUMENT);
    const char prefix[] = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf ";
    char overlong[sizeof(prefix)-1+NT_SPATIAL_OBJ_MAX_NUMBER_BYTES+5];
    size_t length = sizeof(prefix)-1;
    memcpy(overlong,prefix,length);
    memset(overlong+length,'0',NT_SPATIAL_OBJ_MAX_NUMBER_BYTES);
    length += NT_SPATIAL_OBJ_MAX_NUMBER_BYTES;
    memcpy(overlong+length,"1 2 3",5);
    CHECK(nt_spatial_mesh_decode_obj(NULL,(NtByteView){overlong,sizeof(overlong)},&mesh)
          == NT_RESULT_OUT_OF_RANGE);
    CHECK(mesh == prior);
    nt_spatial_mesh_destroy(mesh);

    const char last_finite[] = "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
                               "vt 1.7976931348623158e308 -1.7976931348623158e308\nf 1/1 2 3";
    OK(nt_spatial_mesh_decode_obj(NULL,BYTES(last_finite),&mesh));
    OK(nt_spatial_mesh_data(mesh,&data));
    CHECK(data.vertices[0].uv.x == DBL_MAX && data.vertices[0].uv.y == -DBL_MAX);
    prior = mesh;
    CHECK(nt_spatial_mesh_decode_obj(NULL,BYTES("v 1.7976931348623159e308 0 0"),&mesh)
          == NT_RESULT_OUT_OF_RANGE && mesh == prior);
    nt_spatial_mesh_destroy(mesh);
    return 0;
}

static const NtSpatialRasterOptions options = {
    .projection={NT_SPATIAL_PERSPECTIVE,1,1,1,8},
    .samples={3,3}, .background={4,8,16,255}
};
static const NtRgba pixels[] = {{0,255,0,255},{1,2,3,255},{100,100,100,255},{240,120,60,255}};
static const NtImageView texture = {pixels,{2,2},2,4};

static NtSpatialMeshInstance instance(const NtSpatialMesh *mesh)
{
    return (NtSpatialMeshInstance){
        .mesh=mesh, .object_id=71, .owner=81, .tint={255,255,255,255},
        .texture=&texture, .sampling={NT_IMAGE_NEAREST,NT_PAINT_PAD,NT_PAINT_PAD}
    };
}

static int rasterization(void)
{
    NtSpatialMesh *mesh;
    OK(nt_spatial_mesh_create(NULL,&(NtSpatialMeshData){triangle,3,faces,1},&mesh));
    NtSpatialMeshInstance draws[2] = {instance(mesh),instance(mesh)};
    draws[1].object_id = 72;
    draws[1].owner = 82;
    NtSpatialRaster *raster;
    OK(nt_spatial_rasterize(NULL,&options,draws,2,&raster));
    NtSpatialRasterSample sample;
    OK(nt_spatial_raster_sample(raster,(NtCellPoint){1,1},&sample));
    CHECK(sample.covered && sample.owner == 81 && sample.object_id == 71 && sample.primitive_id == 123456);
    CHECK(sample.depth == 3 && sample.uv.x == .75 && sample.uv.y == .5);
    CHECK(sample.color.red == 240 && sample.color.green == 120 && sample.color.blue == 60);
    OK(nt_spatial_raster_sample(raster,(NtCellPoint){1,0},&sample));
    CHECK(!sample.covered && sample.color.blue == 16 && sample.object_id == 0);

    NtSpatialRaster *prior = raster;
    NtSpatialRasterOptions budget = options;
    budget.sample_limit = 8;
    CHECK(nt_spatial_rasterize(NULL,&budget,draws,2,&raster) == NT_RESULT_OUT_OF_RANGE && raster == prior);
    budget.sample_limit = 9;
    budget.triangle_limit = 1;
    CHECK(nt_spatial_rasterize(NULL,&budget,draws,2,&raster) == NT_RESULT_OUT_OF_RANGE && raster == prior);
    nt_spatial_raster_destroy(raster);

    NtSpatialTransform transform = nt_spatial_transform_identity();
    transform.translation.z = 1;
    draws[1].transform = &transform;
    draws[1].texture = NULL;
    draws[1].tint = (NtRgba){40,80,120,255};
    NtSpatialCamera camera;
    OK(nt_spatial_camera_look_at((NtVec3){0,0,1},(NtVec3){0,0,0},(NtVec3){0,1,0},&camera));
    budget = options;
    budget.camera = &camera;
    OK(nt_spatial_rasterize(NULL,&budget,draws,2,&raster));
    nt_spatial_mesh_destroy(mesh);
    OK(nt_spatial_raster_sample(raster,(NtCellPoint){1,1},&sample));
    CHECK(sample.object_id == 72 && sample.owner == 82 && sample.depth == 3);
    CHECK(sample.color.blue == 120);
    NtFrame *frame;
    OK(nt_spatial_raster_frame(raster,NT_SAMPLE_BRAILLE,NULL,&frame));
    nt_spatial_raster_destroy(raster);
    const NtCellSize size = nt_frame_size(frame);
    CHECK(size.columns == 2 && size.rows == 1);
    NtCellView cell;
    OK(nt_frame_cell(frame,(NtCellPoint){0,0},&cell));
    CHECK(cell.glyph.count && cell.owner == 82);
    nt_frame_destroy(frame);
    return 0;
}

static int closed_clip_contact(void)
{
    const NtSpatialVertex contact[] = {
        {{0,0,-1},{.25,.75}}, {{-1,-1,-.5},{0,0}}, {{1,-1,-.5},{1,0}}
    };
    NtSpatialMesh *mesh;
    OK(nt_spatial_mesh_create(NULL,&(NtSpatialMeshData){contact,3,faces,1},&mesh));
    const NtSpatialMeshInstance draw = {.mesh=mesh, .object_id=4, .owner=5};
    NtSpatialRasterOptions grid = options;
    grid.projection.kind = NT_SPATIAL_ORTHOGRAPHIC;
    NtSpatialRaster *raster;
    OK(nt_spatial_rasterize(NULL,&grid,&draw,1,&raster));
    NtSpatialRasterSample sample;
    OK(nt_spatial_raster_sample(raster,(NtCellPoint){1,1},&sample));
    CHECK(sample.covered && sample.depth == 1 && sample.object_id == 4 && sample.owner == 5);
    CHECK(sample.uv.x == .25 && sample.uv.y == .75 && sample.color.blue == grid.background.blue);
    OK(nt_spatial_raster_sample(raster,(NtCellPoint){1,2},&sample));
    CHECK(!sample.covered);
    nt_spatial_raster_destroy(raster);
    nt_spatial_mesh_destroy(mesh);
    return 0;
}

static int exact_grid_center(void)
{
    const NtSpatialVertex edge[] = {
        {{1,-1,-2},{.25,0}}, {{2,0,-2},{1,0}}, {{1,1,-2},{.25,1}}
    };
    NtSpatialMesh *mesh;
    OK(nt_spatial_mesh_create(NULL,&(NtSpatialMeshData){edge,3,faces,1},&mesh));
    const NtSpatialMeshInstance draw = {.mesh=mesh, .object_id=4, .owner=5};
    NtSpatialRasterOptions grid = options;
    grid.samples = (NtCellSize){3,1};
    grid.projection.half_width = .75;
    NtSpatialRaster *raster;
    OK(nt_spatial_rasterize(NULL,&grid,&draw,1,&raster));
    NtSpatialRasterSample sample;
    OK(nt_spatial_raster_sample(raster,(NtCellPoint){2,0},&sample));
    CHECK(sample.covered && sample.depth == 2 && sample.uv.x == .25 && sample.uv.y == .5);
    nt_spatial_raster_destroy(raster);
    nt_spatial_mesh_destroy(mesh);
    return 0;
}

typedef struct MountedRaster {
    const NtSpatialRaster *raster;
    NtSamplingMode mode;
    NtNodeId node;
} MountedRaster;

static NtResult raster_measure(const void *value, NtMeasureContext *context,
                                NtConstraints constraints, NtCellSize *out)
{
    const MountedRaster *mounted = value;
    NtCellSize child;
    const NtResult status = nt_measure_child(context,mounted->node,(NtConstraints){0},&child);
    *out = (NtCellSize){constraints.columns.min,constraints.rows.min};
    return status;
}

static NtResult raster_layout(const void *value, NtLayoutContext *context, NtCellSize size)
{
    (void)size;
    const MountedRaster *mounted = value;
    const NtRowSpan span = {{1,1},1};
    return nt_layout_place(context,mounted->node,(NtCellRect){{1,0},{3,3}},0,&(NtClipMask){&span,1});
}

static NtResult raster_paint(const void *value, NtPaintContext *paint, NtCellSize size)
{
    (void)size;
    const MountedRaster *mounted = value;
    return nt_spatial_raster_paint(mounted->raster,paint,(NtCellPoint){-1,1},mounted->mode);
}

static NtResult raster_leaf_measure(const void *value, NtMeasureContext *context,
                                     NtConstraints constraints, NtCellSize *out)
{
    (void)value;
    (void)context;
    *out = (NtCellSize){constraints.columns.min,constraints.rows.min};
    return NT_RESULT_OK;
}

static NtResult raster_leaf_layout(const void *value, NtLayoutContext *context, NtCellSize size)
{
    (void)value;
    (void)context;
    (void)size;
    return NT_RESULT_OK;
}

static NtResult raster_container_paint(const void *value, NtPaintContext *paint, NtCellSize size)
{
    (void)value;
    (void)paint;
    (void)size;
    return NT_RESULT_OK;
}

static NtResult translucent_paint(const void *value, NtPaintContext *paint, NtCellSize size)
{
    NtResult status = nt_paint_fill(paint,(NtCellRect){{0,0},size},(NtRgba){173,82,37,255});
    if (status != NT_RESULT_OK) return status;
    fenv_t saved;
    if (fegetenv(&saved)) return NT_RESULT_OUT_OF_RANGE;
    const int modes[] = {FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
    for (unsigned i = 0; i < sizeof(modes)/sizeof(modes[0]); ++i) {
        if (fesetround(modes[i]) || feclearexcept(FE_ALL_EXCEPT) || feraiseexcept(FE_DIVBYZERO)) {
            status = NT_RESULT_OUT_OF_RANGE;
            break;
        }
        const int before = fetestexcept(FE_ALL_EXCEPT);
        status = nt_spatial_raster_paint(value,paint,(NtCellPoint){(int32_t)i,0},NT_SAMPLE_CELL);
        if (fegetround() != modes[i] || fetestexcept(FE_ALL_EXCEPT) != before)
            status = NT_RESULT_OUT_OF_RANGE;
        if (status != NT_RESULT_OK) break;
    }
    return fesetenv(&saved) == 0 ? status : NT_RESULT_OUT_OF_RANGE;
}

static int translucent_composition(void)
{
    const NtSpatialRasterOptions grid = {
        .projection={NT_SPATIAL_ORTHOGRAPHIC,1,1,1,3},.samples={2,4},.background={37,82,173,117}
    };
    NtSpatialRaster *raster;
    OK(nt_spatial_rasterize(NULL,&grid,NULL,0,&raster));
    NtTree *tree;
    OK(nt_tree_create(NULL,&tree));
    NtNodeId node;
    const NtComponentOps ops = {.measure=raster_leaf_measure,.layout=raster_leaf_layout,.paint=translucent_paint};
    OK(nt_tree_mount(tree,(NtNodeId){0},&ops,raster,(NtMountOptions){true,false,false},&node));
    OK(nt_tree_publish(tree,(NtCellSize){4,1}));
    nt_tree_destroy(tree);
    nt_spatial_raster_destroy(raster);
    return 0;
}

static int mounted_paint_and_hit(void)
{
    const NtSpatialVertex vertices[] = {{{-4,-4,-2},{0,0}},{{4,-4,-2},{1,0}},{{0,6,-2},{0,1}}};
    NtSpatialMesh *mesh;
    OK(nt_spatial_mesh_create(NULL,&(NtSpatialMeshData){vertices,3,faces,1},&mesh));
    const NtSpatialRasterOptions grid = {
        .projection={NT_SPATIAL_ORTHOGRAPHIC,1,1,1,3},.samples={5,7},.background={14,17,19,255}
    };
    const NtSpatialMeshInstance draw = {.mesh=mesh,.object_id=50,.owner=81,.tint={30,80,160,255}};
    NtSpatialRaster *raster;
    OK(nt_spatial_rasterize(NULL,&grid,&draw,1,&raster));
    nt_spatial_mesh_destroy(mesh);
    const NtCellSize cells = nt_spatial_raster_cell_size(raster);
    CHECK(cells.columns == 3 && cells.rows == 2);
    NtSpatialCellHit hit;
    bool found = false;
    OK(nt_spatial_raster_cell_hit(raster,(NtCellPoint){2,1},&found,&hit));
    CHECK(found && hit.sample_position.x == 4 && hit.sample_position.y == 5);
    CHECK(hit.sample.object_id == 50 && hit.sample.owner == 81 && hit.sample.primitive_id == 123456);
    CHECK(nt_spatial_raster_cell_hit(raster,(NtCellPoint){3,1},&found,&hit) == NT_RESULT_OUT_OF_RANGE);
    CHECK(found && hit.sample_position.y == 5);
    NtSpatialRaster *clear;
    OK(nt_spatial_rasterize(NULL,&grid,NULL,0,&clear));
    OK(nt_spatial_raster_cell_hit(clear,(NtCellPoint){0,0},&found,&hit));
    CHECK(!found && hit.sample.object_id == 50);
    nt_spatial_raster_destroy(clear);

    NtFrame *direct;
    OK(nt_spatial_raster_frame(raster,NT_SAMPLE_BRAILLE,NULL,&direct));
    NtTree *tree;
    OK(nt_tree_create(NULL,&tree));
    NtNodeId parent;
    const NtMountOptions passive = {true,false,false};
    const NtComponentOps layout = {.measure=raster_measure,.layout=raster_layout,.paint=raster_container_paint};
    const NtComponentOps paint = {.measure=raster_leaf_measure,.layout=raster_leaf_layout,.paint=raster_paint};
    MountedRaster mounted = {raster,NT_SAMPLE_BRAILLE,{0}};
    OK(nt_tree_mount(tree,(NtNodeId){0},&layout,&mounted,passive,&parent));
    OK(nt_tree_mount(tree,parent,&paint,&mounted,passive,&mounted.node));
    OK(nt_tree_publish(tree,(NtCellSize){4,3}));
    const NtFrame *frame = nt_tree_frame(tree);
    NtCellView reference, visible, hidden;
    OK(nt_frame_cell(direct,(NtCellPoint){2,0},&reference));
    OK(nt_frame_cell(frame,(NtCellPoint){2,1},&visible));
    OK(nt_frame_cell(frame,(NtCellPoint){1,1},&hidden));
    CHECK(reference.glyph.count && visible.glyph.count == reference.glyph.count);
    CHECK(memcmp(visible.glyph.data,reference.glyph.data,reference.glyph.count) == 0);
    CHECK(visible.style.foreground.blue == reference.style.foreground.blue);
    CHECK(visible.owner == nt_node_owner(mounted.node) && !hidden.glyph.count);
    mounted.mode = (NtSamplingMode)-1;
    OK(nt_tree_invalidate(tree,mounted.node,NT_INVALIDATE_PAINT));
    CHECK(nt_tree_publish(tree,(NtCellSize){4,3}) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(nt_tree_frame(tree) == frame);
    nt_tree_destroy(tree);
    nt_frame_destroy(direct);
    nt_spatial_raster_destroy(raster);
    return 0;
}

typedef struct Allocation {
    void *address;
    size_t bytes, alignment;
} Allocation;
typedef struct Faults {
    Allocation active[16];
    unsigned calls, refuse;
} Faults;

static void *reserve(void *context, size_t bytes, size_t alignment)
{
    Faults *state = context;
    if (++state->calls == state->refuse) return NULL;
    for (unsigned i = 0; i < 16; ++i) {
        if (!state->active[i].address) {
            void *address = malloc(bytes);
            state->active[i] = (Allocation){address,bytes,alignment};
            return address;
        }
    }
    abort();
}

static void release(void *context, void *address, size_t bytes, size_t alignment)
{
    Faults *state = context;
    for (unsigned i = 0; i < 16; ++i) {
        if (state->active[i].address == address) {
            if (state->active[i].bytes != bytes || state->active[i].alignment != alignment) abort();
            state->active[i].address = NULL;
            free(address);
            return;
        }
    }
    abort();
}

static int allocation_rollback(void)
{
    NtSpatialMesh *mesh;
    OK(nt_spatial_mesh_create(NULL,&(NtSpatialMeshData){triangle,3,faces,1},&mesh));
    const NtSpatialMeshInstance draw = instance(mesh);
    for (unsigned fail = 1; fail <= 5; ++fail) {
        Faults state = {.refuse=fail};
        const NtAllocator allocator = {&state,reserve,release};
        NtSpatialMesh *mesh_out = mesh;
        CHECK(nt_spatial_mesh_decode_obj(&allocator,BYTES(model),&mesh_out) == NT_RESULT_ALLOCATION_FAILED);
        CHECK(mesh_out == mesh);
        state.calls = 0;
        NtSpatialRaster *raster = NULL;
        CHECK(nt_spatial_rasterize(&allocator,&options,&draw,1,&raster) == NT_RESULT_ALLOCATION_FAILED);
        CHECK(!raster);
        for (unsigned i = 0; i < 16; ++i) CHECK(!state.active[i].address);
    }
    nt_spatial_mesh_destroy(mesh);
    return 0;
}

int main(void)
{
    return ownership_and_models() || rasterization() || closed_clip_contact() || exact_grid_center() ||
        mounted_paint_and_hit() || translucent_composition() || allocation_rollback();
}
