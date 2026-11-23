package nocturne

import (
	"bytes"
	"errors"
	"io"
	"testing"
)

type presentationWriter func([]byte) (int, error)

func (write presentationWriter) Write(data []byte) (int, error) { return write(data) }

func TestANSIWriterUsesNativeFallbackAndWriterErrors(t *testing.T) {
	frame, err := FrameFromCells(Size{Columns: 4, Rows: 1}, []ContentCell{
		{Glyph: "界", Columns: 2, Style: testInk},
		{Position: Point{X: 2}, Glyph: "A", Columns: 1, Style: testInk},
	})
	mustOK(t, err)
	defer frame.Close()
	options := ANSIOptions{Capabilities: TerminalCapabilities{Color: TerminalTruecolor}}
	var buffer bytes.Buffer
	mustOK(t, frame.Frame().WriteANSI(&buffer, options, ^uint64(0)))
	if buffer.String() != "??A \n" {
		t.Fatalf("plain frame = %q", buffer.String())
	}

	palette, err := Xterm256Palette()
	mustOK(t, err)
	if len(palette) != 256 || palette[231] != (Color{Red: 255, Green: 255, Blue: 255, Alpha: 255}) {
		t.Fatalf("terminal palette white = %+v", palette[231])
	}
	options.Capabilities = TerminalCapabilities{Color: TerminalIndexed, Interactive: true, UTF8: true}
	options.Palette = palette[:]
	buffer.Reset()
	mustOK(t, frame.Frame().WriteANSI(&buffer, options, 50))
	if !bytes.HasPrefix(buffer.Bytes(), []byte("\x1b[0m\x1b[2J")) ||
		!bytes.Contains(buffer.Bytes(), []byte("界")) || !bytes.HasSuffix(buffer.Bytes(), []byte("\x1b[0m")) {
		t.Fatalf("interactive frame = %q", buffer.String())
	}

	cause := errors.New("cannot write frame")
	calls := 0
	err = frame.Frame().WriteANSI(presentationWriter(func(data []byte) (int, error) {
		calls++
		if _, err := frame.Frame().Size(); err != WrongPhase {
			t.Fatalf("reentrant frame read = %v", err)
		}
		return 0, cause
	}), options, 50)
	if !errors.Is(err, cause) || calls != 1 {
		t.Fatalf("write error = %v, writes = %d", err, calls)
	}
	err = frame.Frame().WriteANSI(presentationWriter(func([]byte) (int, error) { return 0, nil }), options, 50)
	if !errors.Is(err, io.ErrShortWrite) {
		t.Fatalf("short write = %v", err)
	}
}

func TestANSIWriterRejectsExpiredPublishedFrames(t *testing.T) {
	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	label, err := tree.MountLabel(Node{}, MountOptions{Enabled: true}, "A", testInk)
	mustOK(t, err)
	size := Size{Columns: 1, Rows: 1}
	mustOK(t, tree.Publish(size))
	old, err := tree.Frame()
	mustOK(t, err)
	mustOK(t, tree.SetLabel(label, "B", testInk))
	mustOK(t, tree.Publish(size))
	var buffer bytes.Buffer
	if err := old.WriteANSI(&buffer, ANSIOptions{}, 0); err != StaleReference || buffer.Len() != 0 {
		t.Fatalf("expired frame = %v, bytes = %d", err, buffer.Len())
	}
	current, err := tree.Frame()
	mustOK(t, err)
	mustOK(t, current.WriteANSI(&buffer, ANSIOptions{}, 0))
	if buffer.String() != "B\n" {
		t.Fatalf("current frame = %q", buffer.String())
	}
}
