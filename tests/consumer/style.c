#include <nocturne/style.h>
#include <nocturne/widgets.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "style line %d: %s\n", __LINE__, #e); exit(EXIT_FAILURE); } } while (0)
#define OK(e) CHECK((e) == NT_RESULT_OK)
#define VIEW(s) ((NtByteView){(s), sizeof(s)-1})
#define REF(n,p) ((NtPropertyRef){(n),(p)})
#define TEXT(s) ((NtValue){.kind=NT_VALUE_TEXT,.as.text=VIEW(s)})
#define NUM(n) ((NtValue){.kind=NT_VALUE_U64,.as.u64=(n)})
#define RED(n) ((NtValue){.kind=NT_VALUE_RGBA,.as.rgba={(n),0,0,255}})
#define LIST(a) ((NtStyleSheets){(a),sizeof(a)/sizeof(*(a))})

enum { ROOT = 1, SIDEBAR, CONTENT, CARD, LABEL, OTHER };
enum { ACTIVE = 1, MUTED = 2, ALERT = 4 };
enum { DETAIL = 40, TEXT_NOTE, TEXT_OVERLAP, TEXT_INTERIOR };
enum { AMOUNT = 100, UNRELATED = 101 };

static NtStyleSheet *sheet(const NtStyleRule *rules, size_t count)
{
    NtStyleSheet *made = NULL;
    OK(nt_style_sheet_create(NULL, rules, count, &made));
    return made;
}

static NtNodeId box(NtTree *tree, NtNodeId parent, NtBoxLayoutKind orientation)
{
    NtNodeId node;
    const NtBoxLayout layout = {.kind=orientation};
    OK(nt_box_layout_mount(tree, parent, (NtMountOptions){.enabled=true}, &layout, &node));
    return node;
}

static NtNodeId label(NtTree *tree, NtNodeId parent, NtByteView text)
{
    const NtLabelOptions options = {.text=text,
        .style={.width={NT_WIDTH_ONE,NT_WIDTH_TWO},.tab_stop=4,
                .color={{255,255,255,255},{0,0,0,0}}}};
    NtNodeId node;
    OK(nt_label_mount(tree,parent,(NtMountOptions){.enabled=true},&options,&node));
    return node;
}

static void expect_label(NtTree *tree, NtNodeId node, const char *text)
{
    NtValue actual;
    OK(nt_tree_property_get(tree, REF(node,NT_PROPERTY_TEXT), &actual));
    CHECK(actual.kind == NT_VALUE_TEXT && actual.as.text.count == strlen(text));
    CHECK(!memcmp(actual.as.text.data, text, actual.as.text.count));
}

typedef struct Model {
    NtTree *tree;
    NtNodeId node;
    uint64_t amount, unrelated;
    unsigned committed, prepared;
} Model;

static NtResult measure(const void *state, NtMeasureContext *context, NtConstraints bound, NtCellSize *out)
{
    (void)state;
    (void)context;
    const uint32_t cols = bound.columns.min ? bound.columns.min :
        (!bound.columns.bounded || bound.columns.max != 0);
    const uint32_t rows = bound.rows.min ? bound.rows.min :
        (!bound.rows.bounded || bound.rows.max != 0);
    *out = (NtCellSize){cols, rows};
    return NT_RESULT_OK;
}

static NtResult no_layout(const void *state, NtLayoutContext *ctx, NtCellSize size)
{
    (void)state; (void)ctx; (void)size;
    return NT_RESULT_OK;
}

static NtResult no_paint(const void *state, NtPaintContext *ctx, NtCellSize size)
{
    (void)state; (void)ctx; (void)size;
    return NT_RESULT_OK;
}

static NtResult read_amount(const void *value, NtValue *out)
{
    *out = NUM(((const Model *)value)->amount);
    return NT_RESULT_OK;
}

static NtResult prepare_amount(const void *value, NtValue number, void **prepared)
{
    (void)prepared;
    Model *model = (Model *)value;
    CHECK(nt_tree_apply_styles(model->tree, (NtStyleSheets){0}, NULL, 0, NULL, 0) == NT_RESULT_WRONG_PHASE);
    ++model->prepared;
    return number.as.u64 <= 100 ? NT_RESULT_OK : NT_RESULT_OUT_OF_RANGE;
}

static void write_amount(void *value, NtValue number, void *prepared, bool commit)
{
    (void)prepared;
    Model *model = value;
    if (commit) {
        model->amount = number.as.u64;
        ++model->committed;
    }
}

static NtResult read_unrelated(const void *value, NtValue *out)
{
    *out = NUM(((const Model *)value)->unrelated);
    return NT_RESULT_OK;
}

static void write_unrelated(void *value, NtValue number, void *prepared, bool commit)
{
    (void)prepared;
    if (commit) ((Model *)value)->unrelated = number.as.u64;
}

typedef struct Allocation { size_t attempts, refuse, live; } Allocation;
static void *allocate(void *context, size_t bytes, size_t alignment)
{
    Allocation *count = context;
    CHECK(bytes && alignment);
    if (++count->attempts == count->refuse) return NULL;
    void *memory = malloc(bytes);
    CHECK(memory && (uintptr_t)memory % alignment == 0);
    ++count->live;
    return memory;
}

static void release(void *context, void *memory, size_t bytes, size_t alignment)
{
    Allocation *count = context;
    CHECK(memory && bytes && alignment && count->live);
    --count->live;
    free(memory);
}

static void mounted_styles(void)
{
    Allocation allocation = {0};
    const NtAllocator allocator = {&allocation,allocate,release};
    NtTree *tree;
    OK(nt_tree_create(&allocator, &tree));
    const NtNodeId root = box(tree, (NtNodeId){0}, NT_BOX_LAYOUT_ROW);
    const NtNodeId left = box(tree, root, NT_BOX_LAYOUT_COLUMN);
    const NtNodeId right = box(tree, root, NT_BOX_LAYOUT_COLUMN);
    const NtNodeId header = label(tree, left, VIEW("old"));
    const NtNodeId other = label(tree, right, VIEW("untouched"));
    Model model = {.tree=tree,.amount=2,.unrelated=99};
    const NtComponentOps custom = {.measure=measure,.layout=no_layout,.paint=no_paint};
    OK(nt_tree_mount(tree,left,&custom,&model,(NtMountOptions){.enabled=true},&model.node));
    const NtPropertyDescriptor descriptors[] = {
        {AMOUNT,NT_VALUE_U64,NT_INVALIDATE_PAINT,read_amount,prepare_amount,write_amount},
        {UNRELATED,NT_VALUE_U64,NT_INVALIDATE_NONE,read_unrelated,NULL,write_unrelated}
    };
    OK(nt_tree_define_properties(tree, model.node, descriptors, 2));
    OK(nt_tree_publish(tree, (NtCellSize){25,4}));

    char title[] = "styled";
    NtStyleRule base[] = {
        {.property=AMOUNT,.value=NUM(10)},
        {.selector={.relation=NT_STYLE_ANCESTOR,.ancestor={.role=ROOT}},.property=AMOUNT,.value=NUM(20)},
        {.selector={.relation=NT_STYLE_PARENT,.ancestor={.role=SIDEBAR}},.property=AMOUNT,.value=NUM(30)},
        {.selector={.relation=NT_STYLE_PARENT,.ancestor={.role=CONTENT}},.property=AMOUNT,.value=NUM(45)},
        {.selector={.subject={.role=CARD,.region=DETAIL,.require=ACTIVE,.forbid=MUTED},
                    .relation=NT_STYLE_PARENT,.ancestor={.role=SIDEBAR}},.property=AMOUNT,.value=NUM(55)},
        {.selector={.subject={.role=LABEL}},.property=NT_PROPERTY_TEXT,.value=TEXT(title)},
        {.selector={.subject={.role=LABEL}},.property=NT_PROPERTY_FOREGROUND,.value=RED(82)},
        {.property=UNRELATED,.value=NUM(7)}
    };
    NtStyleSheet *first = NULL;
    for (size_t attempt = 1; attempt <= 3; ++attempt) {
        const size_t live = allocation.live;
        allocation.refuse = allocation.attempts + attempt;
        CHECK(nt_style_sheet_create(&allocator, base, sizeof(base)/sizeof(*base), &first) ==
              NT_RESULT_ALLOCATION_FAILED);
        CHECK(!first && allocation.live == live);
    }
    allocation.refuse = 0;
    OK(nt_style_sheet_create(&allocator, base, sizeof(base)/sizeof(*base), &first));
    title[0] = 'X';
    base[5].selector.subject.role = OTHER;
    const NtStyleRule late_rules[] = {
        {.selector={.subject={.role=LABEL}},.property=NT_PROPERTY_FOREGROUND,.value=RED(91)},
        {.selector={.subject={.role=CARD,.region=DETAIL,.require=ACTIVE,.forbid=MUTED},
                    .relation=NT_STYLE_PARENT,.ancestor={.role=SIDEBAR}},.property=AMOUNT,.value=NUM(56)},
        {.property=AMOUNT,.value=NUM(1)}
    };
    NtStyleSheet *late = sheet(late_rules, sizeof(late_rules)/sizeof(*late_rules));
    const NtStyleSheet *ordinary[] = {first,late};
    const NtStyleRule override_rules[] = {
        {.selector={.subject={.require=ALERT}},.property=AMOUNT,.value=NUM(70),.override=true}
    };
    NtStyleSheet *override_sheet = sheet(override_rules, 1);
    const NtStyleSheet *full[] = {first,late,override_sheet};
    NtStyleMount mounts[] = {{root,{.role=ROOT}}, {left,{.role=SIDEBAR}}, {right,{.role=CONTENT}},
        {header,{.role=LABEL}}, {other,{.role=OTHER}}, {model.node,{CARD,DETAIL,ACTIVE}}};
    const NtPropertyRef targets[] = {REF(header,NT_PROPERTY_TEXT), REF(header,NT_PROPERTY_FOREGROUND),
        REF(model.node,AMOUNT), REF(other,NT_PROPERTY_FOREGROUND)};
    for (size_t offset = 1; offset <= 2; ++offset) {
        const size_t live = allocation.live;
        allocation.refuse = allocation.attempts + offset;
        CHECK(nt_tree_apply_styles(tree, LIST(ordinary), mounts, 6, targets, 4) == NT_RESULT_ALLOCATION_FAILED);
        CHECK(model.amount == 2 && allocation.live == live && nt_tree_pending(tree) == NT_INVALIDATE_NONE);
        expect_label(tree,header,"old");
    }
    allocation.refuse = 0;
    OK(nt_tree_apply_styles(tree, LIST(ordinary), mounts, 6, targets, 4));
    CHECK(model.amount == 56 && model.unrelated == 99);
    expect_label(tree,header,"styled");
    expect_label(tree,other,"untouched");
    NtValue color;
    OK(nt_tree_property_get(tree, REF(other,NT_PROPERTY_FOREGROUND), &color));
    CHECK(color.as.rgba.red == 255);
    OK(nt_tree_publish(tree,(NtCellSize){25,4}));
    NtCellView cell;
    OK(nt_frame_cell(nt_tree_frame(tree),(NtCellPoint){0,0},&cell));
    CHECK(cell.glyph.count == 1 && cell.glyph.data[0] == 's' && cell.style.foreground.red == 91);
    mounts[5].element.states = ACTIVE|MUTED;
    OK(nt_tree_apply_styles(tree, LIST(full), mounts, 6, &targets[2], 1));
    CHECK(model.amount == 30);
    mounts[5].element.states = ACTIVE|ALERT;
    OK(nt_tree_apply_styles(tree, LIST(full), mounts, 6, &targets[2], 1));
    CHECK(model.amount == 70);

    OK(nt_tree_publish(tree, (NtCellSize){25,4}));
    const NtFrame *frame = nt_tree_frame(tree);
    const NtStyleRule bad_rules[] = {
        {.property=NT_PROPERTY_TEXT,.value=TEXT("discard"),.override=true},
        {.property=AMOUNT,.value=NUM(101),.override=true}
    };
    NtStyleSheet *bad = sheet(bad_rules, 2);
    const NtStyleSheet *bad_layers[] = {bad};
    const size_t live = allocation.live;
    CHECK(nt_tree_apply_styles(tree, LIST(bad_layers), mounts, 6, targets, 4) == NT_RESULT_OUT_OF_RANGE);
    CHECK(allocation.live == live && model.amount == 70 && model.unrelated == 99);
    CHECK(nt_tree_frame(tree) == frame && nt_tree_pending(tree) == NT_INVALIDATE_NONE);
    expect_label(tree,header,"styled");

    OK(nt_tree_move(tree,model.node,right));
    NtNodeGeometry committed;
    OK(nt_tree_geometry(tree,model.node,&committed));
    CHECK(committed.parent.slot == left.slot);
    OK(nt_tree_apply_styles(tree, LIST(ordinary), mounts, 6, &targets[2], 1));
    CHECK(model.amount == 45 && model.unrelated == 99);
    OK(nt_tree_remove(tree, model.node));
    CHECK(nt_tree_apply_styles(tree, LIST(ordinary), mounts, 6, targets, 4) == NT_RESULT_STALE_REFERENCE);
    nt_tree_destroy(tree);
    nt_style_sheet_destroy(first);
    nt_style_sheet_destroy(late);
    nt_style_sheet_destroy(override_sheet);
    nt_style_sheet_destroy(bad);
    CHECK(allocation.live == 0);
    puts("nested styles, override and declaration order, domain rollback, live reparenting and unlisted data passed");
}

typedef struct MountedText {
    const NtTextLayout *layout;
    const NtTextPaintOptions *options;
    const NtTextStyles *styles;
} MountedText;

static NtResult paint_mount(const void *value, NtPaintContext *ctx, NtCellSize size)
{
    (void)size;
    const MountedText *mounted = value;
    return nt_paint_styled_text(ctx, (NtCellPoint){0}, mounted->layout, mounted->options, mounted->styles);
}

static void check_span(const NtFrame *frame, const NtTextLayout *layout, size_t start,
                        unsigned red, unsigned back, NtOwnerToken owner, uint8_t width)
{
    for (size_t i = 0; i < nt_text_layout_cluster_count(layout); ++i) {
        NtClusterView cluster;
        OK(nt_text_layout_cluster(layout,i,&cluster));
        if (cluster.source.begin != start) continue;
        NtCellView cell;
        OK(nt_frame_cell(frame, cluster.position, &cell));
        CHECK(cell.owner == owner && cell.glyph_columns == width && cell.style.foreground.red == red);
        CHECK(cell.glyph.count == cluster.glyph.count && !memcmp(cell.glyph.data,cluster.glyph.data,cell.glyph.count));
        NtCellBackground background;
        OK(nt_frame_background(frame, cluster.position, &background));
        CHECK(background.color.red == back);
        if (width == 2) {
            ++cluster.position.x;
            NtCellView tail;
            OK(nt_frame_cell(frame, cluster.position,&tail));
            CHECK(tail.glyph_columns == 2 && tail.lead.x == cluster.position.x - 1 && tail.owner == owner);
        }
        return;
    }
    CHECK(false);
}

static void text_styles(void)
{
    const char logical[] = "Ae\xcc\x81\xe7\x95\x8c" "B \xd7\x90";
    NtText *text;
    OK(nt_text_create(NULL, VIEW(logical), &text));
    NtTextCluster b;
    OK(nt_text_cluster(text,3,&b));
    CHECK(b.source.begin == 7);
    NtTextLayout *layout;
    const NtTextLayoutOptions layout_options = {.width={NT_WIDTH_ONE,NT_WIDTH_TWO},.tab_stop=4,
                                               .base_direction=NT_DIRECTION_RTL};
    OK(nt_text_layout_create(NULL,text,&layout_options,&layout));
    nt_text_destroy(text);
    const NtStyleRule text_rules[] = {
        {.selector={.subject={.role=LABEL,.require=ACTIVE}},.property=NT_PROPERTY_FOREGROUND,.value=RED(40)},
        {.selector={.text_region=TEXT_NOTE},.property=NT_PROPERTY_FOREGROUND,.value=RED(60)},
        {.selector={.text_region=TEXT_NOTE,.match_characters=true,.first_scalar=0x0301,.last_scalar=0x0301},
         .property=NT_PROPERTY_FOREGROUND,.value=RED(100)},
        {.selector={.text_region=TEXT_OVERLAP},.property=NT_PROPERTY_BACKGROUND,.value=RED(30)},
        {.selector={.text_region=TEXT_INTERIOR},.property=NT_PROPERTY_BACKGROUND,.value=RED(240),.override=true},
        {.selector={.match_bytes=true,.bytes={4,7}},.property=NT_PROPERTY_FOREGROUND,.value=RED(140),.override=true},
        {.selector={.cluster_id=b.id},.property=NT_PROPERTY_FOREGROUND,.value=RED(180),.override=true},
        {.selector={.match_characters=true,.first_scalar=0x05d0,.last_scalar=0x05d0},
         .property=NT_PROPERTY_BACKGROUND,.value=RED(65),.override=true}
    };
    NtStyleSheet *rules = sheet(text_rules, sizeof(text_rules)/sizeof(*text_rules));
    const NtStyleSheet *layers[] = {rules};
    const NtStyleRegion regions[] = {{TEXT_NOTE,{1,7}}, {TEXT_OVERLAP,{1,2}}, {TEXT_INTERIOR,{2,4}}};
    const NtTextStyles styles = {.sheets=LIST(layers), .path.element={LABEL,0,ACTIVE},
                                 .regions=regions,.region_count=3};
    const NtTextPaintSpan span = {{1,7},{.color={{255,255,255,255},{8,0,0,255}},.owner=53}};
    const NtPaint procedural = {.kind=NT_PAINT_SOLID,.content.solid={.space=NT_COLOR_SRGB,
        .channel={1,0,0},.alpha=1},.opacity=.25};
    const NtTextPaintOptions options = {.base={.color={{255,255,255,255},{5,0,0,255}},
        .owner=17,.foreground=&procedural},.spans=&span,.count=1};
    const NtCellSize size = nt_text_layout_size(layout);
    NtFrame *frame;
    OK(nt_frame_create(NULL,size,&frame));
    NtSurfaceView view;
    OK(nt_surface_root(frame,&view));
    OK(nt_style_text_paint(&view,(NtCellPoint){0},layout,&options,&styles));
    check_span(frame,layout,0,40,5,17,1);
    check_span(frame,layout,1,100,30,53,1);
    check_span(frame,layout,4,140,8,53,2);
    check_span(frame,layout,7,180,5,17,1);
    check_span(frame,layout,9,40,65,17,1);

    const NtStyleRule invalid_rule = {.selector={.match_bytes=true,.bytes={4,7}},
        .property=NT_PROPERTY_FOREGROUND,.value=NUM(1),.override=true};
    NtStyleSheet *invalid = sheet(&invalid_rule,1);
    const NtStyleSheet *bad[] = {rules,invalid};
    NtTextStyles failed = styles;
    failed.sheets = LIST(bad);
    CHECK(nt_style_text_paint(&view,(NtCellPoint){0},layout,&options,&failed) == NT_RESULT_INVALID_ARGUMENT);
    check_span(frame,layout,1,100,30,53,1);
    check_span(frame,layout,4,140,8,53,2);

    const NtStyleSubject text_subject = {.path=styles.path,.source=VIEW(logical),.text={1,4},
        .regions=regions,.region_count=3};
    bool matched = false;
    NtValue resolved;
    OK(nt_style_resolve(LIST(layers), &text_subject, NT_PROPERTY_FOREGROUND, &matched, &resolved));
    CHECK(matched && resolved.as.rgba.red == 100);
    resolved = NUM(20);
    OK(nt_style_resolve(LIST(layers), &text_subject, AMOUNT, &matched, &resolved));
    CHECK(!matched && resolved.as.u64 == 20);

    NtTree *tree;
    NtNodeId node;
    OK(nt_tree_create(NULL, &tree));
    const NtComponentOps ops = {.measure=measure,.layout=no_layout,.paint=paint_mount};
    MountedText mounted = {layout,&options,&styles};
    OK(nt_tree_mount(tree,(NtNodeId){0},&ops,&mounted,(NtMountOptions){.enabled=true},&node));
    OK(nt_tree_publish(tree,size));
    check_span(nt_tree_frame(tree),layout,1,100,30,nt_node_owner(node),1);
    check_span(nt_tree_frame(tree),layout,4,140,8,nt_node_owner(node),2);
    const NtFrame *previous = nt_tree_frame(tree);
    mounted.styles = &failed;
    OK(nt_tree_invalidate(tree, node, NT_INVALIDATE_PAINT));
    CHECK(nt_tree_publish(tree, size) == NT_RESULT_INVALID_ARGUMENT && nt_tree_frame(tree) == previous);
    check_span(previous,layout,4,140,8,nt_node_owner(node),2);
    nt_tree_destroy(tree);
    nt_frame_destroy(frame);
    nt_text_layout_destroy(layout);
    nt_style_sheet_destroy(invalid);
    nt_style_sheet_destroy(rules);
    puts("logical UTF-8 regions, scalar and cluster selectors, bidi, whole wide glyphs and paint ownership passed");
}

static void invalid_inputs(void)
{
    NtStyleRule rule = {.selector.subject={.require=ACTIVE,.forbid=ACTIVE},
                        .property=NT_PROPERTY_FOREGROUND,.value=RED(45)};
    NtStyleSheet *sentinel = NULL;
    CHECK(nt_style_sheet_create(NULL, &rule, 1, &sentinel) == NT_RESULT_INVALID_ARGUMENT && !sentinel);
    rule.selector.subject = (NtStyleCondition){0};
    rule.selector.match_characters = true;
    rule.selector.first_scalar = 0xd800;
    rule.selector.last_scalar = 0xdfff;
    CHECK(nt_style_sheet_create(NULL, &rule, 1, &sentinel) == NT_RESULT_INVALID_ARGUMENT && !sentinel);
    rule.selector.first_scalar = rule.selector.last_scalar = 0x0301;
    NtStyleSheet *valid = sheet(&rule, 1);
    const NtStyleSheet *layers[] = {valid};
    const NtStyleSubject malformed = {.source=VIEW("e\xcc\x81"),.text={2,3}};
    NtValue unchanged = NUM(55);
    bool matched = true;
    CHECK(nt_style_resolve(LIST(layers), &malformed, NT_PROPERTY_FOREGROUND, &matched, &unchanged) ==
          NT_RESULT_INVALID_ARGUMENT);
    CHECK(matched && unchanged.kind == NT_VALUE_U64 && unchanged.as.u64 == 55);
    nt_style_sheet_destroy(valid);
}

int main(void)
{
    mounted_styles();
    text_styles();
    invalid_inputs();
    return EXIT_SUCCESS;
}
