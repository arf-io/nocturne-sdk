package nocturne

/*
#include <nocturne/text_layout.h>
*/
import "C"

import (
	"runtime"
	"sync"
)

type WidthChoice uint32

const (
	WidthOne WidthChoice = C.NT_WIDTH_ONE
	WidthTwo WidthChoice = C.NT_WIDTH_TWO
)

// WidthPolicy sets the ambiguous and emoji glyph widths. A zero field uses
// one cell for ambiguous glyphs or two for emoji.
type WidthPolicy struct{ Ambiguous, Emoji WidthChoice }

func (policy WidthPolicy) native() C.NtWidthPolicy {
	ambiguous, emoji := policy.Ambiguous, policy.Emoji
	if ambiguous == 0 {
		ambiguous = WidthOne
	}
	if emoji == 0 {
		emoji = WidthTwo
	}
	return C.NtWidthPolicy{ambiguous: C.NtWidthChoice(ambiguous), emoji: C.NtWidthChoice(emoji)}
}

type GlyphPresentation uint32

const (
	GlyphText         GlyphPresentation = C.NT_GLYPH_TEXT
	GlyphEmoji        GlyphPresentation = C.NT_GLYPH_EMOJI
	GlyphIsolatedMark GlyphPresentation = C.NT_GLYPH_ISOLATED_MARK
	GlyphReplacement  GlyphPresentation = C.NT_GLYPH_REPLACEMENT
	GlyphInvisible    GlyphPresentation = C.NT_GLYPH_INVISIBLE
	GlyphTab          GlyphPresentation = C.NT_GLYPH_TAB
	GlyphLineBreak    GlyphPresentation = C.NT_GLYPH_LINE_BREAK
)

type GlyphMetrics struct {
	Presentation GlyphPresentation
	Columns      uint8
}

// MeasureGlyph measures a single extended grapheme under the width policy.
func MeasureGlyph(glyph string, width WidthPolicy) (GlyphMetrics, error) {
	if err := ensureABI(); err != nil {
		return GlyphMetrics{}, err
	}
	var metrics C.NtGlyphMetrics
	status := C.nt_glyph_measure(nativeBytes(glyph), width.native(), &metrics)
	runtime.KeepAlive(glyph)
	if err := check(status); err != nil {
		return GlyphMetrics{}, err
	}
	return GlyphMetrics{Presentation: GlyphPresentation(metrics.presentation), Columns: uint8(metrics.columns)}, nil
}

type TextAlign uint32

const (
	TextAlignStart  TextAlign = C.NT_TEXT_ALIGN_START
	TextAlignCenter TextAlign = C.NT_TEXT_ALIGN_CENTER
	TextAlignEnd    TextAlign = C.NT_TEXT_ALIGN_END
)

// TextLayoutOptions controls the layout of an immutable text snapshot.
// A zero TabStop selects four columns. Zero-value bounds are unbounded.
type TextLayoutOptions struct {
	Width         WidthPolicy
	Bounds        Constraints
	Wrap          WrapMode
	Align         TextAlign
	BaseDirection Direction
	TabStop       uint32
	Ellipsis      bool
}

// LayoutCluster is a drawable cluster in visual order. Its byte range
// describes the logical source. Glyph contains its display form.
type LayoutCluster struct {
	Cluster
	Glyph     string
	Position  Point
	Columns   uint32
	BidiLevel uint8
}

// TextLayout owns an immutable snapshot of text, cluster IDs and cell geometry.
// Copies share the snapshot. Close invalidates every copy.
type TextLayout struct{ state *textLayoutState }

type textLayoutState struct {
	mu     sync.Mutex
	native *C.NtTextLayout
	uses   int
	closed bool
}

// NewTextLayout snapshots an owned Text and preserves its current cluster IDs.
func NewTextLayout(text *Text, options TextLayoutOptions) (*TextLayout, error) {
	state, err := text.borrow()
	if err != nil {
		return nil, err
	}
	defer state.mu.Unlock()
	tabStop := options.TabStop
	if tabStop == 0 {
		tabStop = 4
	}
	nativeOptions := C.NtTextLayoutOptions{
		width: options.Width.native(), bounds: options.Bounds.native(),
		wrap: C.NtWrapMode(options.Wrap), align: C.NtTextAlign(options.Align),
		base_direction: C.NtDirection(options.BaseDirection), tab_stop: C.uint32_t(tabStop),
		ellipsis: C.bool(options.Ellipsis),
	}
	var native *C.NtTextLayout
	if err := check(C.nt_text_layout_create(nil, state.native, &nativeOptions, &native)); err != nil {
		return nil, err
	}
	layout := &textLayoutState{native: native}
	runtime.SetFinalizer(layout, (*textLayoutState).close)
	return &TextLayout{state: layout}, nil
}

// LayoutText creates an owned layout directly from a counted UTF-8 string.
func LayoutText(value string, options TextLayoutOptions) (*TextLayout, error) {
	text, err := NewText(value)
	if err != nil {
		return nil, err
	}
	defer text.Close()
	return NewTextLayout(text, options)
}

func (layout *TextLayout) borrow() (*C.NtTextLayout, func(), error) {
	if layout == nil || layout.state == nil {
		return nil, nil, StaleReference
	}
	state := layout.state
	state.mu.Lock()
	defer state.mu.Unlock()
	if state.closed {
		return nil, nil, StaleReference
	}
	state.uses++
	return state.native, state.release, nil
}

func (state *textLayoutState) release() {
	state.mu.Lock()
	defer state.mu.Unlock()
	state.uses--
	state.destroyIfUnused()
}

// Size returns the measured cell extent.
func (layout *TextLayout) Size() (Size, error) {
	native, release, err := layout.borrow()
	if err != nil {
		return Size{}, err
	}
	defer release()
	return sizeFromNative(C.nt_text_layout_size(native)), nil
}

// Source returns a copy of the original counted UTF-8 bytes.
func (layout *TextLayout) Source() (string, error) {
	native, release, err := layout.borrow()
	if err != nil {
		return "", err
	}
	defer release()
	return copyBytes(C.nt_text_layout_source(native))
}

// Len returns the count of drawable entries in visual row-major order.
func (layout *TextLayout) Len() (uint, error) {
	native, release, err := layout.borrow()
	if err != nil {
		return 0, err
	}
	defer release()
	return uint(C.nt_text_layout_cluster_count(native)), nil
}

// Cluster copies one drawable entry. An ellipsis has ID zero and an
// empty byte range. A tab is one space glyph with its full cell advance.
func (layout *TextLayout) Cluster(index uint) (LayoutCluster, error) {
	native, release, err := layout.borrow()
	if err != nil {
		return LayoutCluster{}, err
	}
	defer release()
	var entry C.NtClusterView
	if err := check(C.nt_text_layout_cluster(native, C.size_t(index), &entry)); err != nil {
		return LayoutCluster{}, err
	}
	return layoutClusterFromNative(entry)
}

func layoutClusterFromNative(entry C.NtClusterView) (LayoutCluster, error) {
	glyph, err := copyBytes(entry.glyph)
	if err != nil {
		return LayoutCluster{}, err
	}
	return LayoutCluster{
		Cluster: Cluster{ID: uint64(entry.id), Begin: uint(entry.source.begin), End: uint(entry.source.end)},
		Glyph:   glyph, Position: pointFromNative(entry.position), Columns: uint32(entry.columns),
		BidiLevel: uint8(entry.bidi_level),
	}, nil
}

// Hit maps a visual grid edge to its canonical logical caret.
func (layout *TextLayout) Hit(edge Point) (Caret, error) {
	native, release, err := layout.borrow()
	if err != nil {
		return Caret{}, err
	}
	defer release()
	var caret C.NtCaret
	if err := check(C.nt_text_layout_hit(native, edge.native(), &caret)); err != nil {
		return Caret{}, err
	}
	return caretFromNative(caret), nil
}

// CaretPosition maps a visible logical caret to a visual grid edge.
func (layout *TextLayout) CaretPosition(caret Caret) (Point, error) {
	native, release, err := layout.borrow()
	if err != nil {
		return Point{}, err
	}
	defer release()
	var point C.NtCellPoint
	if err := check(C.nt_text_layout_caret_position(native, caret.native(), &point)); err != nil {
		return Point{}, err
	}
	return pointFromNative(point), nil
}

func (state *textLayoutState) close() {
	state.mu.Lock()
	defer state.mu.Unlock()
	state.closed = true
	state.destroyIfUnused()
}

func (state *textLayoutState) destroyIfUnused() {
	if state.closed && state.uses == 0 && state.native != nil {
		C.nt_text_layout_destroy(state.native)
		state.native = nil
	}
}

// Close invalidates the snapshot and releases it once admitted calls finish.
// Repeated calls are safe.
func (layout *TextLayout) Close() {
	if layout != nil && layout.state != nil {
		layout.state.close()
	}
}
