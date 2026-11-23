#include <nocturne/event_rules.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr,"event rules line %d: %s\n",__LINE__,#e); exit(EXIT_FAILURE); } } while (0)
#define OK(e) CHECK((e) == NT_RESULT_OK)
#define VIEW(s) ((NtByteView){(s), sizeof(s)-1})

enum { PREVIEW = 10, RAW, DIRECT, DERIVED, EMPTY, BUBBLE, FOCUS_LOSS };
enum { TITLE = 100, COUNT };
typedef enum Mode {
    NORMAL, BAD_FIELD, CALLBACK_FAILURE, STALE_SOURCE, BAD_ORDER, REFUSAL, BAD_NOTIFICATION
} Mode;

typedef struct Allocation {
    size_t calls, fail, live;
} Allocation;

static void *allocate(void *context, size_t bytes, size_t alignment)
{
    (void)alignment;
    Allocation *allocation = context;
    if (++allocation->calls == allocation->fail) return NULL;
    void *address = malloc(bytes);
    if (address) ++allocation->live;
    return address;
}

static void release(void *context, void *address, size_t bytes, size_t alignment)
{
    (void)bytes;
    (void)alignment;
    Allocation *allocation = context;
    CHECK(allocation->live != 0);
    --allocation->live;
    free(address);
}

typedef struct Model {
    NtTree *tree;
    NtNodeId root, child, stale;
    NtEventContext *active;
    Allocation allocation;
    Mode mode;
    size_t fail_offset, emitted, reads, predicates, fields, disposed, events;
    char title[5], scratch[5];
} Model;

static bool same_node(NtNodeId a, NtNodeId b)
{
    return a.tree == b.tree && a.slot == b.slot && a.generation == b.generation;
}

static bool equal_bytes(NtByteView a, NtByteView b)
{
    return a.count == b.count && (!a.count || !memcmp(a.data,b.data,a.count));
}

static NtResult measure(const void *state, NtMeasureContext *context, NtConstraints limits,
                        NtCellSize *preferred)
{
    (void)state;
    *preferred = (NtCellSize){limits.columns.min,limits.rows.min};
    if (!nt_measure_child_count(context)) return NT_RESULT_OK;
    NtNodeId child;
    NtResult result = nt_measure_child_at(context,0,&child);
    return result == NT_RESULT_OK ? nt_measure_child(context,child,limits,preferred) : result;
}

static NtResult layout(const void *state, NtLayoutContext *context, NtCellSize assigned)
{
    (void)state;
    if (!nt_layout_child_count(context)) return NT_RESULT_OK;
    NtNodeId child;
    NtResult result = nt_layout_child_at(context,0,&child);
    return result == NT_RESULT_OK ? nt_layout_place(context,child,(NtCellRect){{0,0},assigned},0,NULL) : result;
}

static NtResult paint(const void *state, NtPaintContext *context, NtCellSize assigned)
{
    (void)state;
    (void)context;
    (void)assigned;
    return NT_RESULT_OK;
}

static NtResult read_title(const void *state, NtValue *out)
{
    Model *model = (Model *)state;
    ++model->reads;
    *out = (NtValue){.kind=NT_VALUE_TEXT,.as.text={model->title,sizeof(model->title)}};
    return NT_RESULT_OK;
}

static NtResult read_count(const void *state, NtValue *out)
{
    Model *model = (Model *)state;
    ++model->reads;
    *out = (NtValue){.kind=NT_VALUE_U64,.as.u64=33};
    return NT_RESULT_OK;
}

static NtResult allows(void *state, const NtEvent *event, const NtValue *values, size_t n, bool *out)
{
    Model *model = state;
    ++model->predicates;
    CHECK(n == 2 && values[0].kind == NT_VALUE_TEXT && values[1].kind == NT_VALUE_U64);
    CHECK(values[0].as.text.data != model->title);
    CHECK(equal_bytes(values[0].as.text, VIEW("e\xcc\x81\0x")));
    size_t untouched = 999;
    CHECK(nt_event_apply_rules(model->active,NULL,0,&untouched) == NT_RESULT_WRONG_PHASE);
    CHECK(untouched == 999);
    CHECK(nt_event_emit(model->active,RAW,VIEW("forbidden")) == NT_RESULT_WRONG_PHASE);
    CHECK(nt_event_emit_fields(model->active,DIRECT,NULL,0) == NT_RESULT_WRONG_PHASE);
    CHECK(nt_tree_remove(model->tree,model->child) == NT_RESULT_WRONG_PHASE);
    *out = event->data.key.state == NT_KEY_PRESS && values[1].as.u64 == 33;
    return NT_RESULT_OK;
}

static NtResult derived_title(void *state, const NtEvent *event, const NtValue *values,
                              size_t n, NtValue *out)
{
    (void)event;
    Model *model = state;
    ++model->fields;
    CHECK(n == 2);
    memcpy(model->scratch,values[0].as.text.data,sizeof(model->scratch));
    model->scratch[0] = 'E';
    *out = (NtValue){.kind=NT_VALUE_TEXT,.as.text={model->scratch,sizeof(model->scratch)}};
    return NT_RESULT_OK;
}

static NtResult derived_number(void *state, const NtEvent *event, const NtValue *values,
                               size_t n, NtValue *out)
{
    Model *model = state;
    ++model->fields;
    CHECK(n == 2);
    memset(model->scratch,'!',sizeof(model->scratch));
    *out = model->mode == BAD_FIELD ? (NtValue){.kind=NT_VALUE_BOOL,.as.flag=true} :
        (NtValue){.kind=NT_VALUE_U64,.as.u64=values[1].as.u64 + event->at};
    return NT_RESULT_OK;
}

static NtResult on_parent(void *state, NtEventContext *context, const NtEvent *event, NtEventReply *reply)
{
    (void)event;
    (void)reply;
    const Model *model = state;
    if (model->mode != NORMAL && model->mode != CALLBACK_FAILURE) return NT_RESULT_OK;
    const NtEventRule rules[] = {
        {.event=NT_EVENT_KEY,.phase=NT_EVENT_PREVIEW,.intent_kind=PREVIEW},
        {.event=NT_EVENT_KEY,.phase=NT_EVENT_BUBBLE,.intent_kind=BUBBLE}
    };
    return nt_event_apply_rules(context,rules,2,NULL);
}

static NtResult on_child(void *state, NtEventContext *context, const NtEvent *event, NtEventReply *reply)
{
    Model *model = state;
    CHECK(model->disposed == 0);
    ++model->events;
    const NtEventRule loss = {.event=NT_EVENT_FOCUS_LOSS,.phase=NT_EVENT_TARGET,.intent_kind=FOCUS_LOSS};
    if (event->kind != NT_EVENT_KEY) {
        if (model->mode == BAD_NOTIFICATION && event->kind == NT_EVENT_FOCUS_LOSS) {
            reply->capture = (NtCaptureRequest)(NT_CAPTURE_RELEASE + 1);
            reply->invalidate = NT_INVALIDATE_PAINT;
        }
        return nt_event_apply_rules(context,&loss,1,NULL);
    }
    if (event->data.key.state == NT_KEY_PRESS) {
        char bytes[] = {0,'r',0};
        OK(nt_event_emit(context,RAW,(NtByteView){bytes,sizeof(bytes)}));
        bytes[1] = 'b';
        const NtIntentField fields[] = {{2,{.kind=NT_VALUE_TEXT,.as.text={bytes,sizeof(bytes)}}}};
        OK(nt_event_emit_fields(context,DIRECT,fields,1));
        bytes[1] = '!';
    }
    model->active = context;
    NtPropertyRef properties[] = {{model->root,TITLE},{model->root,COUNT}};
    NtPropertyRef stale = {model->stale,TITLE};
    NtEventFieldRule fields[] = {{1,NT_VALUE_TEXT,derived_title},{8,NT_VALUE_U64,derived_number}};
    if (model->mode == BAD_ORDER) fields[1].id = 1;
    const NtEventRule rules[] = {
        {.event=NT_EVENT_KEY,.phase=NT_EVENT_TARGET,.properties=properties,.property_count=2,
         .predicate=allows,.intent_kind=DERIVED,.fields=fields,.field_count=2,.context=model},
        {.event=NT_EVENT_KEY,.phase=NT_EVENT_TARGET,.intent_kind=EMPTY,.properties=&stale,
         .property_count=model->mode == STALE_SOURCE},
        {.event=NT_EVENT_TEXT,.phase=NT_EVENT_TARGET,.intent_kind=EMPTY,.properties=&stale,.property_count=1}
    };
    if (model->mode == REFUSAL) model->allocation.fail = model->allocation.calls + model->fail_offset;
    model->emitted = 999;
    const NtResult result = nt_event_apply_rules(context,rules,3,&model->emitted);
    model->allocation.fail = 0;
    model->active = NULL;
    if (result != NT_RESULT_OK) {
        CHECK(model->emitted == 999);
        CHECK(nt_event_emit(context,RAW,VIEW("later")) == result);
        reply->invalidate = NT_INVALIDATE_LAYOUT;
        return NT_RESULT_OK;
    }
    if (model->mode == CALLBACK_FAILURE) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    return NT_RESULT_OK;
}

static void disposed(void *state)
{
    Model *model = state;
    ++model->disposed;
}

static NtEventIntentView expect(NtTree *tree, NtNodeId source, uint32_t kind,
                                NtIntentPayloadKind type, size_t count)
{
    NtEventIntentView event;
    NtIntentView legacy;
    CHECK(nt_tree_peek_event_intent(tree,&event) && nt_tree_peek_intent(tree,&legacy));
    CHECK(same_node(event.intent.source,source) && same_node(legacy.source,source));
    CHECK(event.intent.kind == kind && legacy.kind == kind);
    CHECK(event.payload_kind == type && event.field_count == count && !!event.fields == !!count);
    CHECK(equal_bytes(event.intent.payload,legacy.payload));
    if (type == NT_INTENT_FIELDS) CHECK(!legacy.payload.count && !legacy.payload.data);
    return event;
}

static void empty(NtTree *tree)
{
    NtEventIntentView value = {.intent.kind=999};
    CHECK(!nt_tree_peek_event_intent(tree,&value) && value.intent.kind == 999);
}

static NtResult key(NtTree *tree, NtTimeUs at, NtKeyState state, bool reserve)
{
    const NtEvent event = {.kind=NT_EVENT_KEY,.at=at,.data.key={NT_KEY_ENTER,0,state}};
    const NtKeyChord chord = {NT_KEY_ENTER,0};
    const NtDispatchPolicy policy = {.reserved=&chord,.reserved_count=reserve};
    NtDispatchResult report;
    const NtResult result = nt_tree_dispatch(tree,&event,(NtByteView){0},&policy,&report);
    if (result == NT_RESULT_OK) CHECK(report.reserved == reserve);
    return result;
}

int main(void)
{
    Model model = {.title={'e',(char)0xcc,(char)0x81,0,'x'}};
    const NtAllocator allocator = {&model.allocation,allocate,release};
    OK(nt_tree_create(&allocator,&model.tree));
    const NtMountOptions options = {.enabled=true,.focusable=true};
    const NtComponentOps parent = {.measure=measure,.layout=layout,.paint=paint,.event=on_parent};
    const NtComponentOps child = {.measure=measure,.layout=layout,.paint=paint,.event=on_child,.dispose=disposed};
    OK(nt_tree_mount(model.tree,(NtNodeId){0},&parent,&model,options,&model.root));
    OK(nt_tree_mount(model.tree,model.root,&child,&model,options,&model.child));
    OK(nt_tree_mount(model.tree,model.root,&parent,&model,options,&model.stale));
    OK(nt_tree_remove(model.tree,model.stale));
    const NtPropertyDescriptor properties[] = {
        {.id=TITLE,.kind=NT_VALUE_TEXT,.read=read_title},
        {.id=COUNT,.kind=NT_VALUE_U64,.read=read_count}
    };
    OK(nt_tree_define_properties(model.tree,model.root,properties,2));
    OK(nt_tree_publish(model.tree,(NtCellSize){5,3}));
    OK(nt_tree_set_focus(model.tree,model.child));
    empty(model.tree);

    OK(key(model.tree,5,NT_KEY_PRESS,true));
    CHECK(model.reads == 0 && model.predicates == 0 && model.fields == 0);
    empty(model.tree);
    OK(key(model.tree,6,NT_KEY_PRESS,false));
    CHECK(model.emitted == 2 && model.reads == 2 && model.predicates == 1 && model.fields == 2);
    expect(model.tree,model.root,PREVIEW,NT_INTENT_FIELDS,0);
    nt_tree_pop_intent(model.tree);
    NtEventIntentView raw = expect(model.tree,model.child,RAW,NT_INTENT_BYTES,0);
    CHECK(equal_bytes(raw.intent.payload,VIEW("\0r\0")));
    nt_tree_pop_intent(model.tree);
    NtEventIntentView direct = expect(model.tree,model.child,DIRECT,NT_INTENT_FIELDS,1);
    CHECK(direct.fields[0].id == 2 && direct.fields[0].value.kind == NT_VALUE_TEXT);
    CHECK(equal_bytes(direct.fields[0].value.as.text,VIEW("\0b\0")));
    nt_tree_pop_intent(model.tree);
    NtEventIntentView derived = expect(model.tree,model.child,DERIVED,NT_INTENT_FIELDS,2);
    CHECK(derived.fields[0].id == 1 && derived.fields[0].value.kind == NT_VALUE_TEXT);
    CHECK(equal_bytes(derived.fields[0].value.as.text,VIEW("E\xcc\x81\0x")));
    CHECK(derived.fields[1].id == 8 && derived.fields[1].value.kind == NT_VALUE_U64);
    CHECK(derived.fields[1].value.as.u64 == 39);
    nt_tree_pop_intent(model.tree);
    expect(model.tree,model.child,EMPTY,NT_INTENT_FIELDS,0);
    nt_tree_pop_intent(model.tree);
    expect(model.tree,model.root,BUBBLE,NT_INTENT_FIELDS,0);
    nt_tree_pop_intent(model.tree);
    empty(model.tree);

    OK(key(model.tree,7,NT_KEY_RELEASE,false));
    CHECK(model.emitted == 1 && model.reads == 4 && model.predicates == 2 && model.fields == 2);
    expect(model.tree,model.root,PREVIEW,NT_INTENT_FIELDS,0);
    nt_tree_pop_intent(model.tree);
    expect(model.tree,model.child,EMPTY,NT_INTENT_FIELDS,0);
    nt_tree_pop_intent(model.tree);
    expect(model.tree,model.root,BUBBLE,NT_INTENT_FIELDS,0);
    nt_tree_pop_intent(model.tree);
    empty(model.tree);

    const NtFrame *frame = nt_tree_frame(model.tree);
    model.mode = CALLBACK_FAILURE;
    CHECK(key(model.tree,8,NT_KEY_PRESS,false) == NT_RESULT_UNSUPPORTED_CAPABILITY);
    expect(model.tree,model.root,PREVIEW,NT_INTENT_FIELDS,0);
    const size_t held = model.allocation.live;
    CHECK(nt_tree_pending(model.tree) == NT_INVALIDATE_NONE);
    const size_t predicates = model.predicates, fields = model.fields, reads = model.reads;

    model.mode = STALE_SOURCE;
    CHECK(key(model.tree,9,NT_KEY_PRESS,false) == NT_RESULT_STALE_REFERENCE);
    CHECK(model.predicates == predicates && model.fields == fields && model.reads == reads);
    model.mode = BAD_ORDER;
    CHECK(key(model.tree,10,NT_KEY_PRESS,false) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(model.predicates == predicates && model.fields == fields && model.reads == reads);
    model.mode = BAD_FIELD;
    CHECK(key(model.tree,11,NT_KEY_PRESS,false) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(model.predicates == predicates+1 && model.fields == fields+2 && model.reads == reads+2);
    CHECK(model.allocation.live == held);

    model.mode = REFUSAL;
    for (size_t i = 1; i <= 5; ++i) {
        model.fail_offset = i;
        CHECK(key(model.tree,11+i,NT_KEY_PRESS,false) == NT_RESULT_ALLOCATION_FAILED);
        CHECK(model.allocation.live == held);
        expect(model.tree,model.root,PREVIEW,NT_INTENT_FIELDS,0);
    }
    CHECK(nt_tree_frame(model.tree) == frame && nt_tree_pending(model.tree) == NT_INVALIDATE_NONE);
    nt_tree_pop_intent(model.tree);
    empty(model.tree);

    model.mode = BAD_NOTIFICATION;
    OK(nt_tree_set_focus(model.tree,(NtNodeId){0}));
    CHECK(!nt_tree_focus(model.tree).tree && nt_tree_pending(model.tree) == NT_INVALIDATE_NONE);
    empty(model.tree);
    model.mode = REFUSAL;
    OK(nt_tree_set_focus(model.tree,model.child));
    OK(nt_tree_remove(model.tree,model.child));
    CHECK(model.disposed == 1);
    expect(model.tree,model.child,FOCUS_LOSS,NT_INTENT_FIELDS,0);
    nt_tree_pop_intent(model.tree);
    const size_t events = model.events;
    OK(key(model.tree,17,NT_KEY_PRESS,false));
    CHECK(model.events == events);
    empty(model.tree);
    nt_tree_destroy(model.tree);
    CHECK(model.allocation.live == 0 && model.disposed == 1);
    return EXIT_SUCCESS;
}
