"""Graceful lifecycle for an exact, verified Windows Horizon process pair.

Process handles remain open through the transaction, so a reused PID cannot be
mistaken for the process being updated. This adapter never forcibly terminates
Core, workers or the UI. The UI owns the ordered mining/CPU/Core drain.
"""
from pathlib import Path
import ctypes
import hashlib
import json
import os
import subprocess
import time


from horizon_lifecycle import LifecycleError, ProcessAbsentError
from horizon_lifecycle import owned_ipc as _owned_ipc, graceful_update_drain as _graceful_update_drain


class OwnedWindowsProcess:
    def __init__(self, pid, executable, sha256, birth=None):
        if os.name != 'nt':
            raise LifecycleError('Windows lifecycle required')
        if type(pid) is not int or pid <= 0:
            raise LifecycleError('Positive owned process ID required')
        self.executable = Path(executable).resolve(strict=True)
        if not self.executable.is_file() or self.executable.is_symlink():
            raise LifecycleError('Plain owned executable required')
        with self.executable.open('rb') as stream:
            if hashlib.file_digest(stream, 'sha256').hexdigest() != sha256:
                raise LifecycleError('Owned executable fingerprint differs')
        self.pid = pid
        self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        k = self.kernel
        k.OpenProcess.argtypes = [ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
        k.OpenProcess.restype = ctypes.c_void_p
        k.CloseHandle.argtypes = [ctypes.c_void_p]
        k.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        k.QueryFullProcessImageNameW.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_ulong)]
        k.GetProcessTimes.argtypes = [ctypes.c_void_p, *([ctypes.POINTER(ctypes.c_ulonglong)] * 4)]
        self.handle = k.OpenProcess(0x100000 | 0x1000, False, pid)
        if not self.handle:
            if ctypes.get_last_error() == 87:
                raise ProcessAbsentError('Owned process no longer exists')
            raise LifecycleError('Owned process is absent or cannot be inspected')
        try:
            # An exited process can remain addressable while another verified
            # handle is open. Windows may refuse its image-name query with
            # ERROR_GEN_FAILURE. Test the handle signal before querying it.
            if self.exited():
                raise ProcessAbsentError('Owned process already exited')
            values = [ctypes.c_ulonglong() for _ in range(4)]
            if not k.GetProcessTimes(self.handle, *(ctypes.byref(v) for v in values)):
                raise ctypes.WinError(ctypes.get_last_error())
            self.birth = str(values[0].value)
            if birth is not None and self.birth != str(birth):
                # The original process is gone. Never inspect/control the new
                # process merely because Windows reused its numeric PID.
                raise ProcessAbsentError('Original owned process is absent; PID creation time differs')
            buffer = ctypes.create_unicode_buffer(32768)
            size = ctypes.c_ulong(len(buffer))
            if not k.QueryFullProcessImageNameW(self.handle, 0, buffer, ctypes.byref(size)):
                raise ctypes.WinError(ctypes.get_last_error())
            if Path(buffer.value).resolve() != self.executable:
                raise LifecycleError('PID belongs to another executable')
            if self.exited():
                raise ProcessAbsentError('Owned process already exited')
        except BaseException:
            self.close()
            raise

    def exited(self):
        result = self.kernel.WaitForSingleObject(self.handle, 0)
        if result == 0:
            return True
        if result == 258:
            return False
        raise ctypes.WinError(ctypes.get_last_error())

    def close(self):
        if self.handle:
            self.kernel.CloseHandle(self.handle)
            self.handle = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def identity(self):
        return dict(pid=self.pid, birth=self.birth, executable=str(self.executable))


def owned_ipc(ui, command):
    return _owned_ipc(ui, command)


def graceful_update_drain(ui, core=None, timeout=75):
    return _graceful_update_drain(ui, core, timeout, ipc=owned_ipc)
