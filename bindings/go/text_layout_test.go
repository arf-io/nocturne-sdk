package nocturne

import "testing"

func TestMeasureGlyph(t *testing.T) {
	for _, test := range []struct {
		glyph string
		width WidthPolicy
		want  GlyphMetrics
	}{
		{"👩‍💻", WidthPolicy{}, GlyphMetrics{GlyphEmoji, 2}},
		{"👩‍💻", WidthPolicy{Emoji: WidthOne}, GlyphMetrics{GlyphEmoji, 1}},
		{"·", WidthPolicy{Ambiguous: WidthTwo}, GlyphMetrics{GlyphText, 2}},
		{"\u0301", WidthPolicy{}, GlyphMetrics{GlyphIsolatedMark, 1}},
		{"\t", WidthPolicy{}, GlyphMetrics{GlyphTab, 0}},
		{"\r\n", WidthPolicy{}, GlyphMetrics{GlyphLineBreak, 0}},
		{"\x00", WidthPolicy{}, GlyphMetrics{GlyphReplacement, 1}},
	} {
		if got, err := MeasureGlyph(test.glyph, test.width); err != nil || got != test.want {
			t.Fatalf("measure %q: %+v, %v", test.glyph, got, err)
		}
	}
	for _, invalid := range []string{"", "ab", "\xff"} {
		if _, err := MeasureGlyph(invalid, WidthPolicy{}); err != InvalidArgument {
			t.Fatalf("measure invalid %q: %v", invalid, err)
		}
	}
	if _, err := MeasureGlyph("a", WidthPolicy{Emoji: 256}); err != InvalidArgument {
		t.Fatalf("invalid width: %v", err)
	}
}

func TestTextLayoutSnapshotAndCarets(t *testing.T) {
	const value = "e\u0301中👩‍💻\tQ\r\nאב"
	source, err := NewText("q" + value)
	if err != nil {
		t.Fatal(err)
	}
	defer source.Close()
	var ids [3]uint64
	for i, index := range []uint{1, 4, 7} {
		item, err := source.Cluster(index)
		if err != nil {
			t.Fatal(err)
		}
		ids[i] = item.ID
	}
	if err := source.Replace(0, 1, ""); err != nil {
		t.Fatal(err)
	}
	layout, err := NewTextLayout(source, TextLayoutOptions{Wrap: WrapWord})
	if err != nil {
		t.Fatal(err)
	}
	defer layout.Close()
	if err := source.Replace(0, uint(len(value)), "changed"); err != nil {
		t.Fatal(err)
	}
	source.Close()

	if got, err := layout.Source(); err != nil || got != value {
		t.Fatalf("source = %q, %v", got, err)
	}
	if got, err := layout.Size(); err != nil || got != (Size{9, 2}) {
		t.Fatalf("size = %+v, %v", got, err)
	}
	if got, err := layout.Len(); err != nil || got != 7 {
		t.Fatalf("length = %d, %v", got, err)
	}
	for _, test := range []struct {
		index uint
		want  LayoutCluster
	}{
		{0, LayoutCluster{Cluster{ids[0], 0, 3}, "e\u0301", Point{0, 0}, 1, 0}},
		{3, LayoutCluster{Cluster{ids[1], 17, 18}, " ", Point{5, 0}, 3, 0}},
		{6, LayoutCluster{Cluster{ids[2], 21, 23}, "א", Point{8, 1}, 1, 1}},
	} {
		if got, err := layout.Cluster(test.index); err != nil || got != test.want {
			t.Fatalf("cluster %d: %+v, %v", test.index, got, err)
		}
	}
	if got, err := layout.Hit(Point{2, 0}); err != nil || got != (Caret{6, CaretTrailing}) {
		t.Fatalf("wide glyph interior hit: %+v, %v", got, err)
	}
	if got, err := layout.CaretPosition(Caret{21, CaretTrailing}); err != nil || got != (Point{9, 1}) {
		t.Fatalf("CRLF trailing edge: %+v, %v", got, err)
	}
	if _, err := layout.CaretPosition(Caret{1, CaretLeading}); err != OutOfRange {
		t.Fatalf("inside grapheme: %v", err)
	}
	if _, err := layout.Hit(Point{-1, 0}); err != OutOfRange {
		t.Fatalf("negative hit: %v", err)
	}
	if _, err := NewTextLayout(source, TextLayoutOptions{}); err != StaleReference {
		t.Fatalf("closed source: %v", err)
	}
	copy := *layout
	layout.Close()
	if _, err := copy.Cluster(0); err != StaleReference {
		t.Fatalf("closed snapshot copy: %v", err)
	}
}

func TestTextLayoutBoundsAndEllipsis(t *testing.T) {
	wrapped, err := LayoutText("ab cd", TextLayoutOptions{Wrap: WrapWord,
		Bounds: Constraints{Columns: Extent{Max: 4, Bounded: true}}})
	if err != nil {
		t.Fatal(err)
	}
	defer wrapped.Close()
	for _, test := range []struct {
		side CaretAffinity
		want Point
	}{{CaretLeading, Point{0, 1}}, {CaretTrailing, Point{3, 0}}} {
		if got, err := wrapped.CaretPosition(Caret{3, test.side}); err != nil || got != test.want {
			t.Fatalf("soft-wrap affinity %d: %+v, %v", test.side, got, err)
		}
	}
	clipped, err := LayoutText("אבאב", TextLayoutOptions{BaseDirection: DirectionRTL, Ellipsis: true,
		Bounds: Constraints{Columns: Extent{Min: 3, Max: 3, Bounded: true}}})
	if err != nil {
		t.Fatal(err)
	}
	defer clipped.Close()
	if mark, err := clipped.Cluster(0); err != nil || mark.Glyph != "…" || mark.ID != 0 ||
		mark.Begin != 4 || mark.End != 4 || mark.Position != (Point{0, 0}) {
		t.Fatalf("RTL ellipsis: %+v, %v", mark, err)
	}
	if hit, err := clipped.Hit(Point{0, 0}); err != nil || hit != (Caret{4, CaretLeading}) {
		t.Fatalf("ellipsis hit: %+v, %v", hit, err)
	}
	empty, err := LayoutText("a", TextLayoutOptions{Bounds: Constraints{Columns: Extent{Bounded: true}}})
	if err != nil {
		t.Fatal(err)
	}
	defer empty.Close()
	if _, err := empty.Hit(Point{0, 0}); err != OutOfRange {
		t.Fatalf("zero-width hit: %v", err)
	}
	if _, err := LayoutText("a", TextLayoutOptions{Bounds: Constraints{Columns: Extent{
		Min: 4, Max: 2, Bounded: true,
	}}}); err != InvalidArgument {
		t.Fatalf("invalid constraints: %v", err)
	}
}
