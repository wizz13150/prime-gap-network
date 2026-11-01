"""One GPU execution lane; prepared search and audit jobs share a bounded scheduler."""
from collections import deque
from concurrent.futures import Future, CancelledError
from dataclasses import dataclass, field
import threading
import copy
import time

@dataclass
class GpuJob:
    ticket: dict
    context: dict
    maximum_piece: int
    future: Future = field(default_factory=Future)
    view_sequence: int | None = None

class ResidentGpuQueue:
    def __init__(self, engine, enabled, stopping, on_start, on_result, on_paused, on_error, on_waiting=None):
        self.engine = engine
        self.enabled, self.stopping = enabled, stopping
        self.on_start, self.on_result = on_start, on_result
        self.on_paused, self.on_error = on_paused, on_error
        self.on_waiting = on_waiting
        self.condition = threading.Condition()
        self.search, self.audits = deque(), deque()
        self.jobs = {}
        self.active = None
        self.resume_job = None
        self.audit_credit = 0
        self.last_completed = None
        self.timings = deque(maxlen=32)

    def submit(self, ticket, context, maximum_piece):
        identity = ticket["ticket_id"]
        role = context.get("role")
        if role not in ("search", "audit"):
            raise ValueError("Explicit GPU job role required")
        with self.condition:
            if identity in self.jobs:
                old = self.jobs[identity]
                if old.ticket != ticket or old.context != context:
                    raise ValueError("Queued GPU work identity changed")
                return old.future
            lane = self.search if role == "search" else self.audits
            if len(lane) >= (2 if role == "search" else 10):
                raise BufferError("Prepared GPU work queue is full")
            job = GpuJob(copy.deepcopy(ticket), copy.deepcopy(context), maximum_piece)
            job.future.set_running_or_notify_cancel()
            self.jobs[identity] = job
            lane.append(job)
            self.condition.notify_all()
            return job.future

    def wake(self):
        with self.condition:
            self.condition.notify_all()

    def _next(self):
        if self.resume_job is not None:
            job, self.resume_job = self.resume_job, None
            return job
        if self.audits and (self.audit_credit > 0 or not self.search):
            return self.audits.popleft()
        if self.search:
            return self.search.popleft()
        return None

    def run(self):
        announced_pause = False
        waiting_since = None
        while not self.stopping.is_set():
            if not self.enabled.wait(.1):
                if not announced_pause:
                    self.on_paused()
                    announced_pause = True
                    self.last_completed = None
                continue
            announced_pause = False
            with self.condition:
                job = self._next()
                if job is None:
                    if waiting_since is None:
                        waiting_since = time.monotonic()
                    if self.on_waiting is not None and time.monotonic() - waiting_since >= .5:
                        self.on_waiting()
                        waiting_since = time.monotonic()
                    self.condition.wait(.05)
                    continue
            waiting_since = None
            self.active = job
            try:
                preparation_started = time.monotonic_ns()
                self.on_start(job)
                started = time.monotonic_ns()
                if self.last_completed is not None:
                    self.timings.append(dict(previous=self.last_completed[0],current=job.ticket["ticket_id"],role=job.context["role"],dispatch_gap_ns=str(started-self.last_completed[1]),context_preparation_ns=str(started-preparation_started),measurement="Host handoff includes durable result publication; not a CUDA kernel trace"))
                result = self.engine.run_ticket(job.ticket, job.maximum_piece)
                if result.get("state") == "PAUSED":
                    self.enabled.clear()
                    with self.condition:
                        self.resume_job = job
                    self.on_paused()
                    announced_pause = True
                    self.last_completed = None
                    continue
                # Persist the completed handoff before publishing another active
                # job. This callback must never perform network requests.
                finished = time.monotonic_ns()
                self.on_result(job, result)
                length = int(job.ticket["end"]) - int(job.ticket["start"])
                if job.context["role"] == "search":
                    self.audit_credit = min(40_000_000_000_000, self.audit_credit + length // 10)
                else:
                    self.audit_credit = max(0, self.audit_credit - length)
                self.last_completed = (job.ticket["ticket_id"],finished)
                job.future.set_result(result)
                with self.condition:
                    self.jobs.pop(job.ticket["ticket_id"], None)
            except Exception as error:
                # Retain this bound job until an explicit restart. Never skip
                # failed work, invent completion or select another arithmetic.
                self.enabled.clear()
                with self.condition:
                    (self.search if job.context["role"] == "search" else self.audits).appendleft(job)
                self.on_error(error)
                self.on_paused()
                announced_pause = True
                self.last_completed = None
            finally:
                self.active = None
        with self.condition:
            for job in self.jobs.values():
                if not job.future.done():
                    job.future.set_exception(CancelledError("GPU work retained for restart"))
