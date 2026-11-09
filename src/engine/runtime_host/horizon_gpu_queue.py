"""One GPU execution lane; prepared search and audit jobs share a bounded scheduler."""
from collections import deque
from concurrent.futures import Future, CancelledError
from dataclasses import dataclass, field
import threading
import copy
import time
from horizon_audit_pressure import AuditPressure, WeightedService

@dataclass
class GpuJob:
    ticket: dict
    context: dict
    maximum_piece: int
    future: Future = field(default_factory=Future)
    view_sequence: int | None = None

class ResidentGpuQueue:
    minimum_service_ns = 1_000_000_000
    maximum_scheduled_search_piece = 4 * 10**12

    def __init__(self, engine, enabled, stopping, on_start, on_result, on_paused, on_error, on_waiting=None, *, clock_ns=time.monotonic_ns, policy_clock=time.monotonic, may_start=None):
        self.engine = engine
        self.enabled, self.stopping = enabled, stopping
        self.on_start, self.on_result = on_start, on_result
        self.on_paused, self.on_error = on_paused, on_error
        self.on_waiting = on_waiting
        # Local workflow state only: admission must never perform network I/O
        # in the execution lane. None preserves an ungated standalone queue.
        self.may_start = may_start
        self.search_waiting_for_audits = False
        self.condition = threading.Condition()
        self.search, self.audits = deque(), deque()
        self.jobs = {}
        self.active = None
        self.resume_job = None
        self.clock_ns = clock_ns
        self.audit_pressure = AuditPressure(clock=policy_clock)
        self.service = WeightedService(self.audit_pressure.weights())
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

    def update_audit_backlog(self, compute_slots, audit_operators, oldest_seconds):
        with self.condition:
            weights = self.audit_pressure.update(compute_slots, audit_operators, oldest_seconds)
            self.service.update_weights(weights)
            self.condition.notify_all()

    def audit_service_view(self):
        with self.condition:
            view = self.audit_pressure.view()
            view["target_audit_share"] = view.pop("audit_share")
            view.update(search_ready=len(self.search), audit_ready=len(self.audits),
                        active_role=self.active.context["role"] if self.active else None,
                        search_waiting_for_audits=self.search_waiting_for_audits)
            return view

    def admitted(self, job):
        allowed = True if self.may_start is None else self.may_start(job)
        if type(allowed) is not bool:
            raise ValueError("Explicit local workflow admission required")
        return allowed

    def should_yield(self, job, started):
        with self.condition:
            if self.stopping.is_set() or not self.enabled.is_set():
                return False  # Explicit shutdown/pause owns the stop request.
            elapsed = self.clock_ns() - started
            if elapsed < self.minimum_service_ns:
                return False  # Amortize checkpoint and context handoff costs.
            other = self.audits if job.context["role"] == "search" else self.search
            other_ready = any(self.admitted(candidate) for candidate in other)
            return self.service.should_yield(job.context["role"], elapsed, other_ready)

    def _next(self):
        if self.resume_job is not None:
            if not self.admitted(self.resume_job):
                return None
            job, self.resume_job = self.resume_job, None
            return job
        # Search remains FIFO. Audit preparation may finish out of order, so
        # find the first permitted audit within the existing ten-job bound.
        search = self.search[0] if self.search and self.admitted(self.search[0]) else None
        audit = next((job for job in self.audits if self.admitted(job)), None)
        self.search_waiting_for_audits = bool(self.search) and search is None
        role = self.service.choose(search is not None, audit is not None)
        if role == "audit":
            self.audits.remove(audit)
            return audit
        if role == "search":
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
            try:
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
            except Exception as error:
                self.enabled.clear()
                self.on_error(error)
                self.on_paused()
                announced_pause = True
                self.last_completed = None
                continue
            waiting_since = None
            self.active = job
            try:
                preparation_started = self.clock_ns()
                self.on_start(job)
                started = self.clock_ns()
                if self.last_completed is not None:
                    self.timings.append(dict(previous=self.last_completed[0],current=job.ticket["ticket_id"],role=job.context["role"],dispatch_gap_ns=str(started-self.last_completed[1]),context_preparation_ns=str(started-preparation_started),measurement="Host handoff includes durable result publication; not a CUDA kernel trace"))
                scheduled = getattr(self.engine, "run_scheduled_ticket", None)
                maximum_piece = (min(job.maximum_piece, self.maximum_scheduled_search_piece)
                                 if job.context["role"] == "search" else job.maximum_piece)
                result = (scheduled(job.ticket, maximum_piece,
                                    lambda: self.should_yield(job, started))
                          if callable(scheduled) else self.engine.run_ticket(job.ticket, job.maximum_piece))
                finished = self.clock_ns()
                with self.condition:
                    self.service.charge(job.context["role"], finished - started)
                if result.get("state") == "YIELDED":
                    if result.get("ticket_id") != job.ticket["ticket_id"]:
                        raise ValueError("Scheduler checkpoint belongs to another ticket")
                    # Retain the same job and Future. Its next presentation
                    # needs a fresh monotonic sequence after another job's view;
                    # otherwise consumers would discard the resumed frames.
                    job.view_sequence = None
                    with self.condition:
                        (self.search if job.context["role"] == "search" else self.audits).appendleft(job)
                    self.last_completed = None
                    continue
                if result.get("state") == "PAUSED":
                    # OFF/ON controls own the desired state. This older job's
                    # pause acknowledgement must not erase a newer ON.
                    with self.condition:
                        self.resume_job = job
                    self.on_paused()
                    announced_pause = True
                    self.last_completed = None
                    continue
                # Persist the completed handoff before publishing another active
                # job. This callback must never perform network requests.
                self.on_result(job, result)
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
                with self.condition:
                    self.active = None
                    self.condition.notify_all()
        with self.condition:
            for job in self.jobs.values():
                if not job.future.done():
                    job.future.set_exception(CancelledError("GPU work retained for restart"))
