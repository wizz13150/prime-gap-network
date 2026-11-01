"""Resident management lanes keep prepared useful work ahead of the single GPU lane."""
from concurrent.futures import TimeoutError, CancelledError
from pathlib import Path
from types import SimpleNamespace
import json
import threading
import time
from horizon_network_audits import NetworkAudits
from horizon_gpu_queue import ResidentGpuQueue
from horizon_report_outbox import ReportOutbox
from horizon_ticket_prefetch import TicketPrefetch
from pgn_native_contract import fields
from pgn_session import atomic_bytes
from report_commitment import canonical_bytes

def read(file, capacity=32768):
    if file.stat().st_size > capacity:
        raise ValueError("Pipeline journal exceeds capacity")
    return json.loads(file.read_bytes())

def save(file, value):
    raw = canonical_bytes(value) + b"\n"
    if len(raw) > 32768:
        raise ValueError("Pipeline journal exceeds capacity")
    atomic_bytes(file, raw)

class AuditGpuProxy:
    def __init__(self, flow):
        self.flow = flow
        self.audit = None

    def run_ticket(self, ticket, maximum_piece):
        presentation = dict(self.audit.presentation)
        presentation["folder"] = self.flow.worker.root / "tickets" / ticket["ticket_id"]
        # Keep queue metadata serializable; restore the local path when active.
        presentation["folder"] = str(presentation["folder"])
        future = self.flow.gpu.submit(ticket, dict(role="audit", presentation=presentation), maximum_piece)
        while not self.flow.worker.quitting.is_set():
            try:
                return future.result(.2)
            except TimeoutError:
                continue
        raise CancelledError("Audit work retained for restart")

class MiningFlow:
    def __init__(self, worker):
        self.closed = False
        self.worker = worker
        self.outbox = ReportOutbox(worker.root)
        self.changed = threading.Event()
        self.prior_active = read(worker.root / "gpu-active.json") if (worker.root / "gpu-active.json").exists() else {}
        current = read(worker.root / "controller.json") if (worker.root / "controller.json").exists() else {}
        self.view_sequence = max(int(current.get("sequence", 0)), int(self.prior_active.get("sequence", 0)))
        self.gpu = ResidentGpuQueue(worker.source, worker.running, worker.quitting,
                                    self.activate, self.completed, self.paused, self.failed, self.waiting)
        allocator = object.__new__(type(worker))
        allocator.args, allocator.rpc = worker.args, worker.rpc
        allocator.root = worker.root / "prepared-ticket"
        allocator.root.mkdir(exist_ok=True)
        allocator.sequence = worker.sequence
        allocator.audits = worker.audits
        allocator.state_lock = threading.RLock()
        allocator.network_lock = worker.network_lock
        self.allocator = allocator
        self.prefetch = TicketPrefetch(allocator, worker.running, worker.quitting, worker.network_lock, self.prepared)
        self.audit_lanes = []
        for index in range(2):
            folder = worker.root / "audit-lanes" / str(index)
            folder.mkdir(parents=True, exist_ok=True)
            proxy = AuditGpuProxy(self)
            lane = SimpleNamespace(root=folder, args=worker.args, rpc=worker.rpc,
                                   running=worker.running, network_lock=worker.network_lock,
                                   source=proxy, address=worker.address, ledger=worker.ledger)
            audit = NetworkAudits(lane)
            proxy.audit = audit
            self.audit_lanes.append(audit)
        self.report_thread = threading.Thread(target=self.publish_reports, name="report-publication", daemon=True)
        self.audit_threads = [threading.Thread(target=self.audit_work, args=(audit,),
                              name="audit-preparation-" + str(i), daemon=True)
                              for i, audit in enumerate(self.audit_lanes)]

    def job(self, work):
        start, end = int(work["start"]), int(work["start"]) + int(work["length"])
        numerator, denominator = int(work["target_numerator"]), int(work["target_denominator"])
        if numerator * 65536 % denominator:
            raise ValueError("Unsupported work target precision")
        return dict(schema="PGN_HORIZON_ENGINE_TICKET_V1", ticket_id=work["ticket"],
                    start=str(start), end=str(end), threshold_policy=fields(start, end)["threshold_policy"],
                    target_merit_q16=numerator * 65536 // denominator)

    def prepared(self, work):
        self.gpu.submit(self.job(work), dict(role="search", work=work, prepared=True), self.worker.args.piece_limit)

    def promote(self, work, sequence):
        with self.worker.state_lock:
            save(self.worker.root / "controller.json", dict(stage="ASSIGNED", sequence=sequence, work=work))

    def activate(self, job):
        worker = self.worker
        if worker.device_lease is None:
            worker.acquire_device()
        if job.view_sequence is None:
            if self.prior_active.get("ticket_id") == job.ticket["ticket_id"]:
                job.view_sequence = int(self.prior_active["sequence"])
                self.prior_active = {}
            else:
                self.view_sequence += 1
                job.view_sequence = self.view_sequence
        worker.sequence = job.view_sequence
        if job.context["role"] == "search":
            work = job.context["work"]
            if job.context.get("prepared"):
                taken = self.prefetch.take(lambda value: self.promote(value, job.view_sequence), 0)
                if taken is None or taken["ticket"] != work["ticket"]:
                    raise ValueError("Prepared GPU ticket was not durably adopted")
                job.context["prepared"] = False
            else:
                self.promote(work, job.view_sequence)
            worker.current = work
            worker.audits.presentation = None
        else:
            worker.audits.presentation = dict(job.context["presentation"])
        save(worker.root / "gpu-active.json", dict(ticket_id=job.ticket["ticket_id"],
             sequence=job.view_sequence, role=job.context["role"], context=job.context))
        (worker.root / "pause.request").unlink(missing_ok=True)

    def completed(self, job, result):
        if job.context["role"] == "search":
            self.outbox.put(job.context["work"], result)
        self.changed.set()
        save(self.worker.root / "pipeline-timing.json", dict(schema="PGN_PIPELINE_TIMING_V1",
             measurement="Host job handoff; CUDA kernel trace still required", transitions=list(self.gpu.timings)))

    def paused(self):
        self.worker.release_device()
        self.worker.progress(dict(phase="PAUSED_ACK"))

    def waiting(self):
        detail = self.prefetch.error or "Preparing the next signed network ticket"
        self.worker.message(detail, "Waiting for next ticket")

    def failed(self, error):
        self.worker.message(str(error), "Needs attention")

    def publish_reports(self):
        next_poll = 0
        while not self.worker.quitting.is_set():
            try:
                entry = self.outbox.first()
                if entry:
                    with self.worker.network_lock:
                        self.worker.submit(None, work=entry["work"])
                    self.outbox.acknowledge(entry["work"]["request_id"], entry["report_root"])
                    continue
                now = time.monotonic()
                if now >= next_poll:
                    with self.worker.network_lock:
                        self.worker.audits.reveal_owner_reports()
                        self.worker.audits.reveal_committed()
                        for audit in self.audit_lanes:
                            audit.reveal_committed()
                    next_poll = now + 5
                self.worker.network_notice = None
            except Exception as error:
                self.worker.network_notice = str(error)[:400]
                self.worker.quitting.wait(1)
            self.changed.wait(.1)
            self.changed.clear()

    def audit_work(self, audit):
        while not self.worker.quitting.is_set():
            if not self.worker.running.wait(.2):
                continue
            try:
                outcome = audit.process_one()
                if outcome == "none":
                    self.worker.quitting.wait(5)
            except Exception as error:
                if self.worker.quitting.is_set():
                    break
                self.worker.network_notice = str(error)[:400]
                self.worker.quitting.wait(2)

    def run(self):
        worker = self.worker
        with worker.network_lock:
            work = worker.assign(recover_only=True)
            resumed = work is not None
            if work is not None:
                session = worker.root / "tickets" / work["ticket"] / "session.json"
                if session.exists() and read(session, 262144).get("state") == "COMPLETE":
                    worker.current = work
                    result = worker.source.run_ticket(self.job(work), worker.args.piece_limit)
                    self.outbox.put(work, result)
                    work = None
                    resumed = False
            if work is None:
                work = self.allocator.assign()
        self.promote(work, self.view_sequence)
        journal = self.allocator.root / "controller.json"
        if journal.exists():
            value = read(journal)
            if value.get("work", {}).get("ticket") == work["ticket"]:
                value["stage"] = "CONSUMED"
                save(journal, value)
        self.gpu.submit(self.job(work), dict(role="search", work=work, prepared=False), worker.args.piece_limit)
        if resumed and self.view_sequence > 0:
            self.gpu.jobs[work["ticket"]].view_sequence = self.view_sequence
        self.prefetch.start()
        self.report_thread.start()
        for thread in self.audit_threads:
            thread.start()
        try:
            if self.prior_active.get("role") == "audit":
                until = time.monotonic() + 5
                identity = self.prior_active["ticket_id"]
                while identity not in self.gpu.jobs and not worker.quitting.is_set() and time.monotonic() < until:
                    worker.quitting.wait(.01)
                with self.gpu.condition:
                    saved = self.gpu.jobs.get(identity)
                    if saved is not None and saved in self.gpu.audits:
                        self.gpu.audits.remove(saved)
                        self.gpu.resume_job = saved
            self.gpu.run()
        finally:
            worker.quitting.set()
            worker.running.set()
            worker.rpc.cancel_all()
            self.gpu.wake()
            self.changed.set()
            self.prefetch.close(5)
            self.report_thread.join(5)
            for thread in self.audit_threads:
                thread.join(5)
            worker.release_device()
            worker.source.close()
            self.closed = True
