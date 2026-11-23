package nocturne

import (
	"errors"
	"testing"
)

func TestCountedText(t *testing.T) {
	const value = "x\x00👩‍💻"
	text, err := NewText(value)
	if err != nil {
		t.Fatal(err)
	}
	defer text.Close()
	if got, err := text.Value(); err != nil || got != value {
		t.Fatalf("value = %q, %v", got, err)
	}
	if count, err := text.Len(); err != nil || count != 3 {
		t.Fatalf("cluster count = %d, %v", count, err)
	}
	cluster, err := text.Cluster(2)
	if err != nil || cluster.ID == 0 || cluster.Begin != 2 || cluster.End != 13 {
		t.Fatalf("cluster = %+v, %v", cluster, err)
	}
	if err := text.Replace(3, 4, "!"); !errors.Is(err, InvalidArgument) {
		t.Fatalf("mid-scalar edit = %v", err)
	}
	if err := text.Replace(0, 1, "!"); err != nil {
		t.Fatal(err)
	}
	if got, err := text.Cluster(2); err != nil || got != cluster {
		t.Fatalf("preserved cluster = %+v, %v", got, err)
	}
	text.Close()
	if _, err := text.Value(); !errors.Is(err, StaleReference) {
		t.Fatalf("closed value = %v", err)
	}
}
