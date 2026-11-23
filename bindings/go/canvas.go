package nocturne

/*
#include <nocturne/canvas.h>
*/
import "C"

import (
	"runtime"
	"strings"
	"sync"
	"unsafe"
)

// ParseSamplingMode resolves a native mode name: cell, half-block, braille or hybrid.
func ParseSamplingMode(name string) (SamplingMode, error) {
	if err := ensureABI(); err != nil {
		return 0, err
	}
	if strings.IndexByte(name, 0) >= 0 {
		return 0, InvalidArgument
	}
	terminated := name + "\x00"
	var out C.NtSamplingMode
	status := C.nt_sampling_mode_parse((*C.char)(unsafe.Pointer(unsafe.StringData(terminated))), &out)
	runtime.KeepAlive(terminated)
	return SamplingMode(out), check(status)
}

type CanvasPixel struct {
	Color   Color
	Owner   uint64
	Covered bool
}

// CellSamples holds an entire 2-by-4 tile in row-major order.
type CellSamples struct {
	Pixels     [8]CanvasPixel
	Background [8]Color
}

type SampledCell struct {
	Glyph string
	Style Style
	Owner uint64
}

func ReduceCellSamples(tile CellSamples, mode SamplingMode) (SampledCell, error) {
	if err := ensureABI(); err != nil {
		return SampledCell{}, err
	}
	var input C.NtCellSamples
	for index, pixel := range tile.Pixels {
		input.pixels[index] = C.NtCanvasPixel{color: pixel.Color.native(),
			owner: C.NtOwnerToken(pixel.Owner), covered: C.bool(pixel.Covered)}
		input.background[index] = tile.Background[index].native()
	}
	var out C.NtSampledCell
	if err := check(C.nt_cell_sample(&input, C.NtSamplingMode(mode), &out)); err != nil {
		return SampledCell{}, err
	}
	return SampledCell{Glyph: C.GoStringN(&out.glyph[0], C.int(out.glyph_size)),
		Style: styleFromNative(out.style), Owner: uint64(out.owner)}, nil
}

// CanvasInk specifies sample color and owner. Nil Composite selects the native
// source-over default in linear sRGB.
type CanvasInk struct {
	Color     Color
	Owner     uint64
	Composite *CompositeOptions
}

// Canvas owns an RGBA sample grid. Copies share the same owner.
type Canvas struct{ state *canvasState }

type canvasState struct {
	mu     sync.Mutex
	native *C.NtCanvas
}

func NewCanvas(size Size) (*Canvas, error) {
	if err := ensureABI(); err != nil {
		return nil, err
	}
	state := &canvasState{}
	if err := check(C.nt_canvas_create(nil, size.native(), &state.native)); err != nil {
		return nil, err
	}
	return &Canvas{state}, nil
}

func (canvas *Canvas) call(operation func(*C.NtCanvas) C.NtResult) error {
	if canvas == nil || canvas.state == nil {
		return StaleReference
	}
	state := canvas.state
	state.mu.Lock()
	defer state.mu.Unlock()
	if state.native == nil {
		return StaleReference
	}
	return check(operation(state.native))
}

func (canvas *Canvas) Close() {
	if canvas == nil || canvas.state == nil {
		return
	}
	state := canvas.state
	state.mu.Lock()
	defer state.mu.Unlock()
	C.nt_canvas_destroy(state.native)
	state.native = nil
}

func (canvas *Canvas) Size() (Size, error) {
	var size Size
	err := canvas.call(func(value *C.NtCanvas) C.NtResult {
		size = sizeFromNative(C.nt_canvas_size(value))
		return C.NT_RESULT_OK
	})
	return size, err
}

func (canvas *Canvas) Clear(background Color) error {
	return canvas.call(func(value *C.NtCanvas) C.NtResult {
		return C.nt_canvas_clear(value, background.native())
	})
}

func (canvas *Canvas) Pixel(at Point) (CanvasPixel, error) {
	var out C.NtCanvasPixel
	err := canvas.call(func(value *C.NtCanvas) C.NtResult {
		return C.nt_canvas_pixel(value, at.native(), &out)
	})
	return CanvasPixel{Color: colorFromNative(out.color), Owner: uint64(out.owner), Covered: bool(out.covered)}, err
}

func (canvas *Canvas) draw(ink CanvasInk, draw func(*C.NtCanvas, C.NtCanvasInk) C.NtResult) error {
	return canvas.call(func(canvas *C.NtCanvas) C.NtResult {
		native := C.NtCanvasInk{color: ink.Color.native(), owner: C.NtOwnerToken(ink.Owner)}
		if ink.Composite != nil {
			var arena RawArena
			defer arena.close()
			composite, err := nativeSlice[C.NtCompositeOptions](&arena, 1)
			if err != nil {
				return C.NT_RESULT_ALLOCATION_FAILED
			}
			composite[0] = *ink.Composite.native()
			native.composite = &composite[0]
		}
		return draw(canvas, native)
	})
}

func (canvas *Canvas) Point(at Point, ink CanvasInk) error {
	return canvas.draw(ink, func(value *C.NtCanvas, ink C.NtCanvasInk) C.NtResult {
		return C.nt_canvas_point(value, at.native(), ink)
	})
}

func (canvas *Canvas) Line(from, to Point, ink CanvasInk) error {
	return canvas.draw(ink, func(value *C.NtCanvas, ink C.NtCanvasInk) C.NtResult {
		return C.nt_canvas_line(value, from.native(), to.native(), ink)
	})
}

func (canvas *Canvas) Triangle(a, b, c Point, ink CanvasInk) error {
	return canvas.draw(ink, func(value *C.NtCanvas, ink C.NtCanvasInk) C.NtResult {
		return C.nt_canvas_triangle(value, a.native(), b.native(), c.native(), ink)
	})
}

func (canvas *Canvas) Fill(bounds Rect, ink CanvasInk) error {
	return canvas.draw(ink, func(value *C.NtCanvas, ink C.NtCanvasInk) C.NtResult {
		return C.nt_canvas_fill(value, bounds.native(), ink)
	})
}

func (canvas *Canvas) Border(bounds Rect, ink CanvasInk) error {
	return canvas.draw(ink, func(value *C.NtCanvas, ink C.NtCanvasInk) C.NtResult {
		return C.nt_canvas_border(value, bounds.native(), ink)
	})
}

// Render returns a frame that owns its cells independently of the canvas.
func (canvas *Canvas) Render(mode SamplingMode) (*OffscreenFrame, error) {
	state := &offscreenState{}
	if err := canvas.call(func(value *C.NtCanvas) C.NtResult {
		return C.nt_canvas_render(value, C.NtSamplingMode(mode), nil, &state.native)
	}); err != nil {
		return nil, err
	}
	return &OffscreenFrame{state}, nil
}
