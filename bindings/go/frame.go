package nocturne

/*
#include <nocturne/component.h>
#include <nocturne/frame.h>
*/
import "C"

// Frame reads a published tree snapshot or a live owned offscreen frame.
// A published snapshot expires on the next successful publication or close.
// An offscreen reference sees the current cells until the frame closes.
type Frame struct {
	tree      Tree
	revision  uint64
	offscreen *offscreenState
}

type Cell struct {
	Glyph   string
	Lead    Point
	Columns uint8
	Owner   uint64
	Style   Style
}

func (tree *Tree) Frame() (Frame, error) {
	state, err := tree.acquire()
	if err != nil {
		return Frame{}, err
	}
	defer state.release()
	if C.nt_tree_frame(state.native) == nil {
		return Frame{}, StaleReference
	}
	return Frame{tree: *tree, revision: state.revision}, nil
}

func (frame Frame) borrow() (*C.NtFrame, func(), error) {
	if frame.offscreen != nil {
		native, err := frame.offscreen.acquire()
		if err != nil {
			return nil, nil, err
		}
		return native, frame.offscreen.release, nil
	}
	state, err := frame.tree.acquire()
	if err != nil {
		return nil, nil, err
	}
	native, err := frame.borrowedNative(state)
	if err != nil {
		state.release()
		return nil, nil, err
	}
	return native, state.release, nil
}

// borrowedNative uses a tree which the caller already holds.
func (frame Frame) borrowedNative(state *treeState) (*C.NtFrame, error) {
	if state.revision != frame.revision {
		return nil, StaleReference
	}
	native := C.nt_tree_frame(state.native)
	if native == nil {
		return nil, StaleReference
	}
	return native, nil
}

func (frame Frame) Size() (Size, error) {
	native, release, err := frame.borrow()
	if err != nil {
		return Size{}, err
	}
	defer release()
	return sizeFromNative(C.nt_frame_size(native)), nil
}

func (frame Frame) Cell(at Point) (Cell, error) {
	native, release, err := frame.borrow()
	if err != nil {
		return Cell{}, err
	}
	defer release()
	var out C.NtCellView
	if err := check(C.nt_frame_cell(native, at.native(), &out)); err != nil {
		return Cell{}, err
	}
	glyph, err := copyBytes(out.glyph)
	if err != nil {
		return Cell{}, err
	}
	return Cell{Glyph: glyph, Lead: pointFromNative(out.lead), Columns: uint8(out.glyph_columns),
		Owner: uint64(out.owner), Style: styleFromNative(out.style)}, nil
}

type CellBackground struct {
	Color Color
	Owner uint64
}

// Background reads a physical cell's background and its separate owner.
func (frame Frame) Background(at Point) (CellBackground, error) {
	native, release, err := frame.borrow()
	if err != nil {
		return CellBackground{}, err
	}
	defer release()
	var out C.NtCellBackground
	status := C.nt_frame_background(native, at.native(), &out)
	return CellBackground{Color: colorFromNative(out.color), Owner: uint64(out.owner)}, check(status)
}

// frameBorrows shares leases across layers of an atomic native call.
type frameBorrows struct {
	paintTree *treeState
	destFrame *offscreenState
	held      map[any]func()
}

func (borrows *frameBorrows) hold(frame Frame) (*C.NtFrame, error) {
	if state := frame.offscreen; state != nil {
		if state != borrows.destFrame && borrows.held[state] == nil {
			if _, err := state.acquire(); err != nil {
				return nil, err
			}
			borrows.save(state, state.release)
		}
		return state.native, nil
	}
	state := frame.tree.state
	if state == nil {
		return nil, StaleReference
	}
	if state != borrows.paintTree && borrows.held[state] == nil {
		if _, err := frame.tree.acquire(); err != nil {
			return nil, err
		}
		borrows.save(state, state.release)
	}
	return frame.borrowedNative(state)
}

func (borrows *frameBorrows) save(owner any, release func()) {
	if borrows.held == nil {
		borrows.held = make(map[any]func())
	}
	borrows.held[owner] = release
}

func (borrows *frameBorrows) close() {
	for _, release := range borrows.held {
		release()
	}
}
