#include <nocturne/controls.h>
#include <nocturne/property.h>
#include <nocturne/schedule.h>
#include <nocturne/state.h>
#include <nocturne/widgets.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr,"state update line %d: %s\n",__LINE__,#e); exit(EXIT_FAILURE); } } while (0)
#define OK(e) CHECK((e) == NT_RESULT_OK)
#define BYTES(s) ((NtByteView){(s),sizeof(s)-1})
#define NUMBER(n) ((NtValue){.kind=NT_VALUE_U64,.as.u64=(n)})
#define TEXT(p,n) ((NtValue){.kind=NT_VALUE_TEXT,.as.text={(p),(n)}})

static NtNodeId label(NtTree *tree, NtNodeId root, NtByteView text)
{
    const NtLabelOptions options = {.text=text,
        .style={.width={NT_WIDTH_ONE,NT_WIDTH_TWO},.tab_stop=4,
                .color={{255,255,255,255},{0,0,0,0}}}};
    NtNodeId node;
    OK(nt_label_mount(tree,root,(NtMountOptions){.enabled=true},&options,&node));
    return node;
}

static void expect_text(NtTree *tree, NtPropertyRef property, NtByteView expected)
{
    NtValue value;
    OK(nt_tree_property_get(tree,property,&value));
    CHECK(value.kind == NT_VALUE_TEXT && value.as.text.count == expected.count);
    CHECK(!expected.count || !memcmp(value.as.text.data,expected.data,expected.count));
}

static void atomic_batch(void)
{
    NtTree *tree;
    NtNodeId root, progress;
    const NtMountOptions enabled = {.enabled=true};
    const NtBoxLayout box = {.kind=NT_BOX_LAYOUT_COLUMN};
    OK(nt_tree_create(NULL,&tree));
    OK(nt_box_layout_mount(tree,(NtNodeId){0},enabled,&box,&root));
    const NtNodeId left = label(tree,root,BYTES("one"));
    const NtNodeId right = label(tree,root,BYTES("two\0\xc3\xa9"));
    OK(nt_progress_mount(tree,root,enabled,(NtProgressOptions){.maximum=12},&progress));
    OK(nt_tree_publish(tree,(NtCellSize){20,6}));
    const NtFrame *frame = nt_tree_frame(tree);
    const NtPropertyRef first = {left,NT_PROPERTY_TEXT}, second = {right,NT_PROPERTY_TEXT};
    const NtPropertyRef counter = {progress,NT_PROPERTY_VALUE};
    NtValue a, b;
    OK(nt_tree_property_get(tree,first,&a));
    OK(nt_tree_property_get(tree,second,&b));
    NtPropertyAssignment update[] = {{first,b},{second,a},{counter,NUMBER(13)}};
    CHECK(nt_tree_properties_apply(tree,update,3) == NT_RESULT_OUT_OF_RANGE);
    CHECK(nt_tree_frame(tree) == frame && nt_tree_pending(tree) == NT_INVALIDATE_NONE);
    expect_text(tree,first,BYTES("one"));
    expect_text(tree,second,BYTES("two\0\xc3\xa9"));

    update[2] = (NtPropertyAssignment){first,a};
    CHECK(nt_tree_properties_apply(tree,update,3) == NT_RESULT_INVALID_ARGUMENT);
    update[2] = (NtPropertyAssignment){counter,NUMBER(12)};
    OK(nt_tree_properties_apply(tree,update,3));
    CHECK(nt_tree_frame(tree) == frame && nt_tree_pending(tree) == NT_INVALIDATE_LAYOUT);
    expect_text(tree,first,BYTES("two\0\xc3\xa9"));
    expect_text(tree,second,BYTES("one"));
    NtValue number;
    OK(nt_tree_property_get(tree,counter,&number));
    CHECK(number.kind == NT_VALUE_U64 && number.as.u64 == 12);
    OK(nt_tree_properties_apply(tree,NULL,0));
    OK(nt_tree_remove(tree,right));
    const NtPropertyAssignment stale = {second,NUMBER(1)};
    CHECK(nt_tree_properties_apply(tree,&stale,1) == NT_RESULT_STALE_REFERENCE);
    nt_tree_destroy(tree);
}

static uint64_t get_number(NtTree *tree, NtPropertyRef property)
{
    NtValue value;
    OK(nt_tree_property_get(tree,property,&value));
    CHECK(value.kind == NT_VALUE_U64);
    return value.as.u64;
}

static void snapshot_round_trip(void)
{
    const NtMountOptions enabled = {.enabled=true};
    const NtBoxLayout box = {.kind=NT_BOX_LAYOUT_COLUMN};
    NtTree *tree;
    NtNodeId root, progress;
    OK(nt_tree_create(NULL,&tree));
    OK(nt_box_layout_mount(tree,(NtNodeId){0},enabled,&box,&root));
    NtNodeId heading = label(tree,root,BYTES("e\xcc\x81\0\xe4\xb8\xad"));
    OK(nt_progress_mount(tree,root,enabled,(NtProgressOptions){.value=9,.maximum=10},&progress));
    const NtPropertyRef source[] = {{heading,NT_PROPERTY_TEXT},{heading,NT_PROPERTY_FOREGROUND},
                                    {progress,NT_PROPERTY_VALUE}};
    const NtValue green = {.kind=NT_VALUE_RGBA,.as.rgba={4,200,19,255}};
    OK(nt_tree_property_set(tree,source[1],green));
    NtStateSnapshot *snapshot;
    OK(nt_tree_state_capture(tree,source,3,NULL,&snapshot));
    nt_tree_destroy(tree);
    const NtByteView bytes = nt_state_snapshot_bytes(snapshot);
    CHECK(bytes.count < 256 && bytes.count > 16 && !memcmp(bytes.data,"NTST\1\0\0\0\3\0\0\0\0\0\0\0",16));
    char copy[256];
    memcpy(copy,bytes.data,bytes.count);
    NtStateSnapshot *imported = NULL;
    for (size_t n = 0; n < bytes.count; ++n) {
        CHECK(nt_state_snapshot_import(NULL,(NtByteView){copy,n},&imported) == NT_RESULT_INVALID_ARGUMENT);
        CHECK(!imported);
    }
    copy[4] = 2;
    CHECK(nt_state_snapshot_import(NULL,(NtByteView){copy,bytes.count},&imported) == NT_RESULT_UNSUPPORTED_CAPABILITY);
    copy[4] = 1;
    copy[29] = (char)0xff;
    CHECK(nt_state_snapshot_import(NULL,(NtByteView){copy,bytes.count},&imported) == NT_RESULT_INVALID_ARGUMENT);
    memcpy(copy,bytes.data,bytes.count);
    copy[bytes.count] = 0;
    CHECK(nt_state_snapshot_import(NULL,(NtByteView){copy,bytes.count+1},&imported) == NT_RESULT_INVALID_ARGUMENT);
    OK(nt_state_snapshot_import(NULL,(NtByteView){copy,bytes.count},&imported));
    nt_state_snapshot_destroy(snapshot);
    memset(copy,0,sizeof(copy));

    OK(nt_tree_create(NULL,&tree));
    OK(nt_box_layout_mount(tree,(NtNodeId){0},enabled,&box,&root));
    heading = label(tree,root,BYTES("before"));
    OK(nt_progress_mount(tree,root,enabled,(NtProgressOptions){.value=4,.maximum=5},&progress));
    NtPropertyRef target[] = {{heading,NT_PROPERTY_TEXT},{heading,NT_PROPERTY_FOREGROUND},
                              {progress,NT_PROPERTY_VALUE}};
    OK(nt_tree_publish(tree,(NtCellSize){20,6}));
    const NtFrame *frame = nt_tree_frame(tree);
    CHECK(nt_tree_state_restore(tree,target,3,imported) == NT_RESULT_OUT_OF_RANGE);
    CHECK(nt_tree_frame(tree) == frame && nt_tree_pending(tree) == NT_INVALIDATE_NONE);
    expect_text(tree,target[0],BYTES("before"));
    CHECK(get_number(tree,target[2]) == 4);
    OK(nt_tree_remove(tree,progress));
    CHECK(nt_tree_state_restore(tree,target,3,imported) == NT_RESULT_STALE_REFERENCE);
    OK(nt_progress_mount(tree,root,enabled,(NtProgressOptions){.maximum=10},&progress));
    target[2].node = progress;
    OK(nt_tree_state_restore(tree,target,3,imported));
    expect_text(tree,target[0],BYTES("e\xcc\x81\0\xe4\xb8\xad"));
    CHECK(get_number(tree,target[2]) == 9);
    NtValue foreground;
    OK(nt_tree_property_get(tree,target[1],&foreground));
    CHECK(foreground.kind == NT_VALUE_RGBA && foreground.as.rgba.green == 200);
    OK(nt_tree_state_capture(tree,target,3,NULL,&snapshot));
    const NtByteView after = nt_state_snapshot_bytes(snapshot), saved = nt_state_snapshot_bytes(imported);
    CHECK(after.count == saved.count && !memcmp(after.data,saved.data,after.count));
    nt_state_snapshot_destroy(snapshot);
    nt_state_snapshot_destroy(imported);
    nt_tree_destroy(tree);
}

enum { MESSAGE = 71 };
typedef struct Recorder {
    NtTree *tree;
    NtNodeId root, publisher;
    NtScheduleId child_sub;
    char observed[8];
    size_t count, disposed;
} Recorder;

static NtResult publisher_measure(const void *state, NtMeasureContext *ctx, NtConstraints limits,
                                  NtCellSize *preferred)
{
    (void)state;
    (void)ctx;
    (void)limits;
    *preferred = (NtCellSize){1,1};
    return NT_RESULT_OK;
}

static NtResult publisher_layout(const void *state, NtLayoutContext *ctx, NtCellSize size)
{
    (void)state;
    (void)ctx;
    (void)size;
    return NT_RESULT_OK;
}

static NtResult publisher_paint(const void *state, NtPaintContext *ctx, NtCellSize size)
{
    (void)state;
    (void)ctx;
    (void)size;
    return NT_RESULT_OK;
}

static NtResult publisher_event(void *state, NtEventContext *context, const NtEvent *event, NtEventReply *reply)
{
    (void)state;
    (void)reply;
    if (event->kind != NT_EVENT_KEY) return NT_RESULT_OK;
    const NtIntentField field = {4,TEXT("q\0z",3)};
    return nt_event_emit_fields(context,MESSAGE,&field,1);
}

static void publisher_dispose(void *state)
{
    Recorder *record = state;
    CHECK(!nt_tree_schedule_next(record->tree).scheduled);
    ++record->disposed;
}

static void receive(void *context, NtNodeId owner, const NtEventIntentView *view)
{
    Recorder *record = context;
    if (owner.slot == record->publisher.slot) CHECK(!record->disposed);
    CHECK(view->intent.kind == MESSAGE && view->payload_kind == NT_INTENT_FIELDS && view->field_count == 1);
    CHECK(view->fields[0].value.as.text.count == 3 && !memcmp(view->fields[0].value.as.text.data,"q\0z",3));
    CHECK(record->count < sizeof(record->observed));
    record->observed[record->count++] = owner.slot == record->root.slot ? 'R' : 'C';
    CHECK(nt_tree_schedule_cancel(record->tree,record->child_sub) == NT_RESULT_WRONG_PHASE);
    size_t untouched = 123;
    CHECK(nt_tree_schedule_deliver(record->tree,1,&untouched) == NT_RESULT_WRONG_PHASE && untouched == 123);
    nt_tree_pop_intent(record->tree);
    NtEventIntentView peek;
    CHECK(nt_tree_peek_event_intent(record->tree,&peek) && peek.fields == view->fields);
}

static void send_key(NtTree *tree, NtTimeUs at)
{
    const NtEvent key = {.kind=NT_EVENT_KEY,.at=at,.data.key={NT_KEY_ENTER,0,NT_KEY_PRESS}};
    NtDispatchResult report;
    OK(nt_tree_dispatch(tree,&key,(NtByteView){0},NULL,&report));
}

static void scheduled_work(void)
{
    Recorder record = {0};
    NtNodeId progress;
    const NtMountOptions enabled = {.enabled=true,.focusable=true};
    const NtBoxLayout box = {.kind=NT_BOX_LAYOUT_COLUMN};
    const NtComponentOps ops = {.measure=publisher_measure,.layout=publisher_layout,.paint=publisher_paint,
                                .event=publisher_event,.dispose=publisher_dispose};
    OK(nt_tree_create(NULL,&record.tree));
    NtTree *tree = record.tree;
    OK(nt_box_layout_mount(tree,(NtNodeId){0},enabled,&box,&record.root));
    OK(nt_tree_mount(tree,record.root,&ops,&record,enabled,&record.publisher));
    OK(nt_progress_mount(tree,record.root,enabled,(NtProgressOptions){.maximum=12},&progress));
    NtNodeId target_label = label(tree,record.root,BYTES("init"));
    const NtPropertyRef counter = {progress,NT_PROPERTY_VALUE};
    const NtPropertyRef title = {target_label,NT_PROPERTY_TEXT};
    NtScheduleRun run = {.attempted=123};
    CHECK(nt_tree_schedule_run(tree,1,1,&run) == NT_RESULT_UNSUPPORTED_CAPABILITY && run.attempted == 123);
    OK(nt_tree_schedule_configure(tree,(NtScheduleLimits){4,2}));
    NtScheduleId first, second, bad, last, ignored = 777;
    char source[] = "saved";
    const NtPropertyAssignment early[] = {{title,TEXT(source,5)},{counter,NUMBER(4)}};
    OK(nt_tree_schedule_batch(tree,record.publisher,UINT64_MAX-2,early,2,&first));
    source[0] = '!';
    NtPropertyAssignment change = {counter,NUMBER(5)};
    OK(nt_tree_schedule_batch(tree,record.root,UINT64_MAX-2,&change,1,&second));
    change.value = NUMBER(99);
    OK(nt_tree_schedule_batch(tree,record.root,UINT64_MAX-1,&change,1,&bad));
    change.value = NUMBER(8);
    OK(nt_tree_schedule_batch(tree,record.root,UINT64_MAX,&change,1,&last));
    CHECK(first < second && second < bad && bad < last);
    CHECK(nt_tree_schedule_next(tree).at == UINT64_MAX-2);
    CHECK(nt_tree_schedule_batch(tree,record.root,1,&change,1,&ignored) == NT_RESULT_OUT_OF_RANGE && ignored == 777);
    CHECK(nt_tree_schedule_configure(tree,(NtScheduleLimits){3,2}) == NT_RESULT_OUT_OF_RANGE);
    OK(nt_tree_schedule_run(tree,UINT64_MAX-3,9,&run));
    CHECK(run.attempted == 0 && !run.failed && get_number(tree,counter) == 0);
    OK(nt_tree_schedule_run(tree,UINT64_MAX-2,1,&run));
    CHECK(run.attempted == 1 && !run.failed && get_number(tree,counter) == 4);
    expect_text(tree,title,BYTES("saved"));
    OK(nt_tree_schedule_run(tree,UINT64_MAX-2,1,&run));
    CHECK(run.attempted == 1 && get_number(tree,counter) == 5);
    CHECK(nt_tree_schedule_run(tree,UINT64_MAX,2,&run) == NT_RESULT_OUT_OF_RANGE);
    CHECK(run.attempted == 1 && run.failed == bad && get_number(tree,counter) == 5);
    CHECK(nt_tree_schedule_cancel(tree,bad) == NT_RESULT_STALE_REFERENCE);
    OK(nt_tree_schedule_run(tree,UINT64_MAX,1,&run));
    CHECK(run.attempted == 1 && !run.failed && get_number(tree,counter) == 8);
    CHECK(nt_tree_schedule_run(tree,0,1,&run) == NT_RESULT_OUT_OF_RANGE && run.attempted == 1);

    NtScheduleId root_sub, cancel_batch, cross;
    OK(nt_tree_schedule_subscribe(tree,record.publisher,MESSAGE,receive,&record,&record.child_sub));
    OK(nt_tree_schedule_subscribe(tree,record.root,MESSAGE,receive,&record,&root_sub));
    CHECK(nt_tree_schedule_subscribe(tree,record.root,MESSAGE,receive,&record,&ignored) == NT_RESULT_OUT_OF_RANGE);
    OK(nt_tree_schedule_batch(tree,record.publisher,UINT64_MAX,&change,1,&cancel_batch));
    OK(nt_tree_schedule_cancel(tree,cancel_batch));
    CHECK(nt_tree_schedule_cancel(tree,cancel_batch) == NT_RESULT_STALE_REFERENCE);
    const NtPropertyAssignment cross_update = {title,TEXT("else",4)};
    OK(nt_tree_schedule_batch(tree,record.root,UINT64_MAX,&cross_update,1,&cross));
    OK(nt_tree_remove(tree,target_label));
    CHECK(nt_tree_schedule_cancel(tree,cross) == NT_RESULT_STALE_REFERENCE);
    OK(nt_tree_schedule_batch(tree,record.publisher,UINT64_MAX,&change,1,&cancel_batch));
    OK(nt_tree_publish(tree,(NtCellSize){20,6}));
    OK(nt_tree_set_focus(tree,record.publisher));
    send_key(tree,0);
    send_key(tree,1);
    size_t delivered = 123;
    OK(nt_tree_schedule_deliver(tree,0,&delivered));
    CHECK(delivered == 0 && record.count == 0);
    OK(nt_tree_schedule_deliver(tree,1,&delivered));
    CHECK(delivered == 1 && record.count == 2 && !memcmp(record.observed,"CR",2));
    OK(nt_tree_remove(tree,record.publisher));
    CHECK(record.disposed == 1 && !nt_tree_schedule_next(tree).scheduled);
    CHECK(nt_tree_schedule_cancel(tree,cancel_batch) == NT_RESULT_STALE_REFERENCE);
    CHECK(nt_tree_schedule_cancel(tree,record.child_sub) == NT_RESULT_STALE_REFERENCE);
    OK(nt_tree_schedule_deliver(tree,5,&delivered));
    CHECK(delivered == 1 && record.count == 3 && !memcmp(record.observed,"CRR",3));
    OK(nt_tree_schedule_deliver(tree,5,&delivered));
    CHECK(delivered == 0 && record.count == 3);
    OK(nt_tree_schedule_cancel(tree,root_sub));
    nt_tree_destroy(tree);
}

int main(void)
{
    atomic_batch();
    snapshot_round_trip();
    scheduled_work();
    return EXIT_SUCCESS;
}
