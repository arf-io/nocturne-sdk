package nocturne

import (
	"errors"
	"testing"
)

func bindingText(text string) PropertyValue { return PropertyValue{Kind: TextValue, Text: text} }

func TestBindingGraphAtomicCommitAndCallbackLifetime(t *testing.T) {
	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	label, err := tree.MountLabel(Node{}, MountOptions{Enabled: true}, "draft", testInk)
	mustOK(t, err)
	property := PropertyRef{Node: label, ID: TextProperty}

	var graph *BindingGraph
	var calls int
	var escaped []PropertyValue
	var mode string
	dependencies := []uint{1, 2}
	values := []BindingValue{
		{Kind: TextValue, Source: BindingComputed, Dependencies: dependencies, Compute: func(inputs []PropertyValue) (PropertyValue, error) {
			calls++
			escaped = inputs
			switch mode {
			case "reject":
				return PropertyValue{}, Result(-701)
			case "panic":
				panic("binding refused")
			case "invalid":
				return PropertyValue{Kind: BoolValue}, nil
			}
			if err := graph.Close(); err != WrongPhase {
				return PropertyValue{}, errors.New("graph closed inside its evaluator")
			}
			if _, err := tree.Property(label, TextProperty); err != WrongPhase {
				return PropertyValue{}, errors.New("read owner tree inside its evaluator")
			}
			return bindingText(inputs[0].Text + "|" + inputs[1].Text), nil
		}},
		{Kind: TextValue, Source: BindingProperty, Property: property},
		{Kind: TextValue, Source: BindingInput},
	}
	targets := []BindingTarget{{Property: property, Value: 0}}
	graph, err = tree.NewBindingGraph(values, targets)
	mustOK(t, err)
	defer graph.Close()
	dependencies[0] = 0
	values[0].Compute = nil
	targets[0].Value = 2

	input := []PropertyValue{bindingText("e\u0301\x00☾")}
	mustOK(t, graph.Apply(input))
	if escaped[0] != bindingText("draft") || escaped[1] != input[0] {
		t.Fatalf("compute received %+v", escaped)
	}
	read := func() PropertyValue {
		got, err := tree.Property(label, TextProperty)
		return must(t, got, err)
	}
	want := bindingText("draft|" + input[0].Text)
	if got := read(); got != want {
		t.Fatalf("committed %+v, want %+v", got, want)
	}
	mode = "reject"
	var failure *CallError
	if err := graph.Apply(input); !errors.Is(err, Result(-701)) ||
		!errors.As(err, &failure) || len(failure.Callbacks) != 1 || failure.Callbacks[0].Phase != "bindingCompute" {
		t.Fatalf("signed compute refusal = %v", err)
	}
	mode = "panic"
	var cause *PanicError
	if err := graph.Apply(input); !errors.Is(err, CallbackFailed) || !errors.As(err, &cause) {
		t.Fatalf("compute panic = %v", err)
	}
	mode = "invalid"
	if err := graph.Apply(input); err != InvalidArgument {
		t.Fatalf("wrong output type = %v", err)
	}
	if got := read(); got != want {
		t.Fatalf("changed after failed compute: %+v", got)
	}
	mustOK(t, tree.Remove(label))
	if err := graph.Apply(input); err != StaleReference || calls != 4 {
		t.Fatalf("stale graph result %v with %d callbacks", err, calls)
	}
	mustOK(t, tree.Close())
	mustOK(t, graph.Close())
	mustOK(t, graph.Close())
	if err := graph.Apply(input); err != StaleReference {
		t.Fatalf("closed graph result = %v", err)
	}
}

func TestPropertyBatchAtomicityAndScheduleOrdering(t *testing.T) {
	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	options := MountOptions{Enabled: true}
	root, err := tree.MountBox(Node{}, options, Box{Kind: BoxColumn})
	mustOK(t, err)
	label, err := tree.MountLabel(root, options, "initial", testInk)
	mustOK(t, err)
	checkbox, err := tree.MountCheckbox(root, options, "Done", false, nil)
	mustOK(t, err)
	name := PropertyRef{Node: label, ID: TextProperty}
	flag := PropertyRef{Node: checkbox, ID: CheckedProperty}
	read := func(property PropertyRef) PropertyValue {
		value, err := tree.Property(property.Node, property.ID)
		return must(t, value, err)
	}
	assignments := []PropertyAssignment{{Property: name, Value: bindingText("pending")}, {Property: flag, Value: bindingText("bad")}}
	if err := tree.ApplyProperties(assignments); err != InvalidArgument || read(name).Text != "initial" {
		t.Fatalf("invalid batch: %v, text %+v", err, read(name))
	}
	assignments[1].Value = PropertyValue{Kind: BoolValue, Bool: true}
	mustOK(t, tree.ApplyProperties(assignments))
	if read(name).Text != "pending" || !read(flag).Bool {
		t.Fatal("valid batch did not commit both writes")
	}
	mustOK(t, tree.ConfigureSchedule(ScheduleLimits{Batches: 3}))
	const deadline = ^uint64(0) - 2
	first, err := tree.ScheduleBatch(root, deadline, assignments[:1])
	mustOK(t, err)
	assignments[0].Value = bindingText("final")
	last, err := tree.ScheduleBatch(root, deadline, assignments[:1])
	mustOK(t, err)
	cancel, err := tree.ScheduleBatch(checkbox, deadline-1, assignments[:1])
	mustOK(t, err)
	if first >= last || last >= cancel {
		t.Fatalf("registration IDs out of order: %v, %v, %v", first, last, cancel)
	}
	mustOK(t, tree.Remove(checkbox))
	if err := tree.CancelSchedule(cancel); err != StaleReference {
		t.Fatalf("removed owner cancellation = %v", err)
	}
	progress, err := tree.MountProgress(root, options, Progress{Value: 5, Maximum: 12})
	mustOK(t, err)
	counter := PropertyRef{Node: progress, ID: ValueProperty}
	rejected, err := tree.ScheduleBatch(root, deadline+1, []PropertyAssignment{
		{Property: name, Value: bindingText("should roll back")},
		{Property: counter, Value: PropertyValue{Kind: UnsignedValue, Unsigned: 99}},
	})
	mustOK(t, err)
	at, scheduled, err := tree.NextScheduled()
	mustOK(t, err)
	if !scheduled || at != deadline {
		t.Fatalf("next deadline = %d, %v", at, scheduled)
	}
	r, err := tree.RunSchedule(deadline, 1)
	mustOK(t, err)
	if r != (ScheduleRun{Attempted: 1}) || read(name).Text != "pending" {
		t.Fatalf("first run %+v, text %+v", r, read(name))
	}
	r, err = tree.RunSchedule(deadline, 1)
	mustOK(t, err)
	if r != (ScheduleRun{Attempted: 1}) || read(name).Text != "final" {
		t.Fatalf("second run %+v, text %+v", r, read(name))
	}
	r, err = tree.RunSchedule(deadline+1, 1)
	if err != OutOfRange || r != (ScheduleRun{Attempted: 1, Failed: rejected}) ||
		read(name).Text != "final" || read(counter).Unsigned != 5 {
		t.Fatalf("failed run %+v, error %v, text %+v, counter %+v", r, err, read(name), read(counter))
	}
	if err := tree.CancelSchedule(rejected); err != StaleReference {
		t.Fatalf("failed batch was not consumed: %v", err)
	}
	if _, err := tree.RunSchedule(deadline-1, 1); err != OutOfRange {
		t.Fatalf("backward run = %v", err)
	}
	_, scheduled, err = tree.NextScheduled()
	mustOK(t, err)
	if scheduled {
		t.Fatal("schedule retains an executed batch")
	}
}
