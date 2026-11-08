"""Linux ownership adapter for the shared native update lifecycle.

A pidfd pins one kernel process. Its UID, executable inode/hash and boot/start
identity must agree before the updater can ask the owned UI to checkpoint.
"""
from pathlib import Path
import hashlib
import os
import re
import select
import sys
from horizon_lifecycle import LifecycleError, ProcessAbsentError, owned_ipc, graceful_update_drain


def _birth(pid):
    try:
        fields = Path(f'/proc/{pid}/stat').read_text().rpartition(') ')[2].split()
        boot = Path('/proc/sys/kernel/random/boot_id').read_text().strip()
        if len(fields) < 20 or not fields[19].isdigit() or not re.fullmatch(r'[0-9a-f-]{36}', boot):
            raise LifecycleError('Kernel process identity is malformed')
        return boot+':'+fields[19]
    except FileNotFoundError as error:
        raise ProcessAbsentError('Owned process no longer exists') from error


class OwnedLinuxProcess:
    def __init__(self, pid, executable, sha256, birth=None):
        self.handle = None
        if sys.platform != 'linux' or not hasattr(os, 'pidfd_open'):
            raise LifecycleError('Linux pidfd process ownership is required')
        if type(pid) is not int or pid <= 0:
            raise LifecycleError('Positive owned process ID required')
        original = Path(executable)
        if not original.is_absolute() or original.is_symlink():
            raise LifecycleError('Plain absolute owned executable required')
        self.executable = original.resolve(strict=True)
        if not self.executable.is_file():
            raise LifecycleError('Owned executable is unavailable')
        self.pid = pid
        try:
            self.handle = os.pidfd_open(pid)
            if self.exited():
                raise ProcessAbsentError('Owned process already exited')
            self.birth = _birth(pid)
            if birth is not None and self.birth != str(birth):
                raise ProcessAbsentError('Original owned process is absent; boot/start identity differs')
            proc = Path(f'/proc/{pid}')
            if proc.stat().st_uid != os.getuid():
                raise LifecycleError('Owned process belongs to another user')
            actual = proc/'exe'
            if actual.resolve(strict=True) != self.executable:
                raise LifecycleError('PID belongs to another executable')
            with actual.open('rb') as image, self.executable.open('rb') as declared:
                a, b = os.fstat(image.fileno()), os.fstat(declared.fileno())
                if (a.st_dev,a.st_ino) != (b.st_dev,b.st_ino):
                    raise LifecycleError('Owned executable inode differs')
                if hashlib.file_digest(image, 'sha256').hexdigest() != sha256:
                    raise LifecycleError('Owned executable fingerprint differs')
            if self.exited() or _birth(pid) != self.birth:
                raise ProcessAbsentError('Owned process exited during inspection')
        except ProcessLookupError as error:
            self.close()
            raise ProcessAbsentError('Owned process no longer exists') from error
        except BaseException:
            self.close()
            raise

    def exited(self):
        if self.handle is None:
            raise LifecycleError('Owned process handle is closed')
        poller = select.poll()
        poller.register(self.handle, select.POLLIN | select.POLLHUP | select.POLLERR)
        return bool(poller.poll(0))

    def identity(self):
        return dict(pid=self.pid, birth=self.birth, executable=str(self.executable))

    def close(self):
        if self.handle is not None:
            os.close(self.handle)
            self.handle = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
