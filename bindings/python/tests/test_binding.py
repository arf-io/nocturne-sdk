import copy
import unittest

from nocturne import CallError, Component, DispatchError, Input, NativeError, Text, Tree, abi_version, available_components
from nocturne.raw import ffi, lib


GLYPH = "👩‍💻"


class Indicator(Component):
    def __init__(self):
        self.updates = 0
        self.disposals = 0
        self.paint_context = None

    def measure(self, context, limits):
        return 2, 1

    def update(self, info):
        self.updates += 1
        return lib.NT_INVALIDATE_PAINT, info.now + 250 if self.updates == 1 else None

    def paint(self, paint, assigned):
        self.paint_context = paint
        glyph, columns = (GLYPH, 2) if self.updates == 1 else ("!", 1)
        paint.glyph((0, 0), glyph, columns, (230, 240, 250, 255))

    def event(self, event):
        if event.kind == lib.NT_EVENT_FOCUS_GAIN:
            event.emit(42, b"focus")
            raise ValueError("focus handler failed")
        if event.key is not None and event.key[0] == ord("!"):
            event.emit(43, b"discard")
            raise NativeError(lib.NT_RESULT_CALLBACK_FAILED)
        if event.kind == lib.NT_EVENT_POINTER_DOWN:
            if event.local_position != (0, 0):
                raise AssertionError("pointer was not translated to local coordinates")
            event.take_capture()
        if event.kind == lib.NT_EVENT_PASTE:
            event.emit(41, event.text)
            event.consume()

    def dispose(self):
        self.disposals += 1


class BindingTests(unittest.TestCase):
    def test_counted_text_and_owned_clusters(self):
        self.assertEqual(abi_version(), 1)
        self.assertEqual(lib.NT_CHOICE_NONE, (1 << (8 * ffi.sizeof("size_t"))) - 1)
        self.assertEqual(lib.NT_BIDI_REMOVED_LEVEL, 255)
        source = "x\0" + GLYPH
        with Text(source) as text:
            self.assertEqual(text.value, source)
            self.assertEqual(len(text), 3)
            cluster = text.cluster(2)
            self.assertEqual((cluster.begin, cluster.end), (2, 13))
            text.replace(0, 1, "!")
            self.assertEqual(text.cluster(2), cluster)
            with self.assertRaises(TypeError):
                copy.copy(text)
            with self.assertRaises(NativeError) as error:
                text.replace(3, 4, "?")
            self.assertEqual(error.exception.name, "NT_RESULT_INVALID_ARGUMENT")
            self.assertEqual(text.value, "!" + source[1:])
        with self.assertRaises(ReferenceError):
            _ = text.value

    def test_custom_component_and_scoped_failures(self):
        indicator = Indicator()
        with Tree() as tree:
            root = tree.mount_box(padding=(1, 0, 0, 0))
            node = tree.mount(indicator, parent=root, focusable=True)
            tree.tick(1000)
            self.assertEqual(tree.next_update, 1250)
            tree.publish(4, 1)
            self.assertEqual(tree.geometry(node).bounds, (1, 0, 2, 1))
            first = tree.frame
            cell = first.cell((2, 0))
            self.assertEqual((cell.glyph, cell.lead, cell.columns), (GLYPH, (1, 0), 2))
            with self.assertRaises(ReferenceError):
                indicator.paint_context.glyph((0, 0), "?", 1, (255, 255, 255, 255))

            with self.assertRaises(CallError) as caught:
                tree.set_focus(node)
            error = caught.exception
            self.assertIsNone(error.native)
            self.assertEqual(error.callbacks[0].phase, "event")
            self.assertEqual(str(error.callbacks[0].cause), "focus handler failed")
            self.assertEqual(tree.focus, node)
            self.assertIsNone(tree.peek_intent())

            with self.assertRaises(DispatchError) as rejected:
                tree.dispatch(1000, Input.key("!", text="!"))
            self.assertEqual(rejected.exception.delivery.target, node)
            self.assertEqual(rejected.exception.cause.native.code, lib.NT_RESULT_CALLBACK_FAILED)
            self.assertIsNone(tree.peek_intent())
            self.assertTrue(tree.dispatch(1000, Input.key("!", text="!"),
                                           reserved=((ord("!"), 0),)).reserved)
            primary = lib.NT_BUTTON_PRIMARY
            with self.assertRaises(DispatchError) as rejected:
                tree.dispatch(1000, Input.pointer(lib.NT_EVENT_POINTER_UP, 7, (1, 0), button=primary))
            self.assertIsNone(rejected.exception.delivery)
            tree.dispatch(1000, Input.pointer(lib.NT_EVENT_POINTER_DOWN, 7, (1, 0),
                                              button=primary, pressed=primary))
            self.assertEqual(tree.capture(7), node)
            tree.dispatch(1000, Input.cancel(pointer=7))
            self.assertIsNone(tree.capture(7))

            pasted = "x\0" + GLYPH
            self.assertTrue(tree.paste(1000, pasted).consumed)
            intent = tree.peek_intent()
            self.assertEqual((intent.kind, intent.payload), (41, pasted.encode()))
            tree.pop_intent()
            tree.tick(1250)
            tree.publish(4, 1)
            self.assertEqual(tree.frame.cell((1, 0)).glyph, "!")
            with self.assertRaises(ReferenceError):
                first.cell((2, 0))
            tree.remove(node)
            self.assertEqual(indicator.disposals, 1)
            with self.assertRaises(NativeError) as error:
                tree.geometry(node)
            self.assertEqual(error.exception.name, "NT_RESULT_STALE_REFERENCE")
            for _ in range(512):
                tree.remove(tree.mount(indicator, parent=root))
            tree.mount(indicator, parent=root)
        self.assertEqual(indicator.disposals, 514)

    def test_stock_label_replaces_copied_text(self):
        with Tree() as tree:
            node = tree.mount_label("🌙")
            tree.publish(4, 1)
            self.assertEqual(tree.frame.cell((1, 0)).glyph, "🌙")
            tree.set_label(node, bytearray(b"Q"))
            tree.publish(4, 1)
            self.assertEqual(tree.frame.cell((0, 0)).glyph, "Q")
            self.assertTrue(tree.geometry(node).placed)

    def test_exception_with_success_code_rolls_back_callback(self):
        class FaultyPaste(Component):
            def event(self, event):
                if event.kind == lib.NT_EVENT_PASTE:
                    event.emit(7, b"discard")
                    raise NativeError(lib.NT_RESULT_OK)

        with Tree() as tree:
            node = tree.mount(FaultyPaste(), focusable=True)
            tree.publish(2, 1)
            tree.set_focus(node)
            with self.assertRaises(DispatchError) as failure:
                tree.paste(1, "x")
            self.assertEqual(failure.exception.delivery.target, node)
            call = failure.exception.cause
            self.assertEqual(call.native.code, lib.NT_RESULT_CALLBACK_FAILED)
            self.assertEqual(call.callbacks[0].cause.code, lib.NT_RESULT_OK)
            self.assertIsNone(tree.peek_intent())

    @unittest.skipUnless("graph" in available_components(), "graph omitted from this wheel")
    def test_optional_graph_mounts_in_python_tree(self):
        options = ffi.new("NtGraphViewOptions *")
        options[0] = lib.nt_graph_view_options_default()
        nodes = ffi.new("NtGraphNode[]", [{"id": 1, "pinned": True, "position": {"x": .5, "y": .5}}])
        options.data.nodes = nodes
        options.data.node_count = len(nodes)
        mount = ffi.new("NtMountOptions *", {"enabled": True, "hit_testable": True})[0]
        with Tree() as tree:
            graph = tree.mount_native(lambda native, parent, out:
                                      lib.nt_graph_view_mount(native, parent, mount, options, out))
            tree.publish(10, 4)
            self.assertTrue(tree.geometry(graph).placed)
            options.layout.kind = lib.NT_GRAPH_RADIAL
            tree.call_native(graph, lambda native, node: lib.nt_graph_view_set(native, node, options))
            tree.publish(10, 4)
            self.assertTrue(tree.geometry(graph).placed)
            tree.remove(graph)


if __name__ == "__main__":
    unittest.main()
