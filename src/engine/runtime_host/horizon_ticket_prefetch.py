"""One signed next-ticket reservation, prepared by an existing network worker."""
from pathlib import Path
import json
import threading
import time
from pgn_session import atomic_bytes
from report_commitment import canonical_bytes

class TicketPrefetch:
    def __init__(self, allocator, enabled, stopping, network_lock, on_ready=None, *, recovery=None, on_recovered=None, recover_allocator=None):
        self.allocator = allocator
        self.enabled = enabled
        self.stopping = stopping
        self.network_lock = network_lock
        self.on_ready = on_ready
        self.recovery, self.on_recovered = recovery, on_recovered
        self.recover_allocator = recover_allocator
        self.ready_recovered = False
        self.condition = threading.Condition()
        self.ready = None
        self.error = None
        self.native_service_status = None
        self.thread = threading.Thread(target=self._run, name="ticket-preparation", daemon=True)

    def start(self):
        self.thread.start()

    def _run(self):
        while not self.stopping.is_set():
            with self.condition:
                if self.ready is not None:
                    self.condition.wait(.1)
                    continue
            if not self.enabled.wait(.1):
                continue
            try:
                with self.network_lock:
                    work = self.recovery() if callable(self.recovery) else None
                    recovered = work is not None
                    if work is None:
                        try:work = self.allocator.assign()
                        except ValueError as error:
                            if not callable(self.recover_allocator):raise
                            work=self.recover_allocator(error)
                            recovered=True
                with self.condition:
                    self.ready = work
                    self.ready_recovered = recovered
                    self.error = None
                    self.condition.notify_all()
                if self.on_ready is not None:
                    self.on_ready(work)
            except Exception as error:
                with self.condition:
                    self.ready = None
                    self.error = str(error)[:400]
                    status=getattr(error,"native_service_status",None)
                    if status is not None:self.native_service_status=status
                    self.condition.notify_all()
                self.stopping.wait(1)

    def take(self, promote, timeout=.2):
        # The caller publishes the active ticket durably before acknowledging
        # consumption. A crash between these writes is resolved by identity.
        until = time.monotonic() + timeout
        with self.condition:
            while self.ready is None and not self.stopping.is_set():
                remaining = until - time.monotonic()
                if remaining <= 0:
                    return None
                self.condition.wait(remaining)
            if self.ready is None:
                return None
            work = self.ready
            if self.ready_recovered:
                promote(work)
                if not callable(self.on_recovered):raise ValueError('Reassignment adoption callback required')
                self.on_recovered(work)
                self.ready=None;self.ready_recovered=False
                self.condition.notify_all()
                return work
            journal = self.allocator.root / "controller.json"
            value = json.loads(journal.read_bytes())
            if value.get("work", {}).get("ticket") != work["ticket"]:
                raise ValueError("Prepared ticket journal identity changed")
            promote(work)
            value["stage"] = "CONSUMED"
            atomic_bytes(journal, canonical_bytes(value) + b"\n")
            self.ready = None
            self.condition.notify_all()
            return work

    def close(self, timeout=5):
        with self.condition:
            self.condition.notify_all()
        if self.thread.ident is None:
            return True
        self.thread.join(timeout)
        return not self.thread.is_alive()
