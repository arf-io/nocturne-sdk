package nocturne

import (
	"slices"
	"sync"
	"testing"
)

func TestUnicodeAnalysis(t *testing.T) {
	for _, test := range []struct {
		text string
		want []LineBreak
	}{
		{"A B\r\nC\u2028D", []LineBreak{{2, false}, {5, true}, {9, true}, {10, true}}},
		{" \u0301", []LineBreak{{1, false}, {3, true}}},
		{"\x00", []LineBreak{{1, true}}},
		{"", []LineBreak{{0, true}}},
	} {
		if got, err := LineBreaks(test.text); err != nil || !slices.Equal(got, test.want) {
			t.Fatalf("LineBreaks(%q) = %+v, %v", test.text, got, err)
		}
	}
	want := []ScriptRun{{0, 5, "Latn"}, {5, 11, "Hira"}, {11, 14, "Kana"}}
	if got, err := ScriptRuns(".a\u0301 あーカ"); err != nil || !slices.Equal(got, want) {
		t.Fatalf("script runs: %+v, %v", got, err)
	}
	if got, err := ScriptRuns(""); err != nil || len(got) != 0 {
		t.Fatalf("empty script runs: %+v, %v", got, err)
	}
	if _, err := LineBreaks("\xff"); err != InvalidArgument {
		t.Fatalf("malformed line break input: %v", err)
	}
	if _, err := ScriptRuns("\xff"); err != InvalidArgument {
		t.Fatalf("malformed script input: %v", err)
	}
	if _, err := NewBidi("\xff", DirectionAuto); err != InvalidArgument {
		t.Fatalf("malformed bidi input: %v", err)
	}
	if _, err := NewBidi("", Direction(100)); err != InvalidArgument {
		t.Fatalf("invalid direction: %v", err)
	}
}

func TestBidiLineAnalysis(t *testing.T) {
	bidi, err := NewBidi("(אב)\u202b\u202c\nb", DirectionAuto)
	if err != nil {
		t.Fatal(err)
	}
	defer bidi.Close()
	if got, err := bidi.Len(); err != nil || got != 8 {
		t.Fatalf("scalar count: %d, %v", got, err)
	}
	if got, err := bidi.Scalar(4); err != nil || got != (BidiScalar{
		Begin: 6, End: 9, Scalar: '\u202b', Level: BidiRemovedLevel, ParagraphLevel: 1,
	}) {
		t.Fatalf("removed scalar: %+v, %v", got, err)
	}
	if got, err := bidi.Scalar(6); err != nil || !got.ParagraphSeparator || got.ParagraphLevel != 1 {
		t.Fatalf("paragraph separator: %+v, %v", got, err)
	}
	want := []BidiVisual{{3, 5, 6, '(', 1}, {2, 3, 5, 'ב', 1}, {1, 1, 3, 'א', 1}, {0, 0, 1, ')', 1}}
	if got, err := bidi.ReorderLine(0, 12); err != nil || !slices.Equal(got, want) {
		t.Fatalf("reorder with mirroring and removed controls: %+v, %v", got, err)
	}
	if got, err := bidi.ReorderLine(6, 12); err != nil || len(got) != 0 {
		t.Fatalf("reorder only removed controls: %+v, %v", got, err)
	}
	for _, test := range []struct {
		begin, end uint
		want       Result
	}{{2, 6, InvalidArgument}, {0, 14, InvalidArgument}, {6, 0, OutOfRange}, {0, 15, OutOfRange}} {
		if _, err := bidi.ReorderLine(test.begin, test.end); err != test.want {
			t.Fatalf("reorder(%d, %d): %v", test.begin, test.end, err)
		}
	}

	copy := *bidi
	var readers sync.WaitGroup
	for i := 0; i < 8; i++ {
		readers.Add(1)
		go func() {
			defer readers.Done()
			for j := 0; j < 20; j++ {
				got, err := copy.ReorderLine(0, 12)
				if err != StaleReference && (err != nil || !slices.Equal(got, want)) {
					t.Errorf("concurrent reorder: %+v, %v", got, err)
				}
			}
		}()
	}
	bidi.Close()
	readers.Wait()
	if _, err := copy.Scalar(0); err != StaleReference {
		t.Fatalf("borrow after close: %v", err)
	}
}
