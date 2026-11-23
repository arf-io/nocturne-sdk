package nocturne

/*
#include "bridge.h"
*/
import "C"

import (
	"io"
	"runtime/cgo"
	"unsafe"
)

type TerminalColorMode uint32

const (
	TerminalMono      TerminalColorMode = C.NT_COLOR_MONO
	TerminalIndexed   TerminalColorMode = C.NT_COLOR_INDEXED
	TerminalTruecolor TerminalColorMode = C.NT_COLOR_TRUECOLOR
)

type TerminalCapabilities struct {
	Color                   TerminalColorMode
	Interactive, UTF8       bool
	HalfBlocks, Braille     bool
	Pointer, BracketedPaste bool
	Width                   WidthPolicy
}

func (caps TerminalCapabilities) native() C.NtTerminalCapabilities {
	return C.NtTerminalCapabilities{
		color: C.NtColorMode(caps.Color), interactive: C.bool(caps.Interactive), utf8: C.bool(caps.UTF8),
		half_blocks: C.bool(caps.HalfBlocks), braille: C.bool(caps.Braille), pointer: C.bool(caps.Pointer),
		bracketed_paste: C.bool(caps.BracketedPaste), width: caps.Width.native(),
	}
}

type ANSIOptions struct {
	Capabilities TerminalCapabilities
	Matte        Color
	Palette      []Color
}

const Xterm256Colors = C.NT_XTERM256_COLORS

func Xterm256Palette() ([Xterm256Colors]Color, error) {
	var palette [Xterm256Colors]Color
	if err := ensureABI(); err != nil {
		return palette, err
	}
	var native [Xterm256Colors]C.NtRgba
	if err := check(C.nt_xterm256_palette(&native[0], C.size_t(len(native)))); err != nil {
		return palette, err
	}
	for index, color := range native {
		palette[index] = colorFromNative(color)
	}
	return palette, nil
}

type ansiWriter struct {
	writer io.Writer
	err    error
}

// WriteANSI presents a frame synchronously through the native frame sink.
// Interactive output assumes caller ownership of the display region.
// A writer failure can leave a partially written frame.
func (frame Frame) WriteANSI(writer io.Writer, options ANSIOptions, now uint64) error {
	if writer == nil {
		return InvalidArgument
	}
	native, release, err := frame.borrow()
	if err != nil {
		return err
	}
	defer release()
	var arena RawArena
	defer arena.close()
	palette, err := nativeSlice[C.NtRgba](&arena, len(options.Palette))
	if err != nil {
		return err
	}
	for index, color := range options.Palette {
		palette[index] = color.native()
	}
	state := &ansiWriter{writer: writer}
	handle := cgo.NewHandle(state)
	defer handle.Delete()
	result := C.nt_go_present_ansi(native, C.uintptr_t(handle), options.Capabilities.native(),
		options.Matte.native(), C.NtPaletteView{colors: unsafe.SliceData(palette), count: C.size_t(len(palette))}, C.NtTimeUs(now))
	if state.err != nil {
		return state.err
	}
	return check(result)
}

//export nocturneGoANSIWrite
func nocturneGoANSIWrite(handle C.uintptr_t, view C.NtByteView) (result C.NtResult) {
	state := cgo.Handle(handle).Value().(*ansiWriter)
	defer func() {
		if cause := recover(); cause != nil {
			state.err = &PanicError{Value: cause}
			result = C.NT_RESULT_CALLBACK_FAILED
		}
	}()
	text, err := copyBytes(view)
	if err != nil {
		return callbackResult(err)
	}
	n, err := state.writer.Write([]byte(text))
	if err == nil && n != len(text) {
		err = io.ErrShortWrite
	}
	if err == nil {
		return C.NT_RESULT_OK
	}
	state.err = err
	return callbackResult(err)
}
