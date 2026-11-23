#ifndef NOCTURNE_CONTROLS_H
#define NOCTURNE_CONTROLS_H

#include <nocturne/editor.h>
#include <nocturne/widgets.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { NT_INTENT_CONTROL = 0x4e430001 };
typedef enum NtControlEventKind {
    NT_CONTROL_ACTIVATED, NT_CONTROL_ITEM_CHANGED, NT_CONTROL_CHECKBOX_CHANGED,
    NT_CONTROL_CHOICE_CHANGED, NT_CONTROL_SCROLL_CHANGED, NT_CONTROL_EDITOR_CHANGED,
    NT_CONTROL_EDITOR_SUBMITTED,
} NtControlEventKind;
typedef struct NtControlEvent { NtControlEventKind kind; uint64_t value; } NtControlEvent;
NtResult nt_control_event_decode(NtIntentView intent, NtControlEvent *out);

typedef struct NtControlStyle {
    NtCellStyle normal, focused, engaged;
    NtWidthPolicy width;
} NtControlStyle;
NtControlStyle nt_control_style_default(void);

/* Label views are copied. NULL style uses the stock palette. User actions
 * emit typed intents. Programmatic setters only change state and invalidate. */
NtResult nt_button_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount,
                         NtByteView label, const NtControlStyle *style, NtNodeId *out);
NtResult nt_button_pressed(const NtTree *tree, NtNodeId node, bool *out);
NtResult nt_item_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount,
                       NtByteView label, const NtControlStyle *style, bool selected, NtNodeId *out);
NtResult nt_item_get(const NtTree *tree, NtNodeId node, bool *out);
NtResult nt_item_set(NtTree *tree, NtNodeId node, bool selected);
NtResult nt_checkbox_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount,
                           NtByteView label, const NtControlStyle *style, bool checked, NtNodeId *out);
NtResult nt_checkbox_get(const NtTree *tree, NtNodeId node, bool *out);
NtResult nt_checkbox_set(NtTree *tree, NtNodeId node, bool checked);

#define NT_CHOICE_NONE SIZE_MAX
typedef enum NtChoiceKind { NT_CHOICE_LIST, NT_CHOICE_RADIO, NT_CHOICE_SELECT } NtChoiceKind;
typedef struct NtChoiceEntry { NtByteView label; bool disabled; } NtChoiceEntry;
typedef struct NtChoiceOptions {
    NtChoiceKind kind;
    const NtChoiceEntry *entries;
    size_t count, selected;
    uint32_t visible_rows;       /* Zero selects all available rows. */
    const NtControlStyle *style;
} NtChoiceOptions;
typedef struct NtChoiceState { size_t selected; bool expanded; } NtChoiceState;
/* A list or radio group displays a bounded window. A select can expand
 * inside its assigned bounds. The tree copies the array and labels. */
NtResult nt_choice_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount,
                         const NtChoiceOptions *options, NtNodeId *out);
NtResult nt_choice_configure(NtTree *tree, NtNodeId node, const NtChoiceOptions *options);
NtResult nt_choice_get(const NtTree *tree, NtNodeId node, NtChoiceState *out);
NtResult nt_choice_set(NtTree *tree, NtNodeId node, size_t selected);

typedef struct NtCursorOptions { NtCellStyle style; NtTimeUs phase_us; bool visible; } NtCursorOptions;
typedef struct NtWaitOptions { NtCellStyle style; NtTimeUs phase_us; bool ascii; } NtWaitOptions;
typedef struct NtProgressOptions {
    uint32_t value, maximum;
    NtCellStyle filled, remaining;
    bool vertical;
} NtProgressOptions;
typedef struct NtScrollBarOptions {
    uint32_t content, viewport, offset;
    NtCellStyle thumb, track;
    bool vertical;
} NtScrollBarOptions;
/* Cursor and wait use caller ticks. A zero phase leaves them steady. */
NtResult nt_cursor_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount,
                         NtCursorOptions options, NtNodeId *out);
NtResult nt_cursor_set(NtTree *tree, NtNodeId node, NtCursorOptions options);
NtResult nt_wait_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount,
                       NtWaitOptions options, NtNodeId *out);
NtResult nt_wait_set(NtTree *tree, NtNodeId node, NtWaitOptions options);
NtResult nt_progress_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount,
                           NtProgressOptions options, NtNodeId *out);
NtResult nt_progress_set(NtTree *tree, NtNodeId node, NtProgressOptions options);
NtResult nt_progress_get(const NtTree *tree, NtNodeId node, NtProgressOptions *out);
NtResult nt_scrollbar_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount,
                            NtScrollBarOptions options, NtNodeId *out);
NtResult nt_scrollbar_set(NtTree *tree, NtNodeId node, NtScrollBarOptions options);
NtResult nt_scrollbar_get(const NtTree *tree, NtNodeId node, NtScrollBarOptions *out);

typedef struct NtEditorViewOptions {
    NtEditor *editor;
    const NtEditorKeyMap *keys;
    const NtControlStyle *style;
    NtWrapMode wrap;
    bool follow_caret, show_caret, submit_on_enter, scroll_keys;
} NtEditorViewOptions;
/* The editor and optional register bank are borrowed for the mount's
 * lifetime. Key bindings and styles are copied. Frame using a parent panel. */
NtResult nt_editor_view_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount,
                              const NtEditorViewOptions *options, NtNodeId *out);
NtResult nt_editor_view_scroll(NtTree *tree, NtNodeId node, NtCellSize requested);
NtResult nt_editor_view_offset(const NtTree *tree, NtNodeId node, NtCellSize *out);

#ifdef __cplusplus
}
#endif
#endif
