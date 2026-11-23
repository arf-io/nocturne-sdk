package nocturne

import "testing"

func TestInputDecoderOwnsRecordsAndPreservesStreamBoundaries(t *testing.T) {
	decoder, err := NewInputDecoder(nil)
	mustOK(t, err)
	defer decoder.Close()
	const start uint64 = 1<<63 + 3
	pending, err := decoder.Read([]byte{27}, start)
	mustOK(t, err)
	if pending.Status != InputPending || pending.Consumed != 1 {
		t.Fatalf("pending Escape = %+v", pending)
	}
	deadline, err := decoder.Deadline()
	mustOK(t, err)
	if !deadline.Scheduled || deadline.At != start+25000 {
		t.Fatalf("Escape deadline = %+v", deadline)
	}
	buffer := []byte("A")
	first, err := decoder.Read(buffer, deadline.At)
	mustOK(t, err)
	if first.Status != InputReady || first.Consumed != 0 || first.At != start ||
		first.Input.Kind != KeyEvent || first.Input.Key.Code != KeyEscape {
		t.Fatalf("expired Escape = %+v", first)
	}
	key, err := decoder.Read(buffer, deadline.At)
	mustOK(t, err)
	if key.Status != InputReady || key.Consumed != 1 || key.Input.Key.Code != 'a' ||
		key.Input.Key.Modifiers != ModShift || key.Input.Text != "A" {
		t.Fatalf("normalized key = %+v", key)
	}

	head, err := decoder.Read([]byte("\x1b[200~café\x00"), deadline.At+1)
	mustOK(t, err)
	if head.Status != InputPending || head.Consumed != 12 {
		t.Fatalf("pending paste = %+v", head)
	}
	paste, err := decoder.Read([]byte("\n\x1b[201~X"), deadline.At+2)
	mustOK(t, err)
	if paste.Status != InputReady || paste.Consumed != 7 || paste.Input.Kind != PasteEvent ||
		paste.Input.Text != "café\x00\n" {
		t.Fatalf("paste = %+v", paste)
	}
	mustOK(t, decoder.ExpectCursorReport(true))
	report, err := decoder.Read([]byte("\x1b[1;2R"), deadline.At+3)
	mustOK(t, err)
	if report.Status != InputDiscarded || report.Consumed != 6 {
		t.Fatalf("expected report = %+v", report)
	}
	cancel, err := decoder.Cancel(deadline.At+4, HostCancel)
	mustOK(t, err)
	if cancel.Kind != HostCancelEvent || cancel.Reason != HostCancel ||
		paste.Input.Text != "café\x00\n" || key.Input.Text != "A" {
		t.Fatalf("cancel = %+v, retained records = %+v, %+v", cancel, paste, key)
	}
	decoder.Close()
	if _, err := decoder.Read(nil, deadline.At+5); err != StaleReference {
		t.Fatalf("read after close = %v", err)
	}
}

func TestInputDecoderKeyboardOptionsAndRejection(t *testing.T) {
	options := DefaultInputDecoderOptions()
	options.Keyboard, options.PasteLimit = KeyboardKitty, 0
	decoder, err := NewInputDecoder(&options)
	mustOK(t, err)
	defer decoder.Close()
	release, err := decoder.Read([]byte("\x1b[97;1:3u"), 5)
	mustOK(t, err)
	if release.Status != InputReady || release.Input.Key.Code != 'a' ||
		release.Input.Key.State != KeyRelease || release.Input.Text != "" {
		t.Fatalf("Kitty release = %+v", release)
	}
	paste, err := decoder.Read([]byte("\x1b[200~X\x1b[201~"), 6)
	mustOK(t, err)
	if paste.Status != InputDiscarded || paste.Consumed != 13 {
		t.Fatalf("oversized paste = %+v", paste)
	}
	if _, err := decoder.Read([]byte("A"), 5); err != InvalidArgument {
		t.Fatalf("backward time = %v", err)
	}
	key, err := decoder.Read([]byte("A"), 7)
	mustOK(t, err)
	if key.Status != InputReady || key.Consumed != 1 || key.Input.Text != "A" {
		t.Fatalf("record after rejection = %+v", key)
	}
}

func TestInputDecoderTracksPointerButtonsAndCopiesUTF8(t *testing.T) {
	decoder, err := NewInputDecoder(nil)
	mustOK(t, err)
	defer decoder.Close()

	point := Point{X: 2, Y: 4}
	down, err := decoder.Read([]byte("\x1b[<0;3;5M"), 1)
	mustOK(t, err)
	if down.Status != InputReady || down.Input.Kind != PointerDownEvent ||
		down.Input.Pointer != (PointerInput{Position: point, HasPosition: true, Button: PrimaryButton, Pressed: PrimaryButton}) {
		t.Fatalf("pointer press = %+v", down)
	}
	up, err := decoder.Read([]byte("\x1b[<0;3;5m"), 2)
	mustOK(t, err)
	if up.Status != InputReady || up.Input.Kind != PointerUpEvent ||
		up.Input.Pointer.Position != point || up.Input.Pointer.Pressed != 0 {
		t.Fatalf("pointer release = %+v", up)
	}
	wheel, err := decoder.Read([]byte("\x1b[<65;3;5M"), 3)
	mustOK(t, err)
	if wheel.Status != InputReady || wheel.Input.Kind != WheelEvent ||
		wheel.Input.Wheel != (WheelInput{Position: point, DY: 1}) {
		t.Fatalf("wheel = %+v", wheel)
	}
	pending, err := decoder.Read([]byte{0xc3}, 4)
	mustOK(t, err)
	if pending.Status != InputPending || pending.Consumed != 1 {
		t.Fatalf("UTF-8 fragment = %+v", pending)
	}
	key, err := decoder.Read([]byte{0xa9}, 5)
	mustOK(t, err)
	if key.Status != InputReady || key.At != 5 || key.Input.Key.Code != 'é' || key.Input.Text != "é" {
		t.Fatalf("UTF-8 key = %+v", key)
	}
	decoder.Close()
	if key.Input.Text != "é" {
		t.Fatalf("retained key text = %q", key.Input.Text)
	}
}
