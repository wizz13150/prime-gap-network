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


class LifecycleError(RuntimeError):
    pass


class ProcessAbsentError(LifecycleError):
    pass


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
    if command not in ('--inspect-owned', '--update-close-owned'):
        raise LifecycleError('Unsupported lifecycle operation')
    if ui.exited():
        raise LifecycleError('Owned UI already exited')
    result = subprocess.run([str(ui.executable), command, str(ui.pid)],
                            stdin=subprocess.DEVNULL, capture_output=True,
                            timeout=8, creationflags=0x08000000)
    if result.returncode or len(result.stdout) > 131072:
        raise LifecycleError('Owned lifecycle response failed or exceeded its bound')
    try:
        value = json.loads(result.stdout)
    except (ValueError, UnicodeError) as error:
        raise LifecycleError('Invalid owned lifecycle response') from error
    if type(value) is not dict:
        raise LifecycleError('Owned lifecycle object required')
    return value


def graceful_update_drain(ui, core=None, timeout=75):
    """Return only after both exact handles confirm actual process exit.

    The caller must have checked the installed deployment's update protocol
    before invoking this API: older UIs do not recognize this CLI operation.
    """
    if not 1 <= timeout <= 120:
        raise LifecycleError('Bounded lifecycle deadline required')
    reply = owned_ipc(ui, '--update-close-owned')
    if reply.get('accepted') is not True or reply.get('scope') != 'ORDERED_MINING_DRAIN_THEN_OWNED_CORE_STOP':
        raise LifecycleError('UI refused the ordered update shutdown')
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if ui.exited():
            if core is None or core.exited():
                return dict(schema='PGN_OWNED_UPDATE_DRAIN_V1', UI_exited=True,
                            Core_exited=core is not None, forced_termination=False)
        else:
            # The server can disappear between the handle check and this IPC
            # as the UI finishes its graceful shutdown. Only actual handle
            # signals establish completion; an IPC race cannot establish it.
            try:
                state = owned_ipc(ui, '--inspect-owned')
            except LifecycleError:
                state = {}
            error = state.get('pgn_update_shutdown_error')
            if error:
                raise LifecycleError(str(error)[:512])
        time.sleep(.25)
    raise LifecycleError('Graceful shutdown deadline exceeded; no process was killed')
