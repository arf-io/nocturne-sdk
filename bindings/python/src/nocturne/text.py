from dataclasses import dataclass
from threading import RLock

from ._base import check, copy_view, counted, ensure_abi
from .raw import ffi, lib


@dataclass(frozen=True)
class Cluster:
    id: int
    begin: int
    end: int


class Text:
    """Owned native UTF-8 and its grapheme segmentation."""

    def __init__(self, text):
        ensure_abi()
        owner, view = counted(text)
        out = ffi.new("NtText **")
        check(lib.nt_text_create(ffi.NULL, view, out))
        self._native = ffi.gc(out[0], lib.nt_text_destroy)
        self._lock = RLock()

    def _handle(self):
        if self._native == ffi.NULL:
            raise ReferenceError("Nocturne text is closed")
        return self._native

    def close(self):
        with self._lock:
            handle = self._native
            self._native = ffi.NULL
            if handle != ffi.NULL:
                ffi.release(handle)

    def __copy__(self):
        raise TypeError("use Text(text.value) to create an independent native text")

    def __deepcopy__(self, _memo):
        return self.__copy__()

    def __enter__(self):
        with self._lock:
            self._handle()
        return self

    def __exit__(self, *_):
        self.close()

    @property
    def value(self):
        with self._lock:
            return copy_view(lib.nt_text_bytes(self._handle())).decode("utf-8")

    def __len__(self):
        with self._lock:
            return int(lib.nt_text_cluster_count(self._handle()))

    def cluster(self, index):
        out = ffi.new("NtTextCluster *")
        with self._lock:
            check(lib.nt_text_cluster(self._handle(), index, out))
        return Cluster(out.id, out.source.begin, out.source.end)

    def replace(self, begin, end, text):
        owner, view = counted(text)
        span = ffi.new("NtByteSpan *", {"begin": begin, "end": end})[0]
        with self._lock:
            check(lib.nt_text_replace(self._handle(), span, view))
