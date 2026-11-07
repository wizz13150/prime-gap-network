"""One bounded, off-lane archive worker; disk state is the recovery queue."""
from collections import deque
from contextlib import contextmanager
from pathlib import Path
import threading
import time

from horizon_ticket_archive import compact, compact_batch, retire_completed_owner_output


class ArchiveLane:
    capacity = 8

    def __init__(self, root, notice, *, sweep_seconds=30):
        if sweep_seconds <= 0:
            raise ValueError("Positive archive maintenance interval required")
        self.root = Path(root).resolve()
        self.notice = notice
        self.sweep_seconds = sweep_seconds
        self.condition = threading.Condition()
        self.pending = deque()
        self.members = set()
        self.foreground = {}
        self.working = None
        self.stopping = False
        self.thread = threading.Thread(target=self._run, name="completed-ticket-archive", daemon=True)
        self.thread.start()

    def offer(self, folder):
        # This method never opens a result, index or archive. The durable ticket
        # remains discoverable by the fair sweep if this small hint queue is full.
        folder = Path(folder)
        if (folder.parent != self.root / "tickets" or len(folder.name) != 64
                or any(c not in "0123456789abcdef" for c in folder.name)):
            raise ValueError("Archive hint must name an exact local ticket")
        with self.condition:
            if self.stopping:
                return False
            if folder not in self.members:
                if len(self.pending) >= self.capacity:
                    return False
                self.pending.append(folder)
                self.members.add(folder)
            self.condition.notify()
            return True

    @contextmanager
    def use_ticket(self, folder):
        """Reserve this ticket, without waiting for maintenance of other work."""
        with self.condition:
            self.condition.wait_for(lambda: self.working != folder)
            self.foreground[folder] = self.foreground.get(folder, 0) + 1
        try:
            yield
        finally:
            with self.condition:
                self.foreground[folder] -= 1
                if not self.foreground[folder]:
                    del self.foreground[folder]
                self.condition.notify_all()

    @contextmanager
    def _ticket_guard(self, folder):
        with self.condition:
            allowed = not self.stopping and folder not in self.foreground
            if allowed:
                self.working = folder
        try:
            yield allowed
        finally:
            if allowed:
                with self.condition:
                    self.working = None
                    self.condition.notify_all()

    def _report(self, error):
        try:
            self.notice(dict(type="notice", message="Completed-ticket cleanup deferred: " + str(error)[:300]))
        except Exception:
            # Presentation failure does not stop cleanup of already durable work.
            pass

    @staticmethod
    def _compact(folder):
        outcome = compact(folder)
        if outcome["compacted"]:
            for owner in folder.glob("native-owner-*"):
                if owner.is_dir():
                    retire_completed_owner_output(owner)

    def _run(self):
        next_sweep = time.monotonic() + self.sweep_seconds
        while True:
            with self.condition:
                while not self.stopping and not self.pending and time.monotonic() < next_sweep:
                    self.condition.wait(next_sweep - time.monotonic())
                if self.stopping:
                    return
                folder = self.pending.popleft() if self.pending else None
            started = time.monotonic()
            try:
                if folder is not None:
                    with self._ticket_guard(folder) as allowed:
                        if allowed:
                            self._compact(folder)
                if time.monotonic() >= next_sweep:
                    # The persistent cursor advances even past active or refused
                    # tickets. One candidate per sweep bounds foreground I/O duty.
                    if (self.root / "tickets").is_dir():
                        compact_batch(self.root, limit=1, _ticket_guard=self._ticket_guard)
                    next_sweep = time.monotonic() + self.sweep_seconds
            except Exception as error:
                self._report(error)
                next_sweep = time.monotonic() + self.sweep_seconds
            finally:
                with self.condition:
                    self.members.discard(folder)
                    # At most 10% maintenance duty after each operation. Waiting
                    # releases the lock; offers never wait for archive I/O.
                    self.condition.wait_for(lambda: self.stopping,
                                            timeout=9 * (time.monotonic() - started))

    def close(self):
        with self.condition:
            self.stopping = True
            self.condition.notify_all()
        self.thread.join(5)
        return not self.thread.is_alive()
