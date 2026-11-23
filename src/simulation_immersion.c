#include "simulation_hull_internal.h"
#include <stdlib.h>
#include <string.h>

enum {
    HULL_CUTS = 4,
    CUT_SUPPORT = NT_HULL_MAX_TRIANGLES,
    POLY_FACES = NT_HULL_MAX_TRIANGLES+HULL_CUTS,
    POLY_POINTS = NT_HULL_MAX_VERTICES+HULL_CUTS*POLY_FACES,
    POLY_CORNERS = (3+HULL_CUTS)*NT_HULL_MAX_TRIANGLES+HULL_CUTS*(POLY_FACES+HULL_CUTS)
};

typedef struct ClipPlane {
    NtSpatialExact normal[3], bias;
} ClipPlane;

typedef struct HullWorkspace HullWorkspace;
typedef struct HullClip {
    const NtHull *hull;
    const ClipPlane *planes;
    HullWorkspace *workspace;
} HullClip;
typedef struct HullQuery {
    const NtHull *hull;
    const NtSpatialTransform *pose;
    const NtWaterPlane *plane;
    const NtWaterTriangle *triangles;
    const int *orientation;
    size_t count;
    HullWorkspace *workspace;
} HullQuery;
typedef struct PolyLine { uint16_t first, last; bool original; } PolyLine;
typedef struct VertexSource { PolyLine line; uint16_t cut; } VertexSource;
typedef struct PolyCorner { uint16_t point; PolyLine outgoing; } PolyCorner;
typedef struct PolyFace { uint16_t begin, count, support; } PolyFace;
typedef struct PolyBoundary {
    PolyFace faces[POLY_FACES];
    PolyCorner corners[POLY_CORNERS];
    size_t face_count, corner_count;
} PolyBoundary;

typedef struct Polyhedron {
    VertexSource sources[POLY_POINTS];
    size_t point_count;
    PolyBoundary boundary;
    bool changed;
} Polyhedron;

struct HullWorkspace {
    Polyhedron poly;
    PolyBoundary clipped;
    ClipPlane planes[HULL_CUTS];
    NtHullExactVertex vertices[4];
    ClipPlane supports[2];
    NtSpatialExact crosses[3][3];
    NtHullTetraScratch tetrahedron;
};

typedef struct CuttingEdge { uint16_t first, last, at; } CuttingEdge;
typedef struct CapEdge { uint16_t first, last; PolyLine line; } CapEdge;

static NtResult measure_query(const HullQuery *query, NtHullMeasure *measure, NtHullRefinement *refinement);

static void local_plane(const NtSpatialTransform *pose, NtVec3 origin, NtVec3 point,
                        const NtSpatialExact normal[3], ClipPlane *plane)
{
    for (unsigned i=0;i<3;++i) {
        NtSpatialExact column[3];
        nt_simulation_exact_vec((NtVec3){pose->linear[0][i],pose->linear[1][i],pose->linear[2][i]},column);
        nt_exact_dot(normal,column,&plane->normal[i]);
    }
    NtSpatialExact offset[3], shift[3], bias;
    nt_exact_displacement(pose->translation,point,offset);
    nt_exact_dot(normal,offset,&plane->bias);
    nt_simulation_exact_vec(origin,shift);
    nt_exact_dot(plane->normal,shift,&bias);
    nt_simulation_accumulate(&plane->bias,&bias);
}

static void plane_score(const ClipPlane *plane, NtVec3 point, NtSpatialExact *out)
{
    NtSpatialExact value[3], dot;
    nt_simulation_exact_vec(point,value);
    nt_exact_dot(plane->normal,value,&dot);
    nt_spatial_exact_add(out,&dot,&plane->bias);
}

static void hull_polyhedron(const NtHull *hull, Polyhedron *poly)
{
    poly->point_count = hull->vertex_count;
    poly->changed = false;
    poly->boundary.face_count = hull->triangle_count;
    poly->boundary.corner_count = 3*hull->triangle_count;
    for (size_t i=0;i<hull->triangle_count;++i) {
        poly->boundary.faces[i] = (PolyFace){(uint16_t)(3*i),3,(uint16_t)i};
        const uint32_t *v = hull->triangles[i].indices;
        for (unsigned j=0;j<3;++j) {
            const uint16_t first = (uint16_t)v[j], last = (uint16_t)v[(j+1)%3];
            poly->boundary.corners[3*i+j] = (PolyCorner){first,{first,last,true}};
        }
    }
}

static const ClipPlane *support_plane(const HullClip *clip, uint16_t index, ClipPlane *scratch)
{
    if (index>=CUT_SUPPORT) return clip->planes+(index-CUT_SUPPORT);
    const uint32_t *v = clip->hull->triangles[index].indices;
    const NtVec3 *p = clip->hull->vertices;
    nt_hull_normal(p[v[0]],p[v[1]],p[v[2]],scratch->normal);
    NtSpatialExact anchor[3];
    nt_simulation_exact_vec(p[v[0]],anchor);
    nt_exact_dot(scratch->normal,anchor,&scratch->bias);
    nt_spatial_exact_negate(&scratch->bias);
    return scratch;
}

static NT_SIMULATION_NOINLINE bool original_edge_vertex(const HullClip *clip, VertexSource source,
                                 const ClipPlane *plane, NtHullExactVertex *out)
{
    const NtVec3 a = clip->hull->vertices[source.line.first];
    const NtVec3 b = clip->hull->vertices[source.line.last];
    NtSpatialExact first, last;
    plane_score(plane,a,&first);
    plane_score(plane,b,&last);
    if (nt_spatial_exact_sign(&first)*nt_spatial_exact_sign(&last)>0) return false;
    first.negative = false;
    last.negative = false;
    NtSpatialFraction position;
    nt_spatial_exact_copy(&position.numerator,&first);
    nt_spatial_exact_add(&position.divisor,&first,&last);
    if (!position.divisor.count) return false;

    NtSpatialExact from[3], to[3];
    nt_simulation_exact_vec(a,from);
    nt_simulation_exact_vec(b,to);
    nt_spatial_exact_copy(&out->divisor,&position.divisor);
    for (unsigned i=0;i<3;++i)
        nt_exact_mix(&from[i],&to[i],&position,&out->coordinate[i]);
    return true;
}

static bool exact_vertex(const HullClip *clip, VertexSource source, NtHullExactVertex *out)
{
    const ClipPlane *cut = clip->planes+source.cut;
    if (source.line.original) return original_edge_vertex(clip,source,cut,out);

    /* Cramer's rule on the two supports of the edge and the cutting plane.
     * Keep the rational vertex through all subsequent side tests. */
    HullWorkspace *workspace = clip->workspace;
    const ClipPlane *first = support_plane(clip,source.line.first,&workspace->supports[0]);
    const ClipPlane *last = support_plane(clip,source.line.last,&workspace->supports[1]);
    NtSpatialExact (*crosses)[3] = workspace->crosses;
    nt_exact_cross(last->normal,cut->normal,crosses[0]);
    nt_exact_cross(cut->normal,first->normal,crosses[1]);
    nt_exact_cross(first->normal,last->normal,crosses[2]);
    nt_exact_dot(first->normal,crosses[0],&out->divisor);
    const int sign = nt_spatial_exact_sign(&out->divisor);
    if (!sign) return false;
    if (sign<0) nt_spatial_exact_negate(&out->divisor);
    const ClipPlane *planes[3] = {first,last,cut};
    for (unsigned i=0;i<3;++i) {
        nt_spatial_exact_number(&out->coordinate[i],0,0);
        for (unsigned j=0;j<3;++j) {
            NtSpatialExact weighted;
            nt_spatial_exact_multiply(&weighted,&crosses[j][i],&planes[j]->bias);
            nt_simulation_accumulate(&out->coordinate[i],&weighted);
        }
        if (sign>0) nt_spatial_exact_negate(&out->coordinate[i]);
    }
    return true;
}

static bool vertex_side(const Polyhedron *poly, const HullClip *clip, uint16_t cut,
                        uint16_t at, signed char *side)
{
    const ClipPlane *plane = clip->planes+cut;
    NtSpatialExact score;
    if (at<clip->hull->vertex_count) {
        plane_score(plane,clip->hull->vertices[at],&score);
    } else {
        NtHullExactVertex vertex;
        if (!exact_vertex(clip,poly->sources[at],&vertex)) return false;
        NtSpatialExact bias;
        nt_exact_dot(plane->normal,vertex.coordinate,&score);
        nt_spatial_exact_multiply(&bias,&plane->bias,&vertex.divisor);
        nt_simulation_accumulate(&score,&bias);
    }
    *side = (signed char)nt_spatial_exact_sign(&score);
    return true;
}

static bool cut_edge(Polyhedron *poly, uint16_t cut, PolyCorner a,
                     uint16_t b, CuttingEdge *edges, size_t *count, uint16_t *at)
{
    const uint16_t first = a.point<b ? a.point : b, last = a.point<b ? b : a.point;
    for (size_t i=0;i<*count;++i) {
        if (edges[i].first==first && edges[i].last==last) {
            *at = edges[i].at;
            return true;
        }
    }
    if (*count>=POLY_FACES || poly->point_count>=POLY_POINTS) return false;
    *at = (uint16_t)poly->point_count++;
    poly->sources[*at] = (VertexSource){a.outgoing,cut};
    edges[(*count)++] = (CuttingEdge){first,last,*at};
    return true;
}

static bool append_corner(PolyBoundary *boundary, PolyCorner corner)
{
    if (boundary->corner_count>=POLY_CORNERS) return false;
    boundary->corners[boundary->corner_count++] = corner;
    return true;
}

static bool finish_face(PolyBoundary *boundary, size_t start, uint16_t support)
{
    const size_t count = boundary->corner_count-start;
    if (count<3) {
        boundary->corner_count = start;
        return true;
    }
    if (boundary->face_count>=POLY_FACES) return false;
    boundary->faces[boundary->face_count++] = (PolyFace){(uint16_t)start,(uint16_t)count,support};
    return true;
}

static NT_SIMULATION_NOINLINE bool join_cap(PolyBoundary *boundary, const signed char *side, uint16_t support)
{
    /* Each retained face contributes its on-plane edges. Cancel edges shared
     * by two faces and traverse the remaining boundary in reverse winding. */
    CapEdge edges[POLY_CORNERS];
    size_t count = 0;
    for (size_t i=0;i<boundary->face_count;++i) {
        const PolyFace face = boundary->faces[i];
        const PolyCorner *v = boundary->corners+face.begin;
        for (unsigned j=0;j<face.count;++j) {
            const uint16_t a = v[j].point, b = v[(j+1)%face.count].point;
            if (side[a] || side[b]) continue;
            size_t k = 0;
            while (k<count && (edges[k].first!=a || edges[k].last!=b)) ++k;
            if (k<count) edges[k] = edges[--count];
            else edges[count++] = (CapEdge){b,a,v[j].outgoing};
        }
    }
    if (count<3) return false;
    const size_t start = boundary->corner_count;
    const uint16_t first = edges[count-1].first;
    uint16_t corner = first;
    do {
        size_t i = 0;
        while (i<count && edges[i].first!=corner) ++i;
        if (i==count || !append_corner(boundary,(PolyCorner){corner,edges[i].line})) return false;
        corner = edges[i].last;
        edges[i] = edges[--count];
    } while (corner!=first);
    return !count && boundary->corner_count-start>=3 && finish_face(boundary,start,support);
}

static bool clip_polyhedron(Polyhedron *poly, const HullClip *clip, uint16_t cut)
{
    signed char side[POLY_POINTS];
    for (size_t i=0;i<poly->point_count;++i) side[i] = 2;
    bool inside = false, outside = false;
    for (size_t i=0;i<poly->boundary.corner_count;++i) {
        const uint16_t v = poly->boundary.corners[i].point;
        if (side[v]==2 && !vertex_side(poly,clip,cut,v,&side[v])) return false;
        inside = inside || side[v]<0;
        outside = outside || side[v]>0;
    }
    if (!inside) {
        poly->changed = true;
        poly->boundary.face_count = 0;
        poly->boundary.corner_count = 0;
        return true;
    }
    if (!outside) return true;
    poly->changed = true;

    PolyBoundary *clipped = &clip->workspace->clipped;
    clipped->face_count = 0;
    clipped->corner_count = 0;
    CuttingEdge edges[POLY_FACES];
    size_t edge_count = 0;
    const uint16_t support = CUT_SUPPORT+cut;
    for (size_t i=0;i<poly->boundary.face_count;++i) {
        const PolyFace face = poly->boundary.faces[i];
        const PolyCorner *v = poly->boundary.corners+face.begin;
        const size_t start = clipped->corner_count;
        const PolyLine cap = {face.support,support,false};
        for (unsigned j=0;j<face.count;++j) {
            const PolyCorner a = v[j];
            const uint16_t b = v[(j+1)%face.count].point;
            const int first = side[a.point], last = side[b];
            if (first<=0) {
                const PolyLine outgoing = !first && last>0 ? cap : a.outgoing;
                if (!append_corner(clipped,(PolyCorner){a.point,outgoing})) return false;
            }
            if (first*last<0) {
                uint16_t at;
                const PolyLine outgoing = first>0 ? a.outgoing : cap;
                if (!cut_edge(poly,cut,a,b,edges,&edge_count,&at) ||
                    !append_corner(clipped,(PolyCorner){at,outgoing}))
                    return false;
                side[at] = 0;
            }
        }
        if (!finish_face(clipped,start,face.support)) return false;
    }
    if (!join_cap(clipped,side,support)) return false;
    poly->boundary.face_count = clipped->face_count;
    poly->boundary.corner_count = clipped->corner_count;
    memcpy(poly->boundary.faces,clipped->faces,clipped->face_count*sizeof(clipped->faces[0]));
    memcpy(poly->boundary.corners,clipped->corners,clipped->corner_count*sizeof(clipped->corners[0]));
    return true;
}

static bool read_vertex(const Polyhedron *poly, const HullClip *clip, uint16_t at, NtHullExactVertex *out)
{
    if (at>=clip->hull->vertex_count) return exact_vertex(clip,poly->sources[at],out);
    nt_simulation_exact_vec(clip->hull->vertices[at],out->coordinate);
    nt_spatial_exact_number(&out->divisor,1,0);
    return true;
}

static bool measure_polyhedron(const Polyhedron *poly, const HullClip *clip, NtHullMeasure *measure,
                                NtHullRefinement *refinement)
{
    if (!poly->boundary.face_count) return true;
    const uint16_t anchor = poly->boundary.corners[0].point;
    if (!poly->changed) {
        if (refinement) return nt_hull_refine_add(refinement,&clip->hull->six_volume);
        const NtVec3 *points = clip->hull->vertices;
        for (size_t i=0;i<clip->hull->triangle_count;++i) {
            const uint32_t *v = clip->hull->triangles[i].indices;
            nt_hull_measure_tetra(measure,points[anchor],points[v[0]],points[v[1]],points[v[2]]);
        }
        return true;
    }
    NtHullExactVertex *vertices = clip->workspace->vertices;
    if (!read_vertex(poly,clip,anchor,&vertices[0])) return false;
    for (size_t i=0;i<poly->boundary.face_count;++i) {
        const PolyFace face = poly->boundary.faces[i];
        const PolyCorner *v = poly->boundary.corners+face.begin;
        if (v[0].point == anchor) continue;
        NtHullExactVertex *first = &vertices[1], *previous = &vertices[2], *next = &vertices[3];
        if (!read_vertex(poly,clip,v[0].point,first) || !read_vertex(poly,clip,v[1].point,previous))
            return false;
        for (unsigned j=2;j<face.count;++j) {
            if (!read_vertex(poly,clip,v[j].point,next)) return false;
            if (v[j-1].point != anchor && v[j].point != anchor) {
                const NtHullExactVertex *tetrahedron[4] = {first,previous,next,&vertices[0]};
                if (!nt_hull_measure_exact(measure,refinement,tetrahedron,&clip->workspace->tetrahedron))
                    return false;
            }
            NtHullExactVertex *swap = previous;
            previous = next;
            next = swap;
        }
    }
    return true;
}

typedef struct VolumeRounding {
    double lower, upper;
    NtSpatialExact midpoint, threshold;
} VolumeRounding;

static void volume_bounds(const NtSpatialExact *volume, const NtSpatialExact *six, VolumeRounding *out)
{
    NtSpatialExact relative, absolute, scaled, error, lo, hi;
    nt_spatial_exact_number(&relative,1,-200);
    nt_spatial_exact_number(&absolute,1,-4000);
    nt_spatial_exact_multiply(&scaled,&relative,volume);
    nt_spatial_exact_add(&error,&scaled,&absolute);
    nt_spatial_exact_subtract(&lo,volume,&error);
    nt_spatial_exact_add(&hi,volume,&error);
    out->lower = nt_spatial_exact_quotient(&lo,six);
    out->upper = nt_spatial_exact_quotient(&hi,six);
    if (out->lower==out->upper) return;

    /* The error interval is narrower than one output ULP. Determine
     * which side of the single midpoint the exact sum occupies. */
    NtSpatialExact a, b;
    nt_spatial_exact_number(&a,out->lower,-1);
    nt_spatial_exact_number(&b,out->upper,-1);
    nt_spatial_exact_add(&out->midpoint,&a,&b);
    nt_spatial_exact_multiply(&out->threshold,&out->midpoint,six);
}

static NtResult refine_volume(const HullQuery *query, const VolumeRounding *rounding, double *out)
{
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    NtHullRefinement refinement = {.allocator=query->hull->allocator};
    NtResult status = measure_query(query,NULL,&refinement);
    int order = 0;
    if (status==NT_RESULT_OK && !nt_hull_refine_order(&refinement,&rounding->threshold,&order))
        status = refinement.status;
    if (refinement.status!=NT_RESULT_OK) status = refinement.status;
    nt_hull_refine_clear(&refinement);
    status = nt_simulation_finish(&saved,status);
    if (status!=NT_RESULT_OK) return status;
    if (order) *out = order<0 ? rounding->lower : rounding->upper;
    else {
        NtSpatialExact one;
        nt_spatial_exact_number(&one,1,0);
        *out = nt_spatial_exact_quotient(&rounding->midpoint,&one);
    }
    return NT_RESULT_OK;
}

static NT_SIMULATION_NOINLINE NtResult immersion_volume(const HullQuery *query,
                                                         const NtHullMeasure *measure, VolumeRounding *out)
{
    const NtHull *hull = query->hull;
    NtSpatialExact six, scale, tolerance;
    nt_spatial_exact_number(&six,6,0);
    nt_spatial_exact_number(&scale,1e-12,0);
    nt_spatial_exact_multiply(&tolerance,&hull->six_volume,&scale);

    NtSpatialExact excess;
    nt_spatial_exact_subtract(&excess,&measure->six_volume,&hull->six_volume);
    if (nt_spatial_exact_compare(&excess,&tolerance)>0) return NT_RESULT_OUT_OF_RANGE;
    nt_spatial_exact_negate(&tolerance);
    if (nt_spatial_exact_compare(&measure->six_volume,&tolerance)<0) return NT_RESULT_OUT_OF_RANGE;
    out->lower = out->upper = 0;
    if (nt_spatial_exact_sign(&measure->six_volume)>0) volume_bounds(&measure->six_volume,&six,out);
    return NT_RESULT_OK;
}

static NT_SIMULATION_NOINLINE void immersion_center(const HullQuery *query, const NtHullMeasure *measure,
                                                     NtHullImmersion *value)
{
    const NtSpatialTransform *pose = query->pose;
    NtSpatialExact four, denominator, local_origin[3];
    nt_spatial_exact_number(&four,4,0);
    nt_spatial_exact_multiply(&denominator,&measure->six_volume,&four);
    nt_simulation_exact_vec(query->hull->origin,local_origin);
    const double origin[3] = {pose->translation.x,pose->translation.y,pose->translation.z};
    double offset[3], center[3];
    for (unsigned i=0;i<3;++i) {
        const double *row = pose->linear[i];
        NtSpatialExact coefficients[3], moment, translation, shift, world;
        nt_simulation_exact_vec((NtVec3){row[0],row[1],row[2]},coefficients);
        nt_exact_dot(coefficients,measure->moment,&moment);
        nt_exact_dot(coefficients,local_origin,&translation);
        nt_spatial_exact_multiply(&shift,&translation,&denominator);
        nt_simulation_accumulate(&moment,&shift);
        offset[i] = nt_spatial_exact_quotient(&moment,&denominator);
        nt_spatial_exact_number(&translation,origin[i],0);
        nt_spatial_exact_multiply(&world,&denominator,&translation);
        nt_simulation_accumulate(&world,&moment);
        center[i] = nt_spatial_exact_quotient(&world,&denominator);
    }
    value->center = (NtVec3){center[0],center[1],center[2]};
    value->center_offset = (NtVec3){offset[0],offset[1],offset[2]};
}

static NtResult immerse_plane(const HullQuery *query, NtHullMeasure *measure, NtHullRefinement *refinement)
{
    const NtHull *hull = query->hull;
    const NtWaterPlane *plane = query->plane;
    HullWorkspace *workspace = query->workspace;
    NtSpatialExact normal[3];
    nt_simulation_exact_vec(plane->normal,normal);
    local_plane(query->pose,hull->origin,plane->point,normal,workspace->planes);
    Polyhedron *poly = &workspace->poly;
    hull_polyhedron(hull,poly);
    const HullClip clip = {hull,workspace->planes,workspace};
    return clip_polyhedron(poly,&clip,0) && measure_polyhedron(poly,&clip,measure,refinement) ?
        NT_RESULT_OK : NT_RESULT_OUT_OF_RANGE;
}

static bool valid_water(const NtWaterTriangle *triangles, size_t count, int *orientation)
{
    NtPoint projected[NT_WATER_MAX_TRIANGLES][3];
    for (size_t i=0;i<count;++i) {
        for (unsigned j=0;j<3;++j) {
            const NtVec3 p = triangles[i].vertices[j];
            if (!nt_simulation_vector(p)) return false;
            projected[i][j] = (NtPoint){p.x,p.z};
        }
        const NtPoint *p = projected[i];
        orientation[i] = nt_hull_orientation_2d(p[0],p[1],p[2]);
        if (!orientation[i]) return false;
        for (size_t j=0;j<i;++j) {
            if (nt_hull_overlap_2d(projected[i],projected[j])) return false;
        }
    }
    return true;
}

static bool clip_water_column(const HullQuery *query, size_t index,
                              NtHullMeasure *measure, NtHullRefinement *refinement)
{
    HullWorkspace *workspace = query->workspace;
    Polyhedron *poly = &workspace->poly;
    ClipPlane *planes = workspace->planes;
    const NtHull *hull = query->hull;
    const HullClip clip = {hull,planes,workspace};
    const NtVec3 *v = query->triangles[index].vertices;
    const int orientation = query->orientation[index];
    hull_polyhedron(hull,poly);
    for (unsigned i=0;i<3;++i) {
        const NtVec3 a = v[i], b = v[(i+1)%3];
        NtSpatialExact normal[3];
        nt_exact_difference(b.z,a.z,&normal[0]);
        nt_spatial_exact_number(&normal[1],0,0);
        nt_exact_difference(a.x,b.x,&normal[2]);
        if (orientation<0) {
            nt_spatial_exact_negate(&normal[0]);
            nt_spatial_exact_negate(&normal[2]);
        }
        local_plane(query->pose,hull->origin,a,normal,planes+i);
        if (!clip_polyhedron(poly,&clip,(uint16_t)i)) return false;
        if (!poly->boundary.face_count) return true;
    }
    NtSpatialExact normal[3];
    nt_hull_normal(v[0],v[1],v[2],normal);
    if (orientation>0)
        for (unsigned i=0;i<3;++i) nt_spatial_exact_negate(&normal[i]);
    local_plane(query->pose,hull->origin,v[0],normal,planes+3);
    return clip_polyhedron(poly,&clip,3) && measure_polyhedron(poly,&clip,measure,refinement);
}

static NtResult measure_query(const HullQuery *query, NtHullMeasure *measure, NtHullRefinement *refinement)
{
    if (query->plane) return immerse_plane(query,measure,refinement);
    for (size_t i=0;i<query->count;++i) {
        if (!clip_water_column(query,i,measure,refinement)) return NT_RESULT_OUT_OF_RANGE;
    }
    return NT_RESULT_OK;
}

static NtResult query_immersion(HullQuery *query, NtHullImmersion *value)
{
    if (query->plane || query->count) {
        query->workspace = malloc(sizeof(*query->workspace));
        if (!query->workspace) return NT_RESULT_ALLOCATION_FAILED;
    }
    NtHullMeasure measure = {0};
    NtHullImmersion result = {.full_volume=query->hull->volume};
    VolumeRounding rounding;
    NtResult status = measure_query(query,&measure,NULL);
    if (status == NT_RESULT_OK) {
        status = immersion_volume(query,&measure,&rounding);
        if (status == NT_RESULT_OK) {
            result.displaced_volume = rounding.lower;
            if (rounding.lower!=rounding.upper)
                status = refine_volume(query,&rounding,&result.displaced_volume);
        }
    }
    if (status == NT_RESULT_OK) {
        result.displaced_volume = fmin(result.full_volume,result.displaced_volume);
        if (result.displaced_volume>0) immersion_center(query,&measure,&result);
        *value = result;
    }
    free(query->workspace);
    return status;
}

NtResult nt_hull_immerse_plane(const NtHull *hull, const NtFloatingBody *body,
                               const NtWaterPlane *plane, NtHullImmersion *out)
{
    if (!hull || !body || !plane || !out) return NT_RESULT_INVALID_ARGUMENT;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    if (!nt_simulation_vector(plane->point) || !nt_simulation_vector(plane->normal) || plane->normal.y<=0)
        return nt_simulation_finish(&saved,NT_RESULT_INVALID_ARGUMENT);
    NtSpatialTransform pose;
    NtHullImmersion value;
    NtResult status = nt_hull_body_transform(body,&pose);
    HullQuery query = {.hull=hull,.pose=&pose,.plane=plane};
    if (status == NT_RESULT_OK) status = query_immersion(&query,&value);
    status = nt_simulation_finish(&saved,status);
    if (status == NT_RESULT_OK) *out = value;
    return status;
}

NtResult nt_hull_immerse_water(const NtHull *hull, const NtFloatingBody *body,
                               const NtWaterTriangle *triangles, size_t count, NtHullImmersion *out)
{
    if (!hull || !body || !out || (count && !triangles)) return NT_RESULT_INVALID_ARGUMENT;
    if (count>NT_WATER_MAX_TRIANGLES) return NT_RESULT_OUT_OF_RANGE;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    int orientation[NT_WATER_MAX_TRIANGLES];
    if (!valid_water(triangles,count,orientation))
        return nt_simulation_finish(&saved,NT_RESULT_INVALID_ARGUMENT);
    NtSpatialTransform pose;
    NtHullImmersion value;
    NtResult status = nt_hull_body_transform(body,&pose);
    HullQuery query = {.hull=hull,.pose=&pose,.triangles=triangles,.orientation=orientation,.count=count};
    if (status == NT_RESULT_OK) status = query_immersion(&query,&value);
    status = nt_simulation_finish(&saved,status);
    if (status == NT_RESULT_OK) *out = value;
    return status;
}
