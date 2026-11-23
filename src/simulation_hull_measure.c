#include "simulation_hull_internal.h"
#include "allocator.h"
#include "simulation_wide_internal.h"
#include <limits.h>

static void wide_product(NtSimulationWide *out, const NtSpatialExact *a, const NtSpatialExact *b)
{
    NtSimulationWide left, right;
    nt_simulation_widen(&left,a);
    nt_simulation_widen(&right,b);
    nt_simulation_wide_multiply(out,&left,&right);
}

static void determinant2(NtSimulationWide *out, const NtSpatialExact *a, const NtSpatialExact *b,
                         const NtSpatialExact *c, const NtSpatialExact *d)
{
    NtSimulationWide left, right;
    wide_product(&left,a,d);
    wide_product(&right,b,c);
    nt_simulation_wide_subtract(out,&left,&right);
}

static void determinant4(NtSimulationWide *out, const NtSpatialExact *rows[4][4])
{
    /* Expand along the first two rows to compute the signed tetrahedral
     * determinant directly, before any rational coordinate is rounded. */
    const unsigned columns[6][4] = {
        {0,1,2,3},{0,2,1,3},{0,3,1,2},{1,2,0,3},{1,3,0,2},{2,3,0,1}
    };
    nt_simulation_wide_number(out,0,0);
    for (unsigned i=0;i<6;++i) {
        const unsigned *v = columns[i];
        NtSimulationWide first, second, product, sum;
        determinant2(&first,rows[0][v[0]],rows[0][v[1]],rows[1][v[0]],rows[1][v[1]]);
        determinant2(&second,rows[2][v[2]],rows[2][v[3]],rows[3][v[2]],rows[3][v[3]]);
        nt_simulation_wide_multiply(&product,&first,&second);
        if ((v[0]+v[1])%2==0) nt_simulation_wide_subtract(&sum,out,&product);
        else nt_simulation_wide_add(&sum,out,&product);
        nt_simulation_wide_copy(out,&sum);
    }
}

static void paired_coordinates(NtSimulationWide *out, const NtHullExactVertex *a,
                               const NtHullExactVertex *b, unsigned axis)
{
    NtSimulationWide first, second;
    wide_product(&first,&a->coordinate[axis],&b->divisor);
    wide_product(&second,&b->coordinate[axis],&a->divisor);
    nt_simulation_wide_add(out,&first,&second);
}

typedef struct HullDigits {
    const uint32_t *limbs;
    unsigned count;
    int offset;
} HullDigits;

NT_DECLARE_ALIGNMENT(uint32_t);

static HullDigits digits(const NtHullInteger *integer)
{
    return (HullDigits){integer->limbs,integer->count,integer->offset};
}

static HullDigits wide_digits(const NtSimulationWide *wide)
{
    return (HullDigits){wide->limbs,wide->count,wide->offset};
}

static bool reserve_digits(NtHullRefinement *refinement, NtHullInteger *out, size_t needed)
{
    if (needed<=out->capacity) return true;
    if (needed>INT_MAX || needed>SIZE_MAX/sizeof(out->limbs[0])) {
        refinement->status = NT_RESULT_OUT_OF_RANGE;
        return false;
    }
    size_t capacity = needed;
    if (needed <= (size_t)INT_MAX/2 && needed <= SIZE_MAX/sizeof(out->limbs[0])/2)
        capacity = needed*2;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) {
        refinement->status = NT_RESULT_UNSUPPORTED_CAPABILITY;
        return false;
    }
    uint32_t *memory = nt_allocate(&refinement->allocator,capacity*sizeof(*memory),NT_ALIGNMENT(uint32_t));
    if (memory) {
        nt_release(&refinement->allocator,out->limbs,out->capacity*sizeof(*memory),NT_ALIGNMENT(uint32_t));
        out->limbs = memory;
        out->capacity = capacity;
    }
    refinement->status = nt_simulation_finish(&saved,memory ? NT_RESULT_OK : NT_RESULT_ALLOCATION_FAILED);
    return refinement->status==NT_RESULT_OK;
}

static bool multiply_integer(NtHullRefinement *refinement, NtHullInteger *out,
                             HullDigits a, HullDigits b)
{
    if (!reserve_digits(refinement,out,(size_t)a.count+b.count)) return false;
    out->count = nt_simulation_multiply_limbs(out->limbs,a.limbs,a.count,b.limbs,b.count);
    out->offset = out->count ? a.offset+b.offset : 0;
    return true;
}

static uint32_t digit_at(HullDigits value, int word)
{
    const unsigned offset = (unsigned)(word-value.offset);
    return offset < value.count ? value.limbs[offset] : 0;
}

static bool add_integer(NtHullRefinement *refinement, NtHullInteger *out,
                        HullDigits a, HullDigits b)
{
    const int first = a.offset<b.offset ? a.offset : b.offset;
    const int a_end = a.offset+(int)a.count, b_end = b.offset+(int)b.count;
    const int last = a_end>b_end ? a_end : b_end;
    if (!reserve_digits(refinement,out,(size_t)(last-first)+1)) return false;
    uint64_t carry = 0;
    for (int i=first;i<last;++i) {
        const uint64_t sum = (uint64_t)digit_at(a,i)+digit_at(b,i)+carry;
        out->limbs[i-first] = (uint32_t)sum;
        carry = sum>>32;
    }
    out->offset = first;
    out->count = (unsigned)(last-first);
    if (carry) out->limbs[out->count++] = (uint32_t)carry;
    return true;
}

static void transfer_integer(NtHullInteger *a, NtHullInteger *b)
{
    const NtHullInteger value = *a;
    *a = *b;
    *b = value;
}

static int compare_digits(HullDigits a, HullDigits b)
{
    return nt_simulation_compare_limbs(a.limbs,a.count,a.offset,b.limbs,b.count,b.offset);
}

static bool add_fraction(NtHullRefinement *refinement, NtSimulationWide *numerator, NtSimulationWide *denominator)
{
    if (!numerator->count) return true;
    /* Remove the radix exponent from each denominator before retaining
     * it. Pure binary powers can be transferred to the numerator too. */
    numerator->offset -= denominator->offset;
    denominator->offset = 0;
    if (denominator->count==1 && !(denominator->limbs[0] & (denominator->limbs[0]-1))) {
        unsigned power = 0;
        for (uint32_t value=denominator->limbs[0];value>1;value>>=1) ++power;
        if (power) {
            NtSimulationWide scale, normalized;
            nt_simulation_wide_number(&scale,1,-(int)power);
            nt_simulation_wide_multiply(&normalized,numerator,&scale);
            nt_simulation_wide_copy(numerator,&normalized);
        }
        denominator->limbs[0] = 1;
    }

    const HullDigits n = wide_digits(numerator), d = wide_digits(denominator);
    const HullDigits current_n = digits(&refinement->numerator), current_d = digits(&refinement->denominator);
    NtHullInteger *work = refinement->work;
    if (!current_n.count) {
        if (!reserve_digits(refinement,&refinement->numerator,n.count) ||
            !reserve_digits(refinement,&refinement->denominator,d.count)) return false;
        memcpy(refinement->numerator.limbs,n.limbs,n.count*sizeof(*n.limbs));
        memcpy(refinement->denominator.limbs,d.limbs,d.count*sizeof(*d.limbs));
        refinement->numerator.count = n.count;
        refinement->numerator.offset = n.offset;
        refinement->denominator.count = d.count;
        refinement->denominator.offset = d.offset;
    } else if (compare_digits(d,current_d)==0) {
        if (!add_integer(refinement,&work[0],n,current_n)) return false;
        transfer_integer(&refinement->numerator,&work[0]);
    } else {
        if (!multiply_integer(refinement,&work[0],current_n,d) ||
            !multiply_integer(refinement,&work[1],n,current_d) ||
            !add_integer(refinement,&work[2],digits(&work[0]),digits(&work[1])) ||
            !multiply_integer(refinement,&work[0],current_d,d)) return false;
        transfer_integer(&refinement->numerator,&work[2]);
        transfer_integer(&refinement->denominator,&work[0]);
    }
    return true;
}

bool nt_hull_refine_add(NtHullRefinement *refinement, const NtSpatialExact *volume)
{
    NtSimulationWide n, d;
    nt_simulation_widen(&n,volume);
    nt_simulation_wide_number(&d,1,0);
    return add_fraction(refinement,&n,&d);
}

bool nt_hull_refine_order(NtHullRefinement *refinement, const NtSpatialExact *threshold, int *order)
{
    if (!refinement->numerator.count) {
        *order = threshold->count ? -1 : 0;
        return true;
    }
    const HullDigits midpoint = {threshold->limbs,threshold->count,threshold->offset};
    if (!multiply_integer(refinement,&refinement->work[0],digits(&refinement->denominator),midpoint))
        return false;
    *order = compare_digits(digits(&refinement->numerator),digits(&refinement->work[0]));
    return true;
}

void nt_hull_refine_clear(NtHullRefinement *refinement)
{
    NtHullInteger *buffers[] = {
        &refinement->numerator,&refinement->denominator,
        &refinement->work[0],&refinement->work[1],&refinement->work[2]
    };
    for (size_t i=0;i<sizeof(buffers)/sizeof(buffers[0]);++i) {
        NtHullInteger *buffer = buffers[i];
        nt_release(&refinement->allocator,buffer->limbs,buffer->capacity*sizeof(buffer->limbs[0]),
                   NT_ALIGNMENT(uint32_t));
    }
}

static void divided_measure(NtSpatialExact *out, const NtSimulationWide *n, const NtSimulationWide *d)
{
    const int shift = n->offset+(int)n->count-d->offset-(int)d->count;
    nt_spatial_exact_number(out,0,0);
    /* Terms below this floor cannot accumulate to representable volume
     * or first moments inside the public geometry and pose bounds. */
    if (!n->count || shift<-128) return;
    NtSimulationWide normalized;
    nt_simulation_wide_copy(&normalized,n);
    normalized.offset -= shift;
    /* Four sequential quotients put roundoff below the absolute force and
     * torque tolerance, even at the largest admitted density and volume. */
    for (unsigned i=0;i<4 && normalized.count;++i) {
        NtSimulationWide head, product, residual;
        const double component = nt_simulation_wide_quotient(&normalized,d);
        if (!component) break;
        NtSpatialExact part;
        nt_spatial_exact_number(&part,component,32*shift);
        nt_simulation_accumulate(out,&part);
        nt_simulation_wide_number(&head,component,0);
        nt_simulation_wide_multiply(&product,&head,d);
        nt_simulation_wide_subtract(&residual,&normalized,&product);
        nt_simulation_wide_copy(&normalized,&residual);
    }
}

bool nt_hull_measure_exact(NtHullMeasure *measure, NtHullRefinement *refinement,
                            const NtHullExactVertex *vertices[4], NtHullTetraScratch *work)
{
    const NtSpatialExact *rows[4][4];
    for (unsigned i=0;i<4;++i) {
        for (unsigned j=0;j<3;++j) rows[i][j] = &vertices[i]->coordinate[j];
        rows[i][3] = &vertices[i]->divisor;
    }
    determinant4(&work->determinant,rows);
    if (!work->determinant.count) return true;
    if (work->determinant.negative) return false;
    wide_product(&work->ab,rows[0][3],rows[1][3]);
    wide_product(&work->cd,rows[2][3],rows[3][3]);
    nt_simulation_wide_multiply(&work->divisor,&work->ab,&work->cd);
    if (refinement) return add_fraction(refinement,&work->determinant,&work->divisor);

    NtSpatialExact volume;
    divided_measure(&volume,&work->determinant,&work->divisor);
    nt_simulation_accumulate(&measure->six_volume,&volume);
    for (unsigned i=0;i<3;++i) {
        paired_coordinates(&work->pair,vertices[0],vertices[1],i);
        nt_simulation_wide_multiply(&work->first,&work->pair,&work->cd);
        paired_coordinates(&work->pair,vertices[2],vertices[3],i);
        nt_simulation_wide_multiply(&work->second,&work->pair,&work->ab);
        nt_simulation_wide_add(&work->sum,&work->first,&work->second);

        NtSpatialExact coordinate, moment;
        divided_measure(&coordinate,&work->sum,&work->divisor);
        nt_spatial_exact_multiply(&moment,&volume,&coordinate);
        nt_simulation_accumulate(&measure->moment[i],&moment);
    }
    return true;
}
