package nocturne

/*
#include <nocturne/component.h>
*/
import "C"

// Point is a cell coordinate. Size measures terminal columns and rows.
type Point struct{ X, Y int32 }
type Size struct{ Columns, Rows uint32 }

// ContinuousPoint uses fractional coordinates in caller-defined units.
type ContinuousPoint = PlotPoint

type Rect struct {
	Origin Point
	Size   Size
}

// Color holds encoded sRGB channels and straight alpha.
type Color struct{ Red, Green, Blue, Alpha uint8 }
type Style struct{ Foreground, Background Color }

func (point Point) native() C.NtCellPoint {
	return C.NtCellPoint{x: C.int32_t(point.X), y: C.int32_t(point.Y)}
}

func pointFromNative(point C.NtCellPoint) Point {
	return Point{X: int32(point.x), Y: int32(point.y)}
}

func (size Size) native() C.NtCellSize {
	return C.NtCellSize{columns: C.uint32_t(size.Columns), rows: C.uint32_t(size.Rows)}
}

func sizeFromNative(size C.NtCellSize) Size {
	return Size{Columns: uint32(size.columns), Rows: uint32(size.rows)}
}

func (rect Rect) native() C.NtCellRect {
	return C.NtCellRect{origin: rect.Origin.native(), size: rect.Size.native()}
}

func rectFromNative(rect C.NtCellRect) Rect {
	return Rect{Origin: pointFromNative(rect.origin), Size: sizeFromNative(rect.size)}
}

func (color Color) native() C.NtRgba {
	return C.NtRgba{red: C.uint8_t(color.Red), green: C.uint8_t(color.Green),
		blue: C.uint8_t(color.Blue), alpha: C.uint8_t(color.Alpha)}
}

func colorFromNative(color C.NtRgba) Color {
	return Color{Red: uint8(color.red), Green: uint8(color.green), Blue: uint8(color.blue), Alpha: uint8(color.alpha)}
}

func (style Style) native() C.NtCellStyle {
	return C.NtCellStyle{foreground: style.Foreground.native(), background: style.Background.native()}
}

func styleFromNative(style C.NtCellStyle) Style {
	return Style{Foreground: colorFromNative(style.foreground), Background: colorFromNative(style.background)}
}
