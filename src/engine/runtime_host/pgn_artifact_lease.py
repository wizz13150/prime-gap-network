"""Session-scoped fingerprints of pinned, read-only release artifacts.

Checkpoints and reports are never admitted to this cache. Windows denies writes
and replacement while a release artifact is pinned. POSIX also checks its file
identity and timestamps on every use; a changed artifact fails closed.
"""

from contextvars import ContextVar
from pathlib import Path
import hashlib
import os

_current = ContextVar("scout_artifact_lease", default=None)


def _signature(path):
    value = os.stat(path)
    return (value.st_dev, value.st_ino, value.st_size,
            value.st_mtime_ns, value.st_ctime_ns)


def _open_read_lease(path):
    if os.name != "nt":
        import fcntl
        stream = open(path, "rb")
        try:
            fcntl.flock(stream.fileno(), fcntl.LOCK_SH | fcntl.LOCK_NB)
        except BaseException:
            stream.close()
            raise
        return stream
    import ctypes
    from ctypes import wintypes
    import msvcrt
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    create = kernel.CreateFileW
    create.argtypes = (wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                       wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD,
                       wintypes.HANDLE)
    create.restype = wintypes.HANDLE
    handle = create(str(path), 0x80000000, 1, None, 3, 0x80, None)
    if handle == ctypes.c_void_p(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        fd = msvcrt.open_osfhandle(handle, os.O_RDONLY | os.O_BINARY)
    except BaseException:
        close = kernel.CloseHandle
        close.argtypes = (wintypes.HANDLE,)
        close(handle)
        raise
    return os.fdopen(fd, "rb")


def _hash(stream):
    digest = hashlib.sha256()
    for block in iter(lambda: stream.read(1 << 20), b""):
        digest.update(block)
    return digest.hexdigest().upper()


class ArtifactLease:
    """Pin release inputs until this controller invocation has stopped."""

    def __init__(self):
        self._entries = {}
        self._token = None

    def __enter__(self):
        if _current.get() is not None:
            raise RuntimeError("Nested release artifact lease")
        self._token = _current.set(self)
        return self

    def digest(self, path, admit=False):
        path = Path(path).resolve(strict=True)
        key = os.path.normcase(str(path))
        entry = self._entries.get(key)
        if entry is not None:
            if _signature(path) != entry[1]:
                raise RuntimeError("Pinned release artifact changed: " + str(path))
            return entry[2]
        if not admit:
            with path.open("rb") as stream:
                return _hash(stream)
        stream = _open_read_lease(path)
        try:
            signature = _signature(path)
            if os.fstat(stream.fileno()).st_ino != signature[1]:
                raise RuntimeError("Release artifact replaced during admission")
            digest = _hash(stream)
            if _signature(path) != signature:
                raise RuntimeError("Release artifact changed during hashing")
            self._entries[key] = (stream, signature, digest)
            return digest
        except BaseException:
            stream.close()
            raise

    def __exit__(self, *error):
        try:
            for stream, _, _ in self._entries.values():
                stream.close()
            self._entries.clear()
        finally:
            _current.reset(self._token)
            self._token = None


def file_digest(path, release_artifact=False):
    lease = _current.get()
    if lease is not None:
        return lease.digest(path, admit=release_artifact)
    with Path(path).open("rb") as stream:
        return _hash(stream)
