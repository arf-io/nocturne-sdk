#include "spatial_mesh_internal.h"
#include "spatial_geometry_internal.h"

#include <math.h>
#include <string.h>

static int orient(NtPoint a, NtPoint b, NtPoint c)
{
    NtSpatialExact x, y, u, v, area;
    nt_exact_difference(b.x, a.x, &x);
    nt_exact_difference(b.y, a.y, &y);
    nt_exact_difference(c.x, a.x, &u);
    nt_exact_difference(c.y, a.y, &v);
    nt_exact_determinant(&x, &y, &u, &v, &area);
    return nt_spatial_exact_sign(&area);
}

static void normal(NtVec3 a, NtVec3 b, NtVec3 c, NtSpatialExact out[3])
{
    NtSpatialExact u[3], v[3];
    nt_exact_displacement(b,a,u);
    nt_exact_displacement(c,a,v);
    nt_exact_cross(u,v,out);
}

static bool on_plane(NtVec3 a, NtVec3 b, const NtSpatialExact n[3])
{
    NtSpatialExact delta[3], total;
    nt_exact_displacement(b,a,delta);
    nt_exact_dot(delta,n,&total);
    return !total.count;
}

static bool flatten(const NtSpatialVertex *points, size_t count, NtPoint *flat)
{
    NtSpatialExact n[3];
    unsigned axis = 3;
    const NtVec3 a = points[0].position, b = points[1].position;
    for (size_t i = 2; i < count && axis == 3; ++i) {
        normal(a, b, points[i].position, n);
        for (unsigned j = 0; j < 3; ++j) {
            if (n[j].count) { axis = j; break; }
        }
    }
    if (axis == 3) return false;
    for (size_t i = 0; i < count; ++i) {
        const NtVec3 p = points[i].position;
        if (!on_plane(a, p, n)) return false;
        flat[i] = axis == 0 ? (NtPoint){p.y,p.z} : axis == 1 ? (NtPoint){p.z,p.x} : (NtPoint){p.x,p.y};
    }
    return true;
}

static bool overlap(double a, double b, double c, double d)
{
    return fmin(a, b) <= fmax(c, d) && fmin(c, d) <= fmax(a, b);
}

static bool edges_cross(NtPoint a, NtPoint b, NtPoint c, NtPoint d)
{
    return overlap(a.x,b.x,c.x,d.x) && overlap(a.y,b.y,c.y,d.y) &&
        orient(a,b,c)*orient(a,b,d) <= 0 && orient(c,d,a)*orient(c,d,b) <= 0;
}

static bool simple(const NtPoint *points, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        const NtPoint a = points[i], b = points[(i+1)%count];
        if (a.x == b.x && a.y == b.y) return false;
        for (size_t j = i+2; j < count; ++j) {
            if (i == 0 && j+1 == count) continue;
            if (edges_cross(a, b, points[j], points[(j+1)%count])) return false;
        }
    }
    return true;
}

static int winding(const NtPoint *points, size_t count)
{
    NtSpatialExact area;
    nt_spatial_exact_number(&area, 0, 0);
    for (size_t i = 0; i < count; ++i) {
        const NtPoint a = points[i], b = points[(i+1)%count];
        NtSpatialExact x, y, u, v, cross, sum;
        nt_spatial_exact_number(&x,a.x,0);
        nt_spatial_exact_number(&y,a.y,0);
        nt_spatial_exact_number(&u,b.x,0);
        nt_spatial_exact_number(&v,b.y,0);
        nt_exact_determinant(&x,&y,&u,&v,&cross);
        nt_spatial_exact_add(&sum,&area,&cross);
        nt_spatial_exact_copy(&area,&sum);
    }
    return nt_spatial_exact_sign(&area);
}

static bool ear(const NtPoint *points, const unsigned *order, size_t count, size_t i, int sign)
{
    const unsigned a = order[(i+count-1)%count], b = order[i], c = order[(i+1)%count];
    if (orient(points[a], points[b], points[c]) != sign) return false;
    for (size_t j = 0; j < count; ++j) {
        const unsigned vertex = order[j];
        if (vertex == a || vertex == b || vertex == c) continue;
        const NtPoint p = points[vertex];
        if (sign*orient(points[a],points[b],p) >= 0 && sign*orient(points[b],points[c],p) >= 0 &&
            sign*orient(points[c],points[a],p) >= 0) return false;
    }
    return true;
}

static void face(NtSpatialMesh *mesh, size_t index, size_t base, unsigned a, unsigned b, unsigned c)
{
    mesh->faces[index] = (NtSpatialFace){{(uint32_t)(base+a),(uint32_t)(base+b),(uint32_t)(base+c)},
                                        (uint64_t)index};
}

bool nt_spatial_polygon_triangulate(NtSpatialMesh *mesh, size_t base, size_t count, size_t first_face)
{
    NtPoint points[NT_SPATIAL_OBJ_MAX_FACE_VERTICES];
    unsigned order[NT_SPATIAL_OBJ_MAX_FACE_VERTICES];
    if (!flatten(mesh->vertices+base, count, points) || !simple(points,count)) return false;
    const int sign = winding(points, count);
    if (!sign) return false;
    for (size_t i = 0; i < count; ++i) order[i] = (unsigned)i;
    while (count > 3) {
        size_t i = 0;
        while (i < count && !ear(points,order,count,i,sign)) ++i;
        if (i == count) return false;
        face(mesh, first_face++, base, order[(i+count-1)%count], order[i], order[(i+1)%count]);
        memmove(order+i, order+i+1, (count-i-1)*sizeof(order[0]));
        --count;
    }
    if (orient(points[order[0]],points[order[1]],points[order[2]]) != sign) return false;
    face(mesh, first_face, base, order[0], order[1], order[2]);
    return true;
}
