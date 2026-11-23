package nocturne

/*
#include <nocturne/text_analysis.h>
*/
import "C"

import (
	"runtime"
	"sync"
)

// LineBreak identifies a Unicode line break opportunity by a UTF-8 byte offset.
type LineBreak struct {
	Offset    uint
	Mandatory bool
}

// ScriptRun identifies a half-open UTF-8 byte range and its ISO 15924 script.
type ScriptRun struct {
	Begin, End uint
	Script     string
}

func textAnalysisRows[Native, Row any](
	read func(*Native, C.size_t, *C.size_t) C.NtResult,
	decode func(Native) Row,
) ([]Row, error) {
	var count C.size_t
	if err := check(read(nil, 0, &count)); err != nil {
		return nil, err
	}
	if count == 0 {
		return []Row{}, nil
	}
	if uint64(count) > uint64(^uint(0)>>1) {
		return nil, OutOfRange
	}
	var arena RawArena
	defer arena.close()
	rows, err := nativeSlice[Native](&arena, int(count))
	if err != nil {
		return nil, err
	}
	if err := check(read(&rows[0], count, &count)); err != nil {
		return nil, err
	}
	result := make([]Row, int(count))
	for i := range result {
		result[i] = decode(rows[i])
	}
	return result, nil
}

// LineBreaks returns default Unicode line break opportunities in byte order.
// The final entry is mandatory, including offset zero for empty input.
func LineBreaks(value string) ([]LineBreak, error) {
	if err := ensureABI(); err != nil {
		return nil, err
	}
	return textAnalysisRows(
		func(rows *C.NtLineBreak, capacity C.size_t, count *C.size_t) C.NtResult {
			result := C.nt_line_breaks(nil, nativeBytes(value), rows, capacity, count)
			runtime.KeepAlive(value)
			return result
		},
		func(row C.NtLineBreak) LineBreak {
			return LineBreak{Offset: uint(row.offset), Mandatory: bool(row.mandatory)}
		},
	)
}

// ScriptRuns partitions counted UTF-8 into grapheme-safe script runs.
func ScriptRuns(value string) ([]ScriptRun, error) {
	if err := ensureABI(); err != nil {
		return nil, err
	}
	return textAnalysisRows(
		func(rows *C.NtScriptRun, capacity C.size_t, count *C.size_t) C.NtResult {
			result := C.nt_script_runs(nil, nativeBytes(value), rows, capacity, count)
			runtime.KeepAlive(value)
			return result
		},
		func(row C.NtScriptRun) ScriptRun {
			script := string([]byte{byte(row.script[0]), byte(row.script[1]), byte(row.script[2]), byte(row.script[3])})
			return ScriptRun{Begin: uint(row.source.begin), End: uint(row.source.end), Script: script}
		},
	)
}

// Direction selects the base direction of each bidi paragraph.
type Direction int32

const (
	DirectionAuto Direction = C.NT_DIRECTION_AUTO
	DirectionLTR  Direction = C.NT_DIRECTION_LTR
	DirectionRTL  Direction = C.NT_DIRECTION_RTL

	// BidiRemovedLevel marks controls excluded from visual order by rule X9.
	BidiRemovedLevel uint8 = C.NT_BIDI_REMOVED_LEVEL
)

// BidiScalar describes a Unicode scalar in logical order before line-level resets.
type BidiScalar struct {
	Begin, End            uint
	Scalar                rune
	Level, ParagraphLevel uint8
	ParagraphSeparator    bool
}

// BidiVisual describes a scalar in visual order. DisplayScalar includes mirroring.
type BidiVisual struct {
	LogicalIndex  uint
	Begin, End    uint
	DisplayScalar rune
	Level         uint8
}

// Bidi owns an immutable analysis of Unicode paragraphs. Copies share the
// analysis, and Close releases it for every copy.
type Bidi struct{ state *bidiState }

type bidiState struct {
	mu     sync.RWMutex
	native *C.NtBidi
}

// NewBidi resolves paragraphs and stores their scalars and UTF-8 byte ranges.
func NewBidi(value string, direction Direction) (*Bidi, error) {
	if err := ensureABI(); err != nil {
		return nil, err
	}
	var native *C.NtBidi
	status := C.nt_bidi_create(nil, nativeBytes(value), C.NtDirection(direction), &native)
	runtime.KeepAlive(value)
	if err := check(status); err != nil {
		return nil, err
	}
	state := &bidiState{native: native}
	runtime.SetFinalizer(state, (*bidiState).close)
	return &Bidi{state: state}, nil
}

func (bidi *Bidi) borrow() (*bidiState, error) {
	if bidi == nil || bidi.state == nil {
		return nil, StaleReference
	}
	state := bidi.state
	state.mu.RLock()
	if state.native == nil {
		state.mu.RUnlock()
		return nil, StaleReference
	}
	return state, nil
}

// Len returns the number of scalars in logical order.
func (bidi *Bidi) Len() (uint, error) {
	state, err := bidi.borrow()
	if err != nil {
		return 0, err
	}
	defer state.mu.RUnlock()
	return uint(C.nt_bidi_count(state.native)), nil
}

// Scalar inspects a scalar at its logical index.
func (bidi *Bidi) Scalar(index uint) (BidiScalar, error) {
	state, err := bidi.borrow()
	if err != nil {
		return BidiScalar{}, err
	}
	defer state.mu.RUnlock()
	var scalar C.NtBidiScalar
	if err := check(C.nt_bidi_scalar(state.native, C.size_t(index), &scalar)); err != nil {
		return BidiScalar{}, err
	}
	return BidiScalar{
		Begin: uint(scalar.source.begin), End: uint(scalar.source.end), Scalar: rune(scalar.scalar),
		Level: uint8(scalar.level), ParagraphLevel: uint8(scalar.paragraph_level),
		ParagraphSeparator: bool(scalar.paragraph_separator),
	}, nil
}

// ReorderLine returns visual order for a half-open UTF-8 byte range.
// The line must stay within a paragraph and both ends must be scalar boundaries.
func (bidi *Bidi) ReorderLine(begin, end uint) ([]BidiVisual, error) {
	state, err := bidi.borrow()
	if err != nil {
		return nil, err
	}
	defer state.mu.RUnlock()
	line := C.NtByteSpan{begin: C.size_t(begin), end: C.size_t(end)}
	return textAnalysisRows(
		func(rows *C.NtBidiVisual, capacity C.size_t, count *C.size_t) C.NtResult {
			return C.nt_bidi_reorder_line(state.native, line, rows, capacity, count)
		},
		func(row C.NtBidiVisual) BidiVisual {
			return BidiVisual{
				LogicalIndex: uint(row.logical_index), Begin: uint(row.source.begin), End: uint(row.source.end),
				DisplayScalar: rune(row.display_scalar), Level: uint8(row.level),
			}
		},
	)
}

func (state *bidiState) close() {
	state.mu.Lock()
	defer state.mu.Unlock()
	if state.native != nil {
		C.nt_bidi_destroy(state.native)
		state.native = nil
	}
}

// Close releases the analysis. Repeated calls are safe.
func (bidi *Bidi) Close() {
	if bidi != nil && bidi.state != nil {
		bidi.state.close()
	}
}
