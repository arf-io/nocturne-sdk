package nocturne

/*
#include <nocturne/notification.h>
*/
import "C"

import "unsafe"

const NotificationIntentKind uint32 = C.NT_INTENT_NOTIFICATION

type NotificationAction struct {
	ID       uint64
	Label    string
	Disabled bool
}

// NotificationOptions maps directly to the native card settings. Use
// DefaultNotificationOptions for the standard width, rows and dismiss control.
type NotificationOptions struct {
	ID                           uint64
	Title, Message, DismissLabel string
	Actions                      []NotificationAction
	ExpiresAt                    uint64 // Absolute tree time in microseconds. Zero persists.
	Columns, MessageRows         uint32
	Style                        *ControlStyle
	Dismissible, DismissOnAction bool
}

// DefaultNotificationOptions returns the native presentation defaults.
// Set a nonzero ID before mounting the card.
func DefaultNotificationOptions() NotificationOptions {
	options := C.nt_notification_options_default()
	return NotificationOptions{DismissLabel: C.GoStringN(options.dismiss_label.data, C.int(options.dismiss_label.count)),
		Columns: uint32(options.columns), MessageRows: uint32(options.message_rows), Dismissible: bool(options.dismissible)}
}

type NotificationStatus uint32

const (
	NotificationActive    NotificationStatus = C.NT_NOTIFICATION_ACTIVE
	NotificationExpired   NotificationStatus = C.NT_NOTIFICATION_EXPIRED
	NotificationDismissed NotificationStatus = C.NT_NOTIFICATION_DISMISSED
)

type NotificationState struct {
	ID, SelectedAction, ExpiresAt uint64
	Status                        NotificationStatus
}

type NotificationEventKind uint32

const (
	NotificationPerformed      NotificationEventKind = C.NT_NOTIFICATION_PERFORMED
	NotificationDismissedEvent NotificationEventKind = C.NT_NOTIFICATION_DISMISSED_EVENT
)

type NotificationEvent struct {
	Kind                 NotificationEventKind
	Notification, Action uint64
}

func (options NotificationOptions) native(arena *RawArena) (C.NtNotificationOptions, error) {
	if len(options.Actions) > C.NT_NOTIFICATION_MAX_ACTIONS {
		return C.NtNotificationOptions{}, OutOfRange
	}
	native := C.nt_notification_options_default()
	native.id, native.expires_at = C.uint64_t(options.ID), C.NtTimeUs(options.ExpiresAt)
	native.columns, native.message_rows = C.uint32_t(options.Columns), C.uint32_t(options.MessageRows)
	native.dismissible, native.dismiss_on_action = C.bool(options.Dismissible), C.bool(options.DismissOnAction)
	if style := options.Style.native(); style != nil {
		native.style = *style
	}
	for _, text := range []struct {
		out   *C.NtByteView
		value string
	}{
		{&native.title, options.Title}, {&native.message, options.Message}, {&native.dismiss_label, options.DismissLabel},
	} {
		view, err := arenaByteView(arena, text.value)
		if err != nil {
			return C.NtNotificationOptions{}, err
		}
		*text.out = view
	}
	actions, err := nativeSlice[C.NtNotificationAction](arena, len(options.Actions))
	if err != nil {
		return C.NtNotificationOptions{}, err
	}
	for index, action := range options.Actions {
		label, err := arenaByteView(arena, action.Label)
		if err != nil {
			return C.NtNotificationOptions{}, err
		}
		actions[index] = C.NtNotificationAction{id: C.uint64_t(action.ID), label: label, disabled: C.bool(action.Disabled)}
	}
	native.actions, native.action_count = unsafe.SliceData(actions), C.size_t(len(actions))
	return native, nil
}

// MountNotification copies the card options into the tree.
func (tree *Tree) MountNotification(parent Node, mount MountOptions, options NotificationOptions) (Node, error) {
	var arena RawArena
	defer arena.close()
	native, err := options.native(&arena)
	if err != nil {
		return Node{}, err
	}
	return tree.mountNative(parent, func(tree *C.NtTree, parent C.NtNodeId, out *C.NtNodeId) C.NtResult {
		return C.nt_notification_mount(tree, parent, mount.native(), &native, out)
	})
}

// SetNotification replaces the card options and resets selection and lifecycle.
func (tree *Tree) SetNotification(node Node, options NotificationOptions) error {
	var arena RawArena
	defer arena.close()
	settings, err := options.native(&arena)
	if err != nil {
		return err
	}
	return tree.callNode(node, func(native *C.NtTree, id C.NtNodeId) C.NtResult {
		return C.nt_notification_set(native, id, &settings)
	})
}

func (tree *Tree) Notification(node Node) (NotificationState, error) {
	var out C.NtNotificationState
	err := tree.callNode(node, func(native *C.NtTree, id C.NtNodeId) C.NtResult {
		return C.nt_notification_get(native, id, &out)
	})
	return NotificationState{ID: uint64(out.id), SelectedAction: uint64(out.selected_action),
		ExpiresAt: uint64(out.expires_at), Status: NotificationStatus(out.status)}, err
}

// DismissNotification dismisses an active card without queuing an intent.
func (tree *Tree) DismissNotification(node Node) error {
	return tree.callNode(node, func(native *C.NtTree, id C.NtNodeId) C.NtResult {
		return C.nt_notification_dismiss(native, id)
	})
}

func DecodeNotificationIntent(intent EventIntent) (NotificationEvent, error) {
	if err := ensureABI(); err != nil {
		return NotificationEvent{}, err
	}
	if intent.Kind != NotificationIntentKind || intent.PayloadKind != IntentFields || len(intent.Fields) != 3 {
		return NotificationEvent{}, InvalidArgument
	}
	var arena RawArena
	defer arena.close()
	fields, err := nativeSlice[C.NtIntentField](&arena, 3)
	if err != nil {
		return NotificationEvent{}, err
	}
	for index, field := range intent.Fields {
		if field.Value.Kind != UnsignedValue {
			return NotificationEvent{}, InvalidArgument
		}
		fields[index].id = C.uint32_t(field.ID)
		if err := field.Value.writeNative(&arena, &fields[index].value); err != nil {
			return NotificationEvent{}, err
		}
	}
	native := C.NtEventIntentView{intent: C.NtIntentView{kind: C.uint32_t(intent.Kind)},
		payload_kind: C.NT_INTENT_FIELDS, fields: unsafe.SliceData(fields), field_count: 3}
	var out C.NtNotificationEvent
	err = check(C.nt_notification_event_decode(native, &out))
	return NotificationEvent{Kind: NotificationEventKind(out.kind), Notification: uint64(out.notification), Action: uint64(out.action)}, err
}
