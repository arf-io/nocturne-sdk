from .raw import ffi, lib


class NativeError(Exception):
    """A native result number and its C identifier when known."""

    def __init__(self, code):
        self.code = int(code)
        name = lib.nt_result_name(code) if -(2**31) <= code < 2**31 else ffi.NULL
        self.name = ffi.string(name).decode("ascii") if name != ffi.NULL else None
        super().__init__(f"{self.name or 'unknown Nocturne result'} ({self.code})")


def check(result):
    if result != lib.NT_RESULT_OK:
        raise NativeError(result)


def ensure_abi():
    if lib.nt_abi_version() != lib.NT_ABI_VERSION:
        raise NativeError(lib.NT_RESULT_UNSUPPORTED_CAPABILITY)


def counted(text):
    encoded = text.encode("utf-8") if isinstance(text, str) else bytes(text)
    owner = ffi.from_buffer("const char[]", encoded)
    return owner, ffi.new("NtByteView *", {"data": owner, "count": len(encoded)})[0]


def copy_view(view):
    return bytes(ffi.buffer(view.data, view.count)) if view.count else b""
