#include "spatial_projection_internal.h"
#include "spatial_mesh_internal.h"

#include <float.h>
#include <string.h>

typedef struct ObjLine { const char *at, *end; } ObjLine;
typedef struct ObjCounts {
    size_t positions, textures, normals, vertices, faces;
} ObjCounts;
typedef struct ObjModel {
    NtSpatialMesh *mesh;
    NtVec3 *positions;
    NtPoint *textures;
    ObjCounts used;
} ObjModel;

NT_DECLARE_ALIGNMENT(NtVec3);
NT_DECLARE_ALIGNMENT(NtPoint);

static bool space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r';
}

static NtByteView word(ObjLine *line)
{
    while (line->at < line->end && space(*line->at)) ++line->at;
    const char *start = line->at;
    while (line->at < line->end && !space(*line->at) && *line->at != '#') ++line->at;
    return (NtByteView){start, (size_t)(line->at-start)};
}

static ObjLine next_line(ObjLine *input)
{
    const char *start = input->at;
    while (input->at < input->end && *input->at != '\n') ++input->at;
    const ObjLine line = {start,input->at};
    if (input->at < input->end) ++input->at;
    return line;
}

static bool token(NtByteView actual, const char *name)
{
    return actual.count == strlen(name) && !memcmp(actual.data,name,actual.count);
}

static bool metadata(NtByteView name)
{
    return token(name,"o") || token(name,"g") || token(name,"s") ||
        token(name,"usemtl") || token(name,"mtllib");
}

static NtResult count_model(ObjLine input, ObjCounts *out)
{
    ObjCounts counts = {0};
    while (input.at < input.end) {
        ObjLine line = next_line(&input);
        const NtByteView name = word(&line);
        if (!name.count || metadata(name)) continue;
        if (token(name,"v")) ++counts.positions;
        else if (token(name,"vt")) ++counts.textures;
        else if (token(name,"vn")) ++counts.normals;
        else if (token(name,"f")) {
            size_t corners = 0;
            while (word(&line).count) {
                if (++corners > NT_SPATIAL_OBJ_MAX_FACE_VERTICES) return NT_RESULT_OUT_OF_RANGE;
            }
            if (corners < 3) return NT_RESULT_INVALID_ARGUMENT;
            counts.vertices += corners;
            counts.faces += corners-2;
        } else return NT_RESULT_UNSUPPORTED_CAPABILITY;
    }
    if (!nt_spatial_mesh_counts_valid(counts.vertices, counts.faces) ||
        counts.positions > UINT32_MAX || counts.positions > SIZE_MAX/sizeof(NtVec3) ||
        counts.textures > UINT32_MAX || counts.textures > SIZE_MAX/sizeof(NtPoint) ||
        counts.normals > UINT32_MAX) return NT_RESULT_OUT_OF_RANGE;
    *out = counts;
    return NT_RESULT_OK;
}

static bool digit(char ch)
{
    return ch >= '0' && ch <= '9';
}

static bool sign(NtByteView field, size_t *i)
{
    if (*i == field.count || (field.data[*i] != '+' && field.data[*i] != '-')) return false;
    return field.data[(*i)++] == '-';
}

static void power_of_ten(unsigned exponent, NtSpatialExact *out)
{
    NtSpatialExact base, product;
    nt_spatial_exact_number(&base,10,0);
    nt_spatial_exact_number(out,1,0);
    while (exponent) {
        if (exponent%2) {
            nt_spatial_exact_multiply(&product,out,&base);
            nt_spatial_exact_copy(out,&product);
        }
        exponent /= 2;
        if (exponent) {
            nt_spatial_exact_multiply(&product,&base,&base);
            nt_spatial_exact_copy(&base,&product);
        }
    }
}

static NtResult number(NtByteView field, double *out)
{
    if (!field.count) return NT_RESULT_INVALID_ARGUMENT;
    if (field.count > NT_SPATIAL_OBJ_MAX_NUMBER_BYTES) return NT_RESULT_OUT_OF_RANGE;
    size_t i = 0;
    const bool negative = sign(field,&i);
    bool decimal = false, seen_digit = false;
    int places = 0;
    NtSpatialExact mantissa, ten, value, scaled;
    nt_spatial_exact_number(&mantissa,0,0);
    nt_spatial_exact_number(&ten,10,0);
    for (; i < field.count; ++i) {
        const char ch = field.data[i];
        if (!decimal && ch == '.') { decimal = true; continue; }
        if (!digit(ch)) break;
        seen_digit = true;
        places += decimal;
        nt_spatial_exact_number(&value,ch-'0',0);
        nt_spatial_exact_multiply(&scaled,&mantissa,&ten);
        nt_spatial_exact_add(&mantissa,&scaled,&value);
    }
    if (!seen_digit) return NT_RESULT_INVALID_ARGUMENT;
    int exponent = 0;
    if (i < field.count && (field.data[i]=='e' || field.data[i]=='E')) {
        ++i;
        const bool below = sign(field,&i);
        if (i == field.count || !digit(field.data[i])) return NT_RESULT_INVALID_ARGUMENT;
        for (; i < field.count && digit(field.data[i]); ++i)
            if (exponent < 10000) exponent = exponent*10+field.data[i]-'0';
        if (below) exponent = -exponent;
    }
    if (i != field.count) return NT_RESULT_INVALID_ARGUMENT;
    exponent -= places;
    if (!mantissa.count || exponent < -500) {
        *out = negative ? -0.0 : 0.0;
        return NT_RESULT_OK;
    }
    if (exponent > 400) return NT_RESULT_OUT_OF_RANGE;
    power_of_ten((unsigned)(exponent < 0 ? -exponent : exponent),&ten);
    double result;
    if (exponent < 0) {
        result = nt_spatial_exact_quotient(&mantissa,&ten);
    } else {
        nt_spatial_exact_multiply(&scaled,&mantissa,&ten);
        nt_spatial_exact_number(&value,1,0);
        result = nt_spatial_exact_quotient(&scaled,&value);
        if (!isfinite(result)) {
            /* Values just above DBL_MAX still round to it. The overflow
             * midpoint is half a final spacing past DBL_MAX. */
            nt_spatial_exact_number(&value,DBL_MAX,0);
            nt_spatial_exact_number(&ten,1,DBL_MAX_EXP-DBL_MANT_DIG-1);
            nt_spatial_exact_add(&mantissa,&value,&ten);
            if (nt_spatial_exact_compare(&scaled,&mantissa) >= 0) return NT_RESULT_OUT_OF_RANGE;
            result = DBL_MAX;
        }
    }
    *out = negative ? -result : result;
    return NT_RESULT_OK;
}

static NtResult coordinates(ObjLine line, size_t minimum, double values[3])
{
    for (size_t i = 0; i < 3; ++i) {
        const NtByteView field = word(&line);
        if (!field.count && i >= minimum) break;
        const NtResult status = number(field,&values[i]);
        if (status != NT_RESULT_OK) return status;
    }
    return word(&line).count ? NT_RESULT_INVALID_ARGUMENT : NT_RESULT_OK;
}

static NtResult index_of(NtByteView field, size_t count, size_t *out)
{
    if (field.count > NT_SPATIAL_OBJ_MAX_NUMBER_BYTES) return NT_RESULT_OUT_OF_RANGE;
    size_t i = 0;
    const bool relative = sign(field,&i);
    const size_t begin = i;
    uint32_t index = 0;
    for (; i < field.count && digit(field.data[i]); ++i) {
        const unsigned value = (unsigned)(field.data[i]-'0');
        if (index > (UINT32_MAX-value)/10) return NT_RESULT_INVALID_ARGUMENT;
        index = index*10+value;
    }
    if (i == begin || i != field.count || !index || index > count) return NT_RESULT_INVALID_ARGUMENT;
    *out = relative ? count-index : index-1;
    return NT_RESULT_OK;
}

static NtByteView part(NtByteView *field)
{
    const char *slash = memchr(field->data,'/',field->count);
    const size_t length = slash ? (size_t)(slash-field->data) : field->count;
    const NtByteView result = {field->data,length};
    *field = slash ? (NtByteView){slash+1,field->count-length-1} : (NtByteView){0};
    return result;
}

static NtResult vertex(ObjModel *model, NtByteView field, NtSpatialVertex *out)
{
    size_t position, uv, normal;
    NtResult status = index_of(part(&field),model->used.positions,&position);
    if (status != NT_RESULT_OK) return status;
    *out = (NtSpatialVertex){.position=model->positions[position]};
    if (!field.data) return NT_RESULT_OK;
    const NtByteView texture = part(&field);
    if (texture.count) {
        status = index_of(texture,model->used.textures,&uv);
        if (status != NT_RESULT_OK) return status;
        out->uv = model->textures[uv];
    } else if (!field.data) return NT_RESULT_INVALID_ARGUMENT;
    return field.data ? index_of(field,model->used.normals,&normal) : NT_RESULT_OK;
}

static NtResult append_face(ObjModel *model, ObjLine line)
{
    const size_t base = model->used.vertices;
    NtByteView field;
    while ((field=word(&line)).count) {
        NtSpatialVertex *out = &model->mesh->vertices[model->used.vertices];
        const NtResult status = vertex(model,field,out);
        if (status != NT_RESULT_OK) return status;
        ++model->used.vertices;
    }
    const size_t count = model->used.vertices-base;
    if (!nt_spatial_polygon_triangulate(model->mesh,base,count,model->used.faces))
        return NT_RESULT_INVALID_ARGUMENT;
    model->used.faces += count-2;
    return NT_RESULT_OK;
}

static NtResult read_model(ObjLine input, ObjModel *model)
{
    while (input.at < input.end) {
        ObjLine line = next_line(&input);
        const NtByteView name = word(&line);
        if (!name.count || metadata(name)) continue;
        if (token(name,"f")) {
            const NtResult status = append_face(model,line);
            if (status != NT_RESULT_OK) return status;
            continue;
        }
        const bool texture = token(name,"vt");
        double values[3] = {0};
        const NtResult status = coordinates(line,texture ? 1u : 3u,values);
        if (status != NT_RESULT_OK) return status;
        if (texture) model->textures[model->used.textures++] = (NtPoint){values[0],values[1]};
        else if (token(name,"v"))
            model->positions[model->used.positions++] = (NtVec3){values[0],values[1],values[2]};
        else ++model->used.normals;
    }
    return NT_RESULT_OK;
}

NtResult nt_spatial_mesh_decode_obj(const NtAllocator *allocator, NtByteView source, NtSpatialMesh **out)
{
    NtAllocator resolved;
    if (!out || !nt_allocator_get(allocator,&resolved) || (source.count && !source.data))
        return NT_RESULT_INVALID_ARGUMENT;
    if (source.count > PTRDIFF_MAX) return NT_RESULT_OUT_OF_RANGE;
    const char *start = source.data ? source.data : "";
    if (memchr(start,0,source.count)) return NT_RESULT_INVALID_ARGUMENT;
    const ObjLine input = {start,start+source.count};
    ObjCounts counts;
    NtResult status = count_model(input,&counts);
    if (status != NT_RESULT_OK) return status;

    ObjModel model = {0};
    status = nt_spatial_mesh_allocate(&resolved,counts.vertices,counts.faces,&model.mesh);
    if (status != NT_RESULT_OK) return status;
    const size_t vertex_bytes = counts.positions*sizeof(model.positions[0]);
    const size_t texture_bytes = counts.textures*sizeof(model.textures[0]);
    if (vertex_bytes) model.positions = nt_allocate(&resolved,vertex_bytes,NT_ALIGNMENT(NtVec3));
    if (texture_bytes) model.textures = nt_allocate(&resolved,texture_bytes,NT_ALIGNMENT(NtPoint));
    if ((vertex_bytes && !model.positions) || (texture_bytes && !model.textures)) {
        status = NT_RESULT_ALLOCATION_FAILED;
    } else {
        fenv_t saved;
        if (!nt_begin_standard_fp(&saved)) status = NT_RESULT_OUT_OF_RANGE;
        else {
            status = read_model(input,&model);
            if (!nt_finish_standard_fp(&saved)) status = NT_RESULT_OUT_OF_RANGE;
        }
    }
    nt_release(&resolved,model.textures,texture_bytes,NT_ALIGNMENT(NtPoint));
    nt_release(&resolved,model.positions,vertex_bytes,NT_ALIGNMENT(NtVec3));
    if (status != NT_RESULT_OK) nt_spatial_mesh_destroy(model.mesh);
    else *out = model.mesh;
    return status;
}
