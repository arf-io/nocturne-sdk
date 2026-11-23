package nocturne

import (
	"bytes"
	"encoding/binary"
	"testing"
)

func TestStateSnapshotOwnedBytesAndAtomicRestore(t *testing.T) {
	const original = "e\u0301\x00界"
	source, err := NewTree()
	mustOK(t, err)
	defer source.Close()
	open := MountOptions{Enabled: true}
	root, err := source.MountBox(Node{}, open, Box{Kind: BoxColumn})
	mustOK(t, err)
	label, err := source.MountLabel(root, open, original, testInk)
	mustOK(t, err)
	progress, err := source.MountProgress(root, open, Progress{Value: 9, Maximum: 10})
	mustOK(t, err)
	from := []PropertyRef{{Node: label, ID: TextProperty}, {Node: progress, ID: ValueProperty}}
	captured, err := source.CaptureState(from)
	mustOK(t, err)
	defer captured.Close()
	if duplicate, err := source.CaptureState([]PropertyRef{from[0], from[0]}); err != InvalidArgument || duplicate != nil {
		t.Fatalf("duplicate source = %v, %v", duplicate, err)
	}
	mustOK(t, source.Close())
	raw, err := captured.Bytes()
	mustOK(t, err)
	want := append([]byte(nil), raw...)
	if len(want) < 16 || string(want[:4]) != "NTST" || binary.LittleEndian.Uint32(want[4:8]) != StateSchemaVersion {
		t.Fatalf("invalid header %x", want)
	}
	raw[0] = 0
	later, err := captured.Bytes()
	if !bytes.Equal(must(t, later, err), want) {
		t.Fatal("mutating an exported value modified the snapshot")
	}
	captured.Close()
	if _, err := captured.Bytes(); err != StaleReference {
		t.Fatalf("closed capture export = %v", err)
	}
	oldVersion := append([]byte(nil), want...)
	oldVersion[4] = StateSchemaVersion + 1
	if bad, err := ImportStateSnapshot(oldVersion); err != UnsupportedCapability || bad != nil {
		t.Fatalf("unsupported version = %v, %v", bad, err)
	}
	for length := range len(want) {
		if bad, err := ImportStateSnapshot(want[:length]); err != InvalidArgument || bad != nil {
			t.Fatalf("truncated length %d = %v, %v", length, bad, err)
		}
	}
	if bad, err := ImportStateSnapshot(append(append([]byte(nil), want...), 0)); err != InvalidArgument || bad != nil {
		t.Fatalf("trailing payload = %v, %v", bad, err)
	}
	loaded, err := ImportStateSnapshot(later)
	mustOK(t, err)
	defer loaded.Close()
	clear(later)

	target, err := NewTree()
	mustOK(t, err)
	defer target.Close()
	root, err = target.MountBox(Node{}, open, Box{Kind: BoxColumn})
	mustOK(t, err)
	label, err = target.MountLabel(root, open, "before", testInk)
	mustOK(t, err)
	progress, err = target.MountProgress(root, open, Progress{Value: 4, Maximum: 5})
	mustOK(t, err)
	to := []PropertyRef{{Node: label, ID: TextProperty}, {Node: progress, ID: ValueProperty}}
	mustOK(t, target.Publish(Size{Columns: 20, Rows: 6}))
	frame, err := target.Frame()
	mustOK(t, err)
	before, err := frame.Cell(Point{})
	mustOK(t, err)
	if err := target.RestoreState(to, loaded); err != OutOfRange {
		t.Fatalf("restore rejected by domain = %v", err)
	}
	untouched, err := target.Property(label, TextProperty)
	if must(t, untouched, err).Text != "before" {
		t.Fatalf("failed restore modified the label: %+v", untouched)
	}
	counter, err := target.Property(progress, ValueProperty)
	if must(t, counter, err).Unsigned != 4 {
		t.Fatalf("failed restore modified the progress: %+v", counter)
	}
	after, err := frame.Cell(Point{})
	if must(t, after, err) != before {
		t.Fatalf("failed restore modified the published frame: %+v", after)
	}
	mustOK(t, target.Remove(progress))
	if err := target.RestoreState(to, loaded); err != StaleReference {
		t.Fatalf("restore on removed destination = %v", err)
	}
	progress, err = target.MountProgress(root, open, Progress{Maximum: 10})
	mustOK(t, err)
	to[1].Node = progress
	if err := target.RestoreState([]PropertyRef{to[1], to[0]}, loaded); err != InvalidArgument {
		t.Fatalf("restore with wrong order = %v", err)
	}
	mustOK(t, target.RestoreState(to, loaded))
	again, err := target.CaptureState(to)
	mustOK(t, err)
	defer again.Close()
	recaptured, err := again.Bytes()
	if !bytes.Equal(must(t, recaptured, err), want) {
		t.Fatalf("recaptured bytes = %x, want %x", recaptured, want)
	}
	copied := *loaded
	loaded.Close()
	if err := target.RestoreState(to, &copied); err != StaleReference {
		t.Fatalf("restore a closed shared snapshot = %v", err)
	}
	if err := target.RestoreState(to, nil); err != StaleReference {
		t.Fatalf("restore a nil snapshot = %v", err)
	}
	empty, err := target.CaptureState(nil)
	mustOK(t, err)
	defer empty.Close()
	mustOK(t, target.RestoreState(nil, empty))
}
