#include <nocturne/abi.h>
#include <nocturne/animation.h>
#include <nocturne/layout.h>
#include <nocturne/text.h>

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "ABI conformance line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)
#define OK(call) CHECK((call) == NT_RESULT_OK)

static const char cluster[] = "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb";
static const NtCellStyle ink = {{230,240,255,255},{16,24,32,255}};
static const NtMountOptions interactive = {true,true,true};

typedef struct Fixture {
    NtTree *tree;
    NtNodeId node;
    NtTimeUs elapsed;
    unsigned updates, disposals, failed_notifications;
    NtResult reentrant;
    bool fail_paint, fail_event;
} Fixture;

static NtResult measure(const void *state, NtMeasureContext *context,
                        NtConstraints limits, NtCellSize *out)
{
    (void)state;
    (void)context;
    (void)limits;
    *out = (NtCellSize){2,1};
    return NT_RESULT_OK;
}

static NtResult layout(const void *state, NtLayoutContext *context, NtCellSize size)
{
    (void)state;
    (void)context;
    return size.columns == 2 && size.rows == 1 ? NT_RESULT_OK : NT_RESULT_OUT_OF_RANGE;
}

static NtResult update(void *state, NtUpdateInfo info, NtUpdateReply *reply)
{
    Fixture *fixture = state;
    fixture->elapsed = info.elapsed;
    fixture->reentrant = nt_tree_invalidate(fixture->tree, fixture->node, NT_INVALIDATE_PAINT);
    ++fixture->updates;
    *reply = (NtUpdateReply){NT_INVALIDATE_PAINT,{fixture->updates == 1,info.now+250}};
    return NT_RESULT_OK;
}

static NtResult paint(const void *state, NtPaintContext *context, NtCellSize size)
{
    (void)size;
    const Fixture *fixture = state;
    if (fixture->fail_paint) return NT_RESULT_CALLBACK_FAILED;
    return fixture->updates > 1 ?
        nt_paint_glyph(context,(NtCellPoint){0,0},(NtByteView){"!",1},1,ink) :
        nt_paint_glyph(context,(NtCellPoint){0,0},(NtByteView){cluster,sizeof cluster-1},2,ink);
}

static NtResult event(void *state, NtEventContext *context, const NtEvent *input,
                       NtEventReply *reply)
{
    Fixture *fixture = state;
    if (nt_event_phase(context) != NT_EVENT_TARGET) return NT_RESULT_OK;
    if (input->kind == NT_EVENT_FOCUS_GAIN) {
        const NtResult result = nt_event_emit(context,42,(NtByteView){"focus",5});
        if (result != NT_RESULT_OK) return result;
        ++fixture->failed_notifications;
        return NT_RESULT_CALLBACK_FAILED;
    }
    if (input->kind != NT_EVENT_PASTE)
        return NT_RESULT_OK;
    const NtResult result = nt_event_emit(context,41,input->data.text);
    if (result != NT_RESULT_OK) return result;
    reply->consume = true;
    return fixture->fail_event ? NT_RESULT_CALLBACK_FAILED : NT_RESULT_OK;
}

static void dispose(void *state)
{
    Fixture *fixture = state;
    ++fixture->disposals;
    fixture->reentrant = nt_tree_remove(fixture->tree,fixture->node);
}

static bool same_node(NtNodeId left, NtNodeId right)
{
    return left.tree == right.tree && left.slot == right.slot && left.generation == right.generation;
}

int nocturne_abi_conformance(void);

int nocturne_abi_conformance(void)
{
    const char *expected_names[] = {
        "NT_RESULT_OK", "NT_RESULT_INVALID_ARGUMENT", "NT_RESULT_OUT_OF_RANGE",
        "NT_RESULT_ALLOCATION_FAILED", "NT_RESULT_INSUFFICIENT_BUFFER",
        "NT_RESULT_STALE_REFERENCE", "NT_RESULT_WRONG_PHASE", "NT_RESULT_UNSUPPORTED_CAPABILITY",
        "NT_RESULT_TRANSPORT_FAILED", "NT_RESULT_CALLBACK_FAILED"
    };
    CHECK(nt_abi_version() == NT_ABI_VERSION && NT_ABI_VERSION == 1);
    for (int32_t i = 0; i < (int32_t)(sizeof expected_names/sizeof *expected_names); ++i) {
        const char *name = nt_result_name(i);
        CHECK(name && strcmp(name,expected_names[i]) == 0);
    }
    CHECK(nt_result_name(-1) == NULL && nt_result_name(INT32_MAX) == NULL);
    const NtTween tween = {0,1,1000,0,NT_LINEAR,NT_ONCE};
    NtAnimationSample animation;
    CHECK(nt_tween_sample(&tween,375,&animation) == NT_OK && animation.value == .375);

    char payload[] = "x\0\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb";
    const NtByteView bytes = {payload,sizeof payload-1};
    NtGraphemeIterator iterator;
    NtByteSpan span;
    bool available;
    OK(nt_grapheme_iterator_init(&iterator,bytes));
    OK(nt_grapheme_iterator_next(&iterator,&span,&available));
    CHECK(available && span.begin == 0 && span.end == 1);
    OK(nt_grapheme_iterator_next(&iterator,&span,&available));
    CHECK(available && span.begin == 1 && span.end == 2);
    OK(nt_grapheme_iterator_next(&iterator,&span,&available));
    CHECK(available && span.begin == 2 && span.end == bytes.count);
    const size_t cluster_end = span.end;
    OK(nt_grapheme_iterator_next(&iterator,&span,&available));
    CHECK(!available);

    Fixture fixture = {0};
    OK(nt_tree_create(NULL,&fixture.tree));
    NtNodeId root;
    const NtBoxLayout stack = {.kind=NT_BOX_LAYOUT_STACK,.padding={1,0,0,0}};
    OK(nt_box_layout_mount(fixture.tree,(NtNodeId){0},(NtMountOptions){true,false,false},&stack,&root));
    const NtComponentOps callbacks = {measure,layout,update,paint,event,dispose};
    NtComponentOps borrowed = callbacks;
    OK(nt_tree_mount(fixture.tree,root,&borrowed,&fixture,interactive,&fixture.node));
    borrowed = (NtComponentOps){0};
    OK(nt_tree_tick(fixture.tree,1000,NT_MOTION_FULL));
    CHECK(fixture.updates == 1 && fixture.elapsed == 0 && fixture.reentrant == NT_RESULT_WRONG_PHASE);
    const NtDeadline deadline = nt_tree_next_update(fixture.tree);
    CHECK(deadline.scheduled && deadline.at == 1250);
    OK(nt_tree_publish(fixture.tree,(NtCellSize){4,1}));
    const NtFrame *first = nt_tree_frame(fixture.tree);
    NtNodeGeometry geometry;
    OK(nt_tree_geometry(fixture.tree,fixture.node,&geometry));
    CHECK(geometry.placed && geometry.bounds.origin.x == 1 && geometry.bounds.origin.y == 0);
    CHECK(geometry.bounds.size.columns == 2 && geometry.bounds.size.rows == 1);
    const int32_t lead_x = geometry.bounds.origin.x;
    NtCellView cell;
    OK(nt_frame_cell(first,(NtCellPoint){2,0},&cell));
    const size_t first_columns = cell.glyph_columns;
    CHECK(first_columns == 2 && cell.lead.x == 1 && cell.owner == nt_node_owner(fixture.node));
    CHECK(cell.glyph.count == sizeof cluster-1 && !memcmp(cell.glyph.data,cluster,sizeof cluster-1));

    OK(nt_tree_set_focus(fixture.tree,fixture.node));
    CHECK(same_node(nt_tree_focus(fixture.tree),fixture.node) && fixture.failed_notifications == 1);
    NtIntentView intent;
    CHECK(!nt_tree_peek_intent(fixture.tree,&intent));
    const NtEvent paste = {.kind=NT_EVENT_PASTE,.at=1000,.data.text=bytes};
    NtDispatchResult dispatched;
    fixture.fail_event = true;
    CHECK(nt_tree_dispatch(fixture.tree,&paste,(NtByteView){0},NULL,&dispatched) == NT_RESULT_CALLBACK_FAILED);
    CHECK(!nt_tree_peek_intent(fixture.tree,&intent));
    fixture.fail_event = false;
    OK(nt_tree_dispatch(fixture.tree,&paste,(NtByteView){0},NULL,&dispatched));
    CHECK(dispatched.consumed && same_node(dispatched.target,fixture.node));
    memset(payload,'?',sizeof payload-1);
    CHECK(nt_tree_peek_intent(fixture.tree,&intent) && intent.kind == 41);
    CHECK(same_node(intent.source,fixture.node) && intent.payload.count == bytes.count);
    CHECK(intent.payload.data[0] == 'x' && intent.payload.data[1] == 0 &&
          !memcmp(intent.payload.data+2,cluster,sizeof cluster-1));
    const uint32_t intent_kind = intent.kind;
    const size_t intent_size = intent.payload.count;
    nt_tree_pop_intent(fixture.tree);

    OK(nt_tree_tick(fixture.tree,1250,NT_MOTION_FULL));
    CHECK(fixture.updates == 2 && fixture.elapsed == 250);
    fixture.fail_paint = true;
    CHECK(nt_tree_publish(fixture.tree,(NtCellSize){4,1}) == NT_RESULT_CALLBACK_FAILED);
    CHECK(nt_tree_frame(fixture.tree) == first && !memcmp(cell.glyph.data,cluster,sizeof cluster-1));
    fixture.fail_paint = false;
    OK(nt_tree_publish(fixture.tree,(NtCellSize){4,1}));
    OK(nt_frame_cell(nt_tree_frame(fixture.tree),(NtCellPoint){1,0},&cell));
    CHECK(cell.glyph.count == 1 && cell.glyph.data[0] == '!');
    const unsigned last_scalar = (unsigned char)cell.glyph.data[0];

    const NtNodeId retired = fixture.node;
    OK(nt_tree_remove(fixture.tree,retired));
    CHECK(fixture.disposals == 1 && fixture.reentrant == NT_RESULT_WRONG_PHASE);
    CHECK(nt_tree_geometry(fixture.tree,retired,&geometry) == NT_RESULT_STALE_REFERENCE);
    NtNodeId untouched = root;
    CHECK(nt_tree_mount(fixture.tree,retired,&callbacks,&fixture,interactive,&untouched) == NT_RESULT_STALE_REFERENCE);
    CHECK(same_node(untouched,root) && fixture.disposals == 1);
    for (unsigned i = 0; i < 512; ++i) {
        OK(nt_tree_mount(fixture.tree,root,&callbacks,&fixture,interactive,&fixture.node));
        CHECK(!same_node(retired,fixture.node));
        OK(nt_tree_remove(fixture.tree,fixture.node));
    }
    OK(nt_tree_mount(fixture.tree,root,&callbacks,&fixture,interactive,&fixture.node));
    nt_tree_destroy(fixture.tree);
    CHECK(fixture.disposals == 514 && fixture.reentrant == NT_RESULT_WRONG_PHASE);

    printf("{\"fixture\":1,\"abi\":%" PRIu32 ",\"cluster_end\":%zu,\"lead_x\":%" PRId32
           ",\"first_columns\":%zu,\"intent_kind\":%" PRIu32 ",\"intent_size\":%zu,"
           "\"elapsed_us\":%" PRIu64 ",\"last_scalar\":%u,\"disposals\":%u,"
           "\"failed_notifications\":%u,\"tween_value\":%.3f}\n",
           nt_abi_version(),cluster_end,lead_x,first_columns,intent_kind,intent_size,
           fixture.elapsed,last_scalar,fixture.disposals,fixture.failed_notifications,animation.value);
    return 0;
}

#ifdef NOCTURNE_ABI_TEST_MAIN
int main(void)
{
    return nocturne_abi_conformance();
}
#endif
