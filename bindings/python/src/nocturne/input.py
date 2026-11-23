from dataclasses import dataclass

from ._base import counted
from .component import point, size
from .raw import ffi, lib


@dataclass(frozen=True)
class Input:
    """A normalized input supplied by the application or terminal host."""

    kind: int
    _data: object
    _key_text: str | bytes = b""

    @classmethod
    def key(cls, key, *, modifiers=0, state=lib.NT_KEY_PRESS, text=b""):
        code = ord(key) if isinstance(key, str) else key
        return cls(lib.NT_EVENT_KEY, {"key": code, "modifiers": modifiers, "state": state}, text)

    @classmethod
    def text(cls, text):
        return cls(lib.NT_EVENT_TEXT, text)

    @classmethod
    def paste(cls, text):
        return cls(lib.NT_EVENT_PASTE, text)

    @classmethod
    def pointer(cls, kind, pointer, position=None, *, button=0, pressed=0, modifiers=0):
        if kind not in (lib.NT_EVENT_POINTER_DOWN, lib.NT_EVENT_POINTER_MOVE, lib.NT_EVENT_POINTER_UP):
            raise ValueError("pointer requires down, move, or up")
        return cls(kind, {"id": pointer, "position": point(position or (0, 0)),
                          "has_position": position is not None, "button": button,
                          "pressed": pressed, "modifiers": modifiers})

    @classmethod
    def wheel(cls, position, dx, dy, *, modifiers=0):
        return cls(lib.NT_EVENT_WHEEL, {"position": point(position), "dx": dx, "dy": dy,
                                       "modifiers": modifiers})

    @classmethod
    def cancel(cls, reason=lib.NT_CANCEL_HOST, *, pointer=None):
        if pointer is None:
            return cls(lib.NT_EVENT_HOST_CANCEL, reason)
        return cls(lib.NT_EVENT_POINTER_CANCEL, {"id": pointer, "reason": reason})

    @classmethod
    def resize(cls, width, height):
        return cls(lib.NT_EVENT_RESIZE, (width, height))

    def _native(self, at):
        key_owner, key_text = counted(self._key_text)
        event_owner = None
        payload = self._data
        if self.kind in (lib.NT_EVENT_TEXT, lib.NT_EVENT_PASTE):
            event_owner, payload = counted(payload)
            field = "text"
        elif self.kind == lib.NT_EVENT_RESIZE:
            field, payload = "resize", size(payload)
        elif self.kind in (lib.NT_EVENT_POINTER_DOWN, lib.NT_EVENT_POINTER_MOVE, lib.NT_EVENT_POINTER_UP):
            field = "pointer"
        else:
            field = {lib.NT_EVENT_KEY: "key", lib.NT_EVENT_WHEEL: "wheel",
                     lib.NT_EVENT_POINTER_CANCEL: "pointer_cancel", lib.NT_EVENT_HOST_CANCEL: "reason"}[self.kind]
        event = ffi.new("NtEvent *", {"kind": self.kind, "at": at, "data": {field: payload}})
        return (event_owner, key_owner), event, key_text


class DispatchError(Exception):
    def __init__(self, cause, delivery):
        self.cause = cause
        self.delivery = delivery
        super().__init__(str(cause))
