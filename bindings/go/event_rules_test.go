package nocturne

import (
	"errors"
	"reflect"
	"testing"
)

type rulePublisher struct {
	ComponentBase
	onEvent func(*Event) error
}

func (rulePublisher) Measure(*Measure, Constraints) (Size, error) {
	return Size{Columns: 1, Rows: 1}, nil
}

func (publisher rulePublisher) Event(event *Event) error {
	if event.Kind() == KeyEvent {
		return publisher.onEvent(event)
	}
	return nil
}

func TestTypedEventRulesAndScheduledDelivery(t *testing.T) {
	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	other, err := NewTree()
	mustOK(t, err)
	defer other.Close()
	offTree, err := other.MountLabel(Node{}, MountOptions{Enabled: true}, "foreign", testInk)
	mustOK(t, err)

	visible := MountOptions{Enabled: true, Focusable: true}
	root, err := tree.MountBox(Node{}, visible, Box{Kind: BoxColumn})
	mustOK(t, err)
	var label Node
	var escaped *Event
	var retained []PropertyValue
	var events []uint
	var foreign, accepted bool
	var calls int
	deriveError := errors.New("derive stopped")
	sender, err := tree.Mount(root, visible, rulePublisher{onEvent: func(event *Event) error {
		escaped = event
		mustOK(t, event.Emit(71, string([]byte{0xff, 0})))
		if foreign {
			_, err := event.ApplyRules([]EventRule{{Event: KeyEvent, Phase: TargetPhase, IntentKind: 72,
				Properties: []PropertyRef{{Node: offTree, ID: TextProperty}}}})
			if err != InvalidArgument {
				t.Errorf("matching foreign property result = %v", err)
			}
			return nil // Rollback applies even when the caller handles the error.
		}
		phase, err := event.Phase()
		mustOK(t, err)
		if phase != TargetPhase {
			t.Errorf("event phase = %v", phase)
		}
		fields := []EventFieldRule{
			{ID: 3, Kind: TextValue, Derive: func(snapshot RuleEvent, properties []PropertyValue) (PropertyValue, error) {
				calls++
				retained = properties
				if snapshot.Kind != KeyEvent || snapshot.Key.Code != KeyEnter || snapshot.Phase != TargetPhase {
					t.Errorf("event snapshot = %+v", snapshot)
				}
				if err := event.Consume(); err != WrongPhase {
					t.Errorf("derive changed reply: %v", err)
				}
				if err := event.EmitFields(74, nil); err != WrongPhase {
					t.Errorf("derive staged fields: %v", err)
				}
				if snapshot.At == 2 {
					return PropertyValue{}, deriveError
				}
				return properties[0], nil
			}},
			{ID: 4, Kind: UnsignedValue, Derive: func(_ RuleEvent, properties []PropertyValue) (PropertyValue, error) {
				if properties[0].Text != "☾\x00" {
					t.Errorf("second derivation read %+v", properties)
				}
				return PropertyValue{Kind: UnsignedValue, Unsigned: ^uint64(0)}, nil
			}},
		}
		passed, err := event.ApplyRules([]EventRule{
			{Event: KeyEvent, Phase: BubblePhase, IntentKind: 72, Properties: []PropertyRef{{Node: offTree, ID: TextProperty}}},
			{Event: KeyEvent, Phase: TargetPhase, IntentKind: 73, Properties: []PropertyRef{{Node: label, ID: TextProperty}},
				Predicate: func(snapshot RuleEvent, properties []PropertyValue) (bool, error) {
					copied := properties[0]
					properties[0] = bindingText("changed locally")
					value, err := other.Property(offTree, TextProperty)
					if value.Text != "foreign" || copied.Text != "☾\x00" {
						t.Errorf("predicate property reads %+v and %+v", value, copied)
					}
					return snapshot.At != 3, err
				}, Fields: fields},
		})
		events = append(events, passed)
		if event.At() == 2 {
			if err != CallbackFailed {
				t.Errorf("failed rule returned %v", err)
			}
			return nil
		}
		mustOK(t, err)
		if event.At() == 3 {
			mustOK(t, event.EmitFields(75, nil))
		}
		accepted = true
		return nil
	}})
	mustOK(t, err)
	label, err = tree.MountLabel(root, visible, "☾\x00", testInk)
	mustOK(t, err)
	mustOK(t, tree.Publish(Size{Columns: 10, Rows: 4}))
	mustOK(t, tree.SetFocus(sender))
	mustOK(t, tree.ConfigureSchedule(ScheduleLimits{Subscriptions: 4}))

	var seen []Node
	var retainedIntent EventIntent
	receiverError := errors.New("receiver failed")
	receive := func(owner Node, intent EventIntent) error {
		if _, err := tree.DeliverIntents(1); err != WrongPhase {
			t.Errorf("reentrant delivery = %v", err)
		}
		seen = append(seen, owner)
		retainedIntent = intent
		if owner == sender {
			return receiverError
		}
		return nil
	}
	childID, err := tree.SubscribeIntent(sender, 73, receive)
	mustOK(t, err)
	parentID, err := tree.SubscribeIntent(root, 73, receive)
	mustOK(t, err)
	if childID >= parentID {
		t.Fatal("subscription order differs from registration order")
	}
	key := Input{Kind: KeyEvent, Key: KeyInput{Code: KeyEnter}}
	for _, at := range []uint64{0, 1, 2, 3} {
		report, err := tree.Dispatch(at, key, DispatchPolicy{})
		if at == 2 {
			var failure *CallError
			if !errors.Is(err, CallbackFailed) || !errors.Is(err, deriveError) || !errors.As(err, &failure) ||
				len(failure.Callbacks) != 1 || failure.Callbacks[0].Phase != "eventDerive" {
				t.Fatalf("derive refusal = %v", err)
			}
		} else {
			mustOK(t, err)
		}
		if !report.Admitted || report.Consumed {
			t.Fatalf("dispatch report = %+v", report)
		}
	}
	foreign = true
	if _, err := tree.Dispatch(4, key, DispatchPolicy{}); err != InvalidArgument {
		t.Fatalf("foreign property rollback = %v", err)
	}
	if !accepted || !reflect.DeepEqual(events, []uint{1, 1, 0, 0}) || calls != 3 {
		t.Fatalf("staged %v, callbacks %d", events, calls)
	}
	if err := escaped.EmitFields(73, nil); err != StaleReference {
		t.Fatalf("expired event use = %v", err)
	}

	got, ok, err := tree.PeekEventIntent()
	mustOK(t, err)
	if !ok || got.Source != sender || got.PayloadKind != IntentBytes || got.Payload != "\xff\x00" {
		t.Fatalf("byte intent = %+v (%v)", got, ok)
	}
	count, err := tree.DeliverIntents(0)
	mustOK(t, err)
	if count != 0 || len(seen) != 0 {
		t.Fatal("zero delivery consumed an item")
	}
	count, err = tree.DeliverIntents(2)
	var failure *CallError
	if count != 2 || !errors.Is(err, receiverError) || !errors.As(err, &failure) ||
		failure.Native != nil || len(failure.Callbacks) != 1 || failure.Callbacks[0].Phase != "intentReceive" {
		t.Fatalf("receiver result = %d, %v", count, err)
	}
	if !reflect.DeepEqual(seen, []Node{sender, root}) {
		t.Fatalf("receiver ordering = %v", seen)
	}
	mustOK(t, tree.Remove(sender))
	if err := tree.CancelSchedule(childID); err != StaleReference {
		t.Fatalf("removed owner's subscription = %v", err)
	}
	mustOK(t, tree.PopIntent()) // raw item emitted by the removed source
	got, ok, err = tree.PeekEventIntent()
	mustOK(t, err)
	if !ok || got.Kind != 73 || got.Source != sender {
		t.Fatalf("historical intent = %+v (%v)", got, ok)
	}
	count, err = tree.DeliverIntents(1)
	mustOK(t, err)
	if count != 1 || !reflect.DeepEqual(seen, []Node{sender, root, root}) {
		t.Fatalf("historical delivery = %d, %v", count, seen)
	}
	mustOK(t, tree.PopIntent())
	got, ok, err = tree.PeekEventIntent()
	mustOK(t, err)
	if !ok || got.Kind != 75 || got.PayloadKind != IntentFields || len(got.Fields) != 0 {
		t.Fatalf("empty typed intent = %+v, %v", got, ok)
	}
	mustOK(t, tree.PopIntent())
	_, ok, err = tree.PeekEventIntent()
	mustOK(t, err)
	if ok || retained[0].Text != "☾\x00" || retainedIntent.Fields[0].Value.Text != "☾\x00" ||
		retainedIntent.Fields[1].Value.Unsigned != ^uint64(0) {
		t.Fatalf("queue or retained copies = %+v, %+v", retained, retainedIntent)
	}
	mustOK(t, tree.CancelSchedule(parentID))
}

func TestIntentDeliverySuspendsAncestorEvent(t *testing.T) {
	outer, err := NewTree()
	mustOK(t, err)
	defer outer.Close()
	inner, err := NewTree()
	mustOK(t, err)
	defer inner.Close()
	options := MountOptions{Enabled: true, Focusable: true}
	in, err := inner.Mount(Node{}, options, rulePublisher{onEvent: func(event *Event) error {
		return event.Emit(7, "inner")
	}})
	mustOK(t, err)
	size := Size{Columns: 10, Rows: 4}
	mustOK(t, inner.Publish(size))
	mustOK(t, inner.SetFocus(in))
	mustOK(t, inner.ConfigureSchedule(ScheduleLimits{Subscriptions: 1}))
	var saved *Event
	calls := 0
	_, err = inner.SubscribeIntent(in, 7, func(_ Node, intent EventIntent) error {
		calls++
		if intent.Payload != "inner" {
			t.Errorf("inner intent = %+v", intent)
		}
		if err := saved.Emit(1, "off route"); err != WrongPhase {
			t.Errorf("ancestor event result = %v", err)
		}
		return nil
	})
	mustOK(t, err)
	key := Input{Kind: KeyEvent, Key: KeyInput{Code: KeyEnter}}
	_, err = inner.Dispatch(0, key, DispatchPolicy{})
	mustOK(t, err)
	out, err := outer.Mount(Node{}, options, rulePublisher{onEvent: func(event *Event) error {
		saved = event
		count, err := inner.DeliverIntents(1)
		if err != nil || count != 1 {
			t.Errorf("nested delivery = %d, %v", count, err)
		}
		return event.Emit(2, "restored")
	}})
	mustOK(t, err)
	mustOK(t, outer.Publish(size))
	mustOK(t, outer.SetFocus(out))
	_, err = outer.Dispatch(0, key, DispatchPolicy{})
	mustOK(t, err)
	intent, ok, err := outer.PeekEventIntent()
	mustOK(t, err)
	if !ok || intent.Kind != 2 || calls != 1 {
		t.Fatalf("restored intent = %+v, %v, %d", intent, ok, calls)
	}
}
