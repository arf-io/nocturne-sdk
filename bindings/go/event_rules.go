package nocturne

/*
#include <nocturne/event_rules.h>

extern NtResult nocturneGoEventPredicate(uintptr_t handle, NtValue *values, size_t count, bool *out);
extern NtResult nocturneGoEventDerive(uintptr_t handle, NtValue *values, size_t count, NtValue *out);

static NtResult go_event_predicate(void *context, const NtEvent *event, const NtValue *values, size_t count, bool *out)
{
    (void)event;
    return nocturneGoEventPredicate((uintptr_t)context, (NtValue *)values, count, out);
}

static NtResult go_event_derive(void *context, const NtEvent *event, const NtValue *values, size_t count, NtValue *out)
{
    (void)event;
    return nocturneGoEventDerive((uintptr_t)context, (NtValue *)values, count, out);
}

static void go_event_rule_callbacks(NtEventRule *rule, bool predicate, uintptr_t handle)
{
    rule->context = (void *)handle;
    if (predicate) rule->predicate = go_event_predicate;
}

static void go_event_field_rule(NtEventFieldRule *field, uint32_t id, NtValueKind kind, bool derive)
{
    *field = (NtEventFieldRule){id, kind, derive ? go_event_derive : NULL};
}
*/
import "C"

import (
	"runtime/cgo"
	"unsafe"
)

type IntentPayloadKind uint32

const (
	IntentBytes  IntentPayloadKind = C.NT_INTENT_BYTES
	IntentFields IntentPayloadKind = C.NT_INTENT_FIELDS
)

type IntentField struct {
	ID    uint32
	Value PropertyValue
}

// EventIntent owns a copy of the queued bytes or typed fields. A source
// reference retains its identity even after that node has been removed.
type EventIntent struct {
	Intent
	PayloadKind IntentPayloadKind
	Fields      []IntentField
}

func (state *treeState) eventIntent(native *C.NtEventIntentView) (EventIntent, error) {
	bytes, err := copyBytes(native.intent.payload)
	if err != nil {
		return EventIntent{}, err
	}
	out := EventIntent{Intent: Intent{Source: state.node(native.intent.source), Kind: uint32(native.intent.kind), Payload: bytes},
		PayloadKind: IntentPayloadKind(native.payload_kind)}
	if native.field_count != 0 {
		out.Fields = make([]IntentField, int(native.field_count))
		for index, field := range unsafe.Slice(native.fields, len(out.Fields)) {
			value, err := propertyFromNative(&field.value)
			if err != nil {
				return EventIntent{}, err
			}
			out.Fields[index] = IntentField{ID: uint32(field.id), Value: value}
		}
	}
	return out, nil
}

func (tree *Tree) PeekEventIntent() (EventIntent, bool, error) {
	state, err := tree.acquire()
	if err != nil {
		return EventIntent{}, false, err
	}
	defer state.release()
	var native C.NtEventIntentView
	if !bool(C.nt_tree_peek_event_intent(state.native, &native)) {
		return EventIntent{}, false, nil
	}
	intent, err := state.eventIntent(&native)
	return intent, err == nil, err
}

// EmitFields stages one intent. Field IDs must be strictly increasing and nonzero.
func (event *Event) EmitFields(kind uint32, fields []IntentField) error {
	if err := event.stageReady(); err != nil {
		return err
	}
	arena := RawArena{}
	defer arena.close()
	native, err := nativeSlice[C.NtIntentField](&arena, len(fields))
	if err != nil {
		return event.rememberFailure(err)
	}
	for i, field := range fields {
		native[i].id = C.uint32_t(field.ID)
		if err = field.Value.writeNative(&arena, &native[i].value); err != nil {
			return event.rememberFailure(err)
		}
	}
	return event.rememberFailure(check(C.nt_event_emit_fields(event.native, C.uint32_t(kind),
		unsafe.SliceData(native), C.size_t(len(native)))))
}

// RuleEvent is an owned observation of the normalized event and routing
// context. Input.Kind determines the active payload.
type RuleEvent struct {
	Input
	At               uint64
	Phase            EventPhase
	LocalPosition    Point
	HasLocalPosition bool
}

func (event *Event) ruleEvent() (RuleEvent, error) {
	out := RuleEvent{Input: Input{Kind: event.kind}, At: event.at}
	var err error
	out.Phase, err = event.Phase()
	if err != nil {
		return RuleEvent{}, err
	}
	out.LocalPosition, out.HasLocalPosition, err = event.LocalPosition()
	if err != nil {
		return RuleEvent{}, err
	}
	switch event.kind {
	case KeyEvent:
		out.Key, err = event.Key()
	case TextEvent, PasteEvent:
		out.Text, err = event.Text()
	case PointerDownEvent, PointerMoveEvent, PointerUpEvent:
		out.Pointer, err = event.Pointer()
	case WheelEvent:
		out.Wheel, err = event.Wheel()
	case PointerCancelEvent:
		out.Cancelled, out.Reason, err = event.CancelledPointer()
	case HostCancelEvent:
		out.Reason, err = event.HostCancelReason()
	case ResizeEvent:
		out.Size, err = event.Resize()
	}
	return out, err
}

type EventPredicate func(event RuleEvent, properties []PropertyValue) (bool, error)
type EventDerive func(event RuleEvent, properties []PropertyValue) (PropertyValue, error)

type EventFieldRule struct {
	ID     uint32
	Kind   PropertyKind
	Derive EventDerive
}

type EventRule struct {
	Event      EventKind
	Phase      EventPhase
	Properties []PropertyRef
	Predicate  EventPredicate
	IntentKind uint32
	Fields     []EventFieldRule
}

type ruleEvaluation struct {
	owner     *treeState
	arena     *RawArena
	event     RuleEvent
	predicate EventPredicate
	derive    []EventDerive
	next      int
}

func (evaluation *ruleEvaluation) run(phase string, values *C.NtValue, count C.size_t,
	call func([]PropertyValue) error) (status C.NtResult) {
	defer func() {
		if cause := recover(); cause != nil {
			evaluation.owner.recordCallbackFailure(phase, &PanicError{Value: cause})
			status = C.NT_RESULT_CALLBACK_FAILED
		}
	}()
	inputs, err := propertyValuesFromNative(values, count)
	if err != nil {
		return callbackResult(err)
	}
	if err = call(inputs); err != nil {
		evaluation.owner.recordCallbackFailure(phase, err)
		return callbackResult(err)
	}
	return C.NT_RESULT_OK
}

//export nocturneGoEventPredicate
func nocturneGoEventPredicate(handle C.uintptr_t, values *C.NtValue, count C.size_t, out *C.bool) C.NtResult {
	evaluation := cgo.Handle(handle).Value().(*ruleEvaluation)
	return evaluation.run("eventPredicate", values, count, func(inputs []PropertyValue) error {
		match, err := evaluation.predicate(evaluation.event, inputs)
		*out = C.bool(match)
		return err
	})
}

//export nocturneGoEventDerive
func nocturneGoEventDerive(handle C.uintptr_t, values *C.NtValue, count C.size_t, out *C.NtValue) C.NtResult {
	evaluation := cgo.Handle(handle).Value().(*ruleEvaluation)
	return evaluation.run("eventDerive", values, count, func(inputs []PropertyValue) error {
		derive := evaluation.derive[evaluation.next]
		evaluation.next++
		value, err := derive(evaluation.event, inputs)
		if err != nil {
			return err
		}
		return value.writeNative(evaluation.arena, out)
	})
}

func (event *Event) nativeRule(arena *RawArena, observed RuleEvent, rule EventRule, out *C.NtEventRule) (cgo.Handle, error) {
	properties, err := nativeSlice[C.NtPropertyRef](arena, len(rule.Properties))
	if err != nil {
		return 0, err
	}
	if rule.Event == observed.Kind && rule.Phase == observed.Phase {
		for index, property := range rule.Properties {
			properties[index], err = property.native(event.owner)
			if err != nil {
				return 0, err
			}
		}
	}
	fields, err := nativeSlice[C.NtEventFieldRule](arena, len(rule.Fields))
	if err != nil {
		return 0, err
	}
	*out = C.NtEventRule{event: C.NtEventKind(rule.Event), phase: C.NtEventPhase(rule.Phase),
		properties: unsafe.SliceData(properties), property_count: C.size_t(len(properties)),
		intent_kind: C.uint32_t(rule.IntentKind), fields: unsafe.SliceData(fields), field_count: C.size_t(len(fields))}
	if rule.Predicate == nil && len(fields) == 0 {
		return 0, nil
	}
	evaluation := &ruleEvaluation{owner: event.owner, arena: arena, event: observed,
		predicate: rule.Predicate, derive: make([]EventDerive, len(fields))}
	for index, field := range rule.Fields {
		evaluation.derive[index] = field.Derive
		C.go_event_field_rule(&fields[index], C.uint32_t(field.ID), C.NtValueKind(field.Kind), C.bool(field.Derive != nil))
	}
	handle := cgo.NewHandle(evaluation)
	C.go_event_rule_callbacks(out, C.bool(rule.Predicate != nil), C.uintptr_t(handle))
	return handle, nil
}

// ApplyRules applies exact event and phase matches in rule order. A failure
// discards every intent staged by this component invocation.
func (event *Event) ApplyRules(rules []EventRule) (uint, error) {
	if err := event.stageReady(); err != nil {
		return 0, err
	}
	event.scope.evaluatingRule = true
	defer func() { event.scope.evaluatingRule = false }()
	observed, err := event.ruleEvent()
	if err != nil {
		return 0, event.rememberFailure(err)
	}
	arena := RawArena{}
	defer arena.close()
	native, err := nativeSlice[C.NtEventRule](&arena, len(rules))
	if err != nil {
		return 0, event.rememberFailure(err)
	}
	handles := make([]cgo.Handle, 0, len(rules))
	defer func() {
		for _, handle := range handles {
			handle.Delete()
		}
	}()
	for i, rule := range rules {
		handle, err := event.nativeRule(&arena, observed, rule, &native[i])
		if err != nil {
			return 0, event.rememberFailure(err)
		}
		if handle != 0 {
			handles = append(handles, handle)
		}
	}
	var emitted C.size_t
	result := C.nt_event_apply_rules(event.native, unsafe.SliceData(native), C.size_t(len(native)), &emitted)
	return uint(emitted), event.rememberFailure(check(result))
}
