package nocturne

import "testing"

func styleRed(red uint8) PropertyValue {
	return PropertyValue{Kind: ColorValue, Color: Color{Red: red, Alpha: 255}}
}

func styleText(text string) PropertyValue { return PropertyValue{Kind: TextValue, Text: text} }

func TestStyleSheetsResolveAndApplyAtomically(t *testing.T) {
	const labelRole, groupRole = StyleTag(^uint64(0)), StyleTag(1 << 63)
	group := StyleElement{Role: groupRole}
	card := StyleElement{Role: labelRole, States: 1 << 63}
	titleSelector := StyleSelector{Subject: StyleCondition{Role: labelRole, Require: 1 << 63},
		Ancestor: StyleCondition{Role: groupRole}, Relation: StyleParent}
	baseRules := []StyleRule{
		{Property: TextProperty, Value: styleText("default")},
		{Selector: titleSelector, Property: TextProperty, Value: styleText("group\x00owned")},
		{Selector: titleSelector, Property: ForegroundProperty, Value: styleRed(21)},
	}
	base, err := NewStyleSheet(baseRules)
	mustOK(t, err)
	defer base.Close()
	baseRules[1].Value = styleText("mutated")
	late, err := NewStyleSheet([]StyleRule{{Selector: titleSelector, Property: ForegroundProperty, Value: styleRed(31)}})
	mustOK(t, err)
	defer late.Close()
	override, err := NewStyleSheet([]StyleRule{{Property: ForegroundProperty, Override: true, Value: styleRed(44)}})
	mustOK(t, err)
	defer override.Close()
	subject := StyleSubject{Path: StylePath{Element: card, Ancestors: []StyleElement{group}}}
	verify := func(sheets []*StyleSheet, property PropertyID, want PropertyValue) {
		t.Helper()
		value, found, err := ResolveStyle(sheets, subject, property)
		mustOK(t, err)
		if !found || value != want {
			t.Fatalf("resolve %v = %+v (%v), want %+v", property, value, found, want)
		}
	}
	verify([]*StyleSheet{base}, TextProperty, styleText("group\x00owned"))
	verify([]*StyleSheet{base, late}, ForegroundProperty, styleRed(31))
	verify([]*StyleSheet{override, base, late}, ForegroundProperty, styleRed(44))
	if _, found, err := ResolveStyle([]*StyleSheet{base}, subject, BackgroundProperty); err != nil || found {
		t.Fatalf("no-match result = %v, %v", found, err)
	}
	invalid, err := NewStyleSheet([]StyleRule{{Selector: StyleSelector{Subject: StyleCondition{Require: 8, Forbid: 8}},
		Property: TextProperty, Value: styleText("invalid")}})
	if err != InvalidArgument || invalid != nil {
		t.Fatalf("conflicting states = %v, %v", invalid, err)
	}

	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	open := MountOptions{Enabled: true}
	root, err := tree.MountBox(Node{}, open, Box{Kind: BoxColumn})
	mustOK(t, err)
	left, err := tree.MountBox(root, open, Box{Kind: BoxColumn})
	mustOK(t, err)
	label, err := tree.MountLabel(left, open, "initial", testInk)
	mustOK(t, err)
	counter, err := tree.MountProgress(root, open, Progress{Value: 5, Maximum: 12})
	mustOK(t, err)
	annotations := []StyleMount{{Node: left, Element: group}, {Node: label, Element: card}}
	ref := PropertyRef{Node: label, ID: TextProperty}
	counterRef := PropertyRef{Node: counter, ID: ValueProperty}
	mustOK(t, tree.ApplyStyles([]*StyleSheet{base, late}, annotations, []PropertyRef{ref, {Node: label, ID: ForegroundProperty}}))
	before, err := tree.Property(label, TextProperty)
	mustOK(t, err)
	bad, err := NewStyleSheet([]StyleRule{
		{Property: TextProperty, Override: true, Value: styleText("rolled back")},
		{Property: ValueProperty, Value: PropertyValue{Kind: UnsignedValue, Unsigned: 99}},
	})
	mustOK(t, err)
	defer bad.Close()
	if err := tree.ApplyStyles([]*StyleSheet{bad}, annotations, []PropertyRef{ref, counterRef}); err != OutOfRange {
		t.Fatalf("invalid batch = %v", err)
	}
	got, err := tree.Property(label, TextProperty)
	if must(t, got, err) != before || before != styleText("group\x00owned") {
		t.Fatalf("atomic styled write = %+v, before %+v", got, before)
	}
	mustOK(t, tree.Move(label, root))
	mustOK(t, tree.ApplyStyles([]*StyleSheet{base}, annotations, []PropertyRef{ref}))
	got, err = tree.Property(label, TextProperty)
	if must(t, got, err) != styleText("default") {
		t.Fatalf("new ancestry = %+v", got)
	}
	mustOK(t, tree.Remove(label))
	if err := tree.ApplyStyles([]*StyleSheet{base}, annotations, []PropertyRef{ref}); err != StaleReference {
		t.Fatalf("removed node = %v", err)
	}

	copied := *base
	value, found, err := ResolveStyle([]*StyleSheet{base}, subject, TextProperty)
	mustOK(t, err)
	base.Close()
	if !found || value != styleText("group\x00owned") {
		t.Fatalf("retained text value = %+v", value)
	}
	if _, _, err := ResolveStyle([]*StyleSheet{late, &copied}, subject, ForegroundProperty); err != StaleReference {
		t.Fatalf("closed sheet = %v", err)
	}
	verify([]*StyleSheet{late}, ForegroundProperty, styleRed(31))
}

func TestPaintStyledTextSelectsClustersAndPreservesOwnership(t *testing.T) {
	const owner, region = uint64(1 << 63), StyleTag(1 << 63)
	const source = "Ae\u0301界"
	layout, err := LayoutText(source, TextLayoutOptions{})
	mustOK(t, err)
	defer layout.Close()
	wide, err := layout.Cluster(2)
	mustOK(t, err)
	made, err := NewStyleSheet([]StyleRule{
		{Property: ForegroundProperty, Value: styleRed(21)},
		{Selector: StyleSelector{TextRegion: region, Characters: &ScalarInterval{First: 0x301, Last: 0x301}},
			Property: ForegroundProperty, Value: styleRed(79)},
		{Selector: StyleSelector{ClusterID: wide.ID, Bytes: &ByteSpan{Begin: 4, End: 7}},
			Property: BackgroundProperty, Value: styleRed(67)},
	})
	mustOK(t, err)
	defer made.Close()
	styles := TextStyles{Sheets: []*StyleSheet{made}, Regions: []StyleRegion{{Tag: region, Bytes: ByteSpan{1, 4}}}}
	sample, matched, err := ResolveStyle(styles.Sheets, StyleSubject{Source: source, Text: ByteSpan{1, 4}, Regions: styles.Regions},
		ForegroundProperty)
	if err != nil || !matched || sample != styleRed(79) {
		t.Fatalf("Unicode selector = %+v, %v, %v", sample, matched, err)
	}
	options := TextPaintOptions{Base: TextPaintStyle{Owner: owner, Color: Style{Background: styleRed(9).Color}}}
	frame, err := NewOffscreenFrame(Size{Columns: 6, Rows: 1})
	mustOK(t, err)
	defer frame.Close()
	surface, err := frame.Surface()
	mustOK(t, err)
	mustOK(t, surface.PaintStyledText(Point{}, layout, options, styles))
	ink := frame.Frame()
	for index, glyph := range []string{"A", "e\u0301", "界", "界"} {
		cell, err := ink.Cell(Point{X: int32(index)})
		mustOK(t, err)
		red := []uint8{21, 79, 21, 21}[index]
		back := []uint8{9, 9, 67, 67}[index]
		if cell.Glyph != glyph || cell.Owner != owner || cell.Style.Foreground.Red != red || cell.Style.Background.Red != back {
			t.Fatalf("styled cell %d = %+v", index, cell)
		}
	}
	bad, err := NewStyleSheet([]StyleRule{{Selector: StyleSelector{ClusterID: wide.ID},
		Property: ForegroundProperty, Value: styleText("invalid color")}})
	mustOK(t, err)
	defer bad.Close()
	before, err := ink.Cell(Point{})
	mustOK(t, err)
	if err := surface.PaintStyledText(Point{}, layout, options, TextStyles{Sheets: []*StyleSheet{made, bad}}); err != InvalidArgument {
		t.Fatalf("invalid overlay = %v", err)
	}
	after, err := ink.Cell(Point{})
	if must(t, after, err) != before {
		t.Fatalf("failed paint changed the frame: %+v", after)
	}

	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	var escaped *Paint
	_, err = tree.Mount(Node{}, MountOptions{Enabled: true}, &panelPaintFixture{draw: func(paint *Paint) error {
		escaped = paint
		return paint.PaintStyledText(Point{}, layout, options, styles)
	}})
	mustOK(t, err)
	mustOK(t, tree.Publish(Size{Columns: 6, Rows: 1}))
	published, err := tree.Frame()
	mustOK(t, err)
	cell, err := published.Cell(Point{X: 1})
	if got := must(t, cell, err); got.Glyph != "e\u0301" || got.Style.Foreground.Red != 79 || got.Owner == owner || got.Owner == 0 {
		t.Fatalf("mounted style = %+v", got)
	}
	if err := escaped.PaintStyledText(Point{}, layout, options, styles); err != StaleReference {
		t.Fatalf("escaped paint = %v", err)
	}
}
