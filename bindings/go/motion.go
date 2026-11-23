package nocturne

/*
#include <nocturne/panel_transform.h>

static NtStatus go_path_length(const NtPoint *points, size_t count, int closed,
                                NtRepeat boundary, double *out)
{
    const NtPath path = {points, count, closed, boundary};
    return nt_path_length(&path, out);
}

static NtStatus go_path_sample(const NtPoint *points, size_t count, int closed,
                                NtRepeat boundary, double distance, NtPathSample *out)
{
    const NtPath path = {points, count, closed, boundary};
    return nt_path_sample(&path, distance, out);
}
*/
import "C"

import "unsafe"

// Path follows the straight segments between consecutive points.
// Closed paths include the segment from the last point to the first.
type Path struct {
	Points   []ContinuousPoint
	Closed   bool
	Boundary Repeat
}

type PathSample struct {
	Position, Tangent ContinuousPoint
	Distance, Length  float64
}

func (path Path) nativePoints() ([]C.NtPoint, C.int) {
	points := make([]C.NtPoint, len(path.Points))
	for i, point := range path.Points {
		points[i] = point.native()
	}
	var closed C.int
	if path.Closed {
		closed = 1
	}
	return points, closed
}

func (path Path) Length() (float64, error) {
	if err := ensureABI(); err != nil {
		return 0, err
	}
	points, closed := path.nativePoints()
	var length C.double
	status := C.go_path_length(unsafe.SliceData(points), C.size_t(len(points)), closed, C.NtRepeat(path.Boundary), &length)
	return float64(length), animationError(status)
}

// Sample resolves distance using the boundary policy and returns a travel tangent.
func (path Path) Sample(distance float64) (PathSample, error) {
	if err := ensureABI(); err != nil {
		return PathSample{}, err
	}
	points, closed := path.nativePoints()
	var sample C.NtPathSample
	status := C.go_path_sample(unsafe.SliceData(points), C.size_t(len(points)), closed, C.NtRepeat(path.Boundary),
		C.double(distance), &sample)
	return PathSample{Position: plotPointFromNative(sample.position), Tangent: plotPointFromNative(sample.tangent),
		Distance: float64(sample.distance), Length: float64(sample.length)}, animationError(status)
}

// PanelPose scales and rotates around Anchor, then translates by Offset.
// Positive turns rotate clockwise in terminal coordinates.
type PanelPose struct {
	Anchor, Offset, Scale ContinuousPoint
	Turns, Opacity        float64
}

func (pose PanelPose) native() C.NtPanelPose {
	return C.NtPanelPose{anchor: pose.Anchor.native(), offset: pose.Offset.native(), scale: pose.Scale.native(),
		turns: C.double(pose.Turns), opacity: C.double(pose.Opacity)}
}

func panelPoseFromNative(pose C.NtPanelPose) PanelPose {
	return PanelPose{Anchor: plotPointFromNative(pose.anchor), Offset: plotPointFromNative(pose.offset),
		Scale: plotPointFromNative(pose.scale), Turns: float64(pose.turns), Opacity: float64(pose.opacity)}
}

func (pose PanelPose) Project(point ContinuousPoint) (ContinuousPoint, error) {
	if err := ensureABI(); err != nil {
		return ContinuousPoint{}, err
	}
	native := pose.native()
	var out C.NtPoint
	status := C.nt_panel_project_point(&native, point.native(), &out)
	return plotPointFromNative(out), check(status)
}

type PanelTransition struct {
	From, To            PanelPose
	DelayUs, DurationUs uint64
	Easing              Easing
}

// Sample returns the eased pose at elapsedUs. Reduced motion selects To.
func (transition PanelTransition) Sample(elapsedUs uint64, motion Motion) (PanelPose, error) {
	if err := ensureABI(); err != nil {
		return PanelPose{}, err
	}
	native := C.NtPanelTransition{from: transition.From.native(), to: transition.To.native(),
		delay_us: C.NtTimeUs(transition.DelayUs), duration_us: C.NtTimeUs(transition.DurationUs),
		easing: C.NtEasing(transition.Easing)}
	var out C.NtPanelPose
	status := C.nt_panel_pose_at(&native, C.NtTimeUs(elapsedUs), C.NtMotionPolicy(motion), &out)
	return panelPoseFromNative(out), check(status)
}
