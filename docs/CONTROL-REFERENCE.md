# Basic control reference

The public headers declare counted UTF-8, copied configuration, typed
values and the tree's ownership rules. Setters update live state and
request paint or layout when needed. For interactive widgets, use `NtMountOptions` to
choose whether they participate in key focus or pointer hit testing. All
control intents carry their source node and can be read with
`nt_control_event_decode` before the queue entry is popped.
[Typed events, ownership and errors](CONTRACTS.md#basic-controls-and-typed-intents)

| Component | Mount, state and defaults | Runnable consumer |
| --- | --- | --- |
| Label | `nt_label_mount`, `nt_label_set`. Copied text and explicit wrap, width, alignment and paint. Display-only. | `composition_consumer`, Field Log heading and status |
| Box layout | `nt_box_layout_mount`, `nt_box_layout_set`. Copied per-child overrides. Zero initialization gives a start-aligned intrinsic row. | `box_layout_consumer`, Field Log root and rows |
| Box or panel | `nt_panel_mount`, `nt_panel_set`. One child. A frame, title and child clip are explicit. | `composition_consumer`, Field Log editor and guide |
| Rule | `nt_rule_mount`, `nt_rule_set`. Horizontal unless requested vertical. ASCII and style are caller choices. Display-only. | `composition_consumer`, Field Log separator |
| Button | `nt_button_mount`, `nt_button_pressed`. Starts released. Enter, Space and captured primary click emit activation. | `controls_consumer`, Field Log Save button |
| Selectable item | `nt_item_mount`, `nt_item_get`, `nt_item_set`. Choose initial selected state. User activation toggles it. | `controls_consumer` |
| Checkbox | `nt_checkbox_mount`, `nt_checkbox_get`, `nt_checkbox_set`. Choose initial checked state. User activation toggles it. | `controls_consumer` |
| List and radio | `nt_choice_mount` with `NT_CHOICE_LIST` or `NT_CHOICE_RADIO`. Set or clear selection, skip disabled rows, and use `visible_rows=0` for the assigned window. | `controls_consumer`, Field Log saved list |
| Select | `nt_choice_mount` with `NT_CHOICE_SELECT`. Starts collapsed. Enter or Space toggles expansion and an enabled row click chooses it. | `controls_consumer` |
| Cursor | `nt_cursor_mount`, `nt_cursor_set`. Caller chooses visibility and interval. Zero interval is steady. | `controls_consumer` |
| Wait | `nt_wait_mount`, `nt_wait_set`. Caller chooses glyph set and interval. Reduced motion uses phase zero. | `controls_consumer` |
| Progress | `nt_progress_mount`, `nt_progress_get`, `nt_progress_set`. Caller supplies value and positive maximum. Horizontal unless requested vertical. | `controls_consumer`, Field Log saved capacity |
| Scrollbar | `nt_scrollbar_mount`, `nt_scrollbar_get`, `nt_scrollbar_set`. Caller supplies extent, viewport and axis. Offset clamps to the available range. Wire its typed changes to your content. | `controls_consumer`, Field Log guide track |
| Editor and reader | `nt_editor_view_mount`, `nt_editor_view_scroll`, `nt_editor_view_offset`. View borrows the buffer. Bind editing commands explicitly. A panel frames it. Optional scrolling keys work for readers. | `editor_consumer`, `editor_history_consumer`, `controls_consumer`, Field Log draft and guide |

`NtControlStyle` uses the stock palette when omitted. Programmatic
setters update state without generating a user-action intent. Validate a
new selection against the configured entries, and check every result
when constructing a tree. See the individual contracts for invalid inputs,
failed allocation behavior, and the lifecycle of borrowed state.
[Stock display contracts](CONTRACTS.md#stock-labels-boxes-panels-and-rules) · [Box layout](CONTRACTS.md#box-layouts) · [Basic controls](CONTRACTS.md#basic-controls-and-typed-intents)

## Run the fixtures against your installed SDK

From the repository root, set `sdk` to the installed prefix. In the alpha
bundle, the sources below live at `$sdk/share/nocturne/tests/consumer`.

```sh
fixture="$PWD/tests/consumer"
cmake -S "$fixture" -B ./control-fixtures -DCMAKE_PREFIX_PATH="$sdk"
cmake --build ./control-fixtures
ctest --test-dir ./control-fixtures -V -R '^(controls_consumer|editor_consumer|editor_history_consumer|composition_consumer|box_layout_consumer|routing_consumer)$'
```

The controls fixture prints the assertions covered in each section. It
drives real focus, intent, pointer and value changes through the installed
API. Display-only controls exercise paint, resizing and explicit time as
applicable. The terminal example drives the same mounted controls from a
live tty. [Controls fixture](../tests/consumer/controls.c) · [Terminal quickstart](QUICKSTART.md)
