//go:build nocturne_spatial

package nocturne

/*
#include <nocturne/spatial_mesh.h>
*/
import "C"

import (
	"sync"
	"unsafe"
)

type SpatialTextureFilter uint32

const (
	SpatialNearest  SpatialTextureFilter = C.NT_IMAGE_NEAREST
	SpatialBilinear SpatialTextureFilter = C.NT_IMAGE_BILINEAR
)

// SpatialTexture uses row-major RGBA8 pixels. Zero stride means tightly packed.
type SpatialTexture struct {
	Pixels               []Color
	Size                 Size
	Stride               uint
	Filter               SpatialTextureFilter
	Horizontal, Vertical PaintAddress
}

type SpatialMeshInstance struct {
	Mesh      *SpatialMesh
	Transform *SpatialTransform
	ObjectID  uint64
	Owner     uint64
	Tint      Color
	Texture   *SpatialTexture
}

type SpatialRasterOptions struct {
	Projection                 SpatialProjection
	Camera                     *SpatialCamera
	Samples                    Size
	Background                 Color
	SampleLimit, TriangleLimit uint
}

type SpatialRasterSample struct {
	Color                 Color
	Owner                 uint64
	Covered               bool
	ObjectID, PrimitiveID uint64
	Depth                 float64
	UV                    ContinuousPoint
}

type SpatialCellHit struct {
	SamplePosition Point
	Sample         SpatialRasterSample
}

// SpatialRaster owns the immutable sample snapshot. Copies share one owner.
type SpatialRaster struct{ state *spatialRasterState }

type spatialRasterState struct {
	mu     sync.RWMutex
	native *C.NtSpatialRaster
}

func (texture *SpatialTexture) writeNative(arena *RawArena, entry *C.NtSpatialMeshInstance) error {
	if texture == nil {
		return nil
	}
	image, err := nativeSlice[C.NtImageView](arena, 1)
	if err != nil {
		return err
	}
	pixels, err := nativeSlice[C.NtRgba](arena, len(texture.Pixels))
	if err != nil {
		return err
	}
	for i, pixel := range texture.Pixels {
		pixels[i] = pixel.native()
	}
	stride := texture.Stride
	if stride == 0 {
		stride = uint(texture.Size.Columns)
	}
	image[0] = C.NtImageView{pixels: unsafe.SliceData(pixels), size: texture.Size.native(),
		stride: C.size_t(stride), count: C.size_t(len(pixels))}
	entry.texture = &image[0]
	entry.sampling = C.NtImageSampling{filter: C.NtImageFilter(texture.Filter),
		horizontal: C.NtPaintAddress(texture.Horizontal), vertical: C.NtPaintAddress(texture.Vertical)}
	return nil
}

func (options SpatialRasterOptions) native(arena *RawArena) (C.NtSpatialRasterOptions, error) {
	input := C.NtSpatialRasterOptions{projection: options.Projection.native(), samples: options.Samples.native(),
		background: options.Background.native(), sample_limit: C.size_t(options.SampleLimit),
		triangle_limit: C.size_t(options.TriangleLimit)}
	if options.Camera != nil {
		camera, err := nativeSlice[C.NtSpatialCamera](arena, 1)
		if err != nil {
			return C.NtSpatialRasterOptions{}, err
		}
		camera[0] = options.Camera.native()
		input.camera = &camera[0]
	}
	return input, nil
}

// RasterizeSpatial copies rendered samples. The snapshot remains valid after
// all input meshes and textures are released. A zero Tint is transparent.
func RasterizeSpatial(options SpatialRasterOptions, instances []SpatialMeshInstance) (*SpatialRaster, error) {
	if err := ensureABI(); err != nil {
		return nil, err
	}
	var arena RawArena
	defer arena.close()
	input, err := options.native(&arena)
	if err != nil {
		return nil, err
	}
	draws, err := nativeSlice[C.NtSpatialMeshInstance](&arena, len(instances))
	if err != nil {
		return nil, err
	}

	borrowed := make(map[*spatialMeshState]*C.NtSpatialMesh)
	defer func() {
		for state := range borrowed {
			state.release()
		}
	}()
	for i, instance := range instances {
		if instance.Mesh == nil {
			return nil, InvalidArgument
		}
		mesh := instance.Mesh.state
		native := borrowed[mesh]
		if native == nil {
			native, err = mesh.borrow()
			if err != nil {
				return nil, err
			}
			borrowed[mesh] = native
		}
		entry := &draws[i]
		*entry = C.NtSpatialMeshInstance{mesh: native, object_id: C.uint64_t(instance.ObjectID),
			owner: C.NtOwnerToken(instance.Owner), tint: instance.Tint.native()}
		if instance.Transform != nil {
			transform, err := nativeSlice[C.NtSpatialTransform](&arena, 1)
			if err != nil {
				return nil, err
			}
			transform[0] = instance.Transform.native()
			entry.transform = &transform[0]
		}
		if err := instance.Texture.writeNative(&arena, entry); err != nil {
			return nil, err
		}
	}
	state := &spatialRasterState{}
	if err := check(C.nt_spatial_rasterize(nil, &input, unsafe.SliceData(draws), C.size_t(len(draws)), &state.native)); err != nil {
		return nil, err
	}
	return &SpatialRaster{state}, nil
}

func spatialSampleFromNative(sample C.NtSpatialRasterSample) SpatialRasterSample {
	return SpatialRasterSample{Color: colorFromNative(sample.color), Owner: uint64(sample.owner), Covered: bool(sample.covered),
		ObjectID: uint64(sample.object_id), PrimitiveID: uint64(sample.primitive_id),
		Depth: float64(sample.depth), UV: plotPointFromNative(sample.uv)}
}

func (raster *SpatialRaster) use(call func(*C.NtSpatialRaster) error) error {
	if raster == nil || raster.state == nil {
		return StaleReference
	}
	state := raster.state
	state.mu.RLock()
	defer state.mu.RUnlock()
	if state.native == nil {
		return StaleReference
	}
	return call(state.native)
}

func (raster *SpatialRaster) Size() (Size, error) {
	var size Size
	err := raster.use(func(native *C.NtSpatialRaster) error {
		size = sizeFromNative(C.nt_spatial_raster_size(native))
		return nil
	})
	return size, err
}

func (raster *SpatialRaster) CellSize() (Size, error) {
	var size Size
	err := raster.use(func(native *C.NtSpatialRaster) error {
		size = sizeFromNative(C.nt_spatial_raster_cell_size(native))
		return nil
	})
	return size, err
}

func (raster *SpatialRaster) Sample(position Point) (SpatialRasterSample, error) {
	var out C.NtSpatialRasterSample
	err := raster.use(func(native *C.NtSpatialRaster) error {
		return check(C.nt_spatial_raster_sample(native, position.native(), &out))
	})
	return spatialSampleFromNative(out), err
}

func (raster *SpatialRaster) CellHit(cell Point) (SpatialCellHit, bool, error) {
	var out C.NtSpatialCellHit
	var hit C.bool
	err := raster.use(func(native *C.NtSpatialRaster) error {
		return check(C.nt_spatial_raster_cell_hit(native, cell.native(), &hit, &out))
	})
	return SpatialCellHit{SamplePosition: pointFromNative(out.sample_position),
		Sample: spatialSampleFromNative(out.sample)}, bool(hit), err
}

// Frame returns an independent terminal frame. The caller owns it.
func (raster *SpatialRaster) Frame(mode SamplingMode) (*OffscreenFrame, error) {
	state := &offscreenState{}
	err := raster.use(func(native *C.NtSpatialRaster) error {
		return check(C.nt_spatial_raster_frame(native, C.NtSamplingMode(mode), nil, &state.native))
	})
	if err != nil {
		return nil, err
	}
	return &OffscreenFrame{state: state}, nil
}

// PaintRaster draws into the active mount using that mount's owner.
func (context *Paint) PaintRaster(raster *SpatialRaster, origin Point, mode SamplingMode) error {
	if err := context.check(); err != nil {
		return err
	}
	return raster.use(func(native *C.NtSpatialRaster) error {
		return check(C.nt_spatial_raster_paint(native, context.native, origin.native(), C.NtSamplingMode(mode)))
	})
}

func (raster *SpatialRaster) Close() {
	if raster == nil || raster.state == nil {
		return
	}
	state := raster.state
	state.mu.Lock()
	defer state.mu.Unlock()
	if state.native != nil {
		C.nt_spatial_raster_destroy(state.native)
		state.native = nil
	}
}
