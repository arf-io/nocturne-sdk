package nocturne

/*
#include <nocturne/panel_transform.h>

extern NtResult nocturneGoSurfaceOwner(uintptr_t handle, NtOwnerToken source, NtOwnerToken *out);

static NtResult go_surface_owner(void *context, NtOwnerToken source, NtOwnerToken *out)
{
    return nocturneGoSurfaceOwner((uintptr_t)context, source, out);
}

static void go_surface_set_owner(NtSurfaceLayer *layer, uintptr_t handle)
{
    layer->map_owner = go_surface_owner;
    layer->owner_context = (void *)handle;
}

static void go_surface_panel_set_owner(NtPanelLayer *layer, uintptr_t handle)
{
    layer->map_owner = go_surface_owner;
    layer->owner_context = (void *)handle;
}
*/
import "C"

import (
	"runtime"
	"runtime/cgo"
	"unsafe"
)

// Surface is an immutable view of a mutable offscreen frame.
// It keeps its captured bounds across resize and expires when the frame closes.
type Surface struct{ state *surfaceState }

type surfaceState struct {
	frame  *offscreenState
	parent *surfaceState
	view   C.NtSurfaceView
	alpha  []byte
}

func (frame *OffscreenFrame) Surface() (Surface, error) {
	state := frame.Frame().offscreen
	native, err := state.acquire()
	if err != nil {
		return Surface{}, err
	}
	defer state.release()
	var view C.NtSurfaceView
	if err := check(C.nt_surface_root(native, &view)); err != nil {
		return Surface{}, err
	}
	view.frame = nil
	return Surface{state: &surfaceState{frame: state, view: view}}, nil
}

func (surface Surface) withView(call func(*RawArena, *C.NtSurfaceView) error) error {
	if surface.state == nil {
		return StaleReference
	}
	owner := surface.state.frame
	frame, err := owner.acquire()
	if err != nil {
		return err
	}
	defer owner.release()
	var arena RawArena
	defer arena.close()
	var ancestry []*surfaceState
	for state := surface.state; state != nil; state = state.parent {
		ancestry = append(ancestry, state)
	}
	views, err := nativeSlice[C.NtSurfaceView](&arena, len(ancestry))
	if err != nil {
		return err
	}
	for i, state := range ancestry {
		views[i] = state.view
		views[i].frame = frame
		if i+1 < len(views) {
			views[i].parent = &views[i+1]
		}
		if len(state.alpha) != 0 {
			alpha, err := nativeSlice[byte](&arena, len(state.alpha))
			if err != nil {
				return err
			}
			copy(alpha, state.alpha)
			views[i].mask.alpha = (*C.uint8_t)(unsafe.Pointer(unsafe.SliceData(alpha)))
		}
	}
	return call(&arena, &views[0])
}

// Child captures parent-local placement and owns a copy of any alpha samples.
func (surface Surface) Child(placement Rect, mask *SurfaceMask) (Surface, error) {
	var child Surface
	err := surface.withView(func(arena *RawArena, parent *C.NtSurfaceView) error {
		nativeMask, err := mask.native(arena)
		if err != nil {
			return err
		}
		var view C.NtSurfaceView
		if err := check(C.nt_surface_child(parent, placement.native(), nativeMask, &view)); err != nil {
			return err
		}
		var alpha []byte
		if mask != nil && mask.Kind == MaskAlpha {
			alpha = append(alpha, mask.Alpha...)
		}
		view.frame, view.parent, view.mask.alpha = nil, nil, nil
		child.state = &surfaceState{frame: surface.state.frame, parent: surface.state, view: view, alpha: alpha}
		return nil
	})
	return child, err
}

// Clip returns the current intersected rectangle in frame coordinates.
func (surface Surface) Clip() (Rect, error) {
	var out C.NtCellRect
	err := surface.withView(func(_ *RawArena, view *C.NtSurfaceView) error {
		return check(C.nt_surface_clip(view, &out))
	})
	return rectFromNative(out), err
}

func (surface Surface) Coverage(at Point) (float64, error) {
	var out C.double
	err := surface.withView(func(_ *RawArena, view *C.NtSurfaceView) error {
		return check(C.nt_surface_coverage(view, at.native(), &out))
	})
	return float64(out), err
}

func (surface Surface) Fill(bounds Rect, color Color, owner uint64) error {
	return surface.withView(func(_ *RawArena, view *C.NtSurfaceView) error {
		return check(C.nt_surface_fill(view, bounds.native(), color.native(), C.NtOwnerToken(owner)))
	})
}

func (surface Surface) Glyph(at Point, glyph string, columns uint8, style Style, owner uint64) error {
	return surface.withView(func(_ *RawArena, view *C.NtSurfaceView) error {
		status := C.nt_surface_glyph(view, at.native(), nativeBytes(glyph), C.uint8_t(columns), style.native(),
			C.NtOwnerToken(owner))
		runtime.KeepAlive(glyph)
		return check(status)
	})
}

func (surface Surface) EraseGlyph(at Point) error {
	return surface.withView(func(_ *RawArena, view *C.NtSurfaceView) error {
		return check(C.nt_surface_erase_glyph(view, at.native()))
	})
}

type SurfaceOwnerMode uint32

const (
	OwnersClear  SurfaceOwnerMode = C.NT_SURFACE_OWNERS_CLEAR
	OwnersShared SurfaceOwnerMode = C.NT_SURFACE_OWNERS_SHARED
	OwnersMap    SurfaceOwnerMode = C.NT_SURFACE_OWNERS_MAP
)

type SurfaceOwnerMap func(owner uint64) (uint64, error)

type SurfaceOwners struct {
	Mode SurfaceOwnerMode
	Map  SurfaceOwnerMap
}

type SurfaceLayer struct {
	Source   Frame
	Region   Rect
	Position Point
	Opacity  float64
	Mask     *SurfaceMask
	Owners   SurfaceOwners
}

type SurfacePanelLayer struct {
	PanelLayer
	Owners SurfaceOwners
}

type surfaceCallback struct {
	remap    PanelRemap
	mapOwner SurfaceOwnerMap
	cause    error
}

type surfaceCallbacks struct{ handles []cgo.Handle }

func (callbacks *surfaceCallbacks) add(call *surfaceCallback) C.uintptr_t {
	handle := cgo.NewHandle(call)
	callbacks.handles = append(callbacks.handles, handle)
	return C.uintptr_t(handle)
}

func (callbacks *surfaceCallbacks) close() {
	for _, handle := range callbacks.handles {
		handle.Delete()
	}
}

func (callbacks *surfaceCallbacks) result(status C.NtResult) error {
	err := check(status)
	for _, handle := range callbacks.handles {
		if cause := handle.Value().(*surfaceCallback).cause; cause != nil {
			return &CallError{Native: err, Callbacks: []CallbackFailure{{Phase: "paint", Cause: cause}}}
		}
	}
	return err
}

//export nocturneGoSurfaceOwner
func nocturneGoSurfaceOwner(handle C.uintptr_t, source C.NtOwnerToken, out *C.NtOwnerToken) (status C.NtResult) {
	call := cgo.Handle(handle).Value().(*surfaceCallback)
	defer func() {
		if cause := recover(); cause != nil {
			call.cause = &PanicError{Value: cause}
			status = C.NT_RESULT_CALLBACK_FAILED
		}
	}()
	owner, err := call.mapOwner(uint64(source))
	if err != nil {
		call.cause = err
		return callbackResult(err)
	}
	*out = C.NtOwnerToken(owner)
	return C.NT_RESULT_OK
}

// Compose paints layers in order. Same-frame sources sample the pre-call cells.
func (surface Surface) Compose(layers []SurfaceLayer) error {
	return surface.withView(func(arena *RawArena, view *C.NtSurfaceView) error {
		native, err := nativeSlice[C.NtSurfaceLayer](arena, len(layers))
		if err != nil {
			return err
		}
		borrows := frameBorrows{destFrame: surface.state.frame}
		defer borrows.close()
		var callbacks surfaceCallbacks
		defer callbacks.close()
		for i, layer := range layers {
			source, err := borrows.hold(layer.Source)
			if err != nil {
				return err
			}
			mask, err := layer.Mask.native(arena)
			if err != nil {
				return err
			}
			native[i] = C.NtSurfaceLayer{source: source, region: layer.Region.native(), position: layer.Position.native(),
				opacity: C.double(layer.Opacity), mask: mask, owners: C.NtSurfaceOwnerPolicy(layer.Owners.Mode)}
			if layer.Owners.Map != nil {
				C.go_surface_set_owner(&native[i], callbacks.add(&surfaceCallback{mapOwner: layer.Owners.Map}))
			}
		}
		return callbacks.result(C.nt_surface_compose(view, unsafe.SliceData(native), C.size_t(len(native))))
	})
}

// ProjectPanels transforms frame crops with masks, remaps and owner policy.
func (surface Surface) ProjectPanels(layers []SurfacePanelLayer) error {
	return surface.withView(func(arena *RawArena, view *C.NtSurfaceView) error {
		native, err := nativeSlice[C.NtPanelLayer](arena, len(layers))
		if err != nil {
			return err
		}
		borrows := frameBorrows{destFrame: surface.state.frame}
		defer borrows.close()
		var callbacks surfaceCallbacks
		defer callbacks.close()
		for i, layer := range layers {
			if err := layer.PanelLayer.writeNative(&native[i], arena, &borrows, &callbacks); err != nil {
				return err
			}
			native[i].owners = C.NtSurfaceOwnerPolicy(layer.Owners.Mode)
			if layer.Owners.Map != nil {
				C.go_surface_panel_set_owner(&native[i], callbacks.add(&surfaceCallback{mapOwner: layer.Owners.Map}))
			}
		}
		return callbacks.result(C.nt_surface_project_panels(view, unsafe.SliceData(native), C.size_t(len(native))))
	})
}
