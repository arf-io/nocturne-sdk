#include <nocturne/property.h>
#include <nocturne/state.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr,"property provider line %d: %s\n",__LINE__,#e); exit(1); } } while (0)
#define OK(e) CHECK((e) == NT_RESULT_OK)

enum { TITLE = 100, AMOUNT, READ_ONLY };
typedef struct Model {
    NtTree *tree;
    NtNodeId node;
    char title[32], order[16];
    size_t extent, transactions;
    uint64_t amount;
    unsigned released, disposed;
    bool write_twice;
} Model;

static NtResult measure(const void *s, NtMeasureContext *ctx, NtConstraints limits, NtCellSize *out)
{
    (void)s; (void)ctx; (void)limits;
    *out = (NtCellSize){1,1};
    return NT_RESULT_OK;
}
static NtResult layout(const void *s, NtLayoutContext *ctx, NtCellSize size)
{
    (void)s; (void)ctx; (void)size;
    return NT_RESULT_OK;
}
static NtResult paint(const void *s, NtPaintContext *ctx, NtCellSize size)
{
    (void)s; (void)ctx; (void)size;
    return NT_RESULT_OK;
}
static void dispose(void *state)
{
    Model *m = state;
    CHECK(m->released == 1);
    ++m->disposed;
}
static void release(void *context)
{
    Model *m = context;
    CHECK(!m->disposed);
    ++m->released;
}
static NtResult read(void *context, NtPropertyId id, NtPropertyOutput *out)
{
    Model *m = context;
    NtValue nested;
    CHECK(nt_tree_property_get(m->tree,(NtPropertyRef){m->node,id},&nested) == NT_RESULT_WRONG_PHASE);
    if (id != TITLE) return nt_property_output_set(out,(NtValue){.kind=NT_VALUE_U64,.as.u64=m->amount});
    char copied[sizeof(m->title)];
    memcpy(copied,m->title,m->extent);
    const NtValue value = {.kind=NT_VALUE_TEXT,.as.text={copied,m->extent}};
    const NtResult result = nt_property_output_set(out,value);
    if (result != NT_RESULT_OK) return result;
    if (m->write_twice) CHECK(nt_property_output_set(out,value) == NT_RESULT_INVALID_ARGUMENT);
    memset(copied,0,sizeof(copied));
    return NT_RESULT_OK;
}
static NtResult prepare(void *context, NtPropertyId id, NtValue value, void **prepared)
{
    Model *m = context;
    m->order[m->transactions++] = id == TITLE ? 'T' : 'A';
    CHECK(nt_tree_properties_apply(m->tree,NULL,0) == NT_RESULT_WRONG_PHASE);
    if ((id == AMOUNT && value.as.u64 > 100) || (id == TITLE && value.as.text.count > sizeof(m->title)))
        return NT_RESULT_OUT_OF_RANGE;
    *prepared = m;
    return NT_RESULT_OK;
}
static void finish(void *context, NtPropertyId id, NtValue value, void *prepared, bool commit)
{
    Model *m = context;
    CHECK(prepared == m);
    m->order[m->transactions++] = commit ? (id == TITLE ? 't' : 'a') : 'd';
    if (!commit) return;
    if (id == TITLE) {
        m->extent = value.as.text.count;
        if (m->extent) memcpy(m->title,value.as.text.data,m->extent);
    } else m->amount = value.as.u64;
}

int main(void)
{
    Model m = {.title="start",.extent=5,.amount=7};
    OK(nt_tree_create(NULL,&m.tree));
    const NtComponentOps ops = {.measure=measure,.layout=layout,.paint=paint,.dispose=dispose};
    OK(nt_tree_mount(m.tree,(NtNodeId){0},&ops,&m,(NtMountOptions){.enabled=true},&m.node));
    NtPropertyProvider provider = {&m,read,prepare,finish,release};
    NtPropertyInfo declarations[] = {{TITLE,NT_VALUE_TEXT,true,NT_INVALIDATE_LAYOUT},
        {AMOUNT,NT_VALUE_U64,true,NT_INVALIDATE_PAINT},{READ_ONLY,NT_VALUE_U64,false,NT_INVALIDATE_NONE}};
    const NtPropertyInfo duplicate[] = {declarations[0],declarations[0]};
    CHECK(nt_tree_define_property_provider(m.tree,m.node,duplicate,2,&provider) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(!m.released);
    OK(nt_tree_define_property_provider(m.tree,m.node,declarations,3,&provider));
    CHECK(nt_tree_define_property_provider(m.tree,m.node,declarations,3,&provider) == NT_RESULT_INVALID_ARGUMENT);
    provider.read = NULL;
    declarations[0].id = 1;
    const NtPropertyRef title = {m.node,TITLE}, amount = {m.node,AMOUNT};
    NtPropertyInfo info;
    OK(nt_tree_property_at(m.tree,m.node,0,&info));
    CHECK(info.id == TITLE && info.kind == NT_VALUE_TEXT && info.writable && info.invalidation == NT_INVALIDATE_LAYOUT);
    NtValue first, again;
    OK(nt_tree_property_get(m.tree,title,&first));
    OK(nt_tree_property_get(m.tree,title,&again));
    CHECK(first.as.text.data == again.as.text.data && !memcmp(first.as.text.data,"start",5));
    m.write_twice = true;
    CHECK(nt_tree_property_get(m.tree,title,&again) == NT_RESULT_INVALID_ARGUMENT);
    m.write_twice = false;
    const NtByteView replacement = {"e\xcc\x81\0",4};
    NtPropertyAssignment changes[] = {{title,{.kind=NT_VALUE_TEXT,.as.text=replacement}},
        {amount,{.kind=NT_VALUE_U64,.as.u64=101}}};
    CHECK(nt_tree_properties_apply(m.tree,changes,2) == NT_RESULT_OUT_OF_RANGE);
    CHECK(m.transactions == 3 && !memcmp(m.order,"TAd",3) && m.amount == 7);
    CHECK(m.extent == 5 && !memcmp(m.title,"start",5));
    changes[1].value.as.u64 = 10;
    OK(nt_tree_properties_apply(m.tree,changes,2));
    CHECK(m.transactions == 7 && !memcmp(m.order+3,"TAta",4) && m.amount == 10);
    OK(nt_tree_property_get(m.tree,title,&again));
    CHECK(again.as.text.count == 4 && !memcmp(again.as.text.data,replacement.data,4));
    CHECK(nt_tree_property_set(m.tree,(NtPropertyRef){m.node,READ_ONLY},changes[1].value) ==
          NT_RESULT_UNSUPPORTED_CAPABILITY);
    const NtPropertyRef refs[] = {title,amount};
    NtStateSnapshot *snapshot;
    OK(nt_tree_state_capture(m.tree,refs,2,NULL,&snapshot));
    OK(nt_tree_state_restore(m.tree,refs,2,snapshot));
    nt_state_snapshot_destroy(snapshot);
    OK(nt_tree_remove(m.tree,m.node));
    CHECK(m.disposed == 1 && m.released == 1);
    nt_tree_destroy(m.tree);
    puts("property provider order TAdTAta, stable text, snapshot restore, released before dispose");
}
