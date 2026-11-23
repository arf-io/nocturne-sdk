#include <nocturne/animation.h>
#include <nocturne/component.h>
#include <nocturne/layout.h>
#include <nocturne/paint.h>
#include <nocturne/widgets.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "binding replay line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)
#define OK(call) CHECK((call) == NT_RESULT_OK)

static const NtCellStyle ink = {{230, 240, 255, 255}, {16, 24, 32, 255}};
static const NtMountOptions passive = {.enabled = true};
static const NtMountOptions interactive = {.enabled = true, .focusable = true, .hit_testable = true};
static const char payload[] = "x\0é";

typedef struct Meter {
    unsigned level, keys, updates, disposals;
    NtTimeUs elapsed;
    bool fail_paint;
} Meter;

static NtResult measure(const void *state, NtMeasureContext *context, NtConstraints limits, NtCellSize *out)
{
    (void)state;
    (void)context;
    (void)limits;
    *out = (NtCellSize){5, 1};
    return NT_RESULT_OK;
}

static NtResult layout(const void *state, NtLayoutContext *context, NtCellSize size)
{
    (void)state;
    (void)context;
    (void)size;
    return NT_RESULT_OK;
}

static NtResult update(void *state, NtUpdateInfo info, NtUpdateReply *reply)
{
    Meter *meter = state;
    meter->elapsed = info.elapsed;
    ++meter->updates;
    *reply = (NtUpdateReply){NT_INVALIDATE_PAINT, {meter->updates == 1, info.now + 250}};
    return NT_RESULT_OK;
}

static NtResult paint(const void *state, NtPaintContext *context, NtCellSize size)
{
    (void)size;
    const Meter *meter = state;
    if (meter->fail_paint) return NT_RESULT_CALLBACK_FAILED;
    const char level = (char)('0' + meter->level);
    NtResult result = nt_paint_glyph(context, (NtCellPoint){0, 0}, (NtByteView){&level, 1}, 1, ink);
    if (result != NT_RESULT_OK) return result;
    return nt_paint_glyph(context, (NtCellPoint){1, 0}, (NtByteView){"界", sizeof "界" - 1}, 2, ink);
}

static NtResult on_event(void *state, NtEventContext *context, const NtEvent *input, NtEventReply *reply)
{
    if (input->kind != NT_EVENT_KEY || input->data.key.key != '+') return NT_RESULT_OK;
    Meter *meter = state;
    ++meter->keys;
    ++meter->level;
    reply->consume = true;
    reply->invalidate = NT_INVALIDATE_PAINT;
    return nt_event_emit(context, 41, (NtByteView){payload, sizeof payload - 1});
}

static void dispose(void *state)
{
    ++((Meter *)state)->disposals;
}

static bool equal_node(NtNodeId a, NtNodeId b)
{
    return a.tree == b.tree && a.slot == b.slot && a.generation == b.generation;
}

static int record_frame(const NtTree *tree, const char *stage)
{
    const NtFrame *frame = nt_tree_frame(tree);
    const NtCellSize size = nt_frame_size(frame);
    NtCellView label, meter, level;
    OK(nt_frame_cell(frame, (NtCellPoint){2, 0}, &label));
    OK(nt_frame_cell(frame, (NtCellPoint){2, 1}, &meter));
    OK(nt_frame_cell(frame, (NtCellPoint){0, 1}, &level));
    CHECK(label.glyph.count == sizeof "👩‍💻" - 1 && meter.glyph.count == sizeof "界" - 1);
    CHECK(level.glyph.count == 1);
    printf("%s %" PRIu32 " %" PRIu32 " %" PRId32 " %" PRId32 " %u %.*s"
           " %" PRId32 " %" PRId32 " %u %.*s %c\n",
           stage, size.columns, size.rows, label.lead.x, label.lead.y,
           (unsigned)label.glyph_columns, (int)label.glyph.count, label.glyph.data,
           meter.lead.x, meter.lead.y, (unsigned)meter.glyph_columns, (int)meter.glyph.count,
           meter.glyph.data, level.glyph.data[0]);
    return 0;
}

static int replay(NtTree *tree, Meter *meter)
{
    const NtBoxLayout column = {.kind = NT_BOX_LAYOUT_COLUMN};
    const NtLabelOptions title = {.text = {"A👩‍💻", sizeof "A👩‍💻" - 1}, .style = {
        .width = {NT_WIDTH_ONE, NT_WIDTH_TWO}, .wrap = NT_WRAP_NONE, .align = NT_TEXT_ALIGN_START,
        .direction = NT_DIRECTION_AUTO, .tab_stop = 4, .color = ink
    }};
    const NtComponentOps ops = {.measure = measure, .layout = layout, .update = update, .paint = paint,
                                .event = on_event, .dispose = dispose};
    NtNodeId parent, label, node;
    OK(nt_box_layout_mount(tree, (NtNodeId){0}, passive, &column, &parent));
    OK(nt_label_mount(tree, parent, passive, &title, &label));
    OK(nt_tree_mount(tree, parent, &ops, meter, interactive, &node));
    OK(nt_tree_tick(tree, 1000, NT_MOTION_FULL));
    OK(nt_tree_publish(tree, (NtCellSize){7, 3}));
    CHECK(record_frame(tree, "initial") == 0);
    const NtDeadline deadline = nt_tree_next_update(tree);
    CHECK(deadline.scheduled);
    OK(nt_tree_set_focus(tree, node));

    const NtKeyChord chord = {'+', 0};
    const NtDispatchPolicy policy = {.reserved = &chord, .reserved_count = 1};
    const NtEvent key = {.kind = NT_EVENT_KEY, .at = 1000, .data.key = {'+', 0, NT_KEY_PRESS}};
    NtDispatchResult reserved, delivered;
    OK(nt_tree_dispatch(tree, &key, (NtByteView){"!", 1}, &policy, &reserved));
    CHECK(meter->keys == 0 && !nt_tree_peek_intent(tree, &(NtIntentView){0}));
    OK(nt_tree_dispatch(tree, &key, (NtByteView){0}, NULL, &delivered));
    NtIntentView intent;
    CHECK(nt_tree_peek_intent(tree, &intent) && equal_node(intent.source, node));
    CHECK(intent.payload.count == sizeof payload - 1 && !memcmp(intent.payload.data, payload, sizeof payload - 1));
    printf("event %d %d %d %d %d %d %u %" PRIu32 " %zu\n",
           reserved.reserved, reserved.consumed, reserved.text_delivered,
           equal_node(delivered.target, node), delivered.consumed, delivered.text_delivered,
           meter->keys, intent.kind, intent.payload.count);
    nt_tree_pop_intent(tree);
    CHECK(!nt_tree_peek_intent(tree, &intent));

    OK(nt_tree_tick(tree, 1250, NT_MOTION_FULL));
    printf("clock %" PRIu64 " %u %" PRIu64 "\n", deadline.at, meter->updates, meter->elapsed);
    meter->fail_paint = true;
    const NtResult failure = nt_tree_publish(tree, (NtCellSize){5, 2});
    CHECK(failure == NT_RESULT_CALLBACK_FAILED);
    printf("failure %d\n", (int)failure);
    CHECK(record_frame(tree, "held") == 0);
    meter->fail_paint = false;
    OK(nt_tree_publish(tree, (NtCellSize){5, 2}));
    CHECK(record_frame(tree, "updated") == 0);
    OK(nt_tree_remove(tree, node));
    for (unsigned i = 0; i < 512; ++i) {
        NtNodeId next;
        OK(nt_tree_mount(tree, parent, &ops, meter, interactive, &next));
        CHECK(!equal_node(node, next));
        OK(nt_tree_remove(tree, next));
    }
    OK(nt_tree_mount(tree, parent, &ops, meter, interactive, &node));
    return 0;
}

static int sample_tweens(void)
{
    const NtTween tween = {-3.25, 8.5, 1000, 100, NT_SINE, NT_PING_PONG};
    const uint64_t times[] = {0, 100, 433, 1100, 1433, 2100};
    NtAnimationSample sample;
    for (size_t i = 0; i < sizeof times / sizeof *times; ++i) {
        CHECK(nt_tween_sample(&tween, times[i], &sample) == NT_OK);
        printf("sample %" PRIu64 " %.17g %.17g %" PRIu64 " %d %d\n",
               times[i], sample.value, sample.progress, sample.iteration, (int)sample.phase, sample.direction);
    }
    const NtTween loop = {0, 1, 1, 0, NT_LINEAR, NT_LOOP};
    CHECK(nt_tween_sample(&loop, UINT64_MAX, &sample) == NT_OK);
    printf("iteration %" PRIu64 "\n", sample.iteration);
    const NtTween invalid = {0, 1, 0, 0, NT_LINEAR, NT_LOOP};
    const NtStatus status = nt_tween_sample(&invalid, 0, &sample);
    printf("invalid %d\n", (int)status);
    return 0;
}

int main(void)
{
    NtTree *tree;
    OK(nt_tree_create(NULL, &tree));
    Meter meter = {.level = 4};
    const int error = replay(tree, &meter);
    nt_tree_destroy(tree);
    if (error) return error;
    printf("dispose %u\n", meter.disposals);
    return sample_tweens();
}
