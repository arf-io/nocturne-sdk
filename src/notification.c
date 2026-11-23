#include <nocturne/notification.h>

#include "controls_internal.h"
#include "surface_internal.h"

typedef struct NoticeAction { NtText *label; uint64_t id; bool disabled; } NoticeAction;
typedef struct NoticeContent {
    NtText *title, *message, *dismiss_label;
    NoticeAction actions[NT_NOTIFICATION_MAX_ACTIONS];
    size_t action_count;
    uint64_t id;
    NtTimeUs expires;
    uint32_t columns, message_rows;
    NtControlStyle style;
    bool dismissible, dismiss_on_action;
} NoticeContent;

typedef struct Notice {
    NtAllocator allocator;
    NtNodeId mount;
    NoticeContent *content;
    NtNotificationStatus status;
    uint64_t revision, selected;
    NtControlPress press;
    size_t held;
    bool enabled;
} Notice;
typedef struct NoticePaint { NtAllocator allocator; uint64_t revision; } NoticePaint;

NT_DECLARE_ALIGNMENT(NoticeContent);
NT_DECLARE_ALIGNMENT(Notice);
NT_DECLARE_ALIGNMENT(NoticePaint);

#define NO_ACTION SIZE_MAX

NtNotificationOptions nt_notification_options_default(void)
{
    return (NtNotificationOptions){.columns=32,.message_rows=2,.style=nt_control_style_default(),
                                    .dismissible=true,.dismiss_label={"Dismiss",7}};
}

static void release_content(const NtAllocator *allocator, NoticeContent *content)
{
    if (!content) return;
    nt_text_destroy(content->title);
    nt_text_destroy(content->message);
    nt_text_destroy(content->dismiss_label);
    for (size_t i = 0; i < content->action_count; ++i) nt_text_destroy(content->actions[i].label);
    nt_release(allocator,content,sizeof(*content),NT_ALIGNMENT(NoticeContent));
}

static NtResult copy_text(const NtAllocator *allocator, NtByteView value, NtText **out)
{
    return value.count ? nt_text_create(nt_control_allocator(allocator),value,out) : NT_RESULT_OK;
}

static NtResult make_content(const NtAllocator *allocator, const NtNotificationOptions *options, NoticeContent **out)
{
    if (!options || !options->id || !nt_control_style_valid(options->style) ||
        (options->action_count && !options->actions)) return NT_RESULT_INVALID_ARGUMENT;
    if (options->action_count > NT_NOTIFICATION_MAX_ACTIONS ||
        options->message_rows > NT_NOTIFICATION_MAX_MESSAGE_ROWS || options->columns > INT32_MAX)
        return NT_RESULT_OUT_OF_RANGE;
    if ((options->message.count && !options->message_rows) ||
        (options->dismissible && !options->dismiss_label.count)) return NT_RESULT_INVALID_ARGUMENT;
    for (size_t i = 0; i < options->action_count; ++i) {
        const NtNotificationAction action = options->actions[i];
        if (!action.id || !action.label.count) return NT_RESULT_INVALID_ARGUMENT;
        for (size_t j = 0; j < i; ++j)
            if (options->actions[j].id == action.id) return NT_RESULT_INVALID_ARGUMENT;
    }

    NoticeContent *content = nt_allocate(allocator,sizeof(*content),NT_ALIGNMENT(NoticeContent));
    if (!content) return NT_RESULT_ALLOCATION_FAILED;
    *content = (NoticeContent){
        .action_count=options->action_count,.id=options->id,.expires=options->expires_at,
        .columns=options->columns,.message_rows=options->message.count ? options->message_rows : 0,
        .style=options->style,.dismissible=options->dismissible,.dismiss_on_action=options->dismiss_on_action
    };
    NtResult result = copy_text(allocator,options->title,&content->title);
    if (result == NT_RESULT_OK) result = copy_text(allocator,options->message,&content->message);
    if (result == NT_RESULT_OK && options->dismissible)
        result = copy_text(allocator,options->dismiss_label,&content->dismiss_label);
    for (size_t i = 0; result == NT_RESULT_OK && i < content->action_count; ++i) {
        const NtNotificationAction action = options->actions[i];
        content->actions[i] = (NoticeAction){NULL,action.id,action.disabled};
        result = copy_text(allocator,action.label,&content->actions[i].label);
    }
    if (result == NT_RESULT_OK) *out = content;
    else release_content(allocator,content);
    return result;
}

static void dispose_notice(void *value)
{
    Notice *notice = value;
    const NtAllocator allocator = notice->allocator;
    release_content(&allocator,notice->content);
    nt_release(&allocator,notice,sizeof(*notice),NT_ALIGNMENT(Notice));
}

static uint32_t action_start(const NoticeContent *content)
{
    return (content->title != NULL) + content->message_rows;
}

static size_t actions_shown(const NoticeContent *content)
{
    return content->action_count + content->dismissible;
}

static bool active(const Notice *notice) { return notice->status == NT_NOTIFICATION_ACTIVE; }

static NtResult measure_notice(const void *value, NtMeasureContext *context,
                                NtConstraints bounds, NtCellSize *out)
{
    if (nt_measure_child_count(context)) return NT_RESULT_INVALID_ARGUMENT;
    const Notice *notice = value;
    const NoticeContent *content = notice->content;
    const NtCellSize desired = active(notice) ?
        (NtCellSize){content->columns,action_start(content)+(uint32_t)actions_shown(content)} : (NtCellSize){0};
    *out = nt_control_fit(desired,bounds);
    return NT_RESULT_OK;
}

static void state_changed(Notice *notice, NtNotificationStatus status)
{
    NtTree *tree = (NtTree *)notice->mount.tree;
    NtNode *node = &tree->nodes[notice->mount.slot];
    notice->status = status;
    notice->press = (NtControlPress){0};
    notice->held = NO_ACTION;
    node->options.enabled = active(notice) && notice->enabled;
    node->deadline = (NtDeadline){active(notice) && notice->content->expires != 0,notice->content->expires};
}

static bool expires_now(const Notice *notice, NtTimeUs now)
{
    return active(notice) && notice->content->expires && now >= notice->content->expires;
}

static NtResult update_notice(void *value, NtUpdateInfo info, NtUpdateReply *reply)
{
    Notice *notice = value;
    if (expires_now(notice,info.now)) {
        state_changed(notice,NT_NOTIFICATION_EXPIRED);
        reply->invalidate = NT_INVALIDATE_LAYOUT;
    } else if (active(notice) && notice->content->expires)
        reply->next = (NtDeadline){true,notice->content->expires};
    return NT_RESULT_OK;
}

static void dispose_notice_paint(void *value)
{
    NoticePaint *paint = value;
    const NtAllocator allocator = paint->allocator;
    nt_release(&allocator,paint,sizeof(*paint),NT_ALIGNMENT(NoticePaint));
}

static NtResult paint_message(const Notice *notice, NtPaintContext *paint, uint32_t x,
                              uint32_t columns, uint32_t rows, NtCellStyle colors)
{
    const NoticeContent *content = notice->content;
    if (!content->message || !columns || !rows) return NT_RESULT_OK;
    const uint32_t y = content->title != NULL;
    if (rows <= y) return NT_RESULT_OK;
    const uint32_t available = rows-y < content->message_rows ? rows-y : content->message_rows;
    const NtConstraints bounds = {{0,columns,true},{0,available,true}};
    NtTextLayout *layout;
    NtResult result = nt_control_text_layout(&notice->allocator,content->message,content->style.width,
                                              NT_WRAP_WORD,bounds,false,&layout);
    if (result != NT_RESULT_OK) return result;
    const NtTextPaintOptions style = {.base={.color=colors}};
    result = nt_component_paint_text(paint,(NtCellPoint){(int32_t)x,(int32_t)y},layout,&style);
    nt_text_layout_destroy(layout);
    return result;
}

static NtResult paint_actions(const Notice *notice, NtPaintContext *paint, NtCellSize size,
                              uint32_t x, uint32_t columns, NtCellStyle colors)
{
    const NoticeContent *content = notice->content;
    const uint32_t start = action_start(content);
    if (!columns) return NT_RESULT_OK;
    for (size_t i = 0; i < actions_shown(content) && start+i < size.rows; ++i) {
        const uint32_t row = start+(uint32_t)i;
        if (i == content->action_count) {
            const NtResult result = nt_control_draw_label(&notice->allocator,content->dismiss_label,
                                                          content->style.width,paint,x,row,columns,colors);
            if (result != NT_RESULT_OK) return result;
            continue;
        }
        const NoticeAction *action = &content->actions[i];
        NtCellStyle style = colors;
        if (action->disabled)
            style.foreground = (NtRgba){style.foreground.red/2,style.foreground.green/2,
                                        style.foreground.blue/2,style.foreground.alpha};
        else if (action->id == notice->selected ||
                 (notice->held == i && nt_control_pressed(notice->mount,notice->press))) {
            style = content->style.engaged;
            const NtResult result = nt_paint_fill(paint,(NtCellRect){{0,(int32_t)row},{size.columns,1}},style.background);
            if (result != NT_RESULT_OK) return result;
        }
        const NtResult result = nt_control_draw_label(&notice->allocator,action->label,content->style.width,
                                                      paint,x,row,columns,style);
        if (result != NT_RESULT_OK) return result;
    }
    return NT_RESULT_OK;
}

static NtResult paint_notice(const void *value, NtPaintContext *paint, NtCellSize size)
{
    const Notice *notice = value;
    if (!active(notice) || !size.columns || !size.rows) return NT_RESULT_OK;
    const NtCellRect clip = nt_paint_clip(paint);
    if (!clip.size.columns || !clip.size.rows) return NT_RESULT_OK;
    NoticePaint *published = nt_allocate(&notice->allocator,sizeof(*published),NT_ALIGNMENT(NoticePaint));
    if (!published) return NT_RESULT_ALLOCATION_FAILED;
    *published = (NoticePaint){notice->allocator,notice->revision};
    nt_component_attach_paint_data(paint,published,dispose_notice_paint);
    const NoticeContent *content = notice->content;
    const NtCellStyle colors = nt_control_colors(notice->mount,content->style,false);
    NtResult result = nt_paint_fill(paint,(NtCellRect){{0,0},size},colors.background);
    if (result != NT_RESULT_OK) return result;
    const uint32_t x = size.columns > 2 ? 1 : 0;
    const uint32_t columns = size.columns - 2*x;
    result = nt_control_draw_label(&notice->allocator,content->title,content->style.width,paint,x,0,columns,colors);
    if (result != NT_RESULT_OK) return result;
    result = paint_message(notice,paint,x,columns,size.rows,colors);
    if (result != NT_RESULT_OK) return result;
    return paint_actions(notice,paint,size,x,columns,colors);
}

static size_t pointer_action(const Notice *notice, NtEventContext *context, const NtEvent *event)
{
    NtCellPoint local;
    if (!nt_control_pointer_inside(notice->mount,&event->data.pointer) ||
        !nt_event_local_position(context,&local) || local.y < (int32_t)action_start(notice->content))
        return NO_ACTION;
    const size_t i = (size_t)local.y - action_start(notice->content);
    if (i >= actions_shown(notice->content)) return NO_ACTION;
    return i == notice->content->action_count || !notice->content->actions[i].disabled ? i : NO_ACTION;
}

static bool row_visible(const NtPlacement *place, uint32_t local)
{
    const NtCellRect clip = place->surface.clip;
    if (local >= place->surface.bounds.size.rows) return false;
    const int64_t y = (int64_t)place->surface.bounds.origin.y + local;
    if (y < clip.origin.y || y >= (int64_t)clip.origin.y+clip.size.rows) return false;
    const int64_t end = (int64_t)clip.origin.x+clip.size.columns;
    for (int64_t x = clip.origin.x; x < end; ++x)
        if (nt_surface_contains_frame(&place->surface,(NtCellPoint){(int32_t)x,(int32_t)y})) return true;
    return false;
}

static size_t key_action(const Notice *notice, const NtPlacement *place, bool reverse, bool advance)
{
    const NoticeContent *content = notice->content;
    size_t shown[NT_NOTIFICATION_MAX_ACTIONS], count = 0, selected = NO_ACTION;
    for (size_t i = 0; i < content->action_count; ++i) {
        const NoticeAction action = content->actions[i];
        if (action.disabled || !row_visible(place,action_start(content)+(uint32_t)i)) continue;
        if (action.id == notice->selected) selected = count;
        shown[count++] = i;
    }
    if (!count) return NO_ACTION;
    if (selected == NO_ACTION) return shown[reverse ? count-1 : 0];
    if (!advance) return shown[selected];
    return shown[reverse ? (selected+count-1)%count : (selected+1)%count];
}

static NtResult activate(Notice *notice, NtEventContext *context, size_t action, NtEventReply *reply)
{
    const NoticeContent *content = notice->content;
    const bool dismiss = action == content->action_count;
    const uint64_t id = dismiss ? 0 : content->actions[action].id;
    const NtIntentField fields[] = {
        {1,{.kind=NT_VALUE_U64,.as.u64=dismiss ? NT_NOTIFICATION_DISMISSED_EVENT : NT_NOTIFICATION_PERFORMED}},
        {2,{.kind=NT_VALUE_U64,.as.u64=content->id}},
        {3,{.kind=NT_VALUE_U64,.as.u64=id}}
    };
    const NtResult result = nt_event_emit_fields(context,NT_INTENT_NOTIFICATION,fields,3);
    if (result != NT_RESULT_OK) return result;
    if (dismiss || content->dismiss_on_action) {
        state_changed(notice,NT_NOTIFICATION_DISMISSED);
        reply->invalidate = NT_INVALIDATE_LAYOUT;
    } else {
        notice->selected = id;
        reply->invalidate = NT_INVALIDATE_PAINT;
    }
    reply->consume = true;
    return NT_RESULT_OK;
}

static NtResult event_notice(void *value, NtEventContext *context, const NtEvent *event, NtEventReply *reply)
{
    Notice *notice = value;
    if (nt_event_phase(context) != NT_EVENT_TARGET || !active(notice)) return NT_RESULT_OK;
    if (expires_now(notice,event->at)) {
        state_changed(notice,NT_NOTIFICATION_EXPIRED);
        reply->invalidate = NT_INVALIDATE_LAYOUT;
        return NT_RESULT_OK;
    }
    const NtPlacement *place = nt_tree_placement(notice->mount.tree,notice->mount);
    const NoticePaint *published = place ? place->paint_data : NULL;
    if (!published || published->revision != notice->revision) return NT_RESULT_OK;

    const uint32_t key = nt_control_key(event,false);
    if (key == NT_KEY_ESCAPE && notice->content->dismissible)
        return activate(notice,context,notice->content->action_count,reply);
    if (key == NT_KEY_UP || key == NT_KEY_DOWN) {
        const size_t index = key_action(notice,place,key == NT_KEY_UP,true);
        if (index != NO_ACTION) {
            notice->selected = notice->content->actions[index].id;
            reply->consume = true;
            reply->invalidate = NT_INVALIDATE_PAINT;
        }
        return NT_RESULT_OK;
    }
    if (key == NT_KEY_ENTER || key == ' ') {
        const size_t index = key_action(notice,place,false,false);
        return index == NO_ACTION ? NT_RESULT_OK : activate(notice,context,index,reply);
    }
    size_t target = NO_ACTION;
    if (event->kind == NT_EVENT_POINTER_DOWN || event->kind == NT_EVENT_POINTER_UP)
        target = pointer_action(notice,context,event);
    if (event->kind == NT_EVENT_POINTER_DOWN && target == NO_ACTION) return NT_RESULT_OK;
    const bool released = nt_control_press(notice->mount,&notice->press,event,reply);
    if (event->kind == NT_EVENT_POINTER_DOWN && reply->capture == NT_CAPTURE_TAKE) notice->held = target;
    if (released && target != NO_ACTION && target == notice->held)
        return activate(notice,context,target,reply);
    return NT_RESULT_OK;
}

static const NtComponentOps notice_ops = {
    measure_notice,nt_control_leaf,update_notice,paint_notice,event_notice,dispose_notice
};

NtResult nt_notification_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount,
                                 const NtNotificationOptions *options, NtNodeId *out)
{
    NtResult result = nt_control_validate_mount(tree,parent,out);
    if (result != NT_RESULT_OK) return result;
    tree->busy = true;
    Notice *notice = nt_allocate(&tree->allocator,sizeof(*notice),NT_ALIGNMENT(Notice));
    if (notice) {
        *notice = (Notice){.allocator=tree->allocator,.revision=1,.enabled=mount.enabled,.held=NO_ACTION};
        result = make_content(&tree->allocator,options,&notice->content);
    } else result = NT_RESULT_ALLOCATION_FAILED;
    tree->busy = false;
    if (result == NT_RESULT_OK) result = nt_tree_mount(tree,parent,&notice_ops,notice,mount,out);
    if (result == NT_RESULT_OK) {
        notice->mount = *out;
        state_changed(notice,expires_now(notice,tree->watermark) ? NT_NOTIFICATION_EXPIRED : NT_NOTIFICATION_ACTIVE);
    } else if (notice) {
        tree->busy = true;
        dispose_notice(notice);
        tree->busy = false;
    }
    return result;
}

static NtResult lookup(const NtTree *tree, NtNodeId node, Notice **out)
{
    void *state;
    const NtResult result = nt_control_lookup(tree,node,&notice_ops,&state);
    if (result == NT_RESULT_OK) *out = state;
    return result;
}

NtResult nt_notification_set(NtTree *tree, NtNodeId node, const NtNotificationOptions *options)
{
    if (!tree) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    Notice *notice;
    NtResult result = lookup(tree,node,&notice);
    if (result != NT_RESULT_OK) return result;
    if (notice->revision == UINT64_MAX) return NT_RESULT_OUT_OF_RANGE;
    tree->busy = true;
    NoticeContent *next;
    result = make_content(&tree->allocator,options,&next);
    if (result == NT_RESULT_OK) {
        release_content(&notice->allocator,notice->content);
        notice->content = next;
        ++notice->revision;
        notice->selected = 0;
        const bool expired = next->expires && tree->watermark >= next->expires;
        state_changed(notice,expired ? NT_NOTIFICATION_EXPIRED : NT_NOTIFICATION_ACTIVE);
        nt_tree_dirty(tree,NT_INVALIDATE_LAYOUT);
    }
    tree->busy = false;
    return result;
}

NtResult nt_notification_get(const NtTree *tree, NtNodeId node, NtNotificationState *out)
{
    if (!out) return NT_RESULT_INVALID_ARGUMENT;
    Notice *notice;
    const NtResult result = lookup(tree,node,&notice);
    if (result == NT_RESULT_OK)
        *out = (NtNotificationState){notice->content->id,notice->selected,notice->content->expires,
                                    notice->status};
    return result;
}

NtResult nt_notification_dismiss(NtTree *tree, NtNodeId node)
{
    if (!tree) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    Notice *notice;
    const NtResult result = lookup(tree,node,&notice);
    if (result == NT_RESULT_OK && active(notice)) {
        state_changed(notice,NT_NOTIFICATION_DISMISSED);
        nt_tree_dirty(tree,NT_INVALIDATE_LAYOUT);
    }
    return result;
}

NtResult nt_notification_event_decode(NtEventIntentView intent, NtNotificationEvent *out)
{
    if (!out || intent.intent.kind != NT_INTENT_NOTIFICATION || intent.payload_kind != NT_INTENT_FIELDS ||
        intent.field_count != 3 || !intent.fields) return NT_RESULT_INVALID_ARGUMENT;
    const NtIntentField *fields = intent.fields;
    for (uint32_t i = 0; i < 3; ++i)
        if (fields[i].id != i+1 || fields[i].value.kind != NT_VALUE_U64) return NT_RESULT_INVALID_ARGUMENT;
    const uint64_t kind = fields[0].value.as.u64, id = fields[1].value.as.u64, action = fields[2].value.as.u64;
    if (!id || (kind != NT_NOTIFICATION_PERFORMED && kind != NT_NOTIFICATION_DISMISSED_EVENT) ||
        (kind == NT_NOTIFICATION_PERFORMED) != (action != 0)) return NT_RESULT_INVALID_ARGUMENT;
    *out = (NtNotificationEvent){(NtNotificationEventKind)kind,id,action};
    return NT_RESULT_OK;
}
