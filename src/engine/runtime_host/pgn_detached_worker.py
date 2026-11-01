"""Windows controller drains one exact worker under explicit OS ownership.

Local file IPC only. The UI never owns the compute Job handle. No GPU API is
called here. A controller cannot be reused to run a second command.
"""

from pathlib import Path
import ctypes as C
import hashlib, json, os, subprocess, sys, threading, time, uuid

sys.path.insert(0, str(Path(__file__).resolve().parent))


PUBLICATION_GRACE_SECONDS = 5.0


def _publication_busy(error):
    """A Windows sharing/access race is temporary; other I/O failures are not."""
    return isinstance(error, PermissionError) or getattr(error, "winerror", None) in (5, 32, 33)


def atomic(path, value):
    path = Path(path)
    tmp = path.with_suffix(path.suffix + ".pending")
    with tmp.open("w", encoding="utf-8") as f:
        json.dump(value, f)
        f.flush()
        os.fsync(f.fileno())
    until = time.monotonic() + PUBLICATION_GRACE_SECONDS
    while True:
        try:
            os.replace(tmp, path)
            return
        except OSError as error:
            if not _publication_busy(error) or time.monotonic() >= until:
                raise
            # Retry only the exact fsynced receipt. Never relaunch a worker or
            # infer completion because a file is temporarily inaccessible.
            time.sleep(0.02)


def load(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def _load_publication(path):
    """Bounded blocking read for admission, outside the running UI poll."""
    until = time.monotonic() + PUBLICATION_GRACE_SECONDS
    while True:
        try:
            return load(path)
        except OSError as error:
            if not _publication_busy(error) or time.monotonic() >= until:
                raise
            time.sleep(0.02)


def boot_main(path):
    from pgn_owned import GpuLease, WindowsOwnedProcess, _kernel, _recorded_alive, image_hash

    path = Path(path).resolve()
    data = _load_publication(path)
    root = path.parent
    token = data["token"]
    if root.name != "native-owner-" + token:
        raise ValueError("Invalid one-shot owner identity")
    k, T = _kernel()
    inside = T.BOOL()
    k.IsProcessInJob.argtypes = [T.HANDLE, T.HANDLE, C.POINTER(T.BOOL)]
    k.IsProcessInJob.restype = T.BOOL
    k.GetCurrentProcess.restype = T.HANDLE
    if not k.IsProcessInJob(k.GetCurrentProcess(), None, C.byref(inside)):
        raise RuntimeError("Cannot determine the controller's enclosing Job; no worker launched")
    # An enclosing host may prohibit breakaway. WindowsOwnedProcess must still
    # assign the suspended compute child to our kill-on-close Job before any
    # launch commitment. Windows enforces both Jobs' limits; incompatible nesting
    # fails while the child remains suspended. If the host kills its whole Job,
    # the controller and nested child terminate together; committed checkpoints
    # remain the only resume authority.
    cmd = data["command"]
    stop = Path(cmd[cmd.index("--stop-file") + 1]).resolve()
    state = Path(cmd[cmd.index("--state") + 1]).resolve()
    if stop.parent != root.parent or state.parent != root.parent:
        raise ValueError("Worker control files must belong to its session")
    if image_hash(cmd[0]) != data["image_sha256"]:
        raise ValueError("Worker image changed before owner admission")
    process = None
    lease = None
    started = False
    orphan = False
    code = None
    failure = None
    copy_error = []
    reader = None
    output_info = {}

    def parent_alive():
        return _recorded_alive(k, T, data["parent"])

    try:
        lease = GpuLease(data["gpu_uuid"], data.get("registry_root")).acquire()
        process = WindowsOwnedProcess(cmd, data["environment"], lease, cwd=data.get("cwd"))
        atomic(
            root / "ready.json",
            {
                "token": token,
                "pid": process.pid,
                "birth": process.birth,
                "owner_pid": os.getpid(),
                "controller_in_parent_job": bool(inside.value),
                "image_sha256": data["image_sha256"],
            },
        )
        until = time.monotonic() + 30
        while not (root / "resume.json").is_file():
            if not parent_alive():
                raise RuntimeError("UI closed before launch commitment; worker stayed suspended")
            if (root / "terminate.request").exists():
                raise RuntimeError("UI cancelled the suspended launch")
            if time.monotonic() > until:
                raise RuntimeError("Launch identity commitment timed out")
            time.sleep(0.01)
        request = _load_publication(root / "resume.json")
        if request["token"] != token:
            raise ValueError("Stale owner command")
        identity = Path(request["identity"]).resolve()
        if identity != root.parent / "native-owner-current.json":
            raise ValueError("Launch identity path escaped this session")
        process.commit_identity(identity)
        committed = _load_publication(identity)
        if committed.get("pid") != process.pid or str(committed.get("birth")) != process.birth:
            raise ValueError("Committed child identity changed")
        atomic(root / "committed-identity.json", committed)
        process.resume()
        started = True

        def copy_output():
            try:
                digest = hashlib.sha256()
                written = 0
                with (root / "stdout.log").open("x", encoding="utf-8", newline="\n") as f:
                    for line in process.stdout:
                        if len(line) > 65536:
                            raise ValueError("Oversized Native publication")
                        data = line.encode("utf-8")
                        f.write(line)
                        f.flush()
                        digest.update(data)
                        written += len(data)
                    os.fsync(f.fileno())
                output_info.update(stdout_sha256=digest.hexdigest().upper(), stdout_bytes=written)
            except BaseException as error:
                copy_error.append(str(error))

        reader = threading.Thread(target=copy_output, name="PGN-worker-output")
        reader.start()
        until = None
        while process.poll() is None:
            if not parent_alive() and not orphan:
                orphan = True
                stop.write_text(
                    "terminal closed: suspend at the next safe boundary\n", encoding="utf-8"
                )
                until = time.monotonic() + 90
            if (
                (root / "terminate.request").exists()
                or copy_error
                or (until and time.monotonic() > until)
            ):
                process.terminate()
                break
            time.sleep(0.02)
        code = process.wait()
        reader.join(5)
        if reader.is_alive():
            raise RuntimeError("Native output did not drain")
        if copy_error:
            raise RuntimeError("Native output persistence: " + copy_error[0])
    except BaseException as error:
        failure = str(error)
        if process is not None and process.poll() is None:
            process.terminate()
    finally:
        closed = False
        try:
            if process is not None:
                process.close()
                closed = True
            elif lease is not None:
                lease.release()
                closed = True
        except BaseException as error:
            failure = (failure + "; " if failure else "") + str(error)
        result = {
            "token": token,
            "exit": code,
            "closed": closed,
            "started": started,
            "orphan": orphan,
            "error": failure,
            "image_sha256": data["image_sha256"],
            "checkpoint": str(state),
            "stdout": str(root / "stdout.log"),
        }
        result.update(output_info)
        committed_file = root / "committed-identity.json"
        if committed_file.is_file():
            result["committed_identity_sha256"] = (
                hashlib.sha256(committed_file.read_bytes()).hexdigest().upper()
            )
        if state.is_file() and state.stat().st_size == 64:
            result["checkpoint_sha256"] = hashlib.sha256(state.read_bytes()).hexdigest().upper()
        atomic(root / "done.json", result)
        if orphan:
            atomic(root.parent / "native-owner-recovery.json", dict(result, owner_folder=root.name))
    return 0 if code == 0 and closed and failure is None else 1


class _Output:
    def __init__(self, owner):
        self.owner = owner
        self.file = None

    def __iter__(self):
        return self

    def __next__(self):
        while True:
            if self.file is None and (self.owner.root / "stdout.log").is_file():
                self.file = (self.owner.root / "stdout.log").open("r", encoding="utf-8")
            line = self.file.readline() if self.file else ""
            if line:
                return line
            if (self.owner.root / "done.json").is_file():
                line = self.file.readline() if self.file else ""
                if line:
                    return line
                raise StopIteration
            time.sleep(0.01)

    def close(self):
        if self.file:
            self.file.close()
            self.file = None


class DetachedWindowsProcess:
    def __init__(self, command, environment, gpu_uuid, cwd=None, registry_root=None):
        from pgn_owned import _kernel, windows_birth, image_hash

        self.command = list(map(str, command))
        self.returncode = None
        self._done_result = None
        self._publication_error_since = None
        self._completion_lock = threading.RLock()
        self._handle = None
        self.resumed = False
        self.k, self.T = _kernel()
        self.k.GetCurrentProcess.restype = self.T.HANDLE
        parent = {
            "pid": os.getpid(),
            "birth": windows_birth(self.k, self.T, self.k.GetCurrentProcess()),
        }
        token = uuid.uuid4().hex
        folder = Path(self.command[self.command.index("--state") + 1]).resolve().parent
        self.root = folder / ("native-owner-" + token)
        self.root.mkdir(exist_ok=False)
        data = {
            "token": token,
            "parent": parent,
            "command": self.command,
            "environment": dict(environment),
            "gpu_uuid": gpu_uuid,
            "registry_root": str(registry_root) if registry_root else None,
            "cwd": str(cwd) if cwd else None,
            "image_sha256": image_hash(self.command[0]),
        }
        self.image_sha256 = data["image_sha256"]
        self.token = token
        bootstrap = self.root / "bootstrap.json"
        atomic(bootstrap, data)
        env = dict(os.environ)
        env["PGN_PRIVATE_OWNER_BOOTSTRAP"] = str(bootstrap)
        entry = (
            [sys.executable]
            if getattr(sys, "frozen", False)
            else [sys.executable, "-I", "-X", "utf8", str(Path(__file__).resolve()), str(bootstrap)]
        )
        with (
            (self.root / "controller.stdout").open("xb") as out,
            (self.root / "controller.stderr").open("xb") as err,
        ):
            options = dict(stdin=subprocess.DEVNULL, stdout=out, stderr=err,
                           env=env, close_fds=True)
            try:
                self.controller = subprocess.Popen(
                    entry, creationflags=0x01000000 | 0x8 | 0x200, **options)
                self.controller_job_mode = "breakaway"
            except OSError as error:
                # Task Scheduler and other process hosts can forbid breakaway.
                # Inherit their limits instead. The compute child still starts
                # suspended and must enter our own kill-on-close nested Job.
                # Never retry a general access error outside an enclosing Job.
                in_job = self.T.BOOL()
                if (getattr(error, "winerror", None) != 5 or
                    not self.k.IsProcessInJob(self.k.GetCurrentProcess(), None,
                                             C.byref(in_job)) or not in_job.value):
                    raise
                self.controller = subprocess.Popen(entry, creationflags=0x8 | 0x200, **options)
                self.controller_job_mode = "inherited-parent-job"
        until = time.monotonic() + 30
        while not (self.root / "ready.json").is_file():
            if self.controller.poll() is not None or (self.root / "done.json").is_file():
                error = (
                    _load_publication(self.root / "done.json").get("error")
                    if (self.root / "done.json").exists()
                    else (self.root / "controller.stderr").read_text(errors="replace")[-4000:]
                )
                raise RuntimeError("GPU owner admission failed: " + str(error))
            if time.monotonic() > until:
                self.terminate()
                raise RuntimeError("GPU owner did not acknowledge its suspended child")
            time.sleep(0.01)
        ready = _load_publication(self.root / "ready.json")
        assert ready["token"] == token and ready["image_sha256"] == data["image_sha256"]
        self.pid = int(ready["pid"])
        self.birth = str(ready["birth"])
        self._handle = self.k.OpenProcess(0x100000 | 0x1000, False, self.pid)
        if not self._handle or windows_birth(self.k, self.T, self._handle) != self.birth:
            raise RuntimeError("Detached worker birth identity changed")
        self.stdout = _Output(self)
        self.token = token

    def commit_identity(self, path):
        self.identity = str(Path(path).resolve())

    def resume(self):
        if self.resumed:
            raise RuntimeError("Worker may be resumed only once")
        atomic(self.root / "resume.json", {"token": self.token, "identity": self.identity})
        self.resumed = True

    def _pending_publication(self, error):
        now = time.monotonic()
        if self._publication_error_since is None:
            self._publication_error_since = now
        if now - self._publication_error_since >= PUBLICATION_GRACE_SECONDS:
            raise RuntimeError(
                "Detached owner completion remains unreadable; closure is unconfirmed: " + str(error)
            ) from error
        return None

    def poll(self):
        # The UI and the lifetime watcher both poll. Admission, temporary-error
        # timing and publication caching form one indivisible state transition.
        with self._completion_lock:
            return self._poll_completion()

    def _poll_completion(self):
        if self.returncode is not None:
            return self.returncode
        p = self.root / "done.json"
        try:
            result = load(p)
        except FileNotFoundError as error:
            # Absence is normal while the owner is alive. Once it exits, its
            # exact final receipt is still required; never invent an exit code.
            if self.controller.poll() is None:
                return None
            return self._pending_publication(error)
        except OSError as error:
            if not _publication_busy(error):
                raise
            return self._pending_publication(error)
        if result["token"] != self.token:
            raise RuntimeError("Detached owner response identity changed")
        if result.get("image_sha256") != self.image_sha256:
            raise RuntimeError("Detached owner image response changed")
        healthy = (
            result.get("closed") is True
            and result.get("started") is True
            and result.get("error") is None
        )
        self._done_result = result
        self._publication_error_since = None
        self.returncode = result["exit"] if healthy and type(result.get("exit")) is int else 1
        return self.returncode

    def wait(self, timeout=None):
        until = time.monotonic() + timeout if timeout is not None else None
        while self.poll() is None:
            if until is not None and time.monotonic() > until:
                raise subprocess.TimeoutExpired(self.command, timeout)
            time.sleep(0.01)
        return self.returncode

    def terminate(self):
        (self.root / "terminate.request").write_text("terminate exact owned worker\n")

    kill = terminate

    def close(self, force=False):
        try:
            if force or self.poll() is None:
                self.terminate()
            self.wait(15)
            # poll()/wait() already authenticated this immutable one-shot
            # receipt. Reopening it introduced another needless sharing race.
            result = self._done_result
            self.controller.wait(5)
        finally:
            if self._handle:
                self.k.CloseHandle(self._handle)
                self._handle = None
            self.stdout.close()
        if not result["closed"]:
            raise RuntimeError("Detached owner closure not observed; lease remains retained")
        if result.get("error") is not None:
            raise RuntimeError("Detached owner failed: " + str(result["error"]))


if __name__ == "__main__":
    raise SystemExit(boot_main(sys.argv[1]))
