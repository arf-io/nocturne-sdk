package nocturne

/*
#include <nocturne/content.h>
*/
import "C"

import "unsafe"

// ContentCell describes a glyph or, with zero columns, a background-only cell.
type ContentCell struct {
	Position Point
	Glyph    string
	Columns  uint8
	Style    Style
	Owner    uint64
}

// FrameFromCells copies the supplied cells into an owned frame in slice order.
func FrameFromCells(size Size, cells []ContentCell) (*OffscreenFrame, error) {
	if err := ensureABI(); err != nil {
		return nil, err
	}
	var arena RawArena
	defer arena.close()
	native, err := nativeSlice[C.NtContentCell](&arena, len(cells))
	if err != nil {
		return nil, err
	}
	for index, cell := range cells {
		glyph, err := arenaByteView(&arena, cell.Glyph)
		if err != nil {
			return nil, err
		}
		native[index] = C.NtContentCell{position: cell.Position.native(), glyph: glyph, columns: C.uint8_t(cell.Columns),
			style: cell.Style.native(), owner: C.NtOwnerToken(cell.Owner)}
	}
	state := &offscreenState{}
	if err := check(C.nt_frame_from_cells(nil, size.native(), unsafe.SliceData(native), C.size_t(len(native)), &state.native)); err != nil {
		return nil, err
	}
	return &OffscreenFrame{state: state}, nil
}

type ImageFilter uint32

const (
	ImageNearest  ImageFilter = C.NT_IMAGE_NEAREST
	ImageBilinear ImageFilter = C.NT_IMAGE_BILINEAR
)

type ImageSampling struct {
	Filter               ImageFilter
	Horizontal, Vertical PaintAddress
}

func (sampling ImageSampling) native() C.NtImageSampling {
	return C.NtImageSampling{filter: C.NtImageFilter(sampling.Filter), horizontal: C.NtPaintAddress(sampling.Horizontal),
		vertical: C.NtPaintAddress(sampling.Vertical)}
}

// ImageView describes a straight RGBA8 rectangle. Stride is measured in pixels.
type ImageView struct {
	Pixels []Color
	Size   Size
	Stride uint
}

func (image ImageView) native(arena *RawArena) (C.NtImageView, error) {
	pixels, err := nativeSlice[C.NtRgba](arena, len(image.Pixels))
	if err != nil {
		return C.NtImageView{}, err
	}
	for index, pixel := range image.Pixels {
		pixels[index] = pixel.native()
	}
	return C.NtImageView{pixels: unsafe.SliceData(pixels), size: image.Size.native(), stride: C.size_t(image.Stride),
		count: C.size_t(len(pixels))}, nil
}

// Sample reads the image at normalized coordinates.
func (image ImageView) Sample(sampling ImageSampling, u, v float64) (Color, error) {
	if err := ensureABI(); err != nil {
		return Color{}, err
	}
	var arena RawArena
	defer arena.close()
	native, err := image.native(&arena)
	if err != nil {
		return Color{}, err
	}
	var out C.NtRgba
	err = check(C.nt_image_sample(&native, sampling.native(), C.double(u), C.double(v), &out))
	return colorFromNative(out), err
}

// Rasterize samples the image into an owned frame with 2-by-4 sample tiles.
func (image ImageView) Rasterize(sampling ImageSampling, extent Size, mode SamplingMode, owner uint64) (*OffscreenFrame, error) {
	if err := ensureABI(); err != nil {
		return nil, err
	}
	var arena RawArena
	defer arena.close()
	native, err := image.native(&arena)
	if err != nil {
		return nil, err
	}
	state := &offscreenState{}
	err = check(C.nt_image_rasterize(&native, sampling.native(), extent.native(), C.NtSamplingMode(mode),
		C.NtOwnerToken(owner), nil, &state.native))
	if err != nil {
		return nil, err
	}
	return &OffscreenFrame{state: state}, nil
}
