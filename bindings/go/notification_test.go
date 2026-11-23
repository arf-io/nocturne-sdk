package nocturne

import "testing"

func TestNotificationIntentsAndLifecycle(t *testing.T) {
	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()

	const id, action, expiry = ^uint64(0), uint64(1)<<63 + 17, uint64(1)<<53 + 7
	card := DefaultNotificationOptions()
	card.ID, card.Title, card.ExpiresAt = id, "Café planning", expiry
	card.Actions = []NotificationAction{{ID: 2, Label: "Unavailable", Disabled: true}, {ID: action, Label: "Review"}}
	node, err := tree.MountNotification(Node{}, MountOptions{Enabled: true, Focusable: true, HitTestable: true}, card)
	mustOK(t, err)
	card.Actions[1].ID = 99
	mustOK(t, tree.Publish(Size{Columns: 32, Rows: 4}))
	mustOK(t, tree.SetFocus(node))

	dispatch := func(at uint64, code uint32) {
		t.Helper()
		report, err := tree.Dispatch(at, Input{Kind: KeyEvent, Key: KeyInput{Code: code}}, DispatchPolicy{})
		mustOK(t, err)
		if !report.Consumed {
			t.Fatalf("key %d was not consumed", code)
		}
	}
	readIntent := func() EventIntent {
		t.Helper()
		intent, found, err := tree.PeekEventIntent()
		mustOK(t, err)
		if !found || intent.Source != node || intent.Kind != NotificationIntentKind {
			t.Fatalf("notification intent = %+v, found = %t", intent, found)
		}
		mustOK(t, tree.PopIntent())
		return intent
	}

	dispatch(1, KeyDown)
	state, err := tree.Notification(node)
	mustOK(t, err)
	if state != (NotificationState{ID: id, SelectedAction: action, ExpiresAt: expiry, Status: NotificationActive}) {
		t.Fatalf("selected state = %+v", state)
	}
	card.ID = 8
	card.Actions[1].ID = card.Actions[0].ID
	if err := tree.SetNotification(node, card); err != InvalidArgument {
		t.Fatalf("duplicate action replacement = %v", err)
	}
	dispatch(2, KeyEnter)
	queued := readIntent()
	event, err := DecodeNotificationIntent(queued)
	mustOK(t, err)
	if event != (NotificationEvent{Kind: NotificationPerformed, Notification: id, Action: action}) {
		t.Fatalf("performed event = %+v", event)
	}

	dispatch(3, KeyEscape)
	event, err = DecodeNotificationIntent(readIntent())
	mustOK(t, err)
	if event != (NotificationEvent{Kind: NotificationDismissedEvent, Notification: id}) {
		t.Fatalf("dismissal event = %+v", event)
	}
	state, err = tree.Notification(node)
	mustOK(t, err)
	if state.Status != NotificationDismissed {
		t.Fatalf("dismissed state = %+v", state)
	}
	card.Actions = nil
	card.Dismissible = false
	mustOK(t, tree.SetNotification(node, card))
	mustOK(t, tree.Tick(4, MotionReduced))
	deadline, err := tree.NextUpdate()
	mustOK(t, err)
	if !deadline.Scheduled || deadline.At != expiry {
		t.Fatalf("scheduled expiry = %+v", deadline)
	}
	mustOK(t, tree.Tick(expiry, MotionReduced))
	state, err = tree.Notification(node)
	mustOK(t, err)
	if state != (NotificationState{ID: card.ID, ExpiresAt: expiry, Status: NotificationExpired}) {
		t.Fatalf("expired state = %+v", state)
	}
	card.ExpiresAt = 0
	mustOK(t, tree.SetNotification(node, card))
	mustOK(t, tree.DismissNotification(node))
	state, err = tree.Notification(node)
	mustOK(t, err)
	if state != (NotificationState{ID: card.ID, Status: NotificationDismissed}) {
		t.Fatalf("dismissed persistent state = %+v", state)
	}
	_, found, err := tree.PeekEventIntent()
	mustOK(t, err)
	if found {
		t.Fatal("expiry and explicit dismissal emitted an intent")
	}
	mustOK(t, tree.Remove(node))
	if _, err := tree.Notification(node); err != StaleReference {
		t.Fatalf("removed notification lookup = %v", err)
	}
	event, err = DecodeNotificationIntent(queued)
	mustOK(t, err)
	if event.Action != action {
		t.Fatalf("owned event was changed: %+v", event)
	}
	queued.Fields[2].Value = PropertyValue{Kind: UnsignedValue}
	if _, err := DecodeNotificationIntent(queued); err != InvalidArgument {
		t.Fatalf("zero performed action = %v", err)
	}
}
