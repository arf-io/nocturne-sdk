package nocturne

/*
#include <nocturne/component.h>
#include <nocturne/panel_transform.h>

extern NtResult nocturneGoPanelRemap(uintptr_t handle, NtPanelSample *sample, NtPanelPixel *out);

static NtResult go_panel_remap(void *context, const NtPanelSample *sample, NtPanelPixel *out)
{
    return nocturneGoPanelRemap((uintptr_t)context, (NtPanelSample *)sample, out);
}

static void go_panel_set_remap(NtPanelLayer *layer, uintptr_t handle)
{
    layer->remap = go_panel_remap;
    layer->remap_context = (void *)handle;
}
*/
import "C"

import (
	"runtime/cgo"
	"unsafe"
)

type SurfaceMaskKind uint32

const (
	MaskRectangle        SurfaceMaskKind = C.NT_MASK_RECTANGLE
	MaskEllipse          SurfaceMaskKind = C.NT_MASK_ELLIPSE
	MaskDiamond          SurfaceMaskKind = C.NT_MASK_DIAMOND
	MaskRoundedRectangle SurfaceMaskKind = C.NT_MASK_ROUNDED_RECTANGLE
	MaskAlpha            SurfaceMaskKind = C.NT_MASK_ALPHA
)

// SurfaceMask clips a layer in viewport-local cells. Alpha is row-major.
type SurfaceMask struct {
	Kind   SurfaceMaskKind
	Bounds Rect
	Radius float64
	Alpha  []byte
	Stride uint
}

func (mask *SurfaceMask) native(arena *RawArena) (*C.NtSurfaceMask, error) {
	if mask == nil {
		return nil, nil
	}
	alpha, err := nativeSlice[byte](arena, len(mask.Alpha))
	if err != nil {
		return nil, err
	}
	copy(alpha, mask.Alpha)
	record, err := nativeSlice[C.NtSurfaceMask](arena, 1)
	if err != nil {
		return nil, err
	}
	record[0] = C.NtSurfaceMask{kind: C.NtSurfaceMaskKind(mask.Kind), bounds: mask.Bounds.native(),
		radius: C.double(mask.Radius), alpha: (*C.uint8_t)(unsafe.Pointer(unsafe.SliceData(alpha))),
		stride: C.size_t(mask.Stride), count: C.size_t(len(alpha))}
	return &record[0], nil
}

type PanelSample struct {
	Destination, Source      ContinuousPoint
	ViewportSize, RegionSize Size
	ElapsedUs                uint64
}

type PanelPixel struct {
	Source  ContinuousPoint
	Opacity float64
}

type PanelRemap func(PanelSample) (PanelPixel, error)

// PanelLayer projects a published frame crop into the paint context's viewport.
type PanelLayer struct {
	Source           Frame
	Region, Viewport Rect
	Pose             PanelPose
	ElapsedUs        uint64
	Mask             *SurfaceMask
	Remap            PanelRemap
}

//export nocturneGoPanelRemap
func nocturneGoPanelRemap(handle C.uintptr_t, sample *C.NtPanelSample, out *C.NtPanelPixel) (status C.NtResult) {
	call := cgo.Handle(handle).Value().(*surfaceCallback)
	defer func() {
		if cause := recover(); cause != nil {
			call.cause = &PanicError{Value: cause}
			status = C.NT_RESULT_CALLBACK_FAILED
		}
	}()
	pixel, err := call.remap(PanelSample{
		Destination: plotPointFromNative(sample.destination), Source: plotPointFromNative(sample.source),
		ViewportSize: sizeFromNative(sample.viewport_size), RegionSize: sizeFromNative(sample.region_size),
		ElapsedUs: uint64(sample.elapsed_us),
	})
	if err != nil {
		call.cause = err
		return callbackResult(err)
	}
	*out = C.NtPanelPixel{source: pixel.Source.native(), opacity: C.double(pixel.Opacity)}
	return C.NT_RESULT_OK
}

// ProjectPanels stages all layers atomically and uses the mounted node's owner.
// Source frames and mask storage are borrowed for the call. Remappers are synchronous.
func (context *Paint) ProjectPanels(layers []PanelLayer) error {
	if err := context.check(); err != nil {
		return err
	}
	context.scope.drawing = true
	defer func() { context.scope.drawing = false }()
	var arena RawArena
	defer arena.close()
	native, err := nativeSlice[C.NtPanelLayer](&arena, len(layers))
	if err != nil {
		return err
	}
	borrows := frameBorrows{paintTree: context.owner}
	defer borrows.close()
	var callbacks surfaceCallbacks
	defer callbacks.close()
	for i, layer := range layers {
		if err := layer.writeNative(&native[i], &arena, &borrows, &callbacks); err != nil {
			return err
		}
	}
	status := C.nt_paint_project_panels(context.native, unsafe.SliceData(native), C.size_t(len(native)))
	return callbacks.result(status)
}

func (layer PanelLayer) writeNative(out *C.NtPanelLayer, arena *RawArena, borrows *frameBorrows, callbacks *surfaceCallbacks) error {
	frame, err := borrows.hold(layer.Source)
	if err != nil {
		return err
	}
	mask, err := layer.Mask.native(arena)
	if err != nil {
		return err
	}
	*out = C.NtPanelLayer{source: frame, region: layer.Region.native(), viewport: layer.Viewport.native(),
		pose: layer.Pose.native(), elapsed_us: C.NtTimeUs(layer.ElapsedUs), mask: mask}
	if layer.Remap != nil {
		C.go_panel_set_remap(out, callbacks.add(&surfaceCallback{remap: layer.Remap}))
	}
	return nil
}
