package nocturne

import (
	"errors"
	"slices"
	"testing"
)

type nativeFixture struct {
	t     *testing.T
	arena *RawArena
}

func (f nativeFixture) value(kind string) RawAddress {
	address, err := f.arena.New(kind)
	return must(f.t, address, err)
}

func (f nativeFixture) pointer(address RawAddress) RawAddress {
	slot := f.value("pointer")
	mustOK(f.t, f.arena.SetPointer(slot, address))
	return slot
}

func (f nativeFixture) set(value RawAddress, kind, name string, bits uint64) {
	field, err := f.arena.API().Field(kind, name)
	field = must(f.t, field, err)
	mustOK(f.t, f.arena.SetUnsigned(value+RawAddress(field.Offset), field.Size, bits))
}

func (f nativeFixture) get(value RawAddress, kind, name string) uint64 {
	field, err := f.arena.API().Field(kind, name)
	field = must(f.t, field, err)
	bits, err := f.arena.Unsigned(value+RawAddress(field.Offset), field.Size)
	return must(f.t, bits, err)
}

func (f nativeFixture) bytes(value string) RawAddress {
	address, err := f.arena.ByteView(value)
	return must(f.t, address, err)
}

func (f nativeFixture) call(name string, args ...RawAddress) RawAddress {
	result, err := f.arena.Call(name, args...)
	return must(f.t, result, err)
}

func TestRawStockTree(t *testing.T) {
	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	container, err := tree.MountBox(Node{}, MountOptions{Enabled: true}, Box{Kind: BoxColumn})
	mustOK(t, err)
	var escaped *RawTree
	var checkBox, progress Node
	mustOK(t, tree.WithNative(func(scope *RawTree) error {
		escaped = scope
		f := nativeFixture{t, scope.RawArena}
		handle, err := scope.Handle()
		mustOK(t, err)
		parent, err := scope.Node(container)
		mustOK(t, err)
		options := f.value("NtMountOptions")
		f.set(options, "NtMountOptions", "enabled", 1)
		f.set(options, "NtMountOptions", "focusable", 1)
		f.set(options, "NtMountOptions", "hit_testable", 1)
		out := f.value("NtNodeId")
		mustOK(t, scope.Result("nt_checkbox_mount", handle, parent, options, f.bytes("Archive"),
			f.pointer(0), f.value("_Bool"), f.pointer(out)))
		checkBox, err = scope.Adopt(out)
		mustOK(t, err)
		data := f.value("NtProgressOptions")
		f.set(data, "NtProgressOptions", "maximum", 10)
		f.set(data, "NtProgressOptions", "value", 5)
		out = f.value("NtNodeId")
		mustOK(t, scope.Result("nt_progress_mount", handle, parent, options, data, f.pointer(out)))
		progress, err = scope.Adopt(out)
		mustOK(t, err)
		if err := tree.SetFocus(checkBox); !errors.Is(err, WrongPhase) {
			t.Fatalf("managed reentry: %v", err)
		}
		return nil
	}))
	if _, err := escaped.Handle(); !errors.Is(err, StaleReference) {
		t.Fatalf("escaped scope: %v", err)
	}
	mustOK(t, tree.Publish(Size{Columns: 16, Rows: 3}))
	mustOK(t, tree.SetFocus(checkBox))
	mustOK(t, tree.WithNative(func(scope *RawTree) error {
		f := nativeFixture{t, scope.RawArena}
		handle, err := scope.Handle()
		mustOK(t, err)
		node, err := scope.Node(checkBox)
		mustOK(t, err)
		value := f.value("_Bool")
		mustOK(t, scope.SetUnsigned(value, 1, 1))
		mustOK(t, scope.Result("nt_checkbox_set", handle, node, value))
		out := f.value("_Bool")
		mustOK(t, scope.Result("nt_checkbox_get", handle, node, f.pointer(out)))
		bits, err := scope.Unsigned(out, 1)
		if got := must(t, bits, err); got != 1 {
			t.Fatalf("checkbox state = %d", got)
		}
		id, err := scope.Node(progress)
		mustOK(t, err)
		bar := f.value("NtProgressOptions")
		mustOK(t, scope.Result("nt_progress_get", handle, id, f.pointer(bar)))
		if got := f.get(bar, "NtProgressOptions", "value"); got != 5 {
			t.Fatalf("progress state = %d", got)
		}
		return nil
	}))
	mustOK(t, tree.Remove(checkBox))
	mustOK(t, tree.Remove(progress))
}

func TestRawHeadersAndEditor(t *testing.T) {
	api, err := NativeAPI()
	mustOK(t, err)
	for _, name := range api.Functions() {
		signature, _ := api.Signature(name)
		if signature.Result != "void" {
			_, err := api.Size(signature.Result)
			mustOK(t, err)
		}
		for _, arg := range signature.Args {
			_, err := api.Size(arg.Type)
			mustOK(t, err)
		}
	}
	pointerSize, err := api.Size("pointer")
	mustOK(t, err)
	none, found := api.Constant("NT_CHOICE_NONE")
	if !found || none != ^uint64(0)>>(64-8*pointerSize) {
		t.Fatalf("wrong choice sentinel: %x", none)
	}
	mustOK(t, api.WithArena(func(arena *RawArena) error {
		f := nativeFixture{t, arena}
		out := f.value("pointer")
		mustOK(t, arena.Result("nt_editor_create", f.pointer(0), f.bytes("note"), f.pointer(0), f.pointer(out)))
		defer f.call("nt_editor_destroy", out)
		mustOK(t, arena.Result("nt_editor_replace_selection", out, f.bytes("☾")))
		text := f.call("nt_editor_text", out)
		view := f.call("nt_text_bytes", text)
		count := f.get(view, "NtByteView", "count")
		data := f.get(view, "NtByteView", "data")
		bytes, err := arena.Read(RawAddress(data), int(count))
		if got := string(must(t, bytes, err)); got != "☾note" {
			t.Fatalf("editor bytes = %q", got)
		}
		if !slices.Contains(api.Modules(), "simulation") {
			_, err := arena.Call("nt_thermal_exchange")
			if !errors.Is(err, UnsupportedCapability) {
				t.Fatalf("absent simulation = %v", err)
			}
			return nil
		}
		stores := f.value("NtThermalPair")
		capacity, err := api.Field("NtThermalStore", "heat_capacity")
		mustOK(t, err)
		energy, err := api.Field("NtThermalStore", "energy")
		mustOK(t, err)
		for _, name := range []string{"first", "second"} {
			store, err := api.Field("NtThermalPair", name)
			mustOK(t, err)
			mustOK(t, arena.SetFloat64(stores+RawAddress(store.Offset+capacity.Offset), 2))
			mustOK(t, arena.SetFloat64(stores+RawAddress(store.Offset+energy.Offset), 100))
		}
		conductance, elapsed, result := f.value("double"), f.value("NtTimeUs"), f.value("NtThermalPair")
		mustOK(t, arena.SetFloat64(conductance, 4))
		mustOK(t, arena.SetUnsigned(elapsed, 8, 1000000))
		mustOK(t, arena.Result("nt_thermal_exchange", f.pointer(stores), conductance, elapsed, f.pointer(result)))
		value, err := arena.Float64(result + RawAddress(energy.Offset))
		if got := must(t, value, err); got != 100 {
			t.Fatalf("equilibrium energy = %g", got)
		}
		return nil
	}))
}

func TestRawFrameSinkCallback(t *testing.T) {
	tree, err := NewTree()
	mustOK(t, err)
	defer tree.Close()
	_, err = tree.MountLabel(Node{}, MountOptions{Enabled: true}, "☾", testInk)
	mustOK(t, err)
	mustOK(t, tree.Publish(Size{Columns: 2, Rows: 1}))
	api, err := NativeAPI()
	mustOK(t, err)
	var escaped *RawCallbackFrame
	var received uint64
	var fail bool
	cause := errors.New("sink refused")
	const callback = "NtFrameSink.present"
	callbacks, err := api.NewCallbacks(map[string]RawCallback{
		callback: func(frame *RawCallbackFrame) error {
			escaped = frame
			now, err := frame.Argument(1)
			if err != nil {
				return err
			}
			received, err = frame.Unsigned(now, 8)
			if fail && err == nil {
				panic(cause)
			}
			return err
		},
	})
	mustOK(t, err)
	defer callbacks.Close()
	mustOK(t, tree.WithNative(func(scope *RawTree) error {
		f := nativeFixture{t, scope.RawArena}
		address, err := callbacks.Function(callback)
		mustOK(t, err)
		sink := f.value("NtFrameSink")
		f.set(sink, "NtFrameSink", "context", uint64(callbacks.Context()))
		f.set(sink, "NtFrameSink", "present", uint64(address))
		handle, err := scope.Handle()
		mustOK(t, err)
		frame := f.call("nt_tree_frame", handle)
		now := f.value("NtTimeUs")
		const instant = uint64(9007199254740993)
		mustOK(t, scope.SetUnsigned(now, 8, instant))
		mustOK(t, scope.Result("nt_frame_present", f.pointer(sink), frame, now))
		if received != instant {
			t.Fatalf("native callback time = %d", received)
		}
		fail = true
		if err := scope.Result("nt_frame_present", f.pointer(sink), frame, now); !errors.Is(err, CallbackFailed) {
			t.Fatalf("callback failure = %v", err)
		}
		if failures := callbacks.TakeErrors(); len(failures) != 1 || !errors.Is(failures[0], cause) {
			t.Fatalf("foreign callback causes = %v", failures)
		}
		return nil
	}))
	if _, err := escaped.Argument(1); !errors.Is(err, StaleReference) {
		t.Fatalf("escaped callback frame = %v", err)
	}
}

func TestRawTimeline(t *testing.T) {
	api, err := NativeAPI()
	mustOK(t, err)
	mustOK(t, api.WithArena(func(arena *RawArena) error {
		f := nativeFixture{t, arena}
		root := f.value("NtTimelineNode")
		clip, err := api.Field("NtTimelineNode", "data.clip")
		mustOK(t, err)
		track := root + RawAddress(clip.Offset)
		f.set(track, "NtTimelineClip", "channel", 1)
		f.set(track, "NtTimelineClip", "span_us", 100)
		tween, err := api.Field("NtTimelineClip", "source.tween")
		mustOK(t, err)
		tweenAt := track + RawAddress(tween.Offset)
		f.set(tweenAt, "NtTween", "duration_us", 100)
		to, err := api.Field("NtTween", "to")
		mustOK(t, err)
		mustOK(t, arena.SetFloat64(tweenAt+RawAddress(to.Offset), 20))
		timeline := f.value("pointer")
		mustOK(t, arena.Result("nt_timeline_create", f.pointer(0), f.pointer(root),
			f.pointer(0), f.pointer(timeline)))
		defer f.call("nt_timeline_destroy", timeline)
		channel, elapsed, out := f.value("uint64_t"), f.value("NtTimeUs"), f.value("NtTimelineSample")
		mustOK(t, arena.SetUnsigned(channel, 8, 1))
		mustOK(t, arena.SetUnsigned(elapsed, 8, 25))
		mustOK(t, arena.Result("nt_timeline_sample", timeline, channel, elapsed, f.pointer(out)))
		field, err := api.Field("NtTimelineSample", "value")
		mustOK(t, err)
		sample, err := arena.Float64(out + RawAddress(field.Offset))
		if got := must(t, sample, err); got != 5 {
			t.Fatalf("timeline sample = %g", got)
		}
		return nil
	}))
}
