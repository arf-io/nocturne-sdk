package nocturne

import (
	"errors"
	"reflect"
	"testing"
)

func TestCustomPropertiesAndTransactions(t *testing.T) {
	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	const titleID, valueID, readonlyID = PropertyID(100), PropertyID(101), PropertyID(102)
	node, err := tree.MountBox(Node{}, MountOptions{Enabled: true}, Box{Kind: BoxColumn})
	mustOK(t, err)
	title, amount := "start", uint64(7)
	titleRef, valueRef := PropertyRef{Node: node, ID: titleID}, PropertyRef{Node: node, ID: valueID}
	readText := func() (PropertyValue, error) { return PropertyValue{Kind: TextValue, Text: title}, nil }
	var order []string
	descriptors := []PropertyDescriptor{
		{ID: titleID, Kind: TextValue, Invalidation: InvalidateLayout, Read: readText,
			Prepare: func(value PropertyValue) (any, error) {
				order = append(order, "title prepare")
				if _, err := tree.Property(node, titleID); err != WrongPhase {
					t.Errorf("property reentry = %v", err)
				}
				return []string{value.Text}, nil
			},
			Finish: func(value PropertyValue, prepared any, commit bool) {
				if prepared.([]string)[0] != value.Text {
					t.Error("text preparation identity was lost")
				}
				if commit {
					title = value.Text
					order = append(order, "title commit")
				} else {
					order = append(order, "title discard")
				}
			}},
		{ID: valueID, Kind: UnsignedValue, Invalidation: InvalidatePaint,
			Read: func() (PropertyValue, error) { return PropertyValue{Kind: UnsignedValue, Unsigned: amount}, nil },
			Prepare: func(value PropertyValue) (any, error) {
				order = append(order, "amount prepare")
				if value.Unsigned > 100 {
					return nil, OutOfRange
				}
				return value.Unsigned, nil
			},
			Finish: func(value PropertyValue, prepared any, commit bool) {
				if !commit || prepared != value.Unsigned {
					t.Errorf("amount prepared value = %v, commit %v", prepared, commit)
				}
				amount = value.Unsigned
				order = append(order, "amount commit")
			}},
		{ID: readonlyID, Kind: BoolValue, Read: func() (PropertyValue, error) { return PropertyValue{Kind: BoolValue, Bool: true}, nil }},
	}
	if err := tree.DefineProperties(node, []PropertyDescriptor{descriptors[0], descriptors[0]}); err != InvalidArgument {
		t.Fatalf("duplicate declaration = %v", err)
	}
	mustOK(t, tree.DefineProperties(node, descriptors))
	descriptors[0].Read = nil
	info, err := tree.Properties(node)
	mustOK(t, err)
	expected := []PropertyInfo{
		{ID: titleID, Kind: TextValue, Writable: true, Invalidation: InvalidateLayout},
		{ID: valueID, Kind: UnsignedValue, Writable: true, Invalidation: InvalidatePaint},
		{ID: readonlyID, Kind: BoolValue},
	}
	if !reflect.DeepEqual(info, expected) {
		t.Fatalf("property metadata = %+v", info)
	}
	old, err := tree.Property(node, titleID)
	mustOK(t, err)
	if old.Text != "start" {
		t.Fatalf("custom text getter = %+v", old)
	}
	const unicode = "e\u0301\x00界"
	writes := []PropertyAssignment{{Property: titleRef, Value: PropertyValue{Kind: TextValue, Text: unicode}},
		{Property: valueRef, Value: PropertyValue{Kind: UnsignedValue, Unsigned: 101}}}
	if err := tree.ApplyProperties(writes); !errors.Is(err, OutOfRange) {
		t.Fatalf("custom preparation rejection = %v", err)
	}
	if title != "start" || amount != 7 || !reflect.DeepEqual(order, []string{"title prepare", "amount prepare", "title discard"}) {
		t.Fatalf("failed batch order = %v, properties = %q, %d", order, title, amount)
	}
	writes[1].Value.Unsigned = 10
	order = nil
	mustOK(t, tree.ApplyProperties(writes))
	if title != unicode || amount != 10 ||
		!reflect.DeepEqual(order, []string{"title prepare", "amount prepare", "title commit", "amount commit"}) {
		t.Fatalf("successful batch order = %v, properties = %q, %d", order, title, amount)
	}
	if old.Text != "start" {
		t.Fatalf("owned text changed: %+v", old)
	}
	if err := tree.SetProperty(node, readonlyID, PropertyValue{Kind: BoolValue}); err != UnsupportedCapability {
		t.Fatalf("read-only write = %v", err)
	}
	refs := []PropertyRef{titleRef, valueRef}
	snapshot, err := tree.CaptureState(refs)
	mustOK(t, err)
	defer snapshot.Close()
	mustOK(t, tree.SetProperty(node, titleID, PropertyValue{Kind: TextValue, Text: "other"}))
	mustOK(t, tree.RestoreState(refs, snapshot))
	if title != unicode || amount != 10 {
		t.Fatalf("snapshot restore = %q, %d", title, amount)
	}
	mustOK(t, tree.Remove(node))
	if _, err := tree.Properties(node); err != StaleReference {
		t.Fatalf("removed properties = %v", err)
	}
}

func TestCustomPropertyCallbackFailures(t *testing.T) {
	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	node, err := tree.MountBox(Node{}, MountOptions{Enabled: true}, Box{})
	mustOK(t, err)
	const id = PropertyID(1)
	cause := errors.New("custom read failure")
	var readError error
	mustOK(t, tree.DefineProperties(node, []PropertyDescriptor{{ID: id, Kind: UnsignedValue,
		Read: func() (PropertyValue, error) {
			return PropertyValue{Kind: UnsignedValue, Unsigned: 9}, readError
		},
		Finish: func(PropertyValue, any, bool) { panic(cause) },
	}}))
	readError = Result(-701)
	if _, err := tree.Property(node, id); !errors.Is(err, readError) {
		t.Fatalf("signed property failure = %v", err)
	}
	readError = nil
	var failure *CallError
	if err := tree.SetProperty(node, id, PropertyValue{Kind: UnsignedValue, Unsigned: 2}); !errors.Is(err, cause) ||
		!errors.As(err, &failure) || failure.Native != nil || failure.Callbacks[0].Phase != "propertyFinish" {
		t.Fatalf("property finish failure = %v", err)
	}
	value, err := tree.Property(node, id)
	if must(t, value, err).Unsigned != 9 {
		t.Fatalf("read after failure = %+v", value)
	}
}
