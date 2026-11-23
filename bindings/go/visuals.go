package nocturne

/*
#include <nocturne/particle_effect.h>
#include <nocturne/plot.h>

static NtResult go_plot_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount,
                              NtPlotOptions options, NtNodeId *out)
{
    return nt_plot_mount(tree, parent, mount, &options, out);
}

static NtResult go_plot_set(NtTree *tree, NtNodeId node, NtPlotOptions options)
{
    return nt_plot_set(tree, node, &options);
}
*/
import "C"

import (
	"runtime"
	"unsafe"
)

type PlotKind uint32

const (
	PlotBars    PlotKind = C.NT_PLOT_BARS
	PlotPie     PlotKind = C.NT_PLOT_PIE
	PlotDonut   PlotKind = C.NT_PLOT_DONUT
	PlotScatter PlotKind = C.NT_PLOT_SCATTER
	PlotLine    PlotKind = C.NT_PLOT_LINE
	PlotEdges   PlotKind = C.NT_PLOT_EDGES
)

type PlotDatum struct {
	X, Y  float64
	Color Color
}

type SamplingMode uint32

const (
	SampleCell      SamplingMode = C.NT_SAMPLE_CELL
	SampleHalfBlock SamplingMode = C.NT_SAMPLE_HALF_BLOCK
	SampleBraille   SamplingMode = C.NT_SAMPLE_BRAILLE
	SampleHybrid    SamplingMode = C.NT_SAMPLE_HYBRID
)

type PlotDomain struct{ Min, Max float64 }
type PlotBounds struct{ X, Y PlotDomain }

func (bounds PlotBounds) native() C.NtPlotBounds {
	return C.NtPlotBounds{x: C.NtPlotDomain{min: C.double(bounds.X.Min), max: C.double(bounds.X.Max)},
		y: C.NtPlotDomain{min: C.double(bounds.Y.Min), max: C.double(bounds.Y.Max)}}
}

// PlotPoint uses continuous data or cell-edge coordinates.
type PlotPoint struct{ X, Y float64 }

func (point PlotPoint) native() C.NtPoint {
	return C.NtPoint{x: C.double(point.X), y: C.double(point.Y)}
}

func plotPointFromNative(point C.NtPoint) PlotPoint {
	return PlotPoint{X: float64(point.x), Y: float64(point.y)}
}

type PlotViewport struct {
	Domain     PlotBounds
	Cells      Rect
	CellAspect float64
	EqualUnits bool
}

// Project projects an inclusive data-domain point to continuous cell edges.
func (view PlotViewport) Project(point PlotPoint) (PlotPoint, error) {
	if err := ensureABI(); err != nil {
		return PlotPoint{}, err
	}
	native := C.NtPlotViewport{domain: view.Domain.native(), cells: view.Cells.native(),
		cell_aspect: C.double(view.CellAspect), equal_units: C.bool(view.EqualUnits)}
	var out C.NtPoint
	err := check(C.nt_plot_project(&native, point.native(), &out))
	return plotPointFromNative(out), err
}

type PlotSegment struct{ First, Last PlotPoint }

// ClipSegment clips a directed segment to the inclusive data domain.
// The returned segment is meaningful only when visible is true.
func (domain PlotBounds) ClipSegment(segment PlotSegment) (PlotSegment, bool, error) {
	if err := ensureABI(); err != nil {
		return PlotSegment{}, false, err
	}
	var visible C.bool
	var first, last C.NtPoint
	err := check(C.nt_plot_clip_segment(domain.native(), segment.First.native(), segment.Last.native(), &visible, &first, &last))
	return PlotSegment{First: plotPointFromNative(first), Last: plotPointFromNative(last)}, bool(visible), err
}

type PlotReveal struct {
	StartUs, DurationUs uint64
	Easing              Easing
}

type PlotOptions struct {
	DataLimit, SampleStride uint
	AutoX, AutoY            bool
	Sampling                SamplingMode
	Reveal                  PlotReveal
	Kind                    PlotKind
	Data                    []PlotDatum
	Domain                  *PlotBounds
	Baseline, HoleRatio     float64
	CellAspect              float64
	ASCII, EqualUnits       bool
	SampleColors            bool
	Preferred               Size
	Foreground, Background  Color
}

func (plot PlotOptions) native() (C.NtPlotOptions, []C.NtPlotDatum) {
	data := make([]C.NtPlotDatum, len(plot.Data))
	for index, datum := range plot.Data {
		data[index] = C.NtPlotDatum{point: C.NtPoint{x: C.double(datum.X), y: C.double(datum.Y)},
			color: datum.Color.native()}
	}
	options := C.NtPlotOptions{kind: C.NtPlotKind(plot.Kind), data: unsafe.SliceData(data),
		count: C.size_t(len(data)), data_limit: C.size_t(plot.DataLimit), sample_stride: C.size_t(plot.SampleStride),
		auto_x: C.bool(plot.Domain == nil || plot.AutoX), auto_y: C.bool(plot.Domain == nil || plot.AutoY),
		baseline: C.double(plot.Baseline), hole_ratio: C.double(plot.HoleRatio), cell_aspect: C.double(plot.CellAspect),
		ascii: C.bool(plot.ASCII), equal_units: C.bool(plot.EqualUnits), sample_colors: C.bool(plot.SampleColors),
		sampling: C.NtSamplingMode(plot.Sampling), preferred: plot.Preferred.native(),
		foreground: plot.Foreground.native(), background: plot.Background.native(),
		reveal: C.NtPlotReveal{start_us: C.NtTimeUs(plot.Reveal.StartUs),
			duration_us: C.NtTimeUs(plot.Reveal.DurationUs), easing: C.NtEasing(plot.Reveal.Easing)}}
	if plot.Domain != nil {
		options.domain = plot.Domain.native()
	}
	return options, data
}

// MountPlot copies the dataset. A nil domain selects the automatic axes.
func (tree *Tree) MountPlot(parent Node, options MountOptions, plot PlotOptions) (Node, error) {
	return tree.mountNative(parent, func(tree *C.NtTree, parent C.NtNodeId, out *C.NtNodeId) C.NtResult {
		native, data := plot.native()
		result := C.go_plot_mount(tree, parent, options.native(), native, out)
		runtime.KeepAlive(data)
		return result
	})
}

func (tree *Tree) SetPlot(node Node, plot PlotOptions) error {
	return tree.callNode(node, func(tree *C.NtTree, id C.NtNodeId) C.NtResult {
		native, data := plot.native()
		result := C.go_plot_set(tree, id, native)
		runtime.KeepAlive(data)
		return result
	})
}

// DigitsOptions displays a signed fixed-point integer in five-row strokes.
type DigitsOptions struct {
	Value                  int64
	FractionalDigits       uint8
	ASCII                  bool
	Foreground, Background Color
}

func (digits DigitsOptions) native() C.NtDigitsOptions {
	return C.NtDigitsOptions{value: C.int64_t(digits.Value), fractional_digits: C.uint8_t(digits.FractionalDigits),
		ascii: C.bool(digits.ASCII), foreground: digits.Foreground.native(), background: digits.Background.native()}
}

func (tree *Tree) MountDigits(parent Node, options MountOptions, digits DigitsOptions) (Node, error) {
	return tree.mountNative(parent, func(tree *C.NtTree, parent C.NtNodeId, out *C.NtNodeId) C.NtResult {
		native := digits.native()
		return C.nt_digits_mount(tree, parent, options.native(), &native, out)
	})
}

func (tree *Tree) SetDigits(node Node, digits DigitsOptions) error {
	return tree.callNode(node, func(tree *C.NtTree, id C.NtNodeId) C.NtResult {
		native := digits.native()
		return C.nt_digits_set(tree, id, &native)
	})
}

type ParticleRecipe uint32

const (
	ParticleBurst    ParticleRecipe = C.NT_PARTICLE_RECIPE_BURST
	ParticleNova     ParticleRecipe = C.NT_PARTICLE_RECIPE_NOVA
	ParticleGlitter  ParticleRecipe = C.NT_PARTICLE_RECIPE_GLITTER
	ParticleRain     ParticleRecipe = C.NT_PARTICLE_RECIPE_RAIN
	ParticleSnow     ParticleRecipe = C.NT_PARTICLE_RECIPE_SNOW
	ParticleEmbers   ParticleRecipe = C.NT_PARTICLE_RECIPE_EMBERS
	ParticleLeaves   ParticleRecipe = C.NT_PARTICLE_RECIPE_LEAVES
	ParticleSparks   ParticleRecipe = C.NT_PARTICLE_RECIPE_SPARKS
	ParticleFirework ParticleRecipe = C.NT_PARTICLE_RECIPE_FIREWORK
)

type ParticleEffectOptions struct {
	Recipe                ParticleRecipe
	Seed, Stream, StartUs uint64
	Preferred             *Size
}

func (effect ParticleEffectOptions) native() (C.NtParticleEffectOptions, C.NtResult) {
	var native C.NtParticleEffectOptions
	if result := C.nt_particle_effect_recipe(C.NtParticleRecipe(effect.Recipe), &native); result != C.NT_RESULT_OK {
		return native, result
	}
	native.view.emitter.seed = C.uint64_t(effect.Seed)
	native.view.emitter.stream = C.uint64_t(effect.Stream)
	native.view.emitter.start_us = C.NtTimeUs(effect.StartUs)
	if effect.Preferred != nil {
		native.view.preferred = effect.Preferred.native()
	}
	return native, C.NT_RESULT_OK
}

func (tree *Tree) MountParticleEffect(parent Node, options MountOptions, effect ParticleEffectOptions) (Node, error) {
	return tree.mountNative(parent, func(tree *C.NtTree, parent C.NtNodeId, out *C.NtNodeId) C.NtResult {
		native, result := effect.native()
		if result != C.NT_RESULT_OK {
			return result
		}
		return C.nt_particle_effect_mount(tree, parent, options.native(), &native, out)
	})
}

func (tree *Tree) SetParticleEffect(node Node, effect ParticleEffectOptions) error {
	return tree.callNode(node, func(tree *C.NtTree, id C.NtNodeId) C.NtResult {
		native, result := effect.native()
		if result != C.NT_RESULT_OK {
			return result
		}
		return C.nt_particle_effect_set(tree, id, &native)
	})
}
