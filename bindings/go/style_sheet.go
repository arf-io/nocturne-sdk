package nocturne

/*
#include <nocturne/style.h>
*/
import "C"

import (
	"runtime"
	"sync"
	"unsafe"
)

// StyleTag identifies a caller-defined role or region. Zero is a wildcard
// in selectors.
type StyleTag uint64

type StyleElement struct {
	Role, Region StyleTag
	States       uint64
}

func (element StyleElement) native() C.NtStyleElement {
	return C.NtStyleElement{role: C.NtStyleTag(element.Role), region: C.NtStyleTag(element.Region),
		states: C.uint64_t(element.States)}
}

type StyleCondition struct {
	Role, Region    StyleTag
	Require, Forbid uint64
}

func (condition StyleCondition) native() C.NtStyleCondition {
	return C.NtStyleCondition{role: C.NtStyleTag(condition.Role), region: C.NtStyleTag(condition.Region),
		require: C.uint64_t(condition.Require), forbid: C.uint64_t(condition.Forbid)}
}

type StyleRelation uint32

const (
	StyleSelf     StyleRelation = C.NT_STYLE_SELF
	StyleParent   StyleRelation = C.NT_STYLE_PARENT
	StyleAncestor StyleRelation = C.NT_STYLE_ANCESTOR
)

// ByteSpan is a half-open range of byte offsets.
type ByteSpan struct{ Begin, End uint }

func (span ByteSpan) native() C.NtByteSpan {
	return C.NtByteSpan{begin: C.size_t(span.Begin), end: C.size_t(span.End)}
}

// ScalarInterval is inclusive at both ends and uses Unicode scalar values.
type ScalarInterval struct{ First, Last uint32 }

type StyleSelector struct {
	Subject, Ancestor StyleCondition
	Relation          StyleRelation
	TextRegion        StyleTag
	ClusterID         uint64
	Bytes             *ByteSpan
	Characters        *ScalarInterval
}

func (selector StyleSelector) native() C.NtStyleSelector {
	out := C.NtStyleSelector{subject: selector.Subject.native(), ancestor: selector.Ancestor.native(),
		relation: C.NtStyleRelation(selector.Relation), text_region: C.NtStyleTag(selector.TextRegion),
		cluster_id: C.NtClusterId(selector.ClusterID)}
	if selector.Bytes != nil {
		out.match_bytes = true
		out.bytes = selector.Bytes.native()
	}
	if selector.Characters != nil {
		out.match_characters = true
		out.first_scalar = C.uint32_t(selector.Characters.First)
		out.last_scalar = C.uint32_t(selector.Characters.Last)
	}
	return out
}

type StyleRule struct {
	Selector StyleSelector
	Property PropertyID
	Value    PropertyValue
	Override bool
}

// StyleSheet owns immutable declarations. Copies share the sheet and can
// be used on independent trees. Close invalidates each copy.
type StyleSheet struct{ state *styleSheetState }

type styleSheetState struct {
	mu     sync.Mutex
	native *C.NtStyleSheet
	uses   int
	closed bool
}

// NewStyleSheet copies rules, including text values.
func NewStyleSheet(rules []StyleRule) (*StyleSheet, error) {
	if err := ensureABI(); err != nil {
		return nil, err
	}
	var arena RawArena
	defer arena.close()
	nativeRules, err := nativeSlice[C.NtStyleRule](&arena, len(rules))
	if err != nil {
		return nil, err
	}
	for i, rule := range rules {
		entry := &nativeRules[i]
		entry.selector = rule.Selector.native()
		entry.property = C.NtPropertyId(rule.Property)
		entry.override = C.bool(rule.Override)
		if err = rule.Value.writeNative(&arena, &entry.value); err != nil {
			return nil, err
		}
	}
	var native *C.NtStyleSheet
	if err = check(C.nt_style_sheet_create(nil, unsafe.SliceData(nativeRules), C.size_t(len(nativeRules)), &native)); err != nil {
		return nil, err
	}
	state := &styleSheetState{native: native}
	runtime.SetFinalizer(state, (*styleSheetState).close)
	return &StyleSheet{state: state}, nil
}

func (sheet *StyleSheet) borrow() (*C.NtStyleSheet, error) {
	if sheet == nil || sheet.state == nil {
		return nil, StaleReference
	}
	state := sheet.state
	state.mu.Lock()
	defer state.mu.Unlock()
	if state.closed {
		return nil, StaleReference
	}
	state.uses++
	return state.native, nil
}

func (state *styleSheetState) release() {
	state.mu.Lock()
	defer state.mu.Unlock()
	state.uses--
	state.destroyIfUnused()
}

func (state *styleSheetState) destroyIfUnused() {
	if state.closed && state.uses == 0 && state.native != nil {
		C.nt_style_sheet_destroy(state.native)
		state.native = nil
	}
}

func (state *styleSheetState) close() {
	state.mu.Lock()
	defer state.mu.Unlock()
	state.closed = true
	state.destroyIfUnused()
}

// Close releases the sheet after its admitted operations finish.
func (sheet *StyleSheet) Close() {
	if sheet != nil && sheet.state != nil {
		sheet.state.close()
	}
}

func withStyleSheets(arena *RawArena, sheets []*StyleSheet, call func(C.NtStyleSheets) error) error {
	items, err := nativeSlice[*C.NtStyleSheet](arena, len(sheets))
	if err != nil {
		return err
	}
	for i, sheet := range sheets {
		items[i], err = sheet.borrow()
		if err != nil {
			return err
		}
		defer sheet.state.release()
	}
	return call(C.NtStyleSheets{items: unsafe.SliceData(items), count: C.size_t(len(items))})
}

type StylePath struct {
	Element   StyleElement
	Ancestors []StyleElement // Immediate parent first.
}

func (path StylePath) native(arena *RawArena) (C.NtStylePath, error) {
	items, err := nativeSlice[C.NtStyleElement](arena, len(path.Ancestors))
	if err != nil {
		return C.NtStylePath{}, err
	}
	for i, ancestor := range path.Ancestors {
		items[i] = ancestor.native()
	}
	return C.NtStylePath{element: path.Element.native(), ancestors: unsafe.SliceData(items),
		ancestor_count: C.size_t(len(items))}, nil
}

type StyleRegion struct {
	Tag   StyleTag
	Bytes ByteSpan
}

func nativeStyleRegions(arena *RawArena, regions []StyleRegion) ([]C.NtStyleRegion, error) {
	items, err := nativeSlice[C.NtStyleRegion](arena, len(regions))
	if err != nil {
		return nil, err
	}
	for i, region := range regions {
		items[i] = C.NtStyleRegion{tag: C.NtStyleTag(region.Tag), bytes: region.Bytes.native()}
	}
	return items, nil
}

type StyleSubject struct {
	Path      StylePath
	Source    string
	Text      ByteSpan
	ClusterID uint64
	Regions   []StyleRegion
}

// ResolveStyle follows override priority, selector specificity and
// declaration order. A matching text result is an owned copy.
func ResolveStyle(sheets []*StyleSheet, subject StyleSubject, property PropertyID) (PropertyValue, bool, error) {
	if err := ensureABI(); err != nil {
		return PropertyValue{}, false, err
	}
	var arena RawArena
	defer arena.close()
	path, err := subject.Path.native(&arena)
	if err != nil {
		return PropertyValue{}, false, err
	}
	source, err := arenaByteView(&arena, subject.Source)
	if err != nil {
		return PropertyValue{}, false, err
	}
	regions, err := nativeStyleRegions(&arena, subject.Regions)
	if err != nil {
		return PropertyValue{}, false, err
	}
	native := C.NtStyleSubject{path: path, source: source, text: subject.Text.native(),
		cluster_id: C.NtClusterId(subject.ClusterID), regions: unsafe.SliceData(regions), region_count: C.size_t(len(regions))}
	var value PropertyValue
	var found C.bool
	err = withStyleSheets(&arena, sheets, func(nativeSheets C.NtStyleSheets) error {
		var out C.NtValue
		err := check(C.nt_style_resolve(nativeSheets, &native, C.NtPropertyId(property), &found, &out))
		if err != nil || !found {
			return err
		}
		value, err = propertyFromNative(&out)
		return err
	})
	return value, bool(found) && err == nil, err
}

type StyleMount struct {
	Node    Node
	Element StyleElement
}

// ApplyStyles uses the tree's current ancestry and atomically updates the
// listed properties that match. Unannotated nodes have zero tags and states.
func (tree *Tree) ApplyStyles(sheets []*StyleSheet, mounts []StyleMount, targets []PropertyRef) error {
	state, err := tree.acquire()
	if err != nil {
		return err
	}
	defer state.release()
	var arena RawArena
	defer arena.close()
	nativeMounts, err := nativeSlice[C.NtStyleMount](&arena, len(mounts))
	if err != nil {
		return err
	}
	for i, mount := range mounts {
		node, err := state.reference(mount.Node)
		if err != nil {
			return err
		}
		nativeMounts[i] = C.NtStyleMount{node: node, element: mount.Element.native()}
	}
	nativeTargets, err := state.nativePropertyRefs(&arena, targets)
	if err != nil {
		return err
	}
	return withStyleSheets(&arena, sheets, func(nativeSheets C.NtStyleSheets) error {
		return state.finish(C.nt_tree_apply_styles(state.native, nativeSheets, unsafe.SliceData(nativeMounts),
			C.size_t(len(nativeMounts)), unsafe.SliceData(nativeTargets), C.size_t(len(nativeTargets))))
	})
}

type TextStyles struct {
	Sheets  []*StyleSheet
	Path    StylePath
	Regions []StyleRegion
}

func drawStyledText(arena *RawArena, layout *TextLayout, options TextPaintOptions, styles TextStyles,
	draw func(*C.NtTextLayout, *C.NtTextPaintOptions, *C.NtTextStyles) C.NtResult) error {
	text, release, err := layout.borrow()
	if err != nil {
		return err
	}
	defer release()
	native, err := options.native(arena)
	if err != nil {
		return err
	}
	path, err := styles.Path.native(arena)
	if err != nil {
		return err
	}
	regions, err := nativeStyleRegions(arena, styles.Regions)
	if err != nil {
		return err
	}
	return withStyleSheets(arena, styles.Sheets, func(sheets C.NtStyleSheets) error {
		spec := C.NtTextStyles{sheets: sheets, path: path, regions: unsafe.SliceData(regions),
			region_count: C.size_t(len(regions))}
		return check(draw(text, &native, &spec))
	})
}

// PaintStyledText overlays sheet colors on visible clusters and preserves
// the span or base owner.
func (surface Surface) PaintStyledText(at Point, layout *TextLayout, options TextPaintOptions, styles TextStyles) error {
	return surface.withView(func(arena *RawArena, view *C.NtSurfaceView) error {
		return drawStyledText(arena, layout, options, styles,
			func(text *C.NtTextLayout, paint *C.NtTextPaintOptions, spec *C.NtTextStyles) C.NtResult {
				return C.nt_style_text_paint(view, at.native(), text, paint, spec)
			})
	})
}

// PaintStyledText overlays sheet colors within a mounted component's paint
// clip. The mounted component owns the contributions.
func (context *Paint) PaintStyledText(at Point, layout *TextLayout, options TextPaintOptions, styles TextStyles) error {
	if err := context.check(); err != nil {
		return err
	}
	var arena RawArena
	defer arena.close()
	return drawStyledText(&arena, layout, options, styles,
		func(text *C.NtTextLayout, paint *C.NtTextPaintOptions, spec *C.NtTextStyles) C.NtResult {
			return C.nt_paint_styled_text(context.native, at.native(), text, paint, spec)
		})
}
