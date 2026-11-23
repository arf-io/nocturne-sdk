package nocturne

import "testing"

func TestTypedPropertyValues(t *testing.T) {
	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	enabled := MountOptions{Enabled: true}
	root, err := tree.MountBox(Node{}, enabled, Box{Kind: BoxColumn})
	mustOK(t, err)
	label, err := tree.MountLabel(root, enabled, "initial", testInk)
	mustOK(t, err)
	checkbox, err := tree.MountCheckbox(root, enabled, "Done", false, nil)
	mustOK(t, err)
	text := PropertyValue{Kind: TextValue, Text: "☾\x00notes"}
	mustOK(t, tree.SetProperty(label, TextProperty, text))
	value, err := tree.Property(label, TextProperty)
	if must(t, value, err) != text {
		t.Fatalf("text property = %+v", value)
	}
	on := PropertyValue{Kind: BoolValue, Bool: true}
	mustOK(t, tree.SetProperty(checkbox, CheckedProperty, on))
	if err := tree.SetProperty(checkbox, CheckedProperty, text); err == nil {
		t.Fatal("accepted a text payload for a Boolean property")
	}
	value, err = tree.Property(checkbox, CheckedProperty)
	if must(t, value, err) != on {
		t.Fatalf("checked property changed after refusal: %+v", value)
	}
}
