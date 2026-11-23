package main

import (
	"errors"
	"fmt"

	"github.com/arf-io/nocturne-sdk/bindings/go"
)

var ink = nocturne.Style{
	Foreground: nocturne.Color{Red: 230, Green: 240, Blue: 255, Alpha: 255},
	Background: nocturne.Color{Red: 16, Green: 24, Blue: 32, Alpha: 255},
}

const payload = "x\x00é"

var paintRefusal = errors.New("fixture paint refusal")

type meter struct {
	nocturne.ComponentBase
	level, keys, updates, disposals int
	elapsed                         uint64
	failPaint                       bool
}

func (*meter) Measure(*nocturne.Measure, nocturne.Constraints) (nocturne.Size, error) {
	return nocturne.Size{Columns: 5, Rows: 1}, nil
}

func (m *meter) Update(info nocturne.UpdateInfo) (nocturne.UpdateReply, error) {
	m.updates++
	m.elapsed = info.Elapsed
	return nocturne.UpdateReply{
		Invalidate: nocturne.InvalidatePaint,
		Next:       nocturne.Deadline{Scheduled: m.updates == 1, At: info.Now + 250},
	}, nil
}

func (m *meter) Paint(paint *nocturne.Paint, _ nocturne.Size) error {
	if m.failPaint {
		return paintRefusal
	}
	if err := paint.Glyph(nocturne.Point{}, fmt.Sprint(m.level), 1, ink); err != nil {
		return err
	}
	return paint.Glyph(nocturne.Point{X: 1}, "界", 2, ink)
}

func (m *meter) Event(event *nocturne.Event) error {
	if event.Kind() != nocturne.KeyEvent {
		return nil
	}
	key, err := event.Key()
	if err != nil || key.Code != '+' {
		return err
	}
	m.keys++
	m.level++
	mustOK(event.Consume())
	mustOK(event.Invalidate(nocturne.InvalidatePaint))
	return event.Emit(41, payload)
}

func (m *meter) Dispose() error {
	m.disposals++
	return nil
}

func mustOK(err error) {
	if err != nil {
		panic(err)
	}
}

func must[T any](value T, err error) T {
	mustOK(err)
	return value
}

func require(test bool, reason string) {
	if !test {
		panic(reason)
	}
}

func bit(test bool) int {
	if test {
		return 1
	}
	return 0
}

func recordFrame(tree *nocturne.Tree, stage string) {
	frame := must(tree.Frame())
	size := must(frame.Size())
	label := must(frame.Cell(nocturne.Point{X: 2}))
	meter := must(frame.Cell(nocturne.Point{X: 2, Y: 1}))
	level := must(frame.Cell(nocturne.Point{Y: 1}))
	fmt.Println(stage, size.Columns, size.Rows, label.Lead.X, label.Lead.Y, label.Columns, label.Glyph,
		meter.Lead.X, meter.Lead.Y, meter.Columns, meter.Glyph, level.Glyph)
}

func replay(tree *nocturne.Tree, m *meter) {
	passive := nocturne.MountOptions{Enabled: true}
	options := nocturne.MountOptions{Enabled: true, Focusable: true, HitTestable: true}
	parent := must(tree.MountBox(nocturne.Node{}, passive, nocturne.Box{Kind: nocturne.BoxColumn}))
	must(tree.MountLabel(parent, passive, "A👩‍💻", ink))
	node := must(tree.Mount(parent, options, m))
	mustOK(tree.Tick(1000, nocturne.MotionFull))
	mustOK(tree.Publish(nocturne.Size{Columns: 7, Rows: 3}))
	recordFrame(tree, "initial")
	deadline := must(tree.NextUpdate())
	require(deadline.Scheduled, "expected first deadline")
	mustOK(tree.SetFocus(node))

	key := nocturne.KeyInput{Code: '+', State: nocturne.KeyPress}
	input := nocturne.Input{Kind: nocturne.KeyEvent, Key: key, Text: "!"}
	policy := nocturne.DispatchPolicy{Reserved: []nocturne.KeyChord{{Code: '+'}}}
	reserved := must(tree.Dispatch(1000, input, policy))
	_, queued, err := tree.PeekIntent()
	mustOK(err)
	require(m.keys == 0 && !queued, "reserved key reached callback")
	input.Text = ""
	delivery := must(tree.Dispatch(1000, input, nocturne.DispatchPolicy{}))
	intent, queued, err := tree.PeekIntent()
	mustOK(err)
	require(queued && intent.Source == node && intent.Payload == payload, "wrong intent")
	fmt.Println("event", bit(reserved.Reserved), bit(reserved.Consumed), bit(reserved.TextDelivered),
		bit(delivery.Target == node), bit(delivery.Consumed), bit(delivery.TextDelivered),
		m.keys, intent.Kind, len(intent.Payload))
	mustOK(tree.PopIntent())
	_, queued, err = tree.PeekIntent()
	mustOK(err)
	require(!queued, "intent pop did not empty the queue")

	mustOK(tree.Tick(1250, nocturne.MotionFull))
	fmt.Println("clock", deadline.At, m.updates, m.elapsed)
	m.failPaint = true
	size := nocturne.Size{Columns: 5, Rows: 2}
	failure := tree.Publish(size)
	var call *nocturne.CallError
	require(errors.As(failure, &call), "failed paint did not report a callback error")
	require(len(call.Callbacks) == 1 && errors.Is(call.Callbacks[0].Cause, paintRefusal), "paint cause lost")
	code, ok := call.Native.(nocturne.Result)
	require(ok, "paint result lost")
	fmt.Println("failure", int32(code))
	recordFrame(tree, "held")
	m.failPaint = false
	mustOK(tree.Publish(size))
	recordFrame(tree, "updated")
	mustOK(tree.Remove(node))
	for range 512 {
		next := must(tree.Mount(parent, options, m))
		require(next != node, "stale node identity reused")
		mustOK(tree.Remove(next))
	}
	must(tree.Mount(parent, options, m))
}

func sampleAnimation() {
	tween := nocturne.Tween{
		From: -3.25, To: 8.5, DurationUs: 1000, DelayUs: 100,
		Easing: nocturne.Sine, Repeat: nocturne.PingPong,
	}
	for _, elapsed := range []uint64{0, 100, 433, 1100, 1433, 2100} {
		sample := must(nocturne.SampleTween(tween, elapsed))
		fmt.Printf("sample %d %.17g %.17g %d %d %d\n", elapsed, sample.Value, sample.Progress,
			sample.Iteration, sample.Phase, sample.Direction)
	}
	loop := nocturne.Tween{From: 0, To: 1, DurationUs: 1, Repeat: nocturne.Loop}
	fmt.Println("iteration", must(nocturne.SampleTween(loop, ^uint64(0))).Iteration)
	loop.DurationUs = 0
	_, err := nocturne.SampleTween(loop, 0)
	code, ok := err.(nocturne.Result)
	require(ok, "zero-duration loop returned no native error")
	fmt.Println("invalid", int32(code))
}

func main() {
	tree := must(nocturne.NewTree())
	defer tree.Close()
	m := &meter{level: 4}
	replay(tree, m)
	mustOK(tree.Close())
	fmt.Println("dispose", m.disposals)
	sampleAnimation()
}
