package nocturne

/*
#include <nocturne/controls.h>
*/
import "C"

import (
	"runtime"
	"unsafe"
)

const ControlIntentKind uint32 = C.NT_INTENT_CONTROL

type ControlEventKind uint32

const (
	ControlActivated       ControlEventKind = C.NT_CONTROL_ACTIVATED
	ControlItemChanged     ControlEventKind = C.NT_CONTROL_ITEM_CHANGED
	ControlCheckboxChanged ControlEventKind = C.NT_CONTROL_CHECKBOX_CHANGED
	ControlChoiceChanged   ControlEventKind = C.NT_CONTROL_CHOICE_CHANGED
	ControlScrollChanged   ControlEventKind = C.NT_CONTROL_SCROLL_CHANGED
	ControlEditorChanged   ControlEventKind = C.NT_CONTROL_EDITOR_CHANGED
	ControlEditorSubmitted ControlEventKind = C.NT_CONTROL_EDITOR_SUBMITTED
)

type ControlEvent struct {
	Kind  ControlEventKind
	Value uint64
}

// DecodeControlIntent checks the kind and payload of a queued stock-control intent.
func DecodeControlIntent(intent Intent) (ControlEvent, error) {
	if err := ensureABI(); err != nil {
		return ControlEvent{}, err
	}
	native := C.NtIntentView{kind: C.uint32_t(intent.Kind), payload: nativeBytes(intent.Payload)}
	var out C.NtControlEvent
	err := check(C.nt_control_event_decode(native, &out))
	runtime.KeepAlive(intent.Payload)
	return ControlEvent{Kind: ControlEventKind(out.kind), Value: uint64(out.value)}, err
}

// ControlStyle sets the three stock interaction palettes. Zero widths
// select one column for ambiguous text and two for emoji.
type ControlStyle struct {
	Normal, Focused, Engaged Style
	AmbiguousWidth           uint8
	EmojiWidth               uint8
}

func (style *ControlStyle) native() *C.NtControlStyle {
	if style == nil {
		return nil
	}
	ambiguous, emoji := style.AmbiguousWidth, style.EmojiWidth
	if ambiguous == 0 {
		ambiguous = 1
	}
	if emoji == 0 {
		emoji = 2
	}
	return &C.NtControlStyle{
		normal: style.Normal.native(), focused: style.Focused.native(), engaged: style.Engaged.native(),
		width: C.NtWidthPolicy{ambiguous: C.NtWidthChoice(ambiguous), emoji: C.NtWidthChoice(emoji)},
	}
}

// MountButton copies the label and optional style into a stock button.
func (tree *Tree) MountButton(parent Node, options MountOptions, label string, style *ControlStyle) (Node, error) {
	return tree.mountNative(parent, func(tree *C.NtTree, parent C.NtNodeId, out *C.NtNodeId) C.NtResult {
		result := C.nt_button_mount(tree, parent, options.native(), nativeBytes(label), style.native(), out)
		runtime.KeepAlive(label)
		return result
	})
}

func (tree *Tree) ButtonPressed(node Node) (bool, error) {
	var pressed C.bool
	err := tree.callNode(node, func(native *C.NtTree, id C.NtNodeId) C.NtResult {
		return C.nt_button_pressed(native, id, &pressed)
	})
	return bool(pressed), err
}

// MountItem copies the label and style into a selectable item.
func (tree *Tree) MountItem(parent Node, options MountOptions, label string, selected bool, style *ControlStyle) (Node, error) {
	return tree.mountNative(parent, func(tree *C.NtTree, parent C.NtNodeId, out *C.NtNodeId) C.NtResult {
		result := C.nt_item_mount(tree, parent, options.native(), nativeBytes(label), style.native(), C.bool(selected), out)
		runtime.KeepAlive(label)
		return result
	})
}

func (tree *Tree) Item(node Node) (bool, error) {
	var selected C.bool
	err := tree.callNode(node, func(native *C.NtTree, id C.NtNodeId) C.NtResult {
		return C.nt_item_get(native, id, &selected)
	})
	return bool(selected), err
}

func (tree *Tree) SetItem(node Node, selected bool) error {
	return tree.callNode(node, func(native *C.NtTree, id C.NtNodeId) C.NtResult {
		return C.nt_item_set(native, id, C.bool(selected))
	})
}

// MountCheckbox copies the label and optional style into a stock checkbox.
func (tree *Tree) MountCheckbox(parent Node, options MountOptions, label string, checked bool, style *ControlStyle) (Node, error) {
	return tree.mountNative(parent, func(tree *C.NtTree, parent C.NtNodeId, out *C.NtNodeId) C.NtResult {
		result := C.nt_checkbox_mount(tree, parent, options.native(), nativeBytes(label), style.native(), C.bool(checked), out)
		runtime.KeepAlive(label)
		return result
	})
}

// Checkbox reads the current checkbox state.
func (tree *Tree) Checkbox(node Node) (bool, error) {
	var checked C.bool
	err := tree.callNode(node, func(native *C.NtTree, id C.NtNodeId) C.NtResult {
		return C.nt_checkbox_get(native, id, &checked)
	})
	return bool(checked), err
}

func (tree *Tree) SetCheckbox(node Node, checked bool) error {
	return tree.callNode(node, func(native *C.NtTree, id C.NtNodeId) C.NtResult {
		return C.nt_checkbox_set(native, id, C.bool(checked))
	})
}

type ChoiceKind uint32

const (
	ChoiceList   ChoiceKind = C.NT_CHOICE_LIST
	ChoiceRadio  ChoiceKind = C.NT_CHOICE_RADIO
	ChoiceSelect ChoiceKind = C.NT_CHOICE_SELECT
	ChoiceNone   uint       = ^uint(0)
)

type ChoiceEntry struct {
	Label    string
	Disabled bool
}

type ChoiceOptions struct {
	Kind        ChoiceKind
	Entries     []ChoiceEntry
	Selected    uint
	VisibleRows uint32
	Style       *ControlStyle
}

type ChoiceState struct {
	Selected uint
	Expanded bool
}

func (options ChoiceOptions) native(pins *runtime.Pinner) C.NtChoiceOptions {
	entries := make([]C.NtChoiceEntry, len(options.Entries))
	for index, entry := range options.Entries {
		entries[index].disabled = C.bool(entry.Disabled)
		if entry.Label != "" {
			pins.Pin(unsafe.StringData(entry.Label))
			entries[index].label = nativeBytes(entry.Label)
		}
	}
	var first *C.NtChoiceEntry
	if len(entries) != 0 {
		first = &entries[0]
		pins.Pin(first)
	}
	style := options.Style.native()
	if style != nil {
		pins.Pin(style)
	}
	return C.NtChoiceOptions{kind: C.NtChoiceKind(options.Kind), entries: first, count: C.size_t(len(entries)),
		selected: C.size_t(options.Selected), visible_rows: C.uint32_t(options.VisibleRows), style: style}
}

// MountChoice copies the entries, labels and style. Supply ChoiceNone for no selection.
func (tree *Tree) MountChoice(parent Node, mount MountOptions, options ChoiceOptions) (Node, error) {
	return tree.mountNative(parent, func(tree *C.NtTree, parent C.NtNodeId, out *C.NtNodeId) C.NtResult {
		var pins runtime.Pinner
		defer pins.Unpin()
		native := options.native(&pins)
		return C.nt_choice_mount(tree, parent, mount.native(), &native, out)
	})
}

// ConfigureChoice atomically replaces the choice options and entries.
func (tree *Tree) ConfigureChoice(node Node, options ChoiceOptions) error {
	return tree.callNode(node, func(tree *C.NtTree, id C.NtNodeId) C.NtResult {
		var pins runtime.Pinner
		defer pins.Unpin()
		native := options.native(&pins)
		return C.nt_choice_configure(tree, id, &native)
	})
}

func (tree *Tree) Choice(node Node) (ChoiceState, error) {
	var out C.NtChoiceState
	err := tree.callNode(node, func(tree *C.NtTree, id C.NtNodeId) C.NtResult {
		return C.nt_choice_get(tree, id, &out)
	})
	return ChoiceState{Selected: uint(out.selected), Expanded: bool(out.expanded)}, err
}

// SetChoice selects an entry or clears the current selection with ChoiceNone.
func (tree *Tree) SetChoice(node Node, selected uint) error {
	return tree.callNode(node, func(tree *C.NtTree, id C.NtNodeId) C.NtResult {
		return C.nt_choice_set(tree, id, C.size_t(selected))
	})
}

type Progress struct {
	Value, Maximum    uint32
	Filled, Remaining Style
	Vertical          bool
}

func (progress Progress) native() C.NtProgressOptions {
	return C.NtProgressOptions{value: C.uint32_t(progress.Value), maximum: C.uint32_t(progress.Maximum),
		filled: progress.Filled.native(), remaining: progress.Remaining.native(), vertical: C.bool(progress.Vertical)}
}

func (tree *Tree) MountProgress(parent Node, options MountOptions, progress Progress) (Node, error) {
	return tree.mountNative(parent, func(tree *C.NtTree, parent C.NtNodeId, out *C.NtNodeId) C.NtResult {
		return C.nt_progress_mount(tree, parent, options.native(), progress.native(), out)
	})
}

func (tree *Tree) SetProgress(node Node, progress Progress) error {
	return tree.callNode(node, func(native *C.NtTree, id C.NtNodeId) C.NtResult {
		return C.nt_progress_set(native, id, progress.native())
	})
}

func (tree *Tree) Progress(node Node) (Progress, error) {
	var out C.NtProgressOptions
	err := tree.callNode(node, func(tree *C.NtTree, id C.NtNodeId) C.NtResult {
		return C.nt_progress_get(tree, id, &out)
	})
	return Progress{Value: uint32(out.value), Maximum: uint32(out.maximum),
		Filled: styleFromNative(out.filled), Remaining: styleFromNative(out.remaining), Vertical: bool(out.vertical)}, err
}

// CursorOptions uses an explicit phase duration. Zero leaves the cursor steady.
type CursorOptions struct {
	Style   Style
	PhaseUs uint64
	Visible bool
}

func (cursor CursorOptions) native() C.NtCursorOptions {
	return C.NtCursorOptions{style: cursor.Style.native(), phase_us: C.NtTimeUs(cursor.PhaseUs), visible: C.bool(cursor.Visible)}
}

func (tree *Tree) MountCursor(parent Node, options MountOptions, cursor CursorOptions) (Node, error) {
	return tree.mountNative(parent, func(tree *C.NtTree, parent C.NtNodeId, out *C.NtNodeId) C.NtResult {
		return C.nt_cursor_mount(tree, parent, options.native(), cursor.native(), out)
	})
}

func (tree *Tree) SetCursor(node Node, cursor CursorOptions) error {
	return tree.callNode(node, func(tree *C.NtTree, id C.NtNodeId) C.NtResult {
		return C.nt_cursor_set(tree, id, cursor.native())
	})
}

type WaitOptions struct {
	Style   Style
	PhaseUs uint64
	ASCII   bool
}

func (wait WaitOptions) native() C.NtWaitOptions {
	return C.NtWaitOptions{style: wait.Style.native(), phase_us: C.NtTimeUs(wait.PhaseUs), ascii: C.bool(wait.ASCII)}
}

func (tree *Tree) MountWait(parent Node, options MountOptions, wait WaitOptions) (Node, error) {
	return tree.mountNative(parent, func(tree *C.NtTree, parent C.NtNodeId, out *C.NtNodeId) C.NtResult {
		return C.nt_wait_mount(tree, parent, options.native(), wait.native(), out)
	})
}

func (tree *Tree) SetWait(node Node, wait WaitOptions) error {
	return tree.callNode(node, func(tree *C.NtTree, id C.NtNodeId) C.NtResult {
		return C.nt_wait_set(tree, id, wait.native())
	})
}

// Scrollbar offsets are clamped to the available content range on set and mount.
type Scrollbar struct {
	Content, Viewport, Offset uint32
	Thumb, Track              Style
	Vertical                  bool
}

func (bar Scrollbar) native() C.NtScrollBarOptions {
	return C.NtScrollBarOptions{content: C.uint32_t(bar.Content), viewport: C.uint32_t(bar.Viewport),
		offset: C.uint32_t(bar.Offset), thumb: bar.Thumb.native(), track: bar.Track.native(), vertical: C.bool(bar.Vertical)}
}

func (tree *Tree) MountScrollbar(parent Node, options MountOptions, bar Scrollbar) (Node, error) {
	return tree.mountNative(parent, func(tree *C.NtTree, parent C.NtNodeId, out *C.NtNodeId) C.NtResult {
		return C.nt_scrollbar_mount(tree, parent, options.native(), bar.native(), out)
	})
}

func (tree *Tree) SetScrollbar(node Node, bar Scrollbar) error {
	return tree.callNode(node, func(tree *C.NtTree, id C.NtNodeId) C.NtResult {
		return C.nt_scrollbar_set(tree, id, bar.native())
	})
}

func (tree *Tree) Scrollbar(node Node) (Scrollbar, error) {
	var out C.NtScrollBarOptions
	err := tree.callNode(node, func(tree *C.NtTree, id C.NtNodeId) C.NtResult {
		return C.nt_scrollbar_get(tree, id, &out)
	})
	return Scrollbar{Content: uint32(out.content), Viewport: uint32(out.viewport), Offset: uint32(out.offset),
		Thumb: styleFromNative(out.thumb), Track: styleFromNative(out.track), Vertical: bool(out.vertical)}, err
}
