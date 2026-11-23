//go:build nocturne_spatial

package nocturne

import (
	"errors"
	"math"
	"testing"
	"time"
)

func TestSpatialMeshCloseDuringBorrow(t *testing.T) {
	mesh, err := NewSpatialMesh(SpatialMeshData{})
	mustOK(t, err)
	native, err := mesh.state.borrow()
	mustOK(t, err)
	defer mesh.state.release()
	if native == nil {
		t.Fatal("borrow has no mesh")
	}
	closed := make(chan struct{})
	go func() {
		mesh.Close()
		close(closed)
	}()
	select {
	case <-closed:
	case <-time.After(3 * time.Second):
		t.Fatal("close waited for an already-admitted borrower")
	}
	if _, err := mesh.Data(); err != StaleReference {
		t.Fatalf("read after close = %v", err)
	}
	mesh.state.mu.Lock()
	preserved := mesh.state.native == native
	mesh.state.mu.Unlock()
	if !preserved {
		t.Fatal("close released the admitted handle")
	}
}

func TestSpatialMeshTextureAndSnapshot(t *testing.T) {
	data := SpatialMeshData{
		Vertices: []SpatialVertex{
			{Vec3{-2, -2, -2}, ContinuousPoint{0, 0}},
			{Vec3{2, -2, -2}, ContinuousPoint{1, 0}},
			{Vec3{0, 2, -4}, ContinuousPoint{1, 1}},
		},
		Faces: []SpatialFace{{[3]uint32{0, 1, 2}, math.MaxUint64}},
	}
	mesh, err := NewSpatialMesh(data)
	mustOK(t, err)
	defer mesh.Close()
	data.Vertices[1].Position.X = 999
	owned, err := mesh.Data()
	mustOK(t, err)
	if owned.Vertices[1].Position.X != 2 || owned.Faces[0].PrimitiveID != math.MaxUint64 {
		t.Fatalf("mesh data = %+v", owned)
	}
	data.Faces[0].Indices[2] = 3
	if _, err := NewSpatialMesh(data); err != InvalidArgument {
		t.Fatalf("face index = %v", err)
	}

	grid := SpatialRasterOptions{Projection: SpatialProjection{SpatialPerspective, 1, 1, 1, 8},
		Samples: Size{3, 3}, Background: Color{4, 8, 16, 255}}
	texture := &SpatialTexture{Size: Size{2, 2}, Stride: 3, Pixels: []Color{
		{0, 255, 0, 255}, {1, 2, 3, 255}, {}, {100, 100, 100, 255}, {240, 120, 60, 255},
	}}
	white := Color{255, 255, 255, 255}
	instances := []SpatialMeshInstance{
		{Mesh: mesh, ObjectID: math.MaxUint64, Owner: 1 << 63, Texture: texture, Tint: white},
		{Mesh: mesh, ObjectID: 72, Owner: 82, Tint: white},
	}
	budget := grid
	budget.SampleLimit = 8
	if _, err := RasterizeSpatial(budget, instances); err != OutOfRange {
		t.Fatalf("sample limit = %v", err)
	}
	budget.SampleLimit, budget.TriangleLimit = 9, 1
	if _, err := RasterizeSpatial(budget, instances); err != OutOfRange {
		t.Fatalf("triangle limit = %v", err)
	}
	raster, err := RasterizeSpatial(grid, instances)
	mustOK(t, err)
	defer raster.Close()
	texture.Pixels[4] = Color{}
	mesh.Close()
	if _, err := mesh.Data(); err != StaleReference || owned.Vertices[1].Position.X != 2 {
		t.Fatalf("mesh close: %v, copied data %+v", err, owned)
	}
	if _, err := RasterizeSpatial(grid, instances); err != StaleReference {
		t.Fatalf("closed mesh draw = %v", err)
	}
	want := SpatialRasterSample{Color: Color{240, 120, 60, 255}, Covered: true, Owner: 1 << 63,
		ObjectID: math.MaxUint64, PrimitiveID: math.MaxUint64, Depth: 3, UV: ContinuousPoint{.75, .5}}
	sample, err := raster.Sample(Point{1, 1})
	if got := must(t, sample, err); got != want {
		t.Fatalf("sample = %+v, want %+v", got, want)
	}
	miss, err := raster.Sample(Point{1, 0})
	if got := must(t, miss, err); got != (SpatialRasterSample{Color: grid.Background}) {
		t.Fatalf("miss = %+v", got)
	}
	frame, err := raster.Frame(SampleBraille)
	mustOK(t, err)
	defer func() { mustOK(t, frame.Close()) }()
	raster.Close()
	size, err := frame.Frame().Size()
	if got := must(t, size, err); got != (Size{2, 1}) {
		t.Fatalf("owned frame size = %+v", got)
	}
	if _, err := raster.Sample(Point{}); err != StaleReference {
		t.Fatalf("closed raster = %v", err)
	}
}

func TestSpatialOBJAndMountedRaster(t *testing.T) {
	const obj = "v -4 -4 -2\nv 4 -4 -2\nv 0 6 -2\nvt 1 0\nvt 0 1\nf -3/1 -2/2 -1\n"
	mesh, err := DecodeSpatialOBJ(obj)
	mustOK(t, err)
	defer mesh.Close()
	data, err := mesh.Data()
	mustOK(t, err)
	if len(data.Vertices) != 3 || len(data.Faces) != 1 || data.Faces[0].PrimitiveID != 0 ||
		data.Vertices[0].UV != (ContinuousPoint{1, 0}) {
		t.Fatalf("OBJ data = %+v", data)
	}
	if _, err := DecodeSpatialOBJ(obj + "\x00"); err != InvalidArgument {
		t.Fatalf("OBJ NUL = %v", err)
	}
	camera, err := SpatialLookAt(Vec3{Z: 1}, Vec3{}, Vec3{Y: 1})
	mustOK(t, err)
	basis := DefaultSpatialTransformOptions()
	basis.Translation.Z = 1
	translated, err := NewSpatialTransform(basis)
	mustOK(t, err)
	grid := SpatialRasterOptions{Projection: SpatialProjection{SpatialOrthographic, 1, 1, 1, 3},
		Samples: Size{5, 7}, Background: Color{14, 17, 19, 255}, Camera: &camera}
	raster, err := RasterizeSpatial(grid, []SpatialMeshInstance{
		{Mesh: mesh, Transform: &translated, ObjectID: 50, Owner: 81, Tint: Color{30, 80, 160, 255}},
	})
	mustOK(t, err)
	defer raster.Close()
	cells, err := raster.CellSize()
	if got := must(t, cells, err); got != (Size{3, 2}) {
		t.Fatalf("cell extent = %v", got)
	}
	hit, found, err := raster.CellHit(Point{2, 1})
	if err != nil || !found || hit.SamplePosition != (Point{4, 5}) ||
		hit.Sample.ObjectID != 50 || hit.Sample.Owner != 81 || hit.Sample.Depth != 2 {
		t.Fatalf("cell hit = %+v, %v, %v", hit, found, err)
	}
	clear, err := RasterizeSpatial(grid, nil)
	mustOK(t, err)
	defer clear.Close()
	if _, found, err := clear.CellHit(Point{}); found || err != nil {
		t.Fatalf("empty cell = %v, %v", found, err)
	}
	if _, _, err := clear.CellHit(Point{X: -1}); err != OutOfRange {
		t.Fatalf("outside cell = %v", err)
	}

	direct, err := raster.Frame(SampleBraille)
	mustOK(t, err)
	defer func() { mustOK(t, direct.Close()) }()
	tree, err := NewTree()
	mustOK(t, err)
	defer func() { mustOK(t, tree.Close()) }()
	mode := SampleBraille
	var escaped *Paint
	node, err := tree.Mount(Node{}, MountOptions{Enabled: true}, &panelPaintFixture{draw: func(paint *Paint) error {
		escaped = paint
		return paint.PaintRaster(raster, Point{X: -1}, mode)
	}})
	mustOK(t, err)
	mustOK(t, tree.Publish(Size{2, 2}))
	out, err := tree.Frame()
	mustOK(t, err)
	cell, err := out.Cell(Point{})
	mustOK(t, err)
	reference, err := direct.Frame().Cell(Point{X: 1})
	mustOK(t, err)
	if cell.Glyph != reference.Glyph || cell.Style != reference.Style || cell.Owner == 0 || cell.Owner == reference.Owner {
		t.Fatalf("mounted cell %+v, independent reference %+v", cell, reference)
	}
	if err := escaped.PaintRaster(raster, Point{}, mode); err != StaleReference {
		t.Fatalf("expired paint = %v", err)
	}
	mode = SamplingMode(99)
	mustOK(t, tree.Invalidate(node, InvalidatePaint))
	if err := tree.Publish(Size{2, 2}); !errors.Is(err, InvalidArgument) {
		t.Fatalf("failed publication = %v", err)
	}
	previous, err := out.Cell(Point{})
	if got := must(t, previous, err); got != cell {
		t.Fatalf("previous cell changed: %+v", got)
	}
}
