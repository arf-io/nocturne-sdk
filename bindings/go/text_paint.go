package nocturne

/*
#include <nocturne/style.h>
*/
import "C"

import "unsafe"

// TextPaintStyle supplies cell colors and optional text-local paint overrides.
type TextPaintStyle struct {
	Color                  Style
	Owner                  uint64
	Foreground, Background *PaintSource
}

func (style TextPaintStyle) native(arena *RawArena) (C.NtTextPaintStyle, error) {
	foreground, err := style.Foreground.native(arena)
	if err != nil {
		return C.NtTextPaintStyle{}, err
	}
	background, err := style.Background.native(arena)
	if err != nil {
		return C.NtTextPaintStyle{}, err
	}
	return C.NtTextPaintStyle{color: style.Color.native(), owner: C.NtOwnerToken(style.Owner),
		foreground: foreground, background: background}, nil
}

// TextPaintSpan matches the first source byte of a cluster.
type TextPaintSpan struct {
	Begin, End uint
	Style      TextPaintStyle
}

type TextPaintOptions struct {
	Base      TextPaintStyle
	Spans     []TextPaintSpan
	ElapsedUs uint64
	Motion    Motion
	Gamut     GamutPolicy
}

func (options TextPaintOptions) native(arena *RawArena) (C.NtTextPaintOptions, error) {
	base, err := options.Base.native(arena)
	if err != nil {
		return C.NtTextPaintOptions{}, err
	}
	spans, err := nativeSlice[C.NtTextPaintSpan](arena, len(options.Spans))
	if err != nil {
		return C.NtTextPaintOptions{}, err
	}
	for i, span := range options.Spans {
		style, err := span.Style.native(arena)
		if err != nil {
			return C.NtTextPaintOptions{}, err
		}
		spans[i] = C.NtTextPaintSpan{source: C.NtByteSpan{begin: C.size_t(span.Begin), end: C.size_t(span.End)},
			style: style}
	}
	return C.NtTextPaintOptions{base: base, spans: unsafe.SliceData(spans), count: C.size_t(len(spans)),
		elapsed_us: C.NtTimeUs(options.ElapsedUs), motion: C.NtMotionPolicy(options.Motion),
		gamut: C.NtGamutPolicy(options.Gamut)}, nil
}

// PaintText draws a layout at a surface-local position. The last matching
// span wins. A failed sample or range validation preserves the frame.
func (surface Surface) PaintText(at Point, layout *TextLayout, options TextPaintOptions) error {
	return surface.withView(func(arena *RawArena, view *C.NtSurfaceView) error {
		text, release, err := layout.borrow()
		if err != nil {
			return err
		}
		defer release()
		native, err := options.native(arena)
		if err != nil {
			return err
		}
		return check(C.nt_text_paint(view, at.native(), text, &native))
	})
}

// PaintText draws a layout in the active paint clip. The mounted component
// owns the contributions. A failed draw preserves the paint candidate.
func (context *Paint) PaintText(at Point, layout *TextLayout, options TextPaintOptions) error {
	if err := context.check(); err != nil {
		return err
	}
	text, release, err := layout.borrow()
	if err != nil {
		return err
	}
	defer release()
	var arena RawArena
	defer arena.close()
	native, err := options.native(&arena)
	if err != nil {
		return err
	}
	styles := C.NtTextStyles{}
	return check(C.nt_paint_styled_text(context.native, at.native(), text, &native, &styles))
}
