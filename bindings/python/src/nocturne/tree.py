from dataclasses import dataclass
from threading import RLock
from weakref import ref

from ._base import NativeError, check, copy_view, counted, ensure_abi
from .component import CallError, OPS, point, size, style
from .input import DispatchError, Input
from .raw import ffi, lib


@dataclass(frozen=True)
class Node:
    _tree: object
    slot: int
    generation: int


@dataclass(frozen=True)
class Geometry:
    parent: Node | None
    bounds: tuple
    clip: tuple
    placed: bool
    enabled: bool


@dataclass(frozen=True)
class Cell:
    glyph: str
    lead: tuple
    columns: int
    owner: int
    foreground: tuple
    background: tuple


@dataclass(frozen=True)
class Intent:
    source: Node
    kind: int
    payload: bytes


@dataclass(frozen=True)
class Dispatch:
    target: Node | None
    reserved: bool
    consumed: bool
    text_delivered: bool


@dataclass
class _Mount:
    tree: object
    component: object


def _mount_options(enabled, focusable, hit_testable):
    return ffi.new("NtMountOptions *", {"enabled": enabled, "focusable": focusable,
                                       "hit_testable": hit_testable})[0]


def _bounds(rect):
    return rect.origin.x, rect.origin.y, rect.size.columns, rect.size.rows


def _channels(color):
    return color.red, color.green, color.blue, color.alpha


def _label(text, foreground, background):
    owner, view = counted(text)
    label_style = {
        "width": {"ambiguous": lib.NT_WIDTH_ONE, "emoji": lib.NT_WIDTH_TWO},
        "wrap": lib.NT_WRAP_NONE, "align": lib.NT_TEXT_ALIGN_START,
        "direction": lib.NT_DIRECTION_AUTO, "tab_stop": 4,
        "color": style(foreground, background),
    }
    return owner, ffi.new("NtLabelOptions *", {"text": view, "style": label_style})


class Frame:
    """A checked view of a published frame. Each cell returns a copied glyph."""

    def __init__(self, tree, revision):
        self._tree = ref(tree)
        self._revision = revision

    def _owner(self):
        tree = self._tree()
        if tree is None:
            raise ReferenceError("Nocturne tree is gone")
        return tree

    def _frame(self, tree):
        if tree._revision != self._revision or tree._native == ffi.NULL:
            raise ReferenceError("Nocturne frame was replaced or closed")
        return lib.nt_tree_frame(tree._native)

    @property
    def size(self):
        tree = self._owner()
        with tree._lock:
            result = lib.nt_frame_size(self._frame(tree))
            return result.columns, result.rows

    def cell(self, at):
        tree = self._owner()
        with tree._lock:
            result = ffi.new("NtCellView *")
            point_value = ffi.new("NtCellPoint *", point(at))[0]
            check(lib.nt_frame_cell(self._frame(tree), point_value, result))
            return Cell(copy_view(result.glyph).decode("utf-8"), (result.lead.x, result.lead.y),
                        result.glyph_columns, result.owner, _channels(result.style.foreground),
                        _channels(result.style.background))


class Tree:
    """Owns a native component tree and forwards caller-driven operations."""

    def __init__(self):
        ensure_abi()
        self._lock = RLock()
        self._native = ffi.NULL
        self._revision = 0
        self._busy = False
        self._mounts = {}
        self._failures = []
        out = ffi.new("NtTree **")
        check(lib.nt_tree_create(ffi.NULL, out))
        self._native = out[0]

    def __copy__(self):
        raise TypeError("a Nocturne tree has one owner")

    def __deepcopy__(self, _memo):
        return self.__copy__()

    def _handle(self):
        if self._native == ffi.NULL:
            raise ReferenceError("Nocturne tree is closed")
        return self._native

    def _id(self, node):
        if node is None:
            return ffi.new("NtNodeId *")[0]
        owner = node._tree()
        if owner is None or owner._native == ffi.NULL:
            raise NativeError(lib.NT_RESULT_STALE_REFERENCE)
        if owner is not self:
            raise NativeError(lib.NT_RESULT_INVALID_ARGUMENT)
        return ffi.new("NtNodeId *", {"tree": self._handle(), "slot": node.slot,
                                     "generation": node.generation})[0]

    def _node(self, native):
        return Node(ref(self), native.slot, native.generation) if native.tree != ffi.NULL else None

    def _call(self, operation, *args):
        if self._busy:
            raise NativeError(lib.NT_RESULT_WRONG_PHASE)
        self._busy = True
        try:
            return operation(self._handle(), *args)
        except BaseException:
            self._failures.clear()
            raise
        finally:
            self._busy = False

    def _finish(self, result):
        callbacks = tuple(self._failures)
        self._failures.clear()
        if callbacks:
            native = NativeError(result) if result != lib.NT_RESULT_OK else None
            raise CallError(native, callbacks)
        check(result)

    def mount(self, component, *, parent=None, enabled=True, focusable=False, hit_testable=True):
        with self._lock:
            parent_id = self._id(parent)
            state = _Mount(ref(self), component)
            keep = ffi.new_handle(state)
            out = ffi.new("NtNodeId *")
            options = _mount_options(enabled, focusable, hit_testable)
            key = id(state)
            self._mounts[key] = keep
            try:
                result = self._call(lib.nt_tree_mount, parent_id, OPS, keep, options, out)
            finally:
                if out.tree == ffi.NULL:
                    self._mounts.pop(key, None)
            self._finish(result)
            return self._node(out[0])

    def mount_box(self, *, parent=None, kind=lib.NT_BOX_LAYOUT_STACK, padding=(0, 0, 0, 0)):
        with self._lock:
            insets = dict(zip(("left", "top", "right", "bottom"), padding, strict=True))
            options = ffi.new("NtBoxLayout *", {"kind": kind, "padding": insets})
            out = ffi.new("NtNodeId *")
            result = self._call(lib.nt_box_layout_mount, self._id(parent),
                                _mount_options(True, False, False), options, out)
            self._finish(result)
            return self._node(out[0])

    def mount_native(self, operation, *, parent=None):
        """Mount through a native operation using (tree, parent, out).

        The operation must obey its C argument and ownership contract and
        complete synchronously while the supplied pointers are borrowed.
        """
        with self._lock:
            out = ffi.new("NtNodeId *")
            self._finish(self._call(operation, self._id(parent), out))
            return self._node(out[0])

    def call_native(self, node, operation):
        """Run a native node operation within the protected tree call.

        The operation receives (tree, node), returns an NtResult, and is
        responsible for its native borrows. It must leave the tree alive.
        """
        with self._lock:
            self._finish(self._call(operation, self._id(node)))

    def mount_label(self, text, *, parent=None, foreground=(235, 240, 250, 255), background=(0, 0, 0, 0)):
        owner, options = _label(text, foreground, background)
        mount = _mount_options(True, False, False)
        return self.mount_native(lambda tree, host, out: lib.nt_label_mount(tree, host, mount, options, out),
                                 parent=parent)

    def set_label(self, node, text, *, foreground=(235, 240, 250, 255), background=(0, 0, 0, 0)):
        owner, options = _label(text, foreground, background)
        self.call_native(node, lambda tree, mounted: lib.nt_label_set(tree, mounted, options))

    def remove(self, node):
        with self._lock:
            self._finish(self._call(lib.nt_tree_remove, self._id(node)))

    def tick(self, now, motion=lib.NT_MOTION_FULL):
        with self._lock:
            self._finish(self._call(lib.nt_tree_tick, now, motion))

    @property
    def next_update(self):
        with self._lock:
            deadline = lib.nt_tree_next_update(self._handle())
            return deadline.at if deadline.scheduled else None

    def publish(self, width, height):
        with self._lock:
            result = self._call(lib.nt_tree_publish, size((width, height)))
            if result == lib.NT_RESULT_OK:
                self._revision += 1
            self._finish(result)

    @property
    def frame(self):
        with self._lock:
            return Frame(self, self._revision) if lib.nt_tree_frame(self._handle()) != ffi.NULL else None

    def geometry(self, node):
        with self._lock:
            out = ffi.new("NtNodeGeometry *")
            check(lib.nt_tree_geometry(self._handle(), self._id(node), out))
            return Geometry(self._node(out.parent), _bounds(out.bounds), _bounds(out.clip),
                            bool(out.placed), bool(out.enabled))

    def set_focus(self, node=None):
        with self._lock:
            self._finish(self._call(lib.nt_tree_set_focus, self._id(node)))

    @property
    def focus(self):
        with self._lock:
            return self._node(lib.nt_tree_focus(self._handle()))

    def dispatch(self, at, value, *, reserved=(), traverse_focus=False):
        owners, event, key_text = value._native(at)
        chords = ffi.new("NtKeyChord[]", [{"key": key, "modifiers": modifiers}
                                              for key, modifiers in reserved])
        policy = ffi.new("NtDispatchPolicy *", {"reserved": chords, "reserved_count": len(chords),
                                                   "traverse_focus": traverse_focus})
        with self._lock:
            out = ffi.new("NtDispatchResult *")
            sentinel = 2**32 - 1
            out.target.slot = sentinel
            result = self._call(lib.nt_tree_dispatch, event, key_text, policy, out)
            delivery = None
            if out.target.slot != sentinel:
                delivery = Dispatch(self._node(out.target), bool(out.reserved), bool(out.consumed),
                                    bool(out.text_delivered))
            try:
                self._finish(result)
            except (NativeError, CallError) as cause:
                raise DispatchError(cause, delivery) from cause
            return delivery

    def paste(self, at, text):
        return self.dispatch(at, Input.paste(text))

    def capture(self, pointer):
        with self._lock:
            return self._node(lib.nt_tree_capture(self._handle(), pointer))

    def peek_intent(self):
        with self._lock:
            out = ffi.new("NtIntentView *")
            if not lib.nt_tree_peek_intent(self._handle(), out):
                return None
            return Intent(self._node(out.source), out.kind, copy_view(out.payload))

    def pop_intent(self):
        with self._lock:
            lib.nt_tree_pop_intent(self._handle())

    def close(self):
        with self._lock:
            if self._native == ffi.NULL:
                return
            self._call(lib.nt_tree_destroy)
            self._native = ffi.NULL
            self._revision += 1
            self._mounts.clear()
            self._finish(lib.NT_RESULT_OK)

    def __enter__(self):
        with self._lock:
            self._handle()
        return self

    def __exit__(self, *_):
        self.close()

    def __del__(self):
        try:
            self.close()
        except BaseException:
            pass
