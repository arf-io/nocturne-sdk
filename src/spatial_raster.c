#include "spatial_projection_internal.h"
#include "spatial_mesh_internal.h"
#include "canvas_internal.h"
#include "component_internal.h"
#include "surface_internal.h"

struct NtSpatialRaster {
    NtAllocator allocator;
    NtCellSize size;
    NtRgba background;
    NtSpatialRasterSample *pixels;
};

typedef struct TriangleInk {
    const NtSpatialMeshInstance *instance;
    NtPoint uv[3];
} TriangleInk;

typedef struct RasterScene {
    NtSpatialPickTriangle *triangles;
    NtSpatialScreenBounds *bounds;
    TriangleInk *inks;
    size_t count;
} RasterScene;

NT_DECLARE_ALIGNMENT(NtSpatialRaster);
NT_DECLARE_ALIGNMENT(NtSpatialRasterSample);
NT_DECLARE_ALIGNMENT(NtSpatialPickTriangle);
NT_DECLARE_ALIGNMENT(NtSpatialScreenBounds);
NT_DECLARE_ALIGNMENT(TriangleInk);

static size_t sample_count(NtCellSize size)
{
    return (size_t)size.columns*size.rows;
}

void nt_spatial_raster_destroy(NtSpatialRaster *raster)
{
    if (!raster) return;
    const NtAllocator allocator = raster->allocator;
    nt_release(&allocator,raster->pixels,sample_count(raster->size)*sizeof(raster->pixels[0]),
               NT_ALIGNMENT(NtSpatialRasterSample));
    nt_release(&allocator,raster,sizeof(*raster),NT_ALIGNMENT(NtSpatialRaster));
}

NtCellSize nt_spatial_raster_size(const NtSpatialRaster *raster)
{
    return raster ? raster->size : (NtCellSize){0};
}

static bool contains(NtCellSize size, int64_t x, int64_t y)
{
    return x >= 0 && y >= 0 && x < size.columns && y < size.rows;
}

NtResult nt_spatial_raster_sample(const NtSpatialRaster *raster, NtCellPoint position,
                                  NtSpatialRasterSample *out)
{
    if (!raster || !out) return NT_RESULT_INVALID_ARGUMENT;
    if (!contains(raster->size,position.x,position.y)) return NT_RESULT_OUT_OF_RANGE;
    *out = raster->pixels[(size_t)position.y*raster->size.columns+(uint32_t)position.x];
    return NT_RESULT_OK;
}

static void release_scene(const NtAllocator *allocator, RasterScene *scene)
{
    nt_release(allocator,scene->inks,scene->count*sizeof(scene->inks[0]),NT_ALIGNMENT(TriangleInk));
    nt_release(allocator,scene->bounds,scene->count*sizeof(scene->bounds[0]),NT_ALIGNMENT(NtSpatialScreenBounds));
    nt_release(allocator,scene->triangles,scene->count*sizeof(scene->triangles[0]),NT_ALIGNMENT(NtSpatialPickTriangle));
}

static NtResult scene_storage(const NtAllocator *allocator, RasterScene *scene)
{
    const size_t count = scene->count;
    if (!count) return NT_RESULT_OK;
    scene->triangles = nt_allocate(allocator,count*sizeof(scene->triangles[0]),NT_ALIGNMENT(NtSpatialPickTriangle));
    if (!scene->triangles) return NT_RESULT_ALLOCATION_FAILED;
    scene->bounds = nt_allocate(allocator,count*sizeof(scene->bounds[0]),NT_ALIGNMENT(NtSpatialScreenBounds));
    if (!scene->bounds) return NT_RESULT_ALLOCATION_FAILED;
    scene->inks = nt_allocate(allocator,count*sizeof(scene->inks[0]),NT_ALIGNMENT(TriangleInk));
    return scene->inks ? NT_RESULT_OK : NT_RESULT_ALLOCATION_FAILED;
}

static NtResult validate_scene(const NtSpatialRasterOptions *options, const NtSpatialMeshInstance *instances,
                                size_t count, size_t *triangles)
{
    if (count > SIZE_MAX/sizeof(instances[0])) return NT_RESULT_OUT_OF_RANGE;
    NtVec3 unused;
    if (options->camera) {
        const NtResult status = nt_spatial_camera_to_view(options->camera,options->camera->position,&unused);
        if (status != NT_RESULT_OK) return status;
    }
    size_t total = 0;
    for (size_t i = 0; i < count; ++i) {
        const NtSpatialMeshInstance *draw = &instances[i];
        if (!draw->mesh) return NT_RESULT_INVALID_ARGUMENT;
        if (draw->mesh->face_count > SIZE_MAX-total) return NT_RESULT_OUT_OF_RANGE;
        total += draw->mesh->face_count;
        if (draw->transform) {
            const NtResult status = nt_spatial_transform_direction(draw->transform,(NtVec3){0},&unused);
            if (status != NT_RESULT_OK) return status;
        }
        if (draw->texture) {
            NtRgba value;
            const NtResult status = nt_image_sample(draw->texture,draw->sampling,0,0,&value);
            if (status != NT_RESULT_OK) return status;
        }
    }
    if ((options->triangle_limit && total > options->triangle_limit) ||
        total > SIZE_MAX/sizeof(NtSpatialPickTriangle) ||
        total > SIZE_MAX/sizeof(NtSpatialScreenBounds) ||
        total > SIZE_MAX/sizeof(TriangleInk)) return NT_RESULT_OUT_OF_RANGE;
    *triangles = total;
    return NT_RESULT_OK;
}

static NtResult face_bounds(const NtSpatialProjection *projection, const NtVec3 view[3],
                              NtSpatialScreenBounds *out)
{
    for (NtSpatialPlane plane = NT_SPATIAL_NEAR; plane <= NT_SPATIAL_TOP; ++plane) {
        bool outside = true;
        for (unsigned i = 0; i < 3 && outside; ++i) {
            NtSpatialExact score;
            nt_spatial_plane_score(projection,plane,view[i],&score);
            outside = score.negative;
        }
        if (outside) { *out = (NtSpatialScreenBounds){.empty=true}; return NT_RESULT_OK; }
    }
    NtSpatialClippedPolygon polygon;
    const NtResult status = nt_spatial_clip_triangle(projection,view,&polygon);
    if (status != NT_RESULT_OK) return status;
    /* The polygon clip omits line or point contact. Keep those candidates
     * so a ray on a closed near or far boundary can still hit. */
    NtSpatialScreenBounds bounds = {{-1,-1},{1,1},false};
    if (polygon.count) {
        bounds.minimum = bounds.maximum = polygon.vertices[0].projected.screen;
        for (size_t i = 1; i < polygon.count; ++i) {
            const NtSpatialScreen point = polygon.vertices[i].projected.screen;
            bounds.minimum.x = fmin(bounds.minimum.x,point.x);
            bounds.minimum.y = fmin(bounds.minimum.y,point.y);
            bounds.maximum.x = fmax(bounds.maximum.x,point.x);
            bounds.maximum.y = fmax(bounds.maximum.y,point.y);
        }
    }
    *out = bounds;
    return NT_RESULT_OK;
}

static NtResult view_position(const NtSpatialRasterOptions *options, const NtSpatialMeshInstance *draw,
                               NtVec3 point, NtVec3 *out)
{
    if (draw->transform) {
        const NtResult status = nt_spatial_transform_point(draw->transform,point,&point);
        if (status != NT_RESULT_OK) return status;
    }
    if (options->camera) return nt_spatial_camera_to_view(options->camera,point,out);
    *out = point;
    return NT_RESULT_OK;
}

static NtResult prepare_scene(const NtSpatialRasterOptions *options, const NtSpatialMeshInstance *instances,
                                size_t count, RasterScene *scene)
{
    size_t index = 0;
    for (size_t i = 0; i < count; ++i) {
        const NtSpatialMeshInstance *draw = &instances[i];
        const NtSpatialMesh *mesh = draw->mesh;
        for (size_t f = 0; f < mesh->face_count; ++f, ++index) {
            const NtSpatialFace *face = &mesh->faces[f];
            NtSpatialPickTriangle *triangle = &scene->triangles[index];
            TriangleInk *ink = &scene->inks[index];
            triangle->object_id = draw->object_id;
            triangle->primitive_id = face->primitive_id;
            ink->instance = draw;
            for (unsigned j = 0; j < 3; ++j) {
                const NtSpatialVertex *vertex = &mesh->vertices[face->indices[j]];
                const NtResult status = view_position(options,draw,vertex->position,&triangle->view[j]);
                if (status != NT_RESULT_OK) return status;
                ink->uv[j] = vertex->uv;
            }
            const NtResult status = face_bounds(&options->projection,triangle->view,&scene->bounds[index]);
            if (status != NT_RESULT_OK) return status;
        }
    }
    return NT_RESULT_OK;
}

static uint8_t modulate(uint8_t first, uint8_t last)
{
    return (uint8_t)(((unsigned)first*last+127)/255);
}

static double uv_value(const NtSpatialWeights *weights, double a, double b, double c)
{
    NtSpatialExact value;
    nt_spatial_weighted(weights,a,b,c,&value);
    return nt_spatial_exact_quotient(&value,&weights->divisor);
}

static NtResult shade(const NtSpatialRasterOptions *options, const RasterScene *scene,
                       NtSpatialScreenRatio screen, NtSpatialRasterSample *out)
{
    NtSpatialWeights weights;
    NtSpatialExact depth;
    size_t index;
    if (!nt_spatial_nearest_weights(&options->projection,screen,scene->triangles,scene->bounds,
                                    scene->count,&index,&weights,&depth)) {
        *out = (NtSpatialRasterSample){.color=options->background};
        return NT_RESULT_OK;
    }
    const TriangleInk *ink = &scene->inks[index];
    const NtSpatialMeshInstance *draw = ink->instance;
    const NtPoint uv = {
        uv_value(&weights,ink->uv[0].x,ink->uv[1].x,ink->uv[2].x),
        uv_value(&weights,ink->uv[0].y,ink->uv[1].y,ink->uv[2].y)};
    NtRgba color = draw->tint;
    if (draw->texture) {
        NtRgba texel;
        const NtResult status = nt_image_sample(draw->texture,draw->sampling,uv.x,uv.y,&texel);
        if (status != NT_RESULT_OK) return status;
        color = (NtRgba){modulate(color.red,texel.red),modulate(color.green,texel.green),
                        modulate(color.blue,texel.blue),modulate(color.alpha,texel.alpha)};
    }
    NtRgba composited;
    const NtResult status = nt_rgba_composite(options->background,color,NULL,&composited);
    if (status != NT_RESULT_OK) return status;
    *out = (NtSpatialRasterSample){
        .color=composited, .owner=draw->owner, .covered=true, .object_id=draw->object_id,
        .primitive_id=scene->triangles[index].primitive_id,
        .depth=nt_spatial_exact_quotient(&depth,&weights.divisor), .uv=uv};
    return NT_RESULT_OK;
}

static NtResult render(const NtSpatialRasterOptions *options, const RasterScene *scene, NtSpatialRaster *raster)
{
    const NtCellSize size = raster->size;
    NtSpatialScreenRatio screen = {.denominator={size.columns,size.rows}};
    for (uint32_t y = 0; size.columns && y < size.rows; ++y) {
        screen.numerator.y = (double)size.rows-2*(double)y-1;
        for (uint32_t x = 0; x < size.columns; ++x) {
            screen.numerator.x = 2*(double)x+1-size.columns;
            const NtResult status = shade(options,scene,screen,
                                           &raster->pixels[(size_t)y*size.columns+x]);
            if (status != NT_RESULT_OK) return status;
        }
    }
    return NT_RESULT_OK;
}

NtResult nt_spatial_rasterize(const NtAllocator *allocator, const NtSpatialRasterOptions *options,
                              const NtSpatialMeshInstance *instances, size_t count, NtSpatialRaster **out)
{
    NtAllocator resolved;
    if (!options || !out || (count && !instances) || !nt_allocator_get(allocator,&resolved))
        return NT_RESULT_INVALID_ARGUMENT;
    const NtCellSize size = options->samples;
    const uint32_t limit = (uint32_t)INT32_MAX+1;
    if (size.columns > limit || size.rows > limit ||
        (size.columns && size.rows > (SIZE_MAX/sizeof(NtSpatialRasterSample))/size.columns))
        return NT_RESULT_OUT_OF_RANGE;
    const size_t samples = sample_count(size);
    if (options->sample_limit && samples > options->sample_limit) return NT_RESULT_OUT_OF_RANGE;
    fenv_t saved;
    NtResult status = nt_spatial_begin_projection(&options->projection,&saved);
    if (status != NT_RESULT_OK) return status;
    RasterScene scene = {0};
    status = nt_spatial_end_projection(&saved,validate_scene(options,instances,count,&scene.count));
    if (status != NT_RESULT_OK) return status;

    NtSpatialRaster *raster = nt_allocate(&resolved,sizeof(*raster),NT_ALIGNMENT(NtSpatialRaster));
    if (!raster) return NT_RESULT_ALLOCATION_FAILED;
    *raster = (NtSpatialRaster){.allocator=resolved,.size=size,.background=options->background};
    if (samples) raster->pixels = nt_allocate(&resolved,samples*sizeof(raster->pixels[0]),
                                               NT_ALIGNMENT(NtSpatialRasterSample));
    status = samples && !raster->pixels ? NT_RESULT_ALLOCATION_FAILED : scene_storage(&resolved,&scene);
    if (status == NT_RESULT_OK) {
        status = nt_spatial_begin_projection(&options->projection,&saved);
        if (status == NT_RESULT_OK) {
            status = prepare_scene(options,instances,count,&scene);
            if (status == NT_RESULT_OK) status = render(options,&scene,raster);
            status = nt_spatial_end_projection(&saved,status);
        }
    }
    release_scene(&resolved,&scene);
    if (status == NT_RESULT_OK) *out = raster;
    else nt_spatial_raster_destroy(raster);
    return status;
}

NtCellSize nt_spatial_raster_cell_size(const NtSpatialRaster *raster)
{
    const NtCellSize size = nt_spatial_raster_size(raster);
    return (NtCellSize){(size.columns+1)/2,(size.rows+3)/4};
}

static const NtSpatialRasterSample *tile_sample(const NtSpatialRaster *raster, NtCellPoint cell,
                                                 unsigned offset, NtCellPoint *position)
{
    const int64_t x = 2*(int64_t)cell.x+offset%2, y = 4*(int64_t)cell.y+offset/2;
    if (!contains(raster->size,x,y)) return NULL;
    if (position) *position = (NtCellPoint){(int32_t)x,(int32_t)y};
    return &raster->pixels[(size_t)y*raster->size.columns+(size_t)x];
}

NtResult nt_spatial_raster_cell_hit(const NtSpatialRaster *raster, NtCellPoint cell,
                                    bool *found, NtSpatialCellHit *out)
{
    if (!raster || !found || !out) return NT_RESULT_INVALID_ARGUMENT;
    if (!contains(nt_spatial_raster_cell_size(raster),cell.x,cell.y)) return NT_RESULT_OUT_OF_RANGE;
    static const unsigned by_distance[8] = {2,3,4,5,0,1,6,7};
    for (unsigned i = 0; i < 8; ++i) {
        NtCellPoint position;
        const NtSpatialRasterSample *sample = tile_sample(raster,cell,by_distance[i],&position);
        if (sample && sample->covered) {
            *out = (NtSpatialCellHit){position,*sample};
            *found = true;
            return NT_RESULT_OK;
        }
    }
    *found = false;
    return NT_RESULT_OK;
}

static NtResult raster_cell(const NtSpatialRaster *raster, NtCellPoint cell, NtSamplingMode mode,
                             NtSampledCell *out)
{
    NtCellSamples tile;
    for (unsigned i = 0; i < 8; ++i) {
        tile.background[i] = raster->background;
        const NtSpatialRasterSample *sample = tile_sample(raster,cell,i,NULL);
        tile.pixels[i] = sample ? (NtCanvasPixel){sample->color,sample->owner,sample->covered} :
                                 (NtCanvasPixel){.color=raster->background};
    }
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_OUT_OF_RANGE;
    return nt_spatial_end_projection(&saved,nt_cell_sample(&tile,mode,out));
}

NtResult nt_spatial_raster_frame(const NtSpatialRaster *raster, NtSamplingMode mode,
                                 const NtAllocator *allocator, NtFrame **out)
{
    if (!raster || !out || !nt_sampling_valid(mode)) return NT_RESULT_INVALID_ARGUMENT;
    const NtCellSize size = nt_spatial_raster_cell_size(raster);
    NtFrame *frame;
    NtResult status = nt_frame_create(allocator,size,&frame);
    if (status != NT_RESULT_OK) return status;
    for (uint32_t y = 0; size.columns && y < size.rows && status == NT_RESULT_OK; ++y) {
        for (uint32_t x = 0; x < size.columns && status == NT_RESULT_OK; ++x) {
            const NtCellPoint cell = {(int32_t)x,(int32_t)y};
            NtSampledCell sampled;
            status = raster_cell(raster,cell,mode,&sampled);
            if (status == NT_RESULT_OK) status = nt_frame_write_sampled_cell(frame,cell,&sampled);
        }
    }
    if (status == NT_RESULT_OK) *out = frame;
    else nt_frame_destroy(frame);
    return status;
}

NtResult nt_spatial_raster_paint(const NtSpatialRaster *raster, NtPaintContext *paint,
                                 NtCellPoint origin, NtSamplingMode mode)
{
    if (!raster || !paint || !nt_sampling_valid(mode)) return NT_RESULT_INVALID_ARGUMENT;
    const NtCellSize size = nt_spatial_raster_cell_size(raster);
    const NtCellRect clip = nt_paint_clip(paint);
    const int64_t left = origin.x > clip.origin.x ? origin.x : clip.origin.x;
    const int64_t top = origin.y > clip.origin.y ? origin.y : clip.origin.y;
    const int64_t raster_right = (int64_t)origin.x+size.columns;
    const int64_t raster_bottom = (int64_t)origin.y+size.rows;
    const int64_t limit = (int64_t)INT32_MAX+1;
    const int64_t clip_end_x = (int64_t)clip.origin.x+clip.size.columns;
    const int64_t clip_end_y = (int64_t)clip.origin.y+clip.size.rows;
    const int64_t clip_right = clip_end_x < limit ? clip_end_x : limit;
    const int64_t clip_bottom = clip_end_y < limit ? clip_end_y : limit;
    const int64_t right = raster_right < clip_right ? raster_right : clip_right;
    const int64_t bottom = raster_bottom < clip_bottom ? raster_bottom : clip_bottom;
    for (int64_t y = top; left < right && y < bottom; ++y) {
        for (int64_t x = left; x < right; ++x) {
            const NtCellPoint cell = {(int32_t)(x-origin.x),(int32_t)(y-origin.y)};
            NtSampledCell sampled;
            NtResult status = raster_cell(raster,cell,mode,&sampled);
            if (status != NT_RESULT_OK) return status;
            if (!sampled.style.foreground.alpha && !sampled.style.background.alpha) continue;
            status = nt_surface_write_glyph(paint->surface,(NtCellPoint){(int32_t)x,(int32_t)y},
                    (NtByteView){sampled.glyph,sampled.glyph_size},1,sampled.style,paint->owner,true);
            if (status != NT_RESULT_OK) return status;
        }
    }
    return NT_RESULT_OK;
}
