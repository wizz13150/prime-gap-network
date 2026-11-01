"""Owned compute-child lifetime and per-UUID OS lease. No CUDA/Driver calls.

Windows compute children begin suspended, are assigned to a kill-on-close Job,
then identity is persisted before ResumeThread. Linux inherits the lease fd and
uses Native --owner-pid before CUDA. Info/plan processes do not compute.
"""

from pathlib import Path
import ctypes as C
import io
import json
import os
import re
import subprocess
import sys
import time
import hashlib


def _atomic(path, value):
    temporary = path.with_suffix(".pending")
    with temporary.open("w", encoding="utf-8") as s:
        json.dump(value, s)
        s.flush()
        os.fsync(s.fileno())
    os.replace(temporary, path)


def _kernel():
    from ctypes import wintypes as T

    k = C.WinDLL("kernel32", use_last_error=True)
    signatures = {
        "CreateMutexW": ([C.c_void_p, T.BOOL, T.LPCWSTR], T.HANDLE),
        "ReleaseMutex": ([T.HANDLE], T.BOOL),
        "WaitForSingleObject": ([T.HANDLE, T.DWORD], T.DWORD),
        "CloseHandle": ([T.HANDLE], T.BOOL),
        "OpenProcess": ([T.DWORD, T.BOOL, T.DWORD], T.HANDLE),
        "GetCurrentProcess": ([], T.HANDLE),
        "IsProcessInJob": ([T.HANDLE, T.HANDLE, C.POINTER(T.BOOL)], T.BOOL),
        "GetProcessTimes": ([T.HANDLE] + [C.POINTER(T.FILETIME)] * 4, T.BOOL),
        "GetExitCodeProcess": ([T.HANDLE, C.POINTER(T.DWORD)], T.BOOL),
        "CreateJobObjectW": ([C.c_void_p, T.LPCWSTR], T.HANDLE),
        "SetInformationJobObject": ([T.HANDLE, C.c_int, C.c_void_p, T.DWORD], T.BOOL),
        "QueryInformationJobObject": ([T.HANDLE, C.c_int, C.c_void_p, T.DWORD, C.c_void_p], T.BOOL),
        "AssignProcessToJobObject": ([T.HANDLE, T.HANDLE], T.BOOL),
        "TerminateJobObject": ([T.HANDLE, T.UINT], T.BOOL),
        "TerminateProcess": ([T.HANDLE, T.UINT], T.BOOL),
        "ResumeThread": ([T.HANDLE], T.DWORD),
    }
    for name, (args, result) in signatures.items():
        fn = getattr(k, name)
        fn.argtypes = args
        fn.restype = result
    return k, T


def _check(value):
    if not value:
        raise C.WinError(C.get_last_error())
    return value


def windows_birth(k, T, handle):
    data = [T.FILETIME() for _ in range(4)]
    _check(k.GetProcessTimes(handle, *[C.byref(x) for x in data]))
    return str((data[0].dwHighDateTime << 32) | data[0].dwLowDateTime)


def _recorded_alive(k, T, record):
    if not record or not record.get("pid"):
        return False
    handle = k.OpenProcess(0x100000 | 0x1000, False, int(record["pid"]))
    if not handle:
        if C.get_last_error() == 87:
            return False
        raise C.WinError(C.get_last_error())
    try:
        return (
            windows_birth(k, T, handle) == str(record["birth"])
            and k.WaitForSingleObject(handle, 0) == 258
        )
    finally:
        k.CloseHandle(handle)


def image_hash(path):
    value = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest().upper()


class GpuLease:
    def __init__(self, uuid, registry_root=None):
        if not re.fullmatch(r"[A-Za-z0-9-]{1,100}", uuid):
            raise ValueError("Invalid GPU lease identity")
        self.uuid = uuid
        self.handle = None
        self.fd = None
        self.record = None
        root = (
            Path(registry_root)
            if registry_root
            else (
                Path(os.environ.get("LOCALAPPDATA", Path.home())) / "PGN" / "leases"
                if os.name == "nt"
                else Path.home() / ".local/state/pgn68/leases"
            )
        )
        root.mkdir(parents=True, exist_ok=True)
        self.registry = root / (uuid + ".json")
        self.lock_path = root / (uuid + ".lock")

    def acquire(self):
        if os.name == "nt":
            self.k, self.T = _kernel()
            self.handle = _check(self.k.CreateMutexW(None, False, "Global\\PGN68-GPU-" + self.uuid))
            result = self.k.WaitForSingleObject(self.handle, 0)
            if result not in (0, 128):
                self.k.CloseHandle(self.handle)
                self.handle = None
                raise RuntimeError("Another PGN compute owner holds this GPU lease")
            # An abandoned mutex alone is not evidence the old child is closed.
            try:
                previous = json.loads(self.registry.read_text()) if self.registry.exists() else None
                until = time.monotonic() + 90
                while _recorded_alive(self.k, self.T, previous):
                    if time.monotonic() >= until:
                        raise RuntimeError(
                            "Previous trusted GPU owner has not closed; no second launch"
                        )
                    time.sleep(0.05)
            except BaseException:
                self.release()
                raise
        else:
            import fcntl

            self.fd = os.open(self.lock_path, os.O_CREAT | os.O_RDWR, 0o600)
            try:
                fcntl.flock(self.fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except OSError:
                os.close(self.fd)
                self.fd = None
                raise RuntimeError("Another PGN compute owner holds this GPU lease")
        return self

    def bind(self, pid, birth, **identity):
        self.record = {"uuid": self.uuid, "pid": int(pid), "birth": str(birth), **identity}
        _atomic(self.registry, self.record)

    def release(self):
        if self.handle:
            self.k.ReleaseMutex(self.handle)
            self.k.CloseHandle(self.handle)
            self.handle = None
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None


class WindowsOwnedProcess:
    def __init__(self, command, environment, lease, stdout=subprocess.PIPE, cwd=None):
        import _winapi
        import msvcrt

        self.command = list(map(str, command))
        self.lease = lease
        self.returncode = None
        self._handle = None
        self.thread = None
        self.job = None
        self.stdout = None
        self.resumed = False
        self.assigned = False
        self.identity_ready = False
        self._read_fd = None
        self.k, self.T = _kernel()
        self._winapi = _winapi
        try:
            self.job = _check(self.k.CreateJobObjectW(None, None))
            # JOBOBJECT_EXTENDED_LIMIT_INFORMATION x64; only KILL_ON_JOB_CLOSE.
            limit = C.create_string_buffer(144)
            C.c_uint32.from_buffer(limit, 16).value = 0x2000
            _check(self.k.SetInformationJobObject(self.job, 9, limit, 144))
            self._null = open(os.devnull, "rb")
            input_handle = msvcrt.get_osfhandle(self._null.fileno())
            read_fd = write_fd = None
            if stdout == subprocess.PIPE:
                read_fd, write_fd = os.pipe()
                self._read_fd = read_fd
                output_handle = msvcrt.get_osfhandle(write_fd)
            else:
                output_handle = msvcrt.get_osfhandle(stdout.fileno())
            handles = [input_handle, output_handle]
            old = [os.get_handle_inheritable(h) for h in handles]
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESTDHANDLES
            startup.hStdInput = input_handle
            startup.hStdOutput = output_handle
            startup.hStdError = output_handle
            startup.lpAttributeList = {"handle_list": handles}
            try:
                for h in handles:
                    os.set_handle_inheritable(h, True)
                flags = 0x4 | 0x08000000 | 0x200 | 0x400
                hp, ht, pid, tid = _winapi.CreateProcess(
                    self.command[0],
                    subprocess.list2cmdline(self.command),
                    None,
                    None,
                    True,
                    flags,
                    environment,
                    cwd,
                    startup,
                )
                self._handle, self.thread, self.pid, self.tid = hp, ht, pid, tid
            finally:
                for h, value in zip(handles, old):
                    os.set_handle_inheritable(h, value)
                if write_fd is not None:
                    os.close(write_fd)
            _check(self.k.AssignProcessToJobObject(self.job, self._handle))
            self.assigned = True
            if read_fd is not None:
                self.stdout = io.TextIOWrapper(
                    os.fdopen(read_fd, "rb"), encoding="utf-8", errors="replace"
                )
                self._read_fd = None
            self.birth = windows_birth(self.k, self.T, self._handle)
            # This registry write is durable while entrypoint is still suspended.
            lease.bind(self.pid, self.birth, command=self.command, state="SUSPENDED_ASSIGNED_JOB")
        except BaseException:
            self.close(force=True)
            raise

    def commit_identity(self, path):
        record = json.loads(Path(path).read_text(encoding="utf-8"))
        if record.get("pid") != self.pid or str(record.get("birth")) != self.birth:
            raise ValueError("Durable child identity does not match the suspended process")
        actual = image_hash(self.command[0])
        if record.get("image_sha256", "").upper() != actual or not re.fullmatch(
            "[0-9a-fA-F]{64}", record.get("configuration_sha256", "")
        ):
            raise ValueError("Image/configuration identity is not pinned")
        if not isinstance(record.get("wal_sequence"), int) or record["wal_sequence"] < 0:
            raise ValueError("Durable identity lacks its journal frontier")
        self.lease.bind(
            self.pid,
            self.birth,
            **{k: v for k, v in record.items() if k not in ("pid", "birth", "uuid")},
        )
        self.identity_ready = True

    def resume(self):
        if self.resumed:
            raise RuntimeError("Owned primary thread may be resumed only once")
        if not self.identity_ready:
            raise RuntimeError("Cannot resume an unjournaled compute process")
        if self.k.ResumeThread(self.thread) == 0xFFFFFFFF:
            raise C.WinError(C.get_last_error())
        self.resumed = True
        self.k.CloseHandle(self.thread)
        self.thread = None

    def poll(self):
        if self.returncode is not None:
            return self.returncode
        if self.k.WaitForSingleObject(self._handle, 0) == 0:
            code = self.T.DWORD()
            _check(self.k.GetExitCodeProcess(self._handle, C.byref(code)))
            self.returncode = code.value if code.value < 0x80000000 else code.value - (1 << 32)
        return self.returncode

    def wait(self, timeout=None):
        ms = 0xFFFFFFFF if timeout is None else max(0, int(timeout * 1000))
        result = self.k.WaitForSingleObject(self._handle, ms)
        if result == 258:
            raise subprocess.TimeoutExpired(self.command, timeout)
        _check(result == 0)
        return self.poll()

    def terminate(self):
        if self.job:
            _check(self.k.TerminateJobObject(self.job, 124))

    kill = terminate

    def close(self, force=False):
        if self.job:
            if self._handle is not None and not self.assigned and self.poll() is None:
                self.k.TerminateProcess(self._handle, 124)
            elif force or (self._handle is not None and self.poll() is None):
                self.k.TerminateJobObject(self.job, 124)
            if self._handle is not None:
                if self.k.WaitForSingleObject(self._handle, 5000) != 0:
                    raise RuntimeError("Owned child closure not observed; GPU lease retained")
                self.poll()

            class Account(C.Structure):
                _fields_ = [
                    (n, C.c_longlong) for n in ("User", "Kernel", "PeriodUser", "PeriodKernel")
                ] + [(n, self.T.DWORD) for n in ("Faults", "Total", "Active", "Terminated")]

            account = Account()
            until = time.monotonic() + 5
            while True:
                _check(
                    self.k.QueryInformationJobObject(
                        self.job, 1, C.byref(account), C.sizeof(account), None
                    )
                )
                if not account.Active:
                    break
                self.k.TerminateJobObject(self.job, 124)
                if time.monotonic() >= until:
                    raise RuntimeError("Owned descendants not drained; GPU lease retained")
                time.sleep(0.02)
            self.k.CloseHandle(self.job)
            self.job = None
        if self.thread:
            self.k.CloseHandle(self.thread)
            self.thread = None
        if self._handle is not None:
            self.k.CloseHandle(self._handle)
            self._handle = None
        if getattr(self, "_null", None):
            self._null.close()
        if self._read_fd is not None:
            os.close(self._read_fd)
            self._read_fd = None
        self.lease.release()


class PosixOwnedProcess(subprocess.Popen):
    def __init__(self, command, environment, lease, stdout=subprocess.PIPE, cwd=None):
        self.lease = lease
        command = [*map(str, command), "--owner-pid", str(os.getpid())]
        super().__init__(
            command,
            stdout=stdout,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            env=environment,
            cwd=cwd,
            start_new_session=True,
            pass_fds=(lease.fd,),
        )
        self.birth = Path(f"/proc/{self.pid}/stat").read_text().rsplit(")", 1)[1].split()[19]
        lease.bind(self.pid, self.birth, command=command, state="OWNER_PID_GUARD_REQUIRED")

    def commit_identity(self, path):
        record = json.loads(Path(path).read_text(encoding="utf-8"))
        if record.get("pid") != self.pid or str(record.get("birth")) != str(self.birth):
            raise ValueError("Durable POSIX child identity mismatch")
        self.lease.bind(
            self.pid,
            self.birth,
            **{k: v for k, v in record.items() if k not in ("pid", "birth", "uuid")},
        )

    def resume(self):
        pass

    def close(self, force=False):
        if self.poll() is None:
            self.terminate()
            self.wait(timeout=10)
        self.lease.release()


def create_owned(
    command, environment, gpu_uuid, stdout=subprocess.PIPE, cwd=None, registry_root=None
):
    if os.name == "nt" and "--state" in command and "--stop-file" in command:
        from pgn_detached_worker import DetachedWindowsProcess

        return DetachedWindowsProcess(
            command, environment, gpu_uuid, cwd=cwd, registry_root=registry_root
        )
    lease = GpuLease(gpu_uuid, registry_root).acquire()
    try:
        cls = WindowsOwnedProcess if os.name == "nt" else PosixOwnedProcess
        return cls(command, environment, lease, stdout, cwd)
    except BaseException:
        lease.release()
        raise


def run_checked(command, environment, gpu_uuid, folder, stdout, timeout=180):
    """Finite numeric child with the same ownership gate, not a Native gain test."""
    process = create_owned(command, environment, gpu_uuid, stdout=stdout)
    try:
        record = {
            "pid": process.pid,
            "birth": str(process.birth),
            "image_sha256": image_hash(command[0]),
            "configuration_sha256": hashlib.sha256(
                json.dumps(list(map(str, command))).encode()
            ).hexdigest(),
            "wal_sequence": 1,
            "kind": "FINITE_DIAGNOSTIC",
            "uuid": gpu_uuid,
        }
        folder = Path(folder)
        with (folder / "owner-journal.jsonl").open("x", encoding="utf-8") as journal:
            journal.write(json.dumps(record) + "\n")
            journal.flush()
            os.fsync(journal.fileno())
        identity = folder / "owner-identity.json"
        _atomic(identity, record)
        process.commit_identity(identity)
        process.resume()
        result = process.wait(timeout=timeout)
        return result, {"pid": process.pid, "birth": str(process.birth), "closed": True}
    except BaseException:
        process.terminate()
        raise
    finally:
        process.close()
