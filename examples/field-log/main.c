#include <nocturne/controls.h>
#include <nocturne/posix_host.h>

#include "pulse.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum { NOTE_COUNT = 8, NOTE_BYTES = 1024, HELP_LINES = 10 };
static const char help_text[] =
    "Field notebook\n"
    "Tab moves focus.\n"
    "Type a field note.\n"
    "Enter saves the draft.\n"
    "Save now also saves.\n"
    "Arrows select notes.\n"
    "Up recalls a draft.\n"
    "Ctrl+Z undoes an edit.\n"
    "Page Down reads on.\n"
    "Ctrl+Q closes the log.";

static const NtNodeId root = {0};
static const NtMountOptions passive = {true,false,false};
static const NtMountOptions interactive = {true,true,true};
static const char key_help[] = "Tab changes focus  |  Ctrl+Q quits";

typedef struct Note { char text[NOTE_BYTES]; size_t length; } Note;

typedef struct FieldLog {
    NtTree *tree;
    NtEditor *draft, *help;
    NtNodeId entry, save, list, reader, scrollbar, progress, status;
    NtControlStyle style;
    Pulse pulse;
    Note notes[NOTE_COUNT];
    size_t count;
    char notice[64], shown_status[128];
    bool monochrome, quit;
} FieldLog;

static NtByteView bytes(const char *text)
{
    return (NtByteView){text, strlen(text)};
}

static NtLabelOptions label_style(const FieldLog *app, NtByteView text)
{
    return (NtLabelOptions){.text=text,.style={.width=app->style.width,.tab_stop=4,
        .direction=NT_DIRECTION_LTR,.ellipsis=true,.color=app->style.normal}};
}

static NtResult mount_label(FieldLog *app, NtNodeId parent, const char *text, NtNodeId *out)
{
    const NtLabelOptions options = label_style(app, bytes(text));
    return nt_label_mount(app->tree, parent, passive, &options, out);
}

static bool same_node(NtNodeId a, NtNodeId b)
{
    return a.tree == b.tree && a.slot == b.slot && a.generation == b.generation;
}

static size_t draft_cursor(const FieldLog *app)
{
    const NtText *text = nt_editor_text(app->draft);
    const size_t offset = nt_editor_selection(app->draft).active.offset;
    size_t first = 0, end = nt_text_cluster_count(text);
    /* Show the logical insertion position in graphemes, including after the last one. */
    while (first < end) {
        const size_t middle = first + (end - first)/2;
        NtTextCluster cluster;
        (void)nt_text_cluster(text,middle,&cluster);
        if (cluster.source.end <= offset) first = middle + 1;
        else end = middle;
    }
    return first + 1;
}

static NtResult sync_status(FieldLog *app)
{
    const char *notice = app->notice[0] ? app->notice : key_help;
    char display[sizeof(app->shown_status)];
    if (app->monochrome) {
        const NtNodeId focus = nt_tree_focus(app->tree);
        if (same_node(focus,app->entry)) {
            snprintf(display,sizeof(display),"Focus Draft  |  Cursor %zu  |  %s",draft_cursor(app),notice);
        } else {
            const char *name = "none";
            if (same_node(focus,app->save)) name = "Save now";
            else if (same_node(focus,app->list)) name = "Saved notes";
            else if (same_node(focus,app->reader)) name = "Field guide";
            else if (same_node(focus,app->scrollbar)) name = "Guide scrollbar";
            snprintf(display,sizeof(display),"Focus %s  |  %s",name,notice);
        }
    } else {
        snprintf(display,sizeof(display),"%s",notice);
    }
    if (!strcmp(display,app->shown_status)) return NT_RESULT_OK;
    const NtLabelOptions options = label_style(app,bytes(display));
    const NtResult result = nt_label_set(app->tree,app->status,&options);
    if (result == NT_RESULT_OK) memcpy(app->shown_status,display,strlen(display)+1);
    return result;
}

static NtResult status(FieldLog *app, const char *text)
{
    snprintf(app->notice,sizeof(app->notice),"%s",text);
    return sync_status(app);
}

static NtResult mount_panel(FieldLog *app, NtNodeId parent, const char *title, NtNodeId *out)
{
    const NtPanelOptions options = {.framed=true,.ascii=true,.border=app->style.focused,
                                    .title=label_style(app,bytes(title))};
    return nt_panel_mount(app->tree, parent, passive, &options, out);
}

static NtChoiceOptions choices(const FieldLog *app, NtChoiceEntry *entries, size_t selected)
{
    for (size_t i = 0; i < app->count; ++i)
        entries[i] = (NtChoiceEntry){{app->notes[i].text,app->notes[i].length},false};
    return (NtChoiceOptions){NT_CHOICE_LIST,entries,app->count,selected,0,&app->style};
}

static NtProgressOptions capacity(const FieldLog *app)
{
    return (NtProgressOptions){(uint32_t)app->count,NOTE_COUNT,app->style.engaged,app->style.normal,false};
}

static NtResult mount_header(FieldLog *app, NtNodeId layout)
{
    const NtBoxItem columns[] = {{.grow_columns=true},{.columns={7,7,true}}};
    const NtBoxLayout row = {.kind=NT_BOX_LAYOUT_ROW,.items=columns,.item_count=2};
    NtNodeId header, title, pulse;
    NtResult result = nt_box_layout_mount(app->tree, layout, passive, &row, &header);
    if (result == NT_RESULT_OK) result = mount_label(app,header,"Field log",&title);
    if (result == NT_RESULT_OK)
        result = nt_tree_mount(app->tree,header,&pulse_ops,&app->pulse,passive,&pulse);
    return result;
}

static NtResult mount_entry(FieldLog *app, NtNodeId layout)
{
    const NtEditorBinding bindings[] = {
        {{NT_KEY_LEFT,0}, NT_EDITOR_ACTION_MOVE, NT_EDITOR_PREVIOUS_CLUSTER, 0,false,true},
        {{NT_KEY_RIGHT,0}, NT_EDITOR_ACTION_MOVE, NT_EDITOR_NEXT_CLUSTER, 0,false,true},
        {{NT_KEY_LEFT,NT_MOD_SHIFT}, NT_EDITOR_ACTION_MOVE, NT_EDITOR_PREVIOUS_CLUSTER, 0,true,true},
        {{NT_KEY_RIGHT,NT_MOD_SHIFT}, NT_EDITOR_ACTION_MOVE, NT_EDITOR_NEXT_CLUSTER, 0,true,true},
        {{NT_KEY_HOME,0}, NT_EDITOR_ACTION_MOVE, NT_EDITOR_DOCUMENT_START, 0,false,false},
        {{NT_KEY_END,0}, NT_EDITOR_ACTION_MOVE, NT_EDITOR_DOCUMENT_END, 0,false,false},
        {{NT_KEY_BACKSPACE,0}, NT_EDITOR_ACTION_DELETE_PREVIOUS, 0, 0,false,true},
        {{NT_KEY_DELETE,0}, NT_EDITOR_ACTION_DELETE_NEXT, 0, 0,false,true},
        {{'z',NT_MOD_CONTROL}, NT_EDITOR_ACTION_UNDO, 0, 0,false,false},
        {{'y',NT_MOD_CONTROL}, NT_EDITOR_ACTION_REDO, 0, 0,false,false},
        {{NT_KEY_UP,0}, NT_EDITOR_ACTION_HISTORY_OLDER, 0, 0,false,false},
        {{NT_KEY_DOWN,0}, NT_EDITOR_ACTION_HISTORY_NEWER, 0, 0,false,false},
    };
    const NtEditorKeyMap keys = {bindings,sizeof(bindings)/sizeof(*bindings),NULL};
    const NtEditorViewOptions view = {
        .editor=app->draft,.keys=&keys,.style=&app->style,.wrap=NT_WRAP_NONE,
        .follow_caret=true,.show_caret=true,.submit_on_enter=true,
    };
    NtNodeId panel;
    NtResult result = mount_panel(app, layout, "New note", &panel);
    if (result == NT_RESULT_OK)
        result = nt_editor_view_mount(app->tree,panel,interactive,&view,&app->entry);
    if (result == NT_RESULT_OK)
        result = nt_button_mount(app->tree,layout,interactive,bytes("Save now"),&app->style,&app->save);
    return result;
}

static NtResult mount_body(FieldLog *app, NtNodeId layout)
{
    const NtBoxItem pane_columns[] = {
        {.columns={.min=15},.grow_columns=true,.grow_rows=true},
        {.columns={25,25,true},.grow_rows=true},
    };
    const NtBoxLayout body_options = {.kind=NT_BOX_LAYOUT_ROW,.gap=1,.items=pane_columns,.item_count=2};
    const NtBoxItem reader_columns[] = {{.grow_columns=true,.grow_rows=true},
                                       {.columns={1,1,true},.grow_rows=true}};
    const NtBoxLayout reader_options = {.kind=NT_BOX_LAYOUT_ROW,.items=reader_columns,.item_count=2};
    NtNodeId body, list_panel, help_panel, reader_row;
    NtChoiceEntry entries[NOTE_COUNT];
    const NtChoiceOptions initial = choices(app,entries,0);
    const NtScrollBarOptions scrollbar = {HELP_LINES,1,0,app->style.engaged,app->style.normal,true};
    const NtEditorViewOptions reader = {
        .editor=app->help,.style=&app->style,.wrap=NT_WRAP_NONE,.scroll_keys=true,
    };
    NtResult result = nt_box_layout_mount(app->tree,layout,passive,&body_options,&body);
    if (result == NT_RESULT_OK) result = mount_panel(app,body,"Saved notes",&list_panel);
    if (result == NT_RESULT_OK)
        result = nt_choice_mount(app->tree,list_panel,interactive,&initial,&app->list);
    if (result == NT_RESULT_OK) result = mount_panel(app,body,"Field guide",&help_panel);
    if (result == NT_RESULT_OK)
        result = nt_box_layout_mount(app->tree,help_panel,passive,&reader_options,&reader_row);
    if (result == NT_RESULT_OK)
        result = nt_editor_view_mount(app->tree,reader_row,interactive,&reader,&app->reader);
    if (result == NT_RESULT_OK)
        result = nt_scrollbar_mount(app->tree,reader_row,interactive,scrollbar,&app->scrollbar);
    return result;
}

static NtResult build_log(FieldLog *app)
{
    const NtEditorOptions entry = {NT_EDITOR_SINGLE_LINE,false};
    const NtEditorOptions reader = {NT_EDITOR_MULTILINE,true};
    const NtBoxItem rows[] = {
        {.rows={1,1,true},.grow_columns=true}, {.rows={1,1,true},.grow_columns=true},
        {.rows={3,3,true},.grow_columns=true}, {.rows={1,1,true}},
        {.rows={.min=3},.grow_columns=true,.grow_rows=true},
        {.rows={1,1,true},.grow_columns=true}, {.rows={1,1,true},.grow_columns=true},
    };
    const NtBoxLayout column = {.kind=NT_BOX_LAYOUT_COLUMN,.items=rows,.item_count=sizeof(rows)/sizeof(*rows)};
    const NtRuleOptions rule = {.ascii=true,.color=app->style.normal};
    NtNodeId layout, separator;
    NtResult result = nt_tree_create(NULL,&app->tree);
    if (result == NT_RESULT_OK) result = nt_editor_create(NULL,(NtByteView){0},&entry,&app->draft);
    if (result == NT_RESULT_OK) result = nt_editor_create(NULL,bytes(help_text),&reader,&app->help);
    if (result == NT_RESULT_OK) result = nt_editor_set_undo_limit(app->draft,32);
    if (result == NT_RESULT_OK) result = nt_editor_set_history_limit(app->draft,NOTE_COUNT);
    if (result == NT_RESULT_OK) result = nt_box_layout_mount(app->tree,root,passive,&column,&layout);
    if (result == NT_RESULT_OK) result = mount_header(app,layout);
    if (result == NT_RESULT_OK) result = nt_rule_mount(app->tree,layout,passive,rule,&separator);
    if (result == NT_RESULT_OK) result = mount_entry(app,layout);
    if (result == NT_RESULT_OK) result = mount_body(app,layout);
    if (result == NT_RESULT_OK)
        result = nt_progress_mount(app->tree,layout,passive,capacity(app),&app->progress);
    if (result == NT_RESULT_OK)
        result = mount_label(app,layout,key_help,&app->status);
    if (result == NT_RESULT_OK) memcpy(app->shown_status,key_help,sizeof(key_help));
    return result;
}

static NtResult save_note(FieldLog *app)
{
    const NtByteView draft = nt_text_bytes(nt_editor_text(app->draft));
    if (!draft.count) return status(app,"Type a note before saving.");
    if (app->count == NOTE_COUNT) return status(app,"This field log is full.");
    if (draft.count > NOTE_BYTES) return status(app,"Use at most 1024 UTF-8 bytes per note.");

    Note *slot = &app->notes[app->count];
    memcpy(slot->text,draft.data,draft.count);
    slot->length = draft.count;
    ++app->count;
    NtChoiceEntry entries[NOTE_COUNT];
    const NtChoiceOptions next = choices(app,entries,app->count-1);
    NtResult result = nt_choice_configure(app->tree,app->list,&next);
    if (result == NT_RESULT_OK) result = nt_progress_set(app->tree,app->progress,capacity(app));
    if (result == NT_RESULT_OK) result = nt_editor_history_add(app->draft,draft);
    if (result == NT_RESULT_OK) result = nt_editor_move(app->draft,NT_EDITOR_DOCUMENT_START,false);
    if (result == NT_RESULT_OK) result = nt_editor_move(app->draft,NT_EDITOR_DOCUMENT_END,true);
    if (result == NT_RESULT_OK) result = nt_editor_replace_selection(app->draft,(NtByteView){0});
    if (result == NT_RESULT_OK) result = nt_tree_invalidate(app->tree,app->entry,NT_INVALIDATE_LAYOUT);
    if (result == NT_RESULT_OK) result = status(app,"Note saved.");
    return result;
}

static NtResult apply_intents(void *context, const NtDispatchResult *dispatch)
{
    (void)dispatch;
    FieldLog *app = context;
    NtIntentView intent;
    while (nt_tree_peek_intent(app->tree,&intent)) {
        NtControlEvent event;
        const NtResult decoded = nt_control_event_decode(intent,&event);
        const NtNodeId source = intent.source;
        nt_tree_pop_intent(app->tree);
        if (decoded != NT_RESULT_OK) continue;
        NtResult result = NT_RESULT_OK;
        if ((same_node(source,app->entry) && event.kind == NT_CONTROL_EDITOR_SUBMITTED) ||
            (same_node(source,app->save) && event.kind == NT_CONTROL_ACTIVATED)) {
            result = save_note(app);
        } else if (same_node(source,app->list) && event.kind == NT_CONTROL_CHOICE_CHANGED) {
            char message[48];
            snprintf(message,sizeof(message),"Selected note %zu.",(size_t)event.value+1);
            result = status(app,message);
        } else if (same_node(source,app->scrollbar) && event.kind == NT_CONTROL_SCROLL_CHANGED) {
            result = nt_editor_view_scroll(app->tree,app->reader,(NtCellSize){0,(uint32_t)event.value});
        }
        if (result != NT_RESULT_OK) return result;
    }
    return sync_status(app);
}

static NtResult sync_scrollbar(FieldLog *app)
{
    NtNodeGeometry geometry;
    NtResult result = nt_tree_geometry(app->tree,app->reader,&geometry);
    if (result != NT_RESULT_OK || !geometry.placed || !geometry.bounds.size.columns ||
        !geometry.bounds.size.rows) return result;
    NtCellSize offset;
    result = nt_editor_view_offset(app->tree,app->reader,&offset);
    if (result != NT_RESULT_OK) return result;
    NtScrollBarOptions state;
    result = nt_scrollbar_get(app->tree,app->scrollbar,&state);
    if (result != NT_RESULT_OK) return result;
    if (state.viewport == geometry.bounds.size.rows && state.offset == offset.rows) return NT_RESULT_OK;
    state.viewport = geometry.bounds.size.rows;
    state.offset = offset.rows;
    return nt_scrollbar_set(app->tree,app->scrollbar,state);
}

static NtResult quit_key(void *context, const NtEvent *event, NtByteView text)
{
    (void)text;
    FieldLog *app = context;
    if (event->data.key.state == NT_KEY_PRESS) app->quit = true;
    return NT_RESULT_OK;
}

static NtResult current_time(NtTimeUs *out)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC,&time) != 0) return NT_RESULT_TRANSPORT_FAILED;
    *out = (NtTimeUs)time.tv_sec * 1000000 + (NtTimeUs)time.tv_nsec / 1000;
    return NT_RESULT_OK;
}

static NtResult wait_input(NtPosixHost *host, NtTree *tree)
{
    if (nt_tree_pending(tree) != NT_INVALIDATE_NONE) return NT_RESULT_OK;
    NtTimeUs now, input_deadline;
    NtResult result = current_time(&now);
    if (result != NT_RESULT_OK) return result;
    const NtDeadline tick = nt_tree_next_update(tree);
    NtTimeUs wait = 50000; /* Observe resize even if no input wakes poll. */
    if (tick.scheduled) {
        if (tick.at <= now) return NT_RESULT_OK;
        if (tick.at - now < wait) wait = tick.at - now;
    }
    if (nt_posix_host_input_deadline(host,&input_deadline)) {
        if (input_deadline <= now) return NT_RESULT_OK;
        if (input_deadline - now < wait) wait = input_deadline - now;
    }
    struct pollfd input = {STDIN_FILENO,POLLIN,0};
    return poll(&input,1,(int)((wait+999)/1000)) < 0 && errno != EINTR ?
        NT_RESULT_TRANSPORT_FAILED : NT_RESULT_OK;
}

static NtResult run(FieldLog *app, NtPosixTerminalReport terminal, NtMotionPolicy motion, int *signal)
{
    NtRgba indexed_colors[NT_XTERM256_COLORS];
    NtPaletteView palette = {0};
    if (terminal.capabilities.color == NT_COLOR_INDEXED) {
        const NtResult result = nt_xterm256_palette(indexed_colors,NT_XTERM256_COLORS);
        if (result != NT_RESULT_OK) return result;
        palette = (NtPaletteView){indexed_colors,NT_XTERM256_COLORS};
    }
    NtPosixHost *host = NULL;
    const NtPosixHostOptions options = {
        .input_fd=STDIN_FILENO,.output_fd=STDOUT_FILENO,.capabilities=terminal.capabilities,
        .matte={14,21,29,255},.palette=palette,.motion=motion,.output_timeout_ms=250,.alternate_screen=true,
        .hide_cursor=true,.catch_signals=true,.reserved_key=quit_key,.reserved_context=app,
        .after_input=apply_intents,.input_context=app,
    };
    const NtKeyChord quit = {'q',NT_MOD_CONTROL};
    const NtDispatchPolicy policy = {&quit,1,true};
    NtTimeUs now;
    NtResult result = current_time(&now);
    if (result == NT_RESULT_OK) result = nt_posix_host_open(app->tree,now,NULL,&options,&host);
    bool initial_focus = false;
    while (result == NT_RESULT_OK && !app->quit && !*signal) {
        result = current_time(&now);
        if (result != NT_RESULT_OK) break;
        NtPosixStep step;
        result = nt_posix_host_step(host,now,&policy,&step);
        if (result != NT_RESULT_OK) break;
        *signal = step.signal_number;
        if (*signal || app->quit) break;
        if (!initial_focus) {
            NtNodeGeometry geometry;
            result = nt_tree_geometry(app->tree,app->entry,&geometry);
            if (result == NT_RESULT_OK && geometry.clip.size.columns && geometry.clip.size.rows) {
                result = nt_tree_set_focus(app->tree,app->entry);
                initial_focus = result == NT_RESULT_OK;
                if (initial_focus) result = sync_status(app);
            }
        } else if (step.resized) {
            result = sync_status(app);
        }
        if (result == NT_RESULT_OK && step.presented) result = sync_scrollbar(app);
        if (result == NT_RESULT_OK) result = wait_input(host,app->tree);
    }
    if (host) {
        const NtResult cleanup = nt_posix_host_close(host);
        nt_posix_host_destroy(host);
        if (result == NT_RESULT_OK) result = cleanup;
    }
    return result;
}

int main(int argc, char **argv)
{
    if (argc > 2 || (argc == 2 && strcmp(argv[1],"--reduced-motion"))) {
        fprintf(stderr,"usage: field_log [--reduced-motion]\n");
        return EXIT_FAILURE;
    }
    NtPosixTerminalReport terminal;
    NtResult result = nt_posix_terminal_probe(STDIN_FILENO,STDOUT_FILENO,NULL,&terminal);
    if (result != NT_RESULT_OK || !terminal.host_available) {
        fprintf(stderr,"field_log requires a terminal on stdin and stdout\n");
        return EXIT_FAILURE;
    }
    FieldLog app = {.notes={{"Survey marker", 13},{"North ridge",11},{"Shelter check",13}},.count=3};
    app.monochrome = terminal.capabilities.color == NT_COLOR_MONO;
    app.style = nt_control_style_default();
    app.style.width = terminal.capabilities.width;
    app.pulse.colors = app.style.focused;
    result = build_log(&app);
    int signal = 0;
    if (result == NT_RESULT_OK)
        result = run(&app,terminal,argc == 2 ? NT_MOTION_REDUCED : NT_MOTION_FULL,&signal);
    nt_tree_destroy(app.tree);
    nt_editor_destroy(app.help);
    nt_editor_destroy(app.draft);
    if (result != NT_RESULT_OK) {
        fprintf(stderr,"field_log stopped with Nocturne status %d\n",(int)result);
        return EXIT_FAILURE;
    }
    return signal ? 128+signal : EXIT_SUCCESS;
}
