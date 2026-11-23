from dataclasses import dataclass
from threading import get_ident

from ._base import NativeError, check, copy_view, counted
from .raw import ffi, lib


@dataclass(frozen=True)
class CallbackFailure:
    phase: str
    cause: BaseException


class CallError(Exception):
    """A native result and the Python callback causes from the same call."""

    def __init__(self, native, callbacks):
        self.native = native
        self.callbacks = tuple(callbacks)
        detail = str(native) if native is not None else "native call completed"
        super().__init__(f"{detail}; {len(self.callbacks)} callback failure(s)")


@dataclass(frozen=True)
class Constraints:
    columns: tuple
    rows: tuple

    @classmethod
    def _from_native(cls, limits):
        def extent(value):
            return (value.min, value.max if value.bounded else None)
        return cls(extent(limits.columns), extent(limits.rows))


@dataclass(frozen=True)
class Update:
    now: int
    elapsed: int
    motion: int


def size(value):
    return ffi.new("NtCellSize *", {"columns": value[0], "rows": value[1]})[0]


def point(value):
    return {"x": value[0], "y": value[1]}


def rectangle(value):
    return ffi.new("NtCellRect *", {"origin": point(value[:2]), "size": size(value[2:])})[0]


def rgba(channels):
    return dict(zip(("red", "green", "blue", "alpha"), channels, strict=True))


def style(foreground, background):
    return ffi.new("NtCellStyle *", {"foreground": rgba(foreground), "background": rgba(background)})[0]


class _Context:
    def __init__(self, native):
        self._native = native
        self._thread = get_ident()

    def _handle(self):
        if self._native == ffi.NULL or self._thread != get_ident():
            raise ReferenceError("Nocturne callback context expired")
        return self._native

    def _expire(self):
        self._native = ffi.NULL


class Measure(_Context):
    def __len__(self):
        return lib.nt_measure_child_count(self._handle())

    def child(self, index, columns, rows):
        def extent(bounds):
            return {"min": bounds[0], "max": bounds[1] or 0, "bounded": bounds[1] is not None}
        context = self._handle()
        child = ffi.new("NtNodeId *")
        check(lib.nt_measure_child_at(context, index, child))
        limits = ffi.new("NtConstraints *", {"columns": extent(columns), "rows": extent(rows)})[0]
        result = ffi.new("NtCellSize *")
        check(lib.nt_measure_child(context, child[0], limits, result))
        return result.columns, result.rows


class Layout(_Context):
    def __len__(self):
        return lib.nt_layout_child_count(self._handle())

    def _child(self, index):
        child = ffi.new("NtNodeId *")
        check(lib.nt_layout_child_at(self._handle(), index, child))
        return child[0]

    def child_size(self, index):
        result = ffi.new("NtCellSize *")
        check(lib.nt_layout_child_size(self._handle(), self._child(index), result))
        return result.columns, result.rows

    def place(self, index, bounds, z_index=0):
        check(lib.nt_layout_place(self._handle(), self._child(index), rectangle(bounds), z_index, ffi.NULL))


class Paint(_Context):
    def glyph(self, at, glyph, columns, foreground, background=(0, 0, 0, 0)):
        owner, view = counted(glyph)
        lead = ffi.new("NtCellPoint *", point(at))[0]
        check(lib.nt_paint_glyph(self._handle(), lead, view, columns, style(foreground, background)))

    def fill(self, bounds, color):
        color = ffi.new("NtRgba *", rgba(color))[0]
        check(lib.nt_paint_fill(self._handle(), rectangle(bounds), color))


class Event(_Context):
    def __init__(self, native, event, reply):
        super().__init__(native)
        self._event = event
        self._reply = reply

    @property
    def kind(self):
        self._handle()
        return int(self._event.kind)

    @property
    def phase(self):
        return int(lib.nt_event_phase(self._handle()))

    @property
    def at(self):
        self._handle()
        return int(self._event.at)

    @property
    def text(self):
        if self.kind not in (lib.NT_EVENT_TEXT, lib.NT_EVENT_PASTE):
            return None
        return copy_view(self._event.data.text).decode("utf-8")

    @property
    def key(self):
        if self.kind != lib.NT_EVENT_KEY:
            return None
        key = self._event.data.key
        return key.key, key.modifiers, key.state

    @property
    def local_position(self):
        out = ffi.new("NtCellPoint *")
        if not lib.nt_event_local_position(self._handle(), out):
            return None
        return out.x, out.y

    def emit(self, kind, payload):
        owner, view = counted(payload)
        check(lib.nt_event_emit(self._handle(), kind, view))

    def consume(self):
        self._handle()
        self._reply.consume = True

    def invalidate(self, flags=lib.NT_INVALIDATE_PAINT):
        self._handle()
        self._reply.invalidate = flags

    def take_capture(self):
        self._handle()
        self._reply.capture = lib.NT_CAPTURE_TAKE


class Component:
    """Override the native phases a custom component uses."""

    def measure(self, context, constraints):
        return constraints.columns[0], constraints.rows[0]

    def layout(self, context, assigned):
        pass

    def update(self, info):
        return lib.NT_INVALIDATE_NONE, None

    def paint(self, context, assigned):
        pass

    def event(self, event):
        pass

    def dispose(self):
        pass


def _callback(handle, phase, operation, context=None):
    state = ffi.from_handle(handle)
    try:
        operation(state.component)
        return lib.NT_RESULT_OK
    except BaseException as cause:
        owner = state.tree()
        if owner is not None:
            owner._failures.append(CallbackFailure(phase, cause))
        if isinstance(cause, NativeError):
            result = int(ffi.cast("NtResult", cause.code))
            if result != lib.NT_RESULT_OK:
                return result
        return lib.NT_RESULT_CALLBACK_FAILED
    finally:
        if context is not None:
            context._expire()


@ffi.def_extern(error=lib.NT_RESULT_CALLBACK_FAILED)
def nocturne_py_measure(state, native, limits, out):
    context = Measure(native)

    def measure(component):
        out[0] = size(component.measure(context, Constraints._from_native(limits)))

    return _callback(state, "measure", measure, context)


@ffi.def_extern(error=lib.NT_RESULT_CALLBACK_FAILED)
def nocturne_py_layout(state, native, assigned):
    context = Layout(native)
    return _callback(state, "layout", lambda c: c.layout(context, (assigned.columns, assigned.rows)), context)


@ffi.def_extern(error=lib.NT_RESULT_CALLBACK_FAILED)
def nocturne_py_update(state, info, out):
    def update(component):
        flags, deadline = component.update(Update(info.now, info.elapsed, info.motion))
        out.invalidate = flags
        out.next.scheduled = deadline is not None
        out.next.at = 0 if deadline is None else deadline

    return _callback(state, "update", update)


@ffi.def_extern(error=lib.NT_RESULT_CALLBACK_FAILED)
def nocturne_py_paint(state, native, assigned):
    context = Paint(native)
    return _callback(state, "paint", lambda c: c.paint(context, (assigned.columns, assigned.rows)), context)


@ffi.def_extern(error=lib.NT_RESULT_CALLBACK_FAILED)
def nocturne_py_event(state, native, event, reply):
    context = Event(native, event, reply)
    return _callback(state, "event", lambda c: c.event(context), context)


@ffi.def_extern()
def nocturne_py_dispose(handle):
    state = ffi.from_handle(handle)
    try:
        _callback(handle, "dispose", lambda c: c.dispose())
    finally:
        owner = state.tree()
        if owner is not None:
            owner._mounts.pop(id(state), None)


OPS = ffi.new("NtComponentOps *", {name: getattr(lib, f"nocturne_py_{name}")
                                 for name in ("measure", "layout", "update", "paint", "event", "dispose")})
