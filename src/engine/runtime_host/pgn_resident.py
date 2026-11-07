"""Resident job transport. Job completion never claims that its process exited."""
from pathlib import Path
import datetime as dt, json, os, queue, struct, threading, time, uuid
from dataclasses import dataclass

class ResidentProtocolError(RuntimeError):
    pass

@dataclass(frozen=True)
class WorkReceipt:
    """A verified job receipt bound to the durable owner and exact input interval."""
    pid: int
    birth: str
    nonce: str
    sequence: int
    command: tuple
    exit_code: int

    def matches(self, chunk):
        instance = chunk.get('instance') or {}
        identity = chunk['identity']
        if self.exit_code or self.pid != instance.get('pid') or str(self.birth) != str(instance.get('birth')) or (self.nonce != instance.get('job_nonce')):
            return False
        for option, key in (('--start', 'start'), ('--length', 'length'), ('--gap-min', 'gap_min')):
            if option not in self.command or int(self.command[self.command.index(option) + 1]) != int(identity[key]):
                return False
        return True

def atomic(path, data):
    path = Path(path)
    temp = path.with_suffix(path.suffix + '.updating')
    with temp.open('wb') as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temp, path)

def frame(arguments, nonce):
    if len(nonce) != 32 or any((c not in '0123456789abcdef' for c in nonce)):
        raise ResidentProtocolError('Invalid job nonce')
    if not 1 <= len(arguments) <= 96:
        raise ResidentProtocolError('Argument count budget')
    encoded = [str(a).encode('utf8') for a in arguments]
    if any((not a or len(a) > 32768 or b'\x00' in a for a in encoded)) or sum(map(len, encoded)) > 1048576:
        raise ResidentProtocolError('Argument byte budget')
    return b'PGNJOB1\x00' + struct.pack('<I', len(encoded)) + nonce.encode() + b''.join((struct.pack('<I', len(a)) + a for a in encoded))


class CommandReadGuard:
    """Keep a published Windows command readable until its job is acknowledged."""
    def __init__(self, path, timeout=5):
        import ctypes
        from ctypes import wintypes
        self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        self.kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                           ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD,
                                           wintypes.HANDLE]
        self.kernel.CreateFileW.restype = wintypes.HANDLE
        self.kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        self.lock = threading.Lock()
        self.handle = None
        deadline = time.monotonic() + timeout
        while True:
            # READ + DELETE sharing permits atomic publication and the native
            # reader, while an exclusive scanner cannot interrupt that handoff.
            handle = self.kernel.CreateFileW(str(path), 0x80000000, 1 | 4, None, 3, 0x80, None)
            if handle not in (None, wintypes.HANDLE(-1).value):
                self.handle = handle
                return
            error = ctypes.get_last_error()
            if error not in (5, 32, 33) or time.monotonic() >= deadline:
                raise ctypes.WinError(error)
            time.sleep(.01)

    def close(self):
        with self.lock:
            handle, self.handle = self.handle, None
        if handle is not None:
            self.kernel.CloseHandle(handle)


def publish_command(path, data):
    path = Path(path)
    if path.exists():
        raise ResidentProtocolError('Duplicate resident command sequence')
    temporary = path.with_suffix(path.suffix + '.updating')
    with temporary.open('xb') as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    guard = None
    try:
        if os.name == 'nt':
            guard = CommandReadGuard(temporary)
        deadline = time.monotonic() + 5
        while True:
            try:
                os.replace(temporary, path)
                return guard
            except PermissionError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(.01)
    except BaseException:
        if guard is not None:
            guard.close()
        raise


class JobClock:
    """Measure one job while keeping the physical process creation identity unchanged."""

    def __init__(self, job, before, utc):
        self.job, self.before, self.utc = (job, before, utc)
        self._finished = None
        self._creation = int(job.birth) if job.birth else None
        self._lock = threading.Lock()

    def elapsed(self):
        return self._finished[0] if self._finished else max(0, time.monotonic() - self.before)

    def finish(self):
        with self._lock:
            if self._finished is None:
                self.job.wait()
                self._finished = (max(0, time.monotonic() - self.before), dt.datetime.now(dt.timezone.utc))
            return self._finished

class Job:
    """A finite command served by a process that may remain alive after completion."""

    def __init__(self, manager, sequence, nonce, command, first=False):
        self.manager, self.sequence, self.nonce = (manager, sequence, nonce)
        self.args = list(command)
        self.first = first
        self.committed = False
        self.resumed = False
        self.released = False
        self.lines = queue.Queue(maxsize=128)
        self.output_closed = threading.Event()
        self.stream_finished = threading.Event()
        self._result = None
        self._input_guard = None
        self._receipt_error_since = None
        self.is_plan = '--plan' in self.args
        self._output_end = None
        self._output_binding = None

    @property
    def pid(self):
        return self.manager.owner.pid

    @property
    def birth(self):
        return getattr(self, '_job_birth', None) or getattr(self.manager.owner, 'birth', None)

    def __getattr__(self, name):
        return getattr(self.manager.owner, name)

    @property
    def stdout(self):
        while True:
            try:
                value = self.lines.get(timeout=0.05)
            except queue.Empty:
                if self.manager.owner.poll() is not None and self.lines.empty():
                    return
                continue
            if value is None:
                return
            if self.terminal(value):
                self._output_end = getattr(value, 'end_offset', None)
            yield value

    def terminal(self, line):
        if self.is_plan:
            try:
                return isinstance(json.loads(line), dict) and line.lstrip().startswith('{')
            except ValueError:
                return False
        return '[phase0-gpu] --- end ---' in line or '[phase0-gpu] stopped positions=' in line

    def result(self):
        if self._result is not None:
            return self._result
        path = self.manager.directory / f'{self.sequence}.done.json'
        try:
            if path.stat().st_size > 65536:
                raise ResidentProtocolError('Oversized result')
            value = json.loads(path.read_bytes())
        except FileNotFoundError:
            return None
        except PermissionError:
            now = time.monotonic()
            if self._receipt_error_since is None:
                self._receipt_error_since = now
            if now - self._receipt_error_since >= 5:
                raise ResidentProtocolError('Resident completion remains unreadable')
            return None
        self._receipt_error_since = None
        if type(value.get('sequence')) is not int or type(value.get('pid')) is not int:
            raise ResidentProtocolError('Invalid result identity types')
        if value.get('schema') != 'SCOUT_RESIDENT_JOB_V1' or value.get('sequence') != self.sequence or value.get('nonce') != self.nonce or (value.get('pid') != self.pid):
            raise ResidentProtocolError('Result identity mismatch')
        if type(value.get('exit')) is not int or type(value.get('wall_ns')) is not int or value['wall_ns'] < 0:
            raise ResidentProtocolError('Invalid result units')
        self._result = value
        self.release_input_guard()
        return value

    def poll(self):
        if self.manager.errors:
            raise ResidentProtocolError(self.manager.errors[-1])
        value = self.result()
        if value is not None and self.output_closed.is_set():
            return value['exit']
        if self._receipt_error_since is not None:
            return None
        closed = self.manager.owner.poll()
        if closed is not None:
            if value is None:
                return closed or 8
            if self.output_closed.is_set():
                return value['exit']
            raise ResidentProtocolError('Owner exited without its terminal output record')
        return None

    def wait(self, timeout=None):
        end = None if timeout is None else time.monotonic() + timeout
        while True:
            value = self.poll()
            if value is not None:
                return value
            if end is not None and time.monotonic() >= end:
                raise TimeoutError('Resident job did not complete')
            time.sleep(0.005)

    def commit_identity(self, path):
        identity = json.loads(Path(path).read_bytes())
        if identity.get('job_nonce') != self.nonce:
            raise ResidentProtocolError('WAL job identity mismatch')
        if self.first:
            self.manager.owner.commit_identity(path)
        elif identity.get('pid') != self.pid or str(identity.get('birth')) != str(self.birth):
            raise ResidentProtocolError('WAL owner identity mismatch')
        if not self.is_plan and hasattr(self.manager.owner, 'bind_output'):
            self._output_binding = self.manager.owner.bind_output(path, self.sequence, self.nonce, self.args)
        self.committed = True

    def resume(self):
        if self.resumed:
            raise ResidentProtocolError('Duplicate job dispatch')
        if not self.committed and (not self.is_plan):
            raise ResidentProtocolError('Job requires durable identity before dispatch')
        self.resumed = True
        if not self.is_plan and self._output_binding is not None:
            self.manager.owner.dispatch_output(self._output_binding)
        if self.first:
            self.manager.owner.resume()
        else:
            self._input_guard = publish_command(
                self.manager.directory / f'{self.sequence}.args', frame(self.args[1:], self.nonce)
            )

    def release_input_guard(self):
        guard = self._input_guard
        if guard is not None:
            guard.close()
            self._input_guard = None

    def acknowledge_output(self):
        """Only the host's successful durable store.closed may call this."""
        if self.is_plan or self._output_end is None:
            return False
        if hasattr(self.manager.owner, 'acknowledge_output'):
            return self.manager.owner.acknowledge_output(self._output_end)
        return False

    def output_recovery_locator(self):
        if self._output_binding is None:
            return None
        from pgn_output_spool import recovery_locator
        return recovery_locator(self.manager.owner.root, self._output_binding)

    def close(self, force=False):
        if not force and self.poll() is None:
            raise ResidentProtocolError('Cannot retire unfinished resident job')
        self.released = True
        stop = None
        if '--stop-file' in self.args:
            stop = Path(self.args[self.args.index('--stop-file') + 1])
        if force or (stop and stop.exists()):
            self.manager.shutdown(force=force)

    def terminate(self):
        self.manager.shutdown(force=True)

    def process_exited(self):
        return self.manager.owner.poll() is not None

    def clock(self, before, utc, fallback=None):
        clock = JobClock(self, before, utc)
        if clock._creation is None and fallback:
            clock._creation = fallback(self.manager.owner, before, utc)._creation
        return clock

    def work_receipt(self):
        if not self.committed or self.is_plan or self.poll() != 0 or (not self.output_closed.is_set()):
            raise ResidentProtocolError('Unverified work completion')
        return WorkReceipt(self.pid, str(self.birth), self.nonce, self.sequence, tuple(self.args), 0)

class Resident:
    """Serialize jobs through one owned process with bounded, ordered output delivery."""

    def __init__(self, owner, directory, command, nonce):
        self.owner = owner
        self.directory = Path(directory)
        self.command = list(command)
        self.current = Job(self, 0, nonce, command, True)
        self.sequence = 0
        self._lock = threading.Lock()
        self._closed = False
        self._stop = threading.Event()
        self.errors = []
        self.reader = threading.Thread(target=self._receive, daemon=True)
        self.reader.start()

    @staticmethod
    def launch(factory, command, environment, gpu_uuid, folder):
        directory = Path(folder) / ('resident-' + uuid.uuid4().hex)
        directory.mkdir()
        nonce = uuid.uuid4().hex
        wire = list(command) + ['--resident-directory', str(directory), '--resident-nonce', nonce]
        owner = factory(wire, environment, gpu_uuid)
        return Resident(owner, directory, command, nonce)

    def _receive(self):
        try:
            for line in self.owner.stdout:
                if self._stop.is_set():
                    break
                if len(line) > 65536:
                    raise ResidentProtocolError('Oversized resident output')
                with self._lock:
                    job = self.current
                if job is None:
                    raise ResidentProtocolError('Output without a bound job')
                while not self._stop.is_set():
                    try:
                        job.lines.put(line, timeout=0.05)
                        break
                    except queue.Full:
                        continue
                if job.terminal(line):
                    job.output_closed.set()
                    while not self._stop.is_set():
                        try:
                            job.lines.put(None, timeout=0.05)
                            break
                        except queue.Full:
                            continue
        except BaseException as e:
            if not self._stop.is_set():
                self.errors.append(str(e))
        finally:
            with self._lock:
                job = self.current
            if job:
                job.stream_finished.set()
                if not job.output_closed.is_set():
                    try:
                        job.lines.put_nowait(None)
                    except queue.Full:
                        pass

    def next(self, command):
        if self._closed or self.owner.poll() is not None:
            raise ResidentProtocolError('Resident owner is not live')
        if not self.current.released:
            raise ResidentProtocolError('Previous job has not retired')
        if str(command[0]) != str(self.command[0]):
            raise ResidentProtocolError('A new module/profile needs a new resident owner')
        command = list(command)
        if os.name != 'nt' and '--owner-pid' not in command:
            command += ['--owner-pid', str(os.getpid())]
        self.sequence += 1
        job = Job(self, self.sequence, uuid.uuid4().hex, command)
        with self._lock:
            self.current = job
        return job

    def plan(self, command, timeout=30):
        job = self.next(command)
        if not job.is_plan:
            raise ResidentProtocolError('Plan command required')
        job.resume()
        code = job.wait(timeout)
        lines = list(job.stdout)
        job.close()
        if code not in (0, 1):
            raise ResidentProtocolError('Resident plan failed: ' + str(code))
        if code == 1:
            self.shutdown()
        docs = [json.loads(line) for line in lines if line.lstrip().startswith('{')]
        if len(docs) != 1:
            raise ResidentProtocolError('One plan response required')
        return docs[0]

    def shutdown(self, force=False):
        if self._closed:
            return
        atomic(self.directory / 'shutdown', b'close\n')
        if self.current and (not self.current.released) and ('--stop-file' in self.current.args):
            stop = Path(self.current.args[self.current.args.index('--stop-file') + 1])
            atomic(stop, b'resident shutdown\n')
        problem = None
        try:
            if force:
                self.owner.terminate()
            self.owner.wait(timeout=90)
        except Exception as e:
            problem = e
        finally:
            self._stop.set()
            try:
                self.owner.close(force=force or problem is not None)
            except Exception as e:
                if problem is None:
                    problem = e
            self._closed = self.owner.poll() is not None
        if not self._closed:
            raise ResidentProtocolError('Resident process closure not confirmed')
        if self.current:
            self.current.release_input_guard()
        self.reader.join(timeout=5)
        if problem is not None and (not force):
            raise problem
        if self.reader.is_alive():
            raise ResidentProtocolError('Resident output did not close')

class Manager:
    """Keep one installed cached module/profile resident for this session; require protocol support."""

    def __init__(self, api, folder, gpu_uuid, factory):
        self.api, self.folder, self.gpu_uuid, self.factory = (api, Path(folder), gpu_uuid, factory)
        self.resident = None

    def active_worker(self):
        """An exact warm profile may be reused after a fresh bound plan check."""
        if self.resident is None or self.resident._closed or self.resident.owner.poll() is not None:
            return None
        return self.resident.command[0]

    def query(self, command):
        if self.resident is None:
            return None
        if self.resident._closed or self.resident.owner.poll() is not None:
            self.shutdown()
            return None
        if str(command[0]) != str(self.resident.command[0]):
            self.shutdown()
            return None
        return self.resident.plan(command)

    def create(self, command, environment):
        if self.resident is None:
            from pgn_native_contract import module_metadata
            meta = module_metadata(command[0])
            if meta.get('resident_job_schema') != 'SCOUT_RESIDENT_JOB_V1':
                raise ResidentProtocolError('Installed cached module does not support required resident jobs')
            self.resident = Resident.launch(self.factory, command, environment, self.gpu_uuid, self.folder)
            return self.resident.current
        if str(command[0]) != str(self.resident.command[0]):
            self.shutdown()
            return self.create(command, environment)
        return self.resident.next(command)

    def shutdown(self, force=False):
        if self.resident is not None:
            resident = self.resident
            resident.shutdown(force=force or not resident.current.resumed)
            self.resident = None
