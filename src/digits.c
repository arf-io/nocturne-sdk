#include <nocturne/plot.h>

#include "property_internal.h"

typedef struct Digits { NtAllocator allocator; NtDigitsOptions options; } Digits;
typedef struct DigitsText { char characters[24]; size_t count; uint32_t columns; } DigitsText;

NT_DECLARE_ALIGNMENT(Digits);

enum { TOP=1, UPPER_RIGHT=2, LOWER_RIGHT=4, BOTTOM=8, LOWER_LEFT=16, UPPER_LEFT=32, MIDDLE=64 };
static const uint8_t segments[] = {
    TOP|UPPER_RIGHT|LOWER_RIGHT|BOTTOM|LOWER_LEFT|UPPER_LEFT,
    UPPER_RIGHT|LOWER_RIGHT,
    TOP|UPPER_RIGHT|MIDDLE|LOWER_LEFT|BOTTOM,
    TOP|UPPER_RIGHT|MIDDLE|LOWER_RIGHT|BOTTOM,
    UPPER_LEFT|MIDDLE|UPPER_RIGHT|LOWER_RIGHT,
    TOP|UPPER_LEFT|MIDDLE|LOWER_RIGHT|BOTTOM,
    TOP|UPPER_LEFT|MIDDLE|LOWER_LEFT|LOWER_RIGHT|BOTTOM,
    TOP|UPPER_RIGHT|LOWER_RIGHT,
    TOP|UPPER_RIGHT|LOWER_RIGHT|BOTTOM|LOWER_LEFT|UPPER_LEFT|MIDDLE,
    TOP|UPPER_RIGHT|LOWER_RIGHT|BOTTOM|UPPER_LEFT|MIDDLE
};

static DigitsText format(NtDigitsOptions options)
{
    DigitsText format = {0};
    const bool negative = options.value < 0;
    uint64_t magnitude = negative ? 0 - (uint64_t)options.value : (uint64_t)options.value;
    unsigned index = 0;
    do {
        if (index && index == options.fractional_digits)
            format.characters[format.count++] = '.';
        format.characters[format.count++] = (char)('0' + magnitude % 10);
        magnitude /= 10;
        ++index;
    } while (magnitude || index <= options.fractional_digits);
    if (negative) format.characters[format.count++] = '-';
    for (size_t i = 0; i < format.count; ++i)
        format.columns += format.characters[i] == '.' ? 2u : 4u;
    --format.columns;
    return format;
}

static uint32_t fit_extent(uint32_t value, NtExtentRange range)
{
    if (value < range.min) return range.min;
    return range.bounded && value > range.max ? range.max : value;
}

static NtResult measure(const void *value, NtMeasureContext *context, NtConstraints bounds, NtCellSize *out)
{
    if (nt_measure_child_count(context)) return NT_RESULT_INVALID_ARGUMENT;
    const Digits *state = value;
    const DigitsText text = format(state->options);
    *out = (NtCellSize){fit_extent(text.columns,bounds.columns),fit_extent(5,bounds.rows)};
    return NT_RESULT_OK;
}

static NtResult layout(const void *value, NtLayoutContext *context, NtCellSize assigned)
{
    (void)value;
    (void)assigned;
    return nt_layout_child_count(context) ? NT_RESULT_INVALID_ARGUMENT : NT_RESULT_OK;
}

static bool covered(char digit, int32_t x, int32_t y)
{
    if (digit == '.') return x == 0 && y == 4;
    const uint8_t mask = digit == '-' ? MIDDLE : segments[digit-'0'];
    static const uint8_t row[5][3] = {
        {TOP,TOP,TOP}, {UPPER_LEFT,0,UPPER_RIGHT}, {MIDDLE,MIDDLE,MIDDLE},
        {LOWER_LEFT,0,LOWER_RIGHT}, {BOTTOM,BOTTOM,BOTTOM}
    };
    return (row[y][x] & mask) != 0;
}

static NtResult paint_digits(const void *value, NtPaintContext *paint, NtCellSize size)
{
    const Digits *state = value;
    const NtDigitsOptions options = state->options;
    const NtCellRect clip = nt_paint_clip(paint);
    if (!clip.size.columns || !clip.size.rows) return NT_RESULT_OK;
    NtResult result = nt_paint_fill(paint,clip,options.background);
    if (result != NT_RESULT_OK || !options.foreground.alpha) return result;
    const NtCellStyle style = {options.foreground,{0}};
    const NtByteView ink = options.ascii ? (NtByteView){"#",1} : (NtByteView){"\xe2\x96\x88",3};
    const DigitsText text = format(options);
    int32_t origin = 0;
    for (size_t i = text.count; i; --i) {
        const char digit = text.characters[i-1];
        const int32_t width = digit == '.' ? 1 : 3;
        for (int32_t y = 0; y < 5 && (uint32_t)y < size.rows; ++y) {
            for (int32_t x = 0; x < width && (uint32_t)(origin+x) < size.columns; ++x) {
                if (!covered(digit,x,y)) continue;
                result = nt_paint_glyph(paint,(NtCellPoint){origin+x,y},ink,1,style);
                if (result != NT_RESULT_OK) return result;
            }
        }
        origin += width+1;
    }
    return NT_RESULT_OK;
}

static void dispose(void *value)
{
    Digits *state = value;
    const NtAllocator allocator = state->allocator;
    nt_release(&allocator,state,sizeof(*state),NT_ALIGNMENT(Digits));
}

static NtResult read_number(const void *state, NtValue *out)
{
    *out = (NtValue){.kind=NT_VALUE_I64,.as.i64=((const Digits *)state)->options.value};
    return NT_RESULT_OK;
}

static void write_number(void *state, NtValue value, void *prepared, bool commit)
{
    (void)prepared;
    if (commit) ((Digits *)state)->options.value = value.as.i64;
}

static NtResult read_foreground(const void *state, NtValue *out)
{
    *out = (NtValue){.kind=NT_VALUE_RGBA,.as.rgba=((const Digits *)state)->options.foreground};
    return NT_RESULT_OK;
}

static NtResult read_background(const void *state, NtValue *out)
{
    *out = (NtValue){.kind=NT_VALUE_RGBA,.as.rgba=((const Digits *)state)->options.background};
    return NT_RESULT_OK;
}

static void write_foreground(void *state, NtValue value, void *prepared, bool commit)
{
    (void)prepared;
    if (commit) ((Digits *)state)->options.foreground = value.as.rgba;
}

static void write_background(void *state, NtValue value, void *prepared, bool commit)
{
    (void)prepared;
    if (commit) ((Digits *)state)->options.background = value.as.rgba;
}

static const NtPropertyDescriptor properties[] = {
    {NT_PROPERTY_VALUE,NT_VALUE_I64,NT_INVALIDATE_LAYOUT,read_number,NULL,write_number},
    {NT_PROPERTY_FOREGROUND,NT_VALUE_RGBA,NT_INVALIDATE_PAINT,read_foreground,NULL,write_foreground},
    {NT_PROPERTY_BACKGROUND,NT_VALUE_RGBA,NT_INVALIDATE_PAINT,read_background,NULL,write_background}
};

static const NtComponentOps digits_ops = {measure,layout,NULL,paint_digits,NULL,dispose};

NtResult nt_digits_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount,
                         const NtDigitsOptions *options, NtNodeId *out)
{
    if (!tree || !out || !options) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    if (options->fractional_digits > 18) return NT_RESULT_OUT_OF_RANGE;
    if (!nt_node_null(parent)) {
        const NtResult result = nt_node_validate(tree,parent);
        if (result != NT_RESULT_OK) return result;
    }
    tree->busy = true;
    Digits *state = nt_allocate(&tree->allocator,sizeof(*state),NT_ALIGNMENT(Digits));
    tree->busy = false;
    if (!state) return NT_RESULT_ALLOCATION_FAILED;
    *state = (Digits){tree->allocator,*options};
    NtNodeId node;
    const NtResult result = nt_tree_mount(tree,parent,&digits_ops,state,mount,&node);
    if (result == NT_RESULT_OK) {
        nt_property_stock_table(tree,node,properties,sizeof(properties)/sizeof(*properties));
        *out = node;
    } else {
        tree->busy = true;
        dispose(state);
        tree->busy = false;
    }
    return result;
}

NtResult nt_digits_set(NtTree *tree, NtNodeId node, const NtDigitsOptions *options)
{
    if (!tree || !options) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    const NtResult result = nt_node_validate(tree,node);
    if (result != NT_RESULT_OK) return result;
    NtNode *mount = &tree->nodes[node.slot];
    if (mount->ops.paint != paint_digits) return NT_RESULT_INVALID_ARGUMENT;
    if (options->fractional_digits > 18) return NT_RESULT_OUT_OF_RANGE;
    Digits *state = mount->state;
    state->options = *options;
    nt_tree_dirty(tree,NT_INVALIDATE_LAYOUT);
    return NT_RESULT_OK;
}
