#include <nocturne/binding.h>
#include <nocturne/controls.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "binding line %d: %s\n", __LINE__, #expr); exit(EXIT_FAILURE); \
} } while (0)
#define OK(expr) CHECK((expr) == NT_RESULT_OK)
#define BYTES(s) ((NtByteView){(s), sizeof(s) - 1})
#define REF(n, p) ((NtPropertyRef){(n), (p)})
#define NUMBER(n) ((NtValue){.kind = NT_VALUE_U64, .as.u64 = (n)})
#define TEXT(t) ((NtValue){.kind = NT_VALUE_TEXT, .as.text = (t)})

typedef struct AllocationLog { size_t attempt, refuse, live; } AllocationLog;

static void *allocate(void *context, size_t bytes, size_t alignment)
{
    AllocationLog *log = context;
    CHECK(bytes && alignment);
    if (++log->attempt == log->refuse) return NULL;
    void *address = malloc(bytes);
    CHECK(address && (uintptr_t)address % alignment == 0);
    memset(address, 0xa5, bytes);
    ++log->live;
    return address;
}

static void release(void *context, void *address, size_t bytes, size_t alignment)
{
    AllocationLog *log = context;
    CHECK(address && bytes && alignment && log->live);
    --log->live;
    free(address);
}

typedef struct State {
    NtTree *tree;
    NtNodeId id;
    NtBindingGraph *graph;
    double amount;
    unsigned prepares, commits, discards, computes, disposals;
} State;

static NtResult measure(const void *state, NtMeasureContext *ctx, NtConstraints limits, NtCellSize *out)
{
    (void)state;
    (void)ctx;
    (void)limits;
    *out = (NtCellSize){1, 1};
    return NT_RESULT_OK;
}

static NtResult layout(const void *state, NtLayoutContext *ctx, NtCellSize size)
{
    (void)state;
    (void)ctx;
    (void)size;
    return NT_RESULT_OK;
}

static NtResult paint(const void *state, NtPaintContext *ctx, NtCellSize size)
{
    (void)state;
    (void)ctx;
    (void)size;
    return NT_RESULT_OK;
}

static void dispose(void *value)
{
    State *state = value;
    CHECK(nt_tree_invalidate(state->tree, state->id, NT_INVALIDATE_PAINT) == NT_RESULT_WRONG_PHASE);
    ++state->disposals;
}

static NtResult read_amount(const void *value, NtValue *out)
{
    const State *state = value;
    *out = (NtValue){.kind = NT_VALUE_REAL, .as.real = state->amount};
    return NT_RESULT_OK;
}

static NtResult stage_amount(const void *value, NtValue number, void **out)
{
    State *state = (State *)value;
    CHECK(nt_tree_remove(state->tree, state->id) == NT_RESULT_WRONG_PHASE);
    CHECK(nt_binding_graph_destroy(state->graph) == NT_RESULT_WRONG_PHASE);
    if (number.as.real < 0 || number.as.real > 100) return NT_RESULT_OUT_OF_RANGE;
    double *copy = malloc(sizeof(*copy));
    if (!copy) return NT_RESULT_ALLOCATION_FAILED;
    *copy = number.as.real;
    *out = copy;
    ++state->prepares;
    return NT_RESULT_OK;
}

static void finish_amount(void *value, NtValue number, void *prepared, bool commit)
{
    State *state = value;
    double *copy = prepared;
    CHECK(*copy == number.as.real);
    if (commit) {
        state->amount = *copy;
        ++state->commits;
    } else ++state->discards;
    free(copy);
}

static NtResult add(void *context, const NtValue *values, size_t count, NtValue *out)
{
    State *state = context;
    CHECK(count == 2 && values[0].kind == NT_VALUE_U64 && values[1].kind == NT_VALUE_U64);
    CHECK(nt_tree_invalidate(state->tree, state->id, NT_INVALIDATE_PAINT) == NT_RESULT_WRONG_PHASE);
    ++state->computes;
    if (values[1].as.u64 > UINT64_MAX - values[0].as.u64) return NT_RESULT_OUT_OF_RANGE;
    *out = NUMBER(values[0].as.u64 + values[1].as.u64);
    return NT_RESULT_OK;
}

static NtResult to_real(void *context, const NtValue *values, size_t count, NtValue *out)
{
    (void)context;
    CHECK(count == 1 && values[0].kind == NT_VALUE_U64);
    *out = (NtValue){.kind = NT_VALUE_REAL, .as.real = (double)values[0].as.u64 / 2};
    return NT_RESULT_OK;
}

static void expect_text(NtTree *tree, NtNodeId label, NtByteView expected)
{
    NtValue value;
    OK(nt_tree_property_get(tree, REF(label, NT_PROPERTY_TEXT), &value));
    CHECK(value.kind == NT_VALUE_TEXT && value.as.text.count == expected.count);
    CHECK(!expected.count || !memcmp(value.as.text.data, expected.data, expected.count));
}

static void expect_unsigned(NtTree *tree, NtPropertyRef property, uint64_t expected)
{
    NtValue value = TEXT(BYTES("sentinel"));
    OK(nt_tree_property_get(tree, property, &value));
    CHECK(value.kind == NT_VALUE_U64 && value.as.u64 == expected);
}

static void bindings(void)
{
    AllocationLog log = {0};
    const NtAllocator allocator = {&log, allocate, release};
    const NtMountOptions enabled = {.enabled = true};
    const NtComponentOps ops = {.measure = measure, .layout = layout, .paint = paint, .dispose = dispose};
    NtTree *tree;
    OK(nt_tree_create(&allocator, &tree));
    NtNodeId root, label, progress, scroll, item, check, choice;
    OK(nt_box_layout_mount(tree, (NtNodeId){0}, enabled,
                           &(NtBoxLayout){.kind = NT_BOX_LAYOUT_COLUMN}, &root));
    NtLabelOptions label_options = {.text = BYTES("old"), .style = {
        .width = {NT_WIDTH_ONE, NT_WIDTH_TWO}, .tab_stop = 4, .color.foreground = {255,255,255,255}
    }};
    OK(nt_label_mount(tree, root, enabled, &label_options, &label));
    OK(nt_progress_mount(tree, root, enabled, (NtProgressOptions){.value = 2, .maximum = 100}, &progress));
    OK(nt_scrollbar_mount(tree, root, enabled, (NtScrollBarOptions){.content = 13, .viewport = 2}, &scroll));
    OK(nt_item_mount(tree, root, enabled, BYTES("item"), NULL, false, &item));
    OK(nt_checkbox_mount(tree, root, enabled, BYTES("check"), NULL, false, &check));
    const NtChoiceEntry entries[] = {{BYTES("first"), false}, {BYTES("second"), true}};
    const NtChoiceOptions choices = {.entries = entries, .count = 2, .selected = NT_CHOICE_NONE};
    OK(nt_choice_mount(tree, root, enabled, &choices, &choice));
    State model = {.tree = tree, .amount = 17};
    OK(nt_tree_mount(tree, root, &ops, &model, enabled, &model.id));

    enum { CUSTOM_AMOUNT = 400, READ_ONLY = 401 };
    NtPropertyDescriptor properties[] = {
        {CUSTOM_AMOUNT, NT_VALUE_REAL, NT_INVALIDATE_PAINT, read_amount, stage_amount, finish_amount},
        {READ_ONLY, NT_VALUE_REAL, NT_INVALIDATE_NONE, read_amount, NULL, NULL}
    };
    size_t count = SIZE_MAX;
    OK(nt_tree_property_count(tree, model.id, &count));
    CHECK(count == 0);
    log.refuse = log.attempt + 1;
    CHECK(nt_tree_define_properties(tree, model.id, properties, 2) == NT_RESULT_ALLOCATION_FAILED);
    OK(nt_tree_property_count(tree, model.id, &count));
    CHECK(count == 0);
    OK(nt_tree_define_properties(tree, model.id, properties, 2));
    properties[0].read = NULL;
    NtPropertyInfo info;
    OK(nt_tree_property_at(tree, model.id, 0, &info));
    CHECK(info.id == CUSTOM_AMOUNT && info.kind == NT_VALUE_REAL && info.writable);
    OK(nt_tree_property_at(tree, model.id, 1, &info));
    CHECK(info.id == READ_ONLY && !info.writable);
    CHECK(nt_tree_property_set(tree, REF(model.id, READ_ONLY), (NtValue){.kind=NT_VALUE_REAL}) ==
          NT_RESULT_UNSUPPORTED_CAPABILITY);
    NtValue rejected = NUMBER(19);
    CHECK(nt_tree_property_get(tree, REF(root, NT_PROPERTY_TEXT), &rejected) == NT_RESULT_UNSUPPORTED_CAPABILITY);
    CHECK(rejected.kind == NT_VALUE_U64 && rejected.as.u64 == 19);

    OK(nt_tree_property_set(tree, REF(item, NT_PROPERTY_SELECTED), (NtValue){.kind=NT_VALUE_BOOL,.as.flag=true}));
    OK(nt_tree_property_set(tree, REF(check, NT_PROPERTY_CHECKED), (NtValue){.kind=NT_VALUE_BOOL,.as.flag=true}));
    bool flag = false;
    OK(nt_item_get(tree, item, &flag));
    CHECK(flag);
    OK(nt_checkbox_get(tree, check, &flag));
    CHECK(flag);
    expect_unsigned(tree, REF(choice, NT_PROPERTY_SELECTED), UINT64_MAX);
    CHECK(nt_tree_property_set(tree, REF(choice, NT_PROPERTY_SELECTED), NUMBER(1)) == NT_RESULT_INVALID_ARGUMENT);
    expect_unsigned(tree, REF(choice, NT_PROPERTY_SELECTED), UINT64_MAX);
    OK(nt_tree_property_set(tree, REF(choice, NT_PROPERTY_SELECTED), NUMBER(0)));
    OK(nt_choice_set(tree, choice, NT_CHOICE_NONE));
    expect_unsigned(tree, REF(choice, NT_PROPERTY_SELECTED), UINT64_MAX);

    size_t summands[] = {2, 3}, dependent[] = {0};
    NtBindingValue values[] = {
        {.kind=NT_VALUE_U64, .source=NT_BINDING_COMPUTED, .from.computed={summands, 2, add, &model}},
        {.kind=NT_VALUE_TEXT, .source=NT_BINDING_INPUT},
        {.kind=NT_VALUE_U64, .source=NT_BINDING_PROPERTY, .from.property=REF(progress, NT_PROPERTY_VALUE)},
        {.kind=NT_VALUE_U64, .source=NT_BINDING_INPUT},
        {.kind=NT_VALUE_REAL, .source=NT_BINDING_COMPUTED, .from.computed={dependent, 1, to_real, NULL}},
    };
    NtBindingTarget targets[] = {
        {REF(label, NT_PROPERTY_TEXT), 1}, {REF(model.id, CUSTOM_AMOUNT), 4},
        {REF(progress, NT_PROPERTY_VALUE), 0}, {REF(scroll, NT_PROPERTY_OFFSET), 0}
    };
    NtBindingGraph *sentinel = NULL;
    summands[0] = 0;
    CHECK(nt_binding_graph_create(tree, values, 5, targets, 4, &sentinel) == NT_RESULT_INVALID_ARGUMENT && !sentinel);
    summands[0] = 2;
    const NtBindingTarget duplicates[] = {targets[0], targets[0]};
    CHECK(nt_binding_graph_create(tree, values, 5, duplicates, 2, &sentinel) == NT_RESULT_INVALID_ARGUMENT);
    const NtBindingTarget mismatched = {REF(progress, NT_PROPERTY_VALUE), 1};
    CHECK(nt_binding_graph_create(tree, values, 5, &mismatched, 1, &sentinel) == NT_RESULT_INVALID_ARGUMENT);
    size_t create_failures = 0;
    for (size_t offset = 1; offset < 20; ++offset) {
        const size_t before = log.live;
        log.refuse = log.attempt + offset;
        const NtResult result = nt_binding_graph_create(tree, values, 5, targets, 4, &model.graph);
        if (result == NT_RESULT_OK) break;
        CHECK(result == NT_RESULT_ALLOCATION_FAILED && log.live == before && !model.graph);
        ++create_failures;
    }
    log.refuse = 0;
    CHECK(model.graph && create_failures >= 5);
    summands[0] = SIZE_MAX;
    dependent[0] = SIZE_MAX;
    values[2].kind = NT_VALUE_BOOL;

    label_options.text = BYTES("live");
    OK(nt_label_set(tree, label, &label_options));
    OK(nt_tree_publish(tree, (NtCellSize){20, 10}));
    const NtFrame *published = nt_tree_frame(tree);
    NtValue inputs[] = {TEXT(BYTES("next")), NUMBER(20)};
    CHECK(nt_binding_graph_apply(model.graph, inputs, 2) == NT_RESULT_OUT_OF_RANGE);
    CHECK(model.prepares == 1 && model.discards == 1 && model.commits == 0 && model.amount == 17);
    CHECK(nt_tree_frame(tree) == published && nt_tree_pending(tree) == NT_INVALIDATE_NONE);
    expect_text(tree, label, BYTES("live"));
    expect_unsigned(tree, REF(progress, NT_PROPERTY_VALUE), 2);
    inputs[0] = TEXT(BYTES("\xc0"));
    const unsigned before_computes = model.computes;
    CHECK(nt_binding_graph_apply(model.graph, inputs, 2) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(model.computes == before_computes);

    char incoming[] = "copy";
    inputs[0] = TEXT(BYTES(incoming));
    inputs[1] = NUMBER(4);
    size_t apply_failures = 0;
    for (size_t offset = 1; offset < 20; ++offset) {
        const size_t before = log.live;
        log.refuse = log.attempt + offset;
        const NtResult result = nt_binding_graph_apply(model.graph, inputs, 2);
        if (result == NT_RESULT_OK) break;
        CHECK(result == NT_RESULT_ALLOCATION_FAILED && log.live == before);
        CHECK(model.amount == 17 && model.commits == 0);
        CHECK(nt_tree_frame(tree) == published && nt_tree_pending(tree) == NT_INVALIDATE_NONE);
        expect_text(tree, label, BYTES("live"));
        ++apply_failures;
    }
    log.refuse = 0;
    CHECK(model.amount == 3 && model.commits == 1 && apply_failures >= 2);
    CHECK(nt_tree_frame(tree) == published && nt_tree_pending(tree) == NT_INVALIDATE_LAYOUT);
    incoming[0] = 'X';
    expect_text(tree, label, BYTES("copy"));
    expect_unsigned(tree, REF(progress, NT_PROPERTY_VALUE), 6);
    expect_unsigned(tree, REF(scroll, NT_PROPERTY_OFFSET), 6);
    NtProgressOptions options;
    OK(nt_progress_get(tree, progress, &options));
    CHECK(options.value == 6);
    OK(nt_tree_publish(tree, (NtCellSize){20, 10}));
    NtCellView glyph;
    OK(nt_frame_cell(nt_tree_frame(tree), (NtCellPoint){0, 0}, &glyph));
    CHECK(glyph.glyph.count == 1 && glyph.glyph.data[0] == 'c');
    const NtValue color = {.kind=NT_VALUE_RGBA,.as.rgba={40, 120, 240, 255}};
    const NtPropertyRef foreground = REF(label, NT_PROPERTY_FOREGROUND);
    OK(nt_tree_property_set(tree, foreground, color));
    CHECK(nt_tree_pending(tree) == NT_INVALIDATE_PAINT);
    OK(nt_tree_publish(tree, (NtCellSize){20, 10}));
    OK(nt_frame_cell(nt_tree_frame(tree), (NtCellPoint){0, 0}, &glyph));
    CHECK(glyph.style.foreground.red == 40 && glyph.style.foreground.blue == 240);
    const unsigned before_remove = model.computes;
    const NtNodeId retired = model.id;
    OK(nt_tree_remove(tree, retired));
    OK(nt_tree_mount(tree, root, &ops, &model, enabled, &model.id));
    CHECK(model.id.slot == retired.slot && model.id.generation != retired.generation);
    CHECK(nt_binding_graph_apply(model.graph, inputs, 2) == NT_RESULT_STALE_REFERENCE);
    CHECK(model.computes == before_remove && model.disposals == 1);
    nt_tree_destroy(tree);
    CHECK(model.disposals == 2);
    CHECK(nt_binding_graph_apply(model.graph, inputs, 2) == NT_RESULT_STALE_REFERENCE);
    OK(nt_binding_graph_destroy(model.graph));
    CHECK(log.live == 0);
    printf("binding transaction, stale lifetimes, and %zu create / %zu apply refusal points passed\n",
           create_failures, apply_failures);
}

static NtResult one_more(void *context, const NtValue *args, size_t count, NtValue *out)
{
    (void)context;
    CHECK(count == 1 && args[0].kind == NT_VALUE_U64);
    *out = NUMBER(args[0].as.u64 + 1);
    return NT_RESULT_OK;
}

static void deep_order(void)
{
    enum { DEPTH = 12000 };
    NtTree *tree;
    NtNodeId progress;
    OK(nt_tree_create(NULL, &tree));
    OK(nt_progress_mount(tree, (NtNodeId){0}, (NtMountOptions){.enabled=true},
                         (NtProgressOptions){.maximum = DEPTH + 20}, &progress));
    NtBindingValue *spec = calloc(DEPTH, sizeof(*spec));
    size_t *edges = calloc(DEPTH, sizeof(*edges));
    CHECK(spec && edges);
    for (size_t i = 0; i < DEPTH; ++i) {
        edges[i] = (i + 1) % DEPTH;
        spec[i] = (NtBindingValue){.kind=NT_VALUE_U64,.source=NT_BINDING_COMPUTED,
            .from.computed={&edges[i], 1, one_more, NULL}};
    }
    NtBindingGraph *graph = NULL;
    const NtBindingTarget target = {REF(progress, NT_PROPERTY_VALUE), 0};
    CHECK(nt_binding_graph_create(tree, spec, DEPTH, &target, 1, &graph) == NT_RESULT_INVALID_ARGUMENT && !graph);
    spec[DEPTH - 1].source = NT_BINDING_INPUT;
    OK(nt_binding_graph_create(tree, spec, DEPTH, &target, 1, &graph));
    free(spec);
    free(edges);
    const NtValue input = NUMBER(20);
    OK(nt_binding_graph_apply(graph, &input, 1));
    expect_unsigned(tree, target.property, DEPTH + 19);
    OK(nt_binding_graph_destroy(graph));
    nt_tree_destroy(tree);
    printf("%d-node cycle rejected and then evaluated in dependency order\n", DEPTH);
}

int main(void)
{
    CHECK(nt_value_validate((NtValue){.kind=NT_VALUE_REAL,.as.real=INFINITY}) == NT_RESULT_OUT_OF_RANGE);
    OK(nt_value_validate((NtValue){.kind=NT_VALUE_I64,.as.i64=INT64_MIN}));
    OK(nt_value_validate((NtValue){.kind=NT_VALUE_RGBA,.as.rgba={1,2,3,4}}));
    bindings();
    deep_order();
    return EXIT_SUCCESS;
}
