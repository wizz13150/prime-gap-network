"""One GPU execution lane; prepared search and audit jobs share a bounded scheduler."""
from collections import deque
from concurrent.futures import Future, CancelledError
from dataclasses import dataclass, field
import threading
import copy
import time
import errno
import sqlite3
from pgn_index import SessionIndexWalBudgetExceeded

def retryable_storage_error(error):
    if isinstance(error, OSError):
        return error.errno in (errno.ENOSPC, errno.EDQUOT, errno.EBUSY)
    if isinstance(error, sqlite3.Error):
        code = getattr(error, 'sqlite_errorcode', None)
        # SQLite's primary codes are stable. Python exposes the constants and
        # exception code only since 3.11; the Linux host also supports 3.10.
        if type(code) is int:
            return code & 255 in (5, 6, 13)  # BUSY, LOCKED, FULL
        if isinstance(error, sqlite3.OperationalError):
            message = str(error)
            return (message in ('database is locked', 'database table is locked', 'database or disk is full')
                    or message.startswith(('database table is locked: ', 'database schema is locked: ')))
    return False

@dataclass
class GpuJob:
    ticket: dict
    context: dict
    maximum_piece: int
    future: Future = field(default_factory=Future)
    view_sequence: int | None = None
    storage_retry_after: float = 0.0
    storage_retries: int = 0
    completed_result: dict | None = None

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
        self.last_completed = None
        self.timings = deque(maxlen=32)
        self.pause_generation = 0

    def request_pause_ack(self):
        # Every explicit OFF is acknowledged by the execution thread, including
        # an idempotent OFF after this lane was already physically released.
        with self.condition:
            self.pause_generation += 1
            self.condition.notify_all()

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

    def audit_service_view(self):
        with self.condition:
            return dict(mode="two_regular_one_catchup",regular_audits=2,catch_up_audits=1,
                        search_ready=len(self.search),audit_ready=len(self.audits),
                        active_role=self.active.context["role"] if self.active else None,
                        search_waiting_for_audits=self.search_waiting_for_audits)

    def admitted(self, job):
        if time.monotonic() < job.storage_retry_after:
            return False
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
            return job.context["role"] == "search" and other_ready

    def _next(self):
        if self.resume_job is not None and self.admitted(self.resume_job):
            job, self.resume_job = self.resume_job, None
            return job
        # Retain a paused task whose admission is temporarily closed, but let
        # independently eligible work run. In particular an audit can be the
        # prerequisite that makes this paused search admissible again.
        # Search remains FIFO. Audit preparation may finish out of order, so
        # find the first permitted audit within the existing ten-job bound.
        search = self.search[0] if self.search and self.admitted(self.search[0]) else None
        audit = next((job for job in self.audits if self.admitted(job)), None)
        self.search_waiting_for_audits = bool(self.search) and search is None
        role = "audit" if audit is not None else "search" if search is not None else None
        if role == "audit":
            self.audits.remove(audit)
            return audit
        if role == "search":
            return self.search.popleft()
        return None

    def run(self):
        announced_pause = False
        announced_generation = -1
        waiting_since = None
        while not self.stopping.is_set():
            if not self.enabled.wait(.1):
                if not announced_pause or announced_generation != self.pause_generation:
                    generation = self.pause_generation
                    self.on_paused()
                    announced_pause = True
                    announced_generation = generation
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
                if retryable_storage_error(error):
                    self.on_error(error)
                    self.stopping.wait(.1)
                    continue
                self.enabled.clear()
                self.on_error(error)
                self.on_paused()
                announced_pause = True
                self.last_completed = None
                continue
            waiting_since = None
            self.active = job
            try:
                if job.completed_result is not None:
                    # Computation already finished. Retry only the durable
                    # handoff: reactivation could regress a completed cycle to
                    # running, repeat arithmetic, or alter presentation binding.
                    self.on_result(job, copy.deepcopy(job.completed_result))
                    self.last_completed = (job.ticket['ticket_id'], self.clock_ns())
                    job.future.set_result(copy.deepcopy(job.completed_result))
                    with self.condition:
                        self.jobs.pop(job.ticket['ticket_id'], None)
                    continue
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
                        if self.resume_job is None:
                            self.resume_job = job
                        else:
                            # A different paused task may already be deferred.
                            # Never overwrite its identity or unresolved Future.
                            (self.search if job.context["role"] == "search" else self.audits).appendleft(job)
                    self.on_paused()
                    announced_pause = True
                    self.last_completed = None
                    continue
                # Persist the completed handoff before publishing another active
                # job. This callback must never perform network requests.
                job.completed_result = copy.deepcopy(result)
                self.on_result(job, copy.deepcopy(job.completed_result))
                self.last_completed = (job.ticket["ticket_id"],finished)
                job.future.set_result(copy.deepcopy(job.completed_result))
                with self.condition:
                    self.jobs.pop(job.ticket["ticket_id"], None)
            except SessionIndexWalBudgetExceeded as error:
                # Keep the same Future, identity and retained output. Only this
                # database waits; independently ready jobs may still run. An
                # explicit OFF remains OFF and shutdown still owns cancellation.
                job.storage_retries += 1
                job.storage_retry_after = time.monotonic() + min(2.0, .25 * 2 ** min(job.storage_retries - 1, 3))
                job.view_sequence = None
                with self.condition:
                    (self.search if job.context["role"] == "search" else self.audits).appendleft(job)
                self.on_error(error)
                self.last_completed = None
            except Exception as error:
                if retryable_storage_error(error):
                    job.storage_retries += 1
                    job.storage_retry_after = time.monotonic() + min(2.0, .25 * 2 ** min(job.storage_retries - 1, 3))
                    job.view_sequence = None
                    with self.condition:
                        (self.search if job.context['role'] == 'search' else self.audits).appendleft(job)
                    self.on_error(error)
                    self.last_completed = None
                    continue
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
