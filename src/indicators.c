#include "controls_internal.h"
#include "property_internal.h"

typedef enum IndicatorKind { CURSOR, WAIT, PROGRESS, SCROLLBAR } IndicatorKind;
typedef union IndicatorOptions {
    NtCursorOptions cursor;
    NtWaitOptions wait;
    NtProgressOptions progress;
    NtScrollBarOptions scrollbar;
} IndicatorOptions;

typedef struct Indicator {
    NtAllocator allocator;
    NtNodeId node;
    IndicatorKind kind;
    IndicatorOptions options;
    NtControlPress press;
    unsigned phase;
} Indicator;

typedef struct ScrollbarPaint {
    NtAllocator allocator;
    uint32_t limit;
    bool vertical;
} ScrollbarPaint;

NT_DECLARE_ALIGNMENT(Indicator);
NT_DECLARE_ALIGNMENT(ScrollbarPaint);

static uint32_t scroll_limit(NtScrollBarOptions options)
{
    return options.content > options.viewport ? options.content - options.viewport : 0;
}

static NtResult normalize(IndicatorKind kind, IndicatorOptions *options)
{
    if (kind == PROGRESS && (!options->progress.maximum || options->progress.value > options->progress.maximum))
        return NT_RESULT_OUT_OF_RANGE;
    if (kind == SCROLLBAR) {
        const uint32_t limit = scroll_limit(options->scrollbar);
        if (options->scrollbar.offset > limit) options->scrollbar.offset = limit;
    }
    return NT_RESULT_OK;
}

static NtResult measure_indicator(const void *value, NtMeasureContext *context, NtConstraints bounds,
                                  NtCellSize *out)
{
    if (nt_measure_child_count(context)) return NT_RESULT_INVALID_ARGUMENT;
    const Indicator *state = value;
    NtCellSize preferred = {1,1};
    if (state->kind == PROGRESS || state->kind == SCROLLBAR) {
        const bool vertical = state->kind == PROGRESS ? state->options.progress.vertical :
            state->options.scrollbar.vertical;
        preferred = vertical ? (NtCellSize){1,8} : (NtCellSize){8,1};
    }
    *out = nt_control_fit(preferred,bounds);
    return NT_RESULT_OK;
}

static NtResult update_indicator(void *value, NtUpdateInfo info, NtUpdateReply *reply)
{
    Indicator *state = value;
    const NtTimeUs interval = state->kind == CURSOR ? state->options.cursor.phase_us :
        state->options.wait.phase_us;
    unsigned phase = 0;
    if (interval && (state->kind != CURSOR || state->options.cursor.visible)) {
        if (info.motion == NT_MOTION_FULL)
            phase = (unsigned)((info.now / interval) % (state->kind == CURSOR ? 2u : 4u));
        const NtTimeUs until_next = interval - info.now % interval;
        if (until_next <= UINT64_MAX - info.now) reply->next = (NtDeadline){true,info.now + until_next};
    }
    if (state->phase != phase) {
        state->phase = phase;
        reply->invalidate = NT_INVALIDATE_PAINT;
    }
    return NT_RESULT_OK;
}

static uint32_t ratio(uint32_t length, uint32_t value, uint32_t total)
{
    return (uint32_t)(((uint64_t)length * value) / total);
}

static void dispose_scrollbar_paint(void *value)
{
    ScrollbarPaint *snapshot = value;
    const NtAllocator allocator = snapshot->allocator;
    nt_release(&allocator,snapshot,sizeof(*snapshot),NT_ALIGNMENT(ScrollbarPaint));
}

static NtResult paint_bar(const Indicator *state, NtPaintContext *paint, NtCellSize size)
{
    const bool scroll = state->kind == SCROLLBAR;
    const NtProgressOptions progress = scroll ? (NtProgressOptions){0} : state->options.progress;
    const NtScrollBarOptions bar = scroll ? state->options.scrollbar : (NtScrollBarOptions){0};
    const bool vertical = scroll ? bar.vertical : progress.vertical;
    const uint32_t length = vertical ? size.rows : size.columns;
    uint32_t start = 0, count = length;
    if (scroll) {
        const uint32_t limit = scroll_limit(bar);
        ScrollbarPaint *snapshot = nt_allocate(&state->allocator,sizeof(*snapshot),NT_ALIGNMENT(ScrollbarPaint));
        if (!snapshot) return NT_RESULT_ALLOCATION_FAILED;
        *snapshot = (ScrollbarPaint){state->allocator,limit,vertical};
        nt_component_attach_paint_data(paint,snapshot,dispose_scrollbar_paint);
        if (limit) {
            count = ratio(length,bar.viewport,bar.content);
            if (!count) count = 1;
            start = ratio(length-count,bar.offset,limit);
        }
    } else {
        count = ratio(length,progress.value,progress.maximum);
    }
    const bool focused = scroll && nt_node_same(nt_tree_focus(state->node.tree),state->node);
    const NtCellRect clip = nt_paint_clip(paint);
    const int64_t right = (int64_t)clip.origin.x + clip.size.columns;
    const int64_t bottom = (int64_t)clip.origin.y + clip.size.rows;
    for (int64_t y = clip.origin.y; y < bottom; ++y) {
        for (int64_t x = clip.origin.x; x < right; ++x) {
            const uint32_t cell = (uint32_t)(vertical ? y : x);
            const bool fill = cell >= start && cell - start < count;
            const NtCellStyle colors = scroll ? (fill ? bar.thumb : bar.track) :
                (fill ? progress.filled : progress.remaining);
            const char glyph = fill ? (focused ? '@' : '#') : (scroll ? '.' : '-');
            const NtResult result = nt_paint_glyph(paint,(NtCellPoint){(int32_t)x,(int32_t)y},
                                                   (NtByteView){&glyph,1},1,colors);
            if (result != NT_RESULT_OK) return result;
        }
    }
    return NT_RESULT_OK;
}

static NtResult paint_indicator(const void *value, NtPaintContext *paint, NtCellSize size)
{
    if (!size.columns || !size.rows) return NT_RESULT_OK;
    const Indicator *state = value;
    if (state->kind == PROGRESS || state->kind == SCROLLBAR) return paint_bar(state,paint,size);
    if (state->kind == CURSOR) {
        if (!state->options.cursor.visible || state->phase) return NT_RESULT_OK;
        return nt_paint_glyph(paint,(NtCellPoint){0,0},(NtByteView){"|",1},1,state->options.cursor.style);
    }
    const char *ascii = "|/-\\";
    static const char *const unicode[] = {"⠋","⠙","⠹","⠸"};
    const NtByteView glyph = state->options.wait.ascii ? (NtByteView){ascii+state->phase,1} :
        (NtByteView){unicode[state->phase],3};
    return nt_paint_glyph(paint,(NtCellPoint){0,0},glyph,1,state->options.wait.style);
}

static NtResult change_offset(Indicator *state, NtEventContext *context, NtEventReply *reply, int64_t next)
{
    NtScrollBarOptions *bar = &state->options.scrollbar;
    const uint32_t limit = scroll_limit(*bar);
    const uint32_t offset = next <= 0 ? 0 : next >= limit ? limit : (uint32_t)next;
    if (offset != bar->offset) {
        const NtResult result = nt_control_emit(context,NT_CONTROL_SCROLL_CHANGED,offset);
        if (result != NT_RESULT_OK) return result;
        bar->offset = offset;
        reply->invalidate = NT_INVALIDATE_PAINT;
    }
    reply->consume = true;
    return NT_RESULT_OK;
}

static NtResult event_scroll(void *value, NtEventContext *context, const NtEvent *event, NtEventReply *reply)
{
    if (nt_event_phase(context) != NT_EVENT_TARGET) return NT_RESULT_OK;
    Indicator *state = value;
    const NtScrollBarOptions bar = state->options.scrollbar;
    (void)nt_control_press(state->node,&state->press,event,reply);
    const bool down = event->kind == NT_EVENT_POINTER_DOWN && reply->capture == NT_CAPTURE_TAKE;
    if (down || (event->kind == NT_EVENT_POINTER_MOVE && reply->consume)) {
        const NtPlacement *placement = nt_tree_placement(state->node.tree,state->node);
        const ScrollbarPaint *snapshot = placement ? placement->paint_data : NULL;
        const uint32_t limit = scroll_limit(bar);
        if (!snapshot || snapshot->vertical != bar.vertical || snapshot->limit != limit) {
            if (down) {
                reply->capture = NT_CAPTURE_KEEP;
                state->press.tracking = false;
            }
            return NT_RESULT_OK;
        }
        NtLocalPosition point;
        if (!nt_component_local_position(context,&point)) return NT_RESULT_OK;
        const NtCellSize size = placement->surface.bounds.size;
        const uint32_t length = bar.vertical ? size.rows : size.columns;
        const int64_t cell = bar.vertical ? point.y : point.x;
        if (length > 1) {
            const uint32_t bounded = cell <= 0 ? 0 : cell >= length ? length-1 : (uint32_t)cell;
            return change_offset(state,context,reply,ratio(limit,bounded,length-1));
        }
        return NT_RESULT_OK;
    }
    int64_t next = bar.offset;
    if (event->kind == NT_EVENT_WHEEL) {
        const int32_t delta = bar.vertical || !event->data.wheel.dx ?
            event->data.wheel.dy : event->data.wheel.dx;
        if (!delta) return NT_RESULT_OK;
        next += delta;
    } else {
        const uint32_t key = nt_control_key(event,true);
        if (key == (bar.vertical ? NT_KEY_UP : NT_KEY_LEFT)) --next;
        else if (key == (bar.vertical ? NT_KEY_DOWN : NT_KEY_RIGHT)) ++next;
        else if (key == NT_KEY_PAGE_UP) next -= bar.viewport;
        else if (key == NT_KEY_PAGE_DOWN) next += bar.viewport;
        else if (key == NT_KEY_HOME) next = 0;
        else if (key == NT_KEY_END) next = scroll_limit(bar);
        else return NT_RESULT_OK;
    }
    return change_offset(state,context,reply,next);
}

static void dispose_indicator(void *value)
{
    Indicator *state = value;
    const NtAllocator allocator = state->allocator;
    nt_release(&allocator,state,sizeof(*state),NT_ALIGNMENT(Indicator));
}

static const NtComponentOps animated_ops = {
    measure_indicator,nt_control_leaf,update_indicator,paint_indicator,NULL,dispose_indicator
};
static const NtComponentOps progress_ops = {
    measure_indicator,nt_control_leaf,NULL,paint_indicator,NULL,dispose_indicator
};
static const NtComponentOps scrollbar_ops = {
    measure_indicator,nt_control_leaf,NULL,paint_indicator,event_scroll,dispose_indicator
};

static NtResult read_indicator_value(const void *value, NtValue *out)
{
    const Indicator *state = value;
    *out = (NtValue){.kind = NT_VALUE_U64,
        .as.u64 = state->kind == PROGRESS ? state->options.progress.value : state->options.scrollbar.offset};
    return NT_RESULT_OK;
}

static NtResult prepare_indicator_value(const void *value, NtValue number, void **prepared)
{
    (void)prepared;
    const Indicator *state = value;
    const uint32_t limit = state->kind == PROGRESS ? state->options.progress.maximum :
        scroll_limit(state->options.scrollbar);
    return number.as.u64 <= limit ? NT_RESULT_OK : NT_RESULT_OUT_OF_RANGE;
}

static void finish_indicator_value(void *value, NtValue number, void *prepared, bool commit)
{
    (void)prepared;
    Indicator *state = value;
    if (commit) {
        if (state->kind == PROGRESS) state->options.progress.value = (uint32_t)number.as.u64;
        else state->options.scrollbar.offset = (uint32_t)number.as.u64;
    }
}

static const NtPropertyDescriptor progress_property = {
    NT_PROPERTY_VALUE, NT_VALUE_U64, NT_INVALIDATE_PAINT,
    read_indicator_value, prepare_indicator_value, finish_indicator_value
};
static const NtPropertyDescriptor scrollbar_property = {
    NT_PROPERTY_OFFSET, NT_VALUE_U64, NT_INVALIDATE_PAINT,
    read_indicator_value, prepare_indicator_value, finish_indicator_value
};

static NtResult mount_indicator(NtTree *tree, NtNodeId parent, NtMountOptions mount, IndicatorKind kind,
                                IndicatorOptions options, NtNodeId *out)
{
    NtResult result = nt_control_validate_mount(tree,parent,out);
    if (result != NT_RESULT_OK) return result;
    result = normalize(kind,&options);
    if (result != NT_RESULT_OK) return result;
    tree->busy = true;
    Indicator *state = nt_allocate(&tree->allocator,sizeof(*state),NT_ALIGNMENT(Indicator));
    tree->busy = false;
    if (!state) return NT_RESULT_ALLOCATION_FAILED;
    *state = (Indicator){.allocator=tree->allocator,.kind=kind,.options=options};
    const NtComponentOps *ops = kind == SCROLLBAR ? &scrollbar_ops :
        kind == PROGRESS ? &progress_ops : &animated_ops;
    result = nt_tree_mount(tree,parent,ops,state,mount,&state->node);
    if (result == NT_RESULT_OK) {
        if (kind == PROGRESS || kind == SCROLLBAR)
            nt_property_stock_table(tree, state->node,
                                        kind == PROGRESS ? &progress_property : &scrollbar_property, 1);
        *out = state->node;
    } else {
        tree->busy = true;
        dispose_indicator(state);
        tree->busy = false;
    }
    return result;
}

static NtResult get_indicator(const NtTree *tree, NtNodeId node, IndicatorKind kind, Indicator **out)
{
    void *value;
    const NtResult result = nt_control_lookup(tree,node,&progress_ops,&value);
    if (result != NT_RESULT_OK) return result;
    Indicator *state = value;
    if (state->kind != kind) return NT_RESULT_INVALID_ARGUMENT;
    *out = state;
    return NT_RESULT_OK;
}

static NtResult set_indicator(NtTree *tree, NtNodeId node, IndicatorKind kind, IndicatorOptions options)
{
    if (!tree) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    Indicator *state;
    NtResult result = get_indicator(tree,node,kind,&state);
    if (result != NT_RESULT_OK) return result;
    result = normalize(kind,&options);
    if (result != NT_RESULT_OK) return result;
    const bool rotates = kind == PROGRESS ? state->options.progress.vertical != options.progress.vertical :
        kind == SCROLLBAR && state->options.scrollbar.vertical != options.scrollbar.vertical;
    if (kind == CURSOR || kind == WAIT) {
        tree->nodes[node.slot].updated = false;
        state->phase = 0;
    }
    state->options = options;
    nt_tree_dirty(tree,rotates ? NT_INVALIDATE_LAYOUT : NT_INVALIDATE_PAINT);
    return NT_RESULT_OK;
}

NtResult nt_cursor_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount,
                         NtCursorOptions options, NtNodeId *out)
{
    return mount_indicator(tree,parent,mount,CURSOR,(IndicatorOptions){.cursor=options},out);
}

NtResult nt_cursor_set(NtTree *tree, NtNodeId node, NtCursorOptions options)
{
    return set_indicator(tree,node,CURSOR,(IndicatorOptions){.cursor=options});
}

NtResult nt_wait_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount, NtWaitOptions options, NtNodeId *out)
{
    return mount_indicator(tree,parent,mount,WAIT,(IndicatorOptions){.wait=options},out);
}

NtResult nt_wait_set(NtTree *tree, NtNodeId node, NtWaitOptions options)
{
    return set_indicator(tree,node,WAIT,(IndicatorOptions){.wait=options});
}

NtResult nt_progress_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount, NtProgressOptions options,
                           NtNodeId *out)
{
    return mount_indicator(tree,parent,mount,PROGRESS,(IndicatorOptions){.progress=options},out);
}

NtResult nt_progress_set(NtTree *tree, NtNodeId node, NtProgressOptions options)
{
    return set_indicator(tree,node,PROGRESS,(IndicatorOptions){.progress=options});
}

NtResult nt_progress_get(const NtTree *tree, NtNodeId node, NtProgressOptions *out)
{
    if (!out) return NT_RESULT_INVALID_ARGUMENT;
    Indicator *state;
    const NtResult result = get_indicator(tree,node,PROGRESS,&state);
    if (result == NT_RESULT_OK) *out = state->options.progress;
    return result;
}

NtResult nt_scrollbar_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount, NtScrollBarOptions options,
                            NtNodeId *out)
{
    return mount_indicator(tree,parent,mount,SCROLLBAR,(IndicatorOptions){.scrollbar=options},out);
}

NtResult nt_scrollbar_set(NtTree *tree, NtNodeId node, NtScrollBarOptions options)
{
    return set_indicator(tree,node,SCROLLBAR,(IndicatorOptions){.scrollbar=options});
}

NtResult nt_scrollbar_get(const NtTree *tree, NtNodeId node, NtScrollBarOptions *out)
{
    if (!out) return NT_RESULT_INVALID_ARGUMENT;
    Indicator *state;
    const NtResult result = get_indicator(tree,node,SCROLLBAR,&state);
    if (result == NT_RESULT_OK) *out = state->options.scrollbar;
    return result;
}
