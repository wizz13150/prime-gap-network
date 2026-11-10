"""Resident management lanes keep prepared useful work ahead of the single GPU lane."""
from concurrent.futures import TimeoutError, CancelledError
from pathlib import Path
from copy import deepcopy
from types import SimpleNamespace
import json
import threading
import time
from horizon_network_audits import NetworkAudits
from horizon_gpu_queue import ResidentGpuQueue
from horizon_report_outbox import ReportOutbox
from horizon_preseal_queue import PresealQueue
from horizon_ticket_prefetch import TicketPrefetch
from horizon_work_cycle import WorkCycleJournal
from horizon_core_rpc import CoreRpcError
from horizon_ticket_archive import SCHEMA as COMPLETED_TICKET_SCHEMA
from horizon_audit_progress import presentation as audit_presentation
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
    def __init__(self, flow, position):
        self.flow = flow
        self.position = position
        self.audit = None
        self.prepared = {}

    def finalize_report(self, work, computed):
        # NetworkAudits calls this after its GPU Future returns, on the audit
        # preparation thread. Never do expensive residual work in on_result.
        finalizer = getattr(self.flow.worker.source, 'finalize_report', None)
        return finalizer(work, computed) if callable(finalizer) else computed

    def register_preparation(self, tickets):
        register = getattr(self.flow.worker.source, 'register_preparation', None)
        if callable(register):
            register(tickets)
        # All partitions of this confirmed assignment are independent GPU
        # inputs. Expose their readiness before waiting for the first result;
        # otherwise a long search can win the producer's tiny handoff gap.
        # Two existing lanes x five partitions fit the existing ten-job bound.
        for ticket in tickets:
            self.queue_ticket(ticket, self.flow.worker.args.piece_limit)

    def acknowledge_preparation(self, ticket):
        acknowledge = getattr(self.flow.worker.source, 'acknowledge_preparation', None)
        if callable(acknowledge):
            acknowledge(ticket)
        self.prepared.pop(ticket, None)

    def retain_audit_partition(self,audit,part,computed=None):
        retain=getattr(self.flow.worker.source,'retain_audit_partition',None)
        if callable(retain):retain(audit,part,computed)

    def queue_ticket(self, ticket, maximum_piece):
        if ticket['ticket_id'] in self.prepared:
            previous, limit, future = self.prepared[ticket['ticket_id']]
            if previous != ticket or limit != maximum_piece:
                raise ValueError('Prepared audit partition identity changed')
            return future
        audit = self.audit.current_audit
        self.flow.bind_audit_cycle(self.position, audit)
        part = next((p for p in audit['ranges'] if p['start'] == ticket['start'] and p['end'] == ticket['end']), None)
        if part is None:
            raise ValueError('Queued audit partition differs from its confirmed assignment')
        presentation = audit_presentation(audit, part,
                            self.flow.worker.root / 'tickets' / ticket['ticket_id'])
        if audit.get("work_cycle"):
            cycle = self.flow.cycles.snapshot(audit["work_cycle"])
            presentation["title"] = "Ticket " + cycle["work"]["ticket_number"] + " · Audit " + str(self.position + 1) + "/2"
        future = self.flow.gpu.submit(ticket, dict(role="audit", presentation=presentation,
                                     work_cycle=audit.get("work_cycle"), audit_position=self.position,
                                     audit_request_id=audit["request_id"], audit_slot=audit["slot"],
                                     audit_attempt=audit["attempt"]), maximum_piece)
        self.prepared[ticket['ticket_id']] = (deepcopy(ticket), maximum_piece, future)
        return future

    def run_ticket(self, ticket, maximum_piece):
        future = self.queue_ticket(ticket, maximum_piece)
        while not self.flow.worker.quitting.is_set():
            try:
                return future.result(.2)
            except TimeoutError:
                continue
        raise CancelledError("Audit work retained for restart")

class MiningFlow:
    def __init__(self, worker, *, finalize=None):
        self.closed = False
        self.worker = worker
        self.outbox = ReportOutbox(worker.root, discoverer=getattr(worker, 'public_name', None))
        self.recovered_report = None
        self.finalizer = finalize if finalize is not None else getattr(worker.source, 'finalize_report', None)
        if self.finalizer is not None and not callable(self.finalizer):
            raise ValueError('A callable scientific report finalizer is required')
        if self.finalizer is not None:
            retain = getattr(worker.source, 'require_report_finalization', None)
            if not callable(retain):
                raise ValueError('Scientific finalization requires durable source-index retention')
            retain()
        self.preseal = PresealQueue(worker.root)
        self.cycles = WorkCycleJournal(worker.root / "work-cycles", worker.args.genesis)
        self.changed = threading.Event()
        self.prior_active = read(worker.root / "gpu-active.json") if (worker.root / "gpu-active.json").exists() else {}
        current = read(worker.root / "controller.json") if (worker.root / "controller.json").exists() else {}
        self.view_sequence = max(int(current.get("sequence", 0)), int(self.prior_active.get("sequence", 0)))
        self.gpu = ResidentGpuQueue(worker.source, worker.running, worker.quitting,
                                    self.activate, self.completed, self.paused, self.failed, self.waiting,
                                    may_start=self.may_start_work)
        worker.request_pause_ack = self.gpu.request_pause_ack
        allocator = object.__new__(type(worker))
        allocator.args, allocator.rpc = worker.args, worker.rpc
        allocator.root = worker.root / "prepared-ticket"
        allocator.root.mkdir(exist_ok=True)
        allocator.sequence = worker.sequence
        allocator.audits = worker.audits
        allocator.state_lock = threading.RLock()
        allocator.network_lock = worker.network_lock
        self.allocator = allocator
        from horizon_reassignment_queue import ReassignmentQueue
        self.reassignments = ReassignmentQueue(self)
        self.prefetch = TicketPrefetch(allocator, worker.running, worker.quitting, worker.network_lock, self.prepared,
            recovery=self.reassignments.next_work,on_recovered=self.reassignments.adopted)
        self.audit_lanes = []
        self.legacy_audit_publishers = []
        from horizon_assigned_audit_recovery import AssignedAuditRecovery
        from horizon_lab_audit_actors import load_lab_audit_actors
        lab_actors = load_lab_audit_actors(getattr(worker.args, 'controlled_lab_audit_actors', None),
                                         worker.rpc, worker.args.genesis,
                                         pilot_coordination=getattr(worker.args, 'pilot_coordination', False))
        from horizon_audit_actor_transition import native_service_actors
        native_actors = native_service_actors(worker, lab_actors)
        self.assigned_audit_recovery = AssignedAuditRecovery(worker,
            'service-audit-lanes' if native_actors else 'audit-lanes')
        if native_actors:
            # Existing lab answers keep their original signer and durable
            # catalogue. They may reveal already-computed answers only; all
            # new computation/credits use the real miner identity below.
            for index, actor in enumerate(lab_actors):
                folder = worker.root / 'audit-lanes' / str(index)
                folder.mkdir(parents=True, exist_ok=True)
                legacy = SimpleNamespace(root=folder, args=worker.args, rpc=worker.rpc,
                    running=worker.running, quitting=worker.quitting,
                    network_lock=worker.network_lock, address=lambda actor=actor: actor,
                    ledger=worker.ledger)
                self.legacy_audit_publishers.append(NetworkAudits(legacy))
        for index in range(2):
            folder = worker.root / ('service-audit-lanes' if native_actors else 'audit-lanes') / str(index)
            folder.mkdir(parents=True, exist_ok=True)
            proxy = AuditGpuProxy(self, index)
            lane = SimpleNamespace(root=folder, args=worker.args, rpc=worker.rpc,
                                   running=worker.running, quitting=worker.quitting, network_lock=worker.network_lock,
                                   source=proxy, address=worker.address, ledger=worker.ledger)
            if lab_actors is not None and not native_actors:
                lane.address = lambda actor=lab_actors[index]: actor
            else:
                lane.recover_assigned_audit = self.assigned_audit_recovery.claim
            audit = NetworkAudits(lane,
                on_assigned=lambda value, index=index: self.bind_audit_cycle(index, value),
                on_reassigned=lambda value, block, index=index: self.reopen_audit_cycle(index, value, block),
                on_retired=lambda value, anchor, index=index: self.retire_audit_cycle(index, value, anchor),
                reassignment_needed=lambda value, index=index: self.audit_reassignment_needed(index, value),
                on_committed=lambda value, block, index=index: self.audit_committed(index, value, block),
                before_commit=lambda value, index=index: self.before_audit_commit(index, value))
            proxy.audit = audit
            self.audit_lanes.append(audit)
        self.audit_pressure_notice = None
        self.cycle_audit_positions = {}
        self.controlled_lab_audit_actors = lab_actors is not None and not native_actors
        worker.audit_service_snapshot = self.audit_service_snapshot
        self.pressure_thread = threading.Thread(target=self.refresh_audit_pressure, name="audit-service-policy", daemon=True)
        self.report_thread = threading.Thread(target=self.publish_reports, name="report-publication", daemon=True)
        from horizon_report_poll import ReportPoller
        self.report_poller = ReportPoller(worker.audits, [*self.audit_lanes, *self.legacy_audit_publishers])
        self.owner_report_thread = threading.Thread(target=self.report_poller.run_owner,
            args=(worker.quitting,), name="owner-report-maintenance", daemon=True)
        self.finalize_thread = (threading.Thread(target=self.finalize_reports, name="report-finalization", daemon=True)
                                if self.finalizer is not None else None)
        self.audit_threads = [threading.Thread(target=self.audit_work, args=(audit, i),
                              name="audit-preparation-" + str(i), daemon=True)
                              for i, audit in enumerate(self.audit_lanes)]

    def may_start_work(self, job):
        if job.context['role'] == 'search' and getattr(self, 'recovered_report', None) is not None:
            return False
        if job.context['role'] == 'search' and self.finalizer is None and not self.outbox.capacity_available():
            return False
        if (job.context['role'] == 'search' and self.finalizer is not None
                and not self.preseal.capacity_available()):
            return False
        cycle = job.context.get("work_cycle")
        if job.context["role"] == "audit":
            if cycle is not None:
                return self.cycles.may_audit(cycle, job.context["audit_position"])
            owner = self.extra_audit_owner(job)
            return owner is None or self.cycles.may_extra_audit(owner, job.context['presentation']['audit_id'])
        if job.context.get("legacy_assignment"):
            return True
        state = self.cycles.snapshot(cycle)
        return not state["hold"] and (self.cycles.may_search(cycle) or
                                      state["stage"] == "running" or
                                      state["stage"] == "completed" and not job.context.get("prepared"))

    def extra_audit_owner(self, job):
        audit_id = job.context.get('presentation', {}).get('audit_id')
        if not audit_id:
            return None
        retained = self.cycles.extra_audit_cycle(audit_id)
        if retained:
            return retained
        active = getattr(self.gpu, 'active', None)
        if active and active.context.get('role') == 'search' and active.context.get('work_cycle'):
            return active.context['work_cycle']
        # The yielded search stays at the head. Prepared work for the next
        # ticket must not replace the owner of the current cycle's allowance.
        pending = next((item.context.get('work_cycle') for item in self.gpu.search
                        if item.context.get('work_cycle')), None)
        if pending:
            return pending
        current = getattr(self.worker, 'current', None)
        request = current.get('request_id') if current else None
        if request and self.cycles.contains(request) and self.cycles.snapshot(request)['stage'] != 'completed':
            return request
        return None

    def bind_audit_cycle(self, position, audit):
        cycle = audit.get("work_cycle") or self.cycles.bound_audit(audit["request_id"], audit["slot"], audit["attempt"], position)
        if cycle:
            state = self.cycles.snapshot(cycle)
            task = state["audits"][position]
            if not task or (task.get("request"), task.get("slot"), task.get("attempt")) != (audit["request_id"], audit["slot"], audit["attempt"]):
                self.cycles.hold(cycle, "Audit assignment changed; retained work needs reconciliation")
                raise RuntimeError("Audit assignment changed; private computation retained")
            if task["stage"] == "committed" and not self.replaying_audit(state, position, audit):
                self.cycles.hold(cycle, "Audit confirmation changed branch; retained work needs reconciliation")
                raise RuntimeError("Previously committed audit is assigned again; private answer retained")
        else:
            cycle = self.cycles.pending(position, retry_permissions=True)
            if cycle is None:
                return
            # on_assigned follows the Core-confirmed assignment. A later
            # permission grant can clear only a recognized permission hold.
            self.cycles.attach(cycle, position, dict(stage="assigned", request=audit["request_id"], slot=audit["slot"], attempt=audit["attempt"]), permission_verified=True)
        audit["work_cycle"] = cycle
        if not hasattr(self, 'cycle_audit_positions'):self.cycle_audit_positions = {}
        self.cycle_audit_positions[(cycle, position)] = sum(int(r['end'])-int(r['start']) for r in audit['ranges'])
        while len(self.cycle_audit_positions)>8:self.cycle_audit_positions.pop(next(iter(self.cycle_audit_positions)))
        save(self.audit_lanes[position].folder / "current.json", audit)

    def before_audit_commit(self, position, audit):
        self.bind_audit_cycle(position, audit)
        cycle = audit.get("work_cycle")
        if cycle is None:
            return True
        while not self.worker.quitting.is_set() and self.worker.running.is_set():
            state = self.cycles.snapshot(cycle)
            replay = self.replaying_audit(state, position, audit)
            if state["hold"] and not replay:
                raise RuntimeError(state["hold"])
            if position == 0:
                return True
            first = state['audits'][0]
            if first and first['stage'] in ('committed', 'unavailable'):
                if not replay or self.audit_reference_current(first):
                    return True
            with self.gpu.condition:
                self.gpu.condition.wait(.5 if replay else .1)
        return False

    def audit_committed(self, position, audit, block):
        cycle = audit.get("work_cycle") or self.cycles.bound_audit(audit["request_id"], audit["slot"], audit["attempt"], position)
        if cycle:
            if audit.get('replay_previous_block'):
                state = self.cycles.snapshot(cycle)
                # Repeated callback after both local publication and hold
                # release is an idempotent acknowledgement, not a new replay.
                task = state['audits'][position]
                if not task or (task.get('request'), task.get('slot'), task.get('attempt')) != (audit['request_id'], audit['slot'], audit['attempt']):
                    raise ValueError('Replay confirmation belongs to another retained attempt')
                if state['hold'] or task.get('block') != block:
                    self.cycles.replace_audit_confirmation(cycle, position, audit['request_id'], audit['slot'], audit['attempt'], audit['replay_previous_block'], block)
                    self.refresh_cycle_confirmations(cycle)
            else:
                self.cycles.published(cycle, position, audit["request_id"], audit["slot"], audit["attempt"], block)
            self.gpu.wake()

    def reopen_audit_cycle(self, position, audit, previous_block):
        cycle = audit.get('work_cycle') or self.cycles.bound_audit(audit['request_id'], audit['slot'], audit['attempt'], position)
        if cycle:
            started = self.cycles.reopen_audit(cycle, position, audit['request_id'], audit['slot'], audit['attempt'], previous_block)
            if started:
                audit['replay_previous_block'] = previous_block
            else:
                audit.pop('replay_previous_block', None)
            audit['work_cycle'] = cycle
            self.gpu.wake()

    def retire_audit_cycle(self, position, audit, anchor):
        cycle = audit.get('work_cycle') or self.cycles.bound_audit(audit['request_id'], audit['slot'], audit['attempt'], position)
        if cycle:
            self.cycles.retire_assignment(cycle, position, audit['request_id'], audit['slot'], audit['attempt'])
        self.worker.network_notice = 'Changed audit assignment retained; requesting current independent work'
        self.gpu.wake()

    @staticmethod
    def replaying_audit(state, position, audit):
        task = state['audits'][position]
        return (state['stage'] in ('running', 'completed') and state['hold'] == WorkCycleJournal.reorg_hold
                and task is not None and task.get('stage') == 'committed'
                and task.get('block') == audit.get('replay_previous_block')
                and (task.get('request'), task.get('slot'), task.get('attempt')) == (audit['request_id'], audit['slot'], audit['attempt']))

    def audit_reference_current(self, task):
        if not isinstance(task, dict):
            return False
        block = task.get('block') if task.get('stage') == 'committed' else task.get('anchor') if task.get('stage') == 'unavailable' else None
        if not block:
            return False
        try:
            header = self.worker.rpc.call('getblockheader', [block, True])
        except CoreRpcError as error:
            if error.code == -5:
                return False
            raise
        if (not isinstance(header, dict) or header.get('hash') != block or type(header.get('height')) is not int
                or header['height'] < 0 or type(header.get('confirmations')) is not int):
            raise ValueError('Audit service confirmation header is invalid')
        return header['confirmations'] > 0 and self.worker.rpc.call('getblockhash', [header['height']]) == block

    def refresh_cycle_confirmations(self, cycle):
        state = self.cycles.snapshot(cycle)
        if state['hold'] != WorkCycleJournal.reorg_hold:
            return False
        if 'native_service' in state:
            # The consuming request, not a replay in a local audit lane, owns
            # these credits. Re-observe Core before replacing its orphaned
            # issuance anchor, without reopening completed scientific work.
            changed = self.prepare_cycle(dict(state['work'], genesis=self.worker.args.genesis))
            return changed or self.cycles.release_reorg_hold(cycle, state['audits'])
        before = self.worker.rpc.call('getbestblockhash')
        if not all(self.audit_reference_current(task) for task in state['audits']):
            return False
        if self.worker.rpc.call('getbestblockhash') != before:
            return False
        return self.cycles.release_reorg_hold(cycle, state['audits'])

    def report_ready(self, entry):
        request = entry['work']['request_id']
        if not self.cycles.contains(request):
            return True # Preserved legacy reports are not retagged as new cycles.
        cycle = self.cycles.snapshot(request)
        return cycle['stage'] == 'completed' and self.cycles.ready(cycle)

    def audit_reassignment_needed(self, position, audit):
        cycle = audit.get('work_cycle') or self.cycles.bound_audit(audit['request_id'], audit['slot'], audit['attempt'], position)
        if not cycle:
            return False
        task = self.cycles.snapshot(cycle)['audits'][position]
        return task is not None and task['stage'] == 'committed'

    def job(self, work):
        start, end = int(work["start"]), int(work["start"]) + int(work["length"])
        numerator, denominator = int(work["target_numerator"]), int(work["target_denominator"])
        if numerator * 65536 % denominator:
            raise ValueError("Unsupported work target precision")
        return dict(schema="PGN_HORIZON_ENGINE_TICKET_V1", ticket_id=work["ticket"],
                    start=str(start), end=str(end), threshold_policy=fields(start, end)["threshold_policy"],
                    target_merit_q16=numerator * 65536 // denominator)

    def prepared(self, work):
        self.prepare_cycle(work)
        self.gpu.submit(self.job(work), dict(role="search", work=work, prepared=True, work_cycle=work["request_id"]), self.worker.args.piece_limit)

    def prepare_cycle(self, work):
        previous = (self.cycles.snapshot(work['request_id']).get('native_service')
                    if self.cycles.contains(work['request_id']) else None)
        native = None
        if 'native_audit_service' in work or previous is not None:
            from horizon_native_work_duty import observe
            with self.worker.network_lock:
                native = observe(self.worker.rpc, work, self.worker.args.genesis, previous)
        return self.cycles.prepare(work, native_service=native)

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
            if not job.context.get("legacy_assignment"):
                self.cycles.advance(work["request_id"], "running")
            worker.audits.presentation = None
        else:
            if not job.context.get('work_cycle'):
                owner = self.extra_audit_owner(job)
                if owner:
                    self.cycles.reserve_extra_audit(owner, job.context['presentation']['audit_id'])
                    job.context['audit_budget_cycle'] = owner
            worker.audits.presentation = dict(job.context["presentation"])
        bind = getattr(worker.source, "bind_cycle", None)
        if bind is not None:
            cycle = job.context.get("work_cycle")
            if cycle:
                bind(job.ticket, job.context, self.cycles.snapshot(cycle))
            else:
                worker.source.clear_cycle()
        save(worker.root / "gpu-active.json", dict(ticket_id=job.ticket["ticket_id"],
             sequence=job.view_sequence, role=job.context["role"], context=job.context))
        (worker.root / "pause.request").unlink(missing_ok=True)

    def completed(self, job, result):
        if job.context["role"] == "search":
            self.queue_report(job.context["work"], result)
            if not job.context.get("legacy_assignment"):
                self.cycles.advance(job.context["work"]["request_id"], "completed")
        self.changed.set()
        save(self.worker.root / "pipeline-timing.json", dict(schema="PGN_PIPELINE_TIMING_V1",
             measurement="Host job handoff; CUDA kernel trace still required", transitions=list(self.gpu.timings)))

    def queue_report(self, work, result):
        # Heavy scientific repair never runs on the GPU completion callback.
        # The caller must supply a qualified finalizer; the queue itself does
        # not confer scientific coverage or reinterpret legacy commitments.
        if self.finalizer is None:
            return self.outbox.put(work, result)
        return self.preseal.put(work, result)

    def drain_recovered_report(self):
        """Resume a completed handoff without preventing the publisher starting.

        A full bounded outbox is backpressure, not a fatal startup failure.
        The completed session and sealed report remain immutable on disk.
        Audit jobs remain eligible while publication frees a handoff slot.
        """
        pending = self.recovered_report
        if pending is None:
            return False
        queue = self.preseal if self.finalizer is not None else self.outbox
        if not queue.capacity_available():
            return False
        work, result = pending
        try:
            self.queue_report(work, result)
        except BufferError:
            return False
        if self.cycles.contains(work['request_id']):
            self.cycles.advance(work['request_id'], 'completed')
        self.recovered_report = None
        return True

    def finalize_reports(self):
        while not self.worker.quitting.is_set():
            try:
                acknowledge = getattr(getattr(self.worker, 'source', None), 'acknowledge_preparation', None)
                changed = self.preseal.pump(self.finalizer, self.outbox,
                                           cancelled=self.worker.quitting.is_set,
                                           on_sealed=(lambda work: acknowledge(work['ticket'])) if callable(acknowledge) else None)
                if changed:
                    self.changed.set()
                    self.gpu.wake()
                    continue
            except Exception as error:
                if self.worker.quitting.is_set():
                    break
                self.worker.network_notice = str(error)[:400]
                self.worker.quitting.wait(1)
            self.worker.quitting.wait(.1)

    def paused(self):
        self.worker.release_device()
        if self.worker.running.is_set():
            return  # A newer ON superseded this older physical pause.
        acknowledged = getattr(self.worker, 'pause_acknowledged', None)
        if acknowledged is not None:
            acknowledged.set()
        if self.worker.current is None and self.worker.audits.presentation is None:
            self.worker.message('Mining paused; no GPU task is running', 'Paused')
            return
        self.worker.progress(dict(phase="PAUSED_ACK"))

    def waiting(self):
        # A raw GPU partition can be complete while its independent source
        # finalization still runs. A refused ticket prefetch must not hide that
        # audit activity or suggest the miner has no work.
        pending = []
        lanes = getattr(self, 'audit_lanes', [])
        for position, lane in enumerate(lanes):
            audit = getattr(lane, 'current_audit', None)
            if not isinstance(audit, dict):
                continue
            ranges, counts = audit.get('ranges'), audit.get('counts')
            if not isinstance(ranges, list) or not isinstance(counts, list) or not ranges:
                continue
            done, total = len(counts), len(ranges)
            if not 0 <= done <= total:
                continue
            if getattr(lane, 'last_error', None):
                self.worker.message('Audit ' + str(position+1) + ': ' + lane.last_error, 'Needs attention')
                return
            stage = ('verifying computed results' if done < total else 'result publication pending')
            pending.append('Audit ' + str(position + 1) + '/' + str(len(lanes))
                           + ': ' + stage)
        if pending:
            self.worker.message(' | '.join(pending), 'Completing audits')
            return
        if not self.outbox.capacity_available():
            detail = getattr(self.worker, 'network_notice', None) or 'Publishing retained completed reports'
            self.worker.message(detail, 'Submitting reports')
            return
        detail = ("Waiting for required audit submissions" if self.gpu.search_waiting_for_audits else
                  self.prefetch.error or getattr(self.worker, 'network_notice', None) or "Preparing the next signed network ticket")
        self.worker.message(detail, "Waiting for next ticket")

    def failed(self, error):
        self.worker.message(str(error), "Needs attention")

    def publish_reports(self):
        from horizon_report_poll import ReportPoller
        poller = getattr(self, 'report_poller', None)
        independent_owner = poller is not None
        if poller is None:
            poller = ReportPoller(self.worker.audits, self.audit_lanes)
        next_recheck = 0
        next_orphan_recheck = 0
        while not self.worker.quitting.is_set():
            try:
                self.drain_recovered_report()
                if time.monotonic() >= next_orphan_recheck:
                    began = time.monotonic()
                    pending = getattr(self.outbox, 'has_parked_reports', None)
                    if callable(pending) and pending():
                        from horizon_report_reconciliation import retry_one_parked_report
                        with self.worker.network_lock:
                            retry_one_parked_report(self.worker, self.outbox)
                    next_orphan_recheck = time.monotonic() + max(5.0, 3.0 * (time.monotonic()-began))
                if time.monotonic() >= next_recheck:
                    next_recheck = time.monotonic() + 2
                    for cycle in self.cycles.reorg_held():
                        with self.worker.network_lock:
                            self.refresh_cycle_confirmations(cycle)
                entry = getattr(self.outbox, "next_for_publication", self.outbox.first)()
                if entry:
                    with self.worker.network_lock:
                        # A publication may have waited behind other network
                        # I/O. Recheck service after acquiring that lane, not
                        # before waiting for it. No journal lock spans RPC.
                        if self.report_ready(entry):
                            try:
                                self.worker.submit(None, work=entry["work"])
                            except ValueError as error:
                                if ('Report proposal no longer matches completed work: ticket' not in str(error)
                                        or not self.reassignments.stage(entry)):
                                    raise
                                self.worker.network_notice='Reorganized allocation retained; preparing the current ticket of the same request'
                                entry=None
                            except CoreRpcError as error:
                                from horizon_report_reconciliation import park_orphaned_report
                                if (error.code != -8 or 'Request has no current authenticated automatic ticket' not in str(error)
                                        or not park_orphaned_report(self.worker, self.outbox, entry)):
                                    raise
                                self.worker.network_notice = 'Orphaned ticket report retained for reconciliation; publishing current reports'
                                entry = None
                        else:
                            self.worker.network_notice = 'Audit confirmation repair pending; computed report retained'
                            entry = None
                    if entry:
                        self.outbox.acknowledge(entry["work"]["request_id"], entry["report_root"])
                if poller.due():
                    # Historical reads have their own tip/anchor checks. Only
                    # each actual publication holds the mutation lane; a slow
                    # sweep must not prevent preparing the next GPU audit.
                    if independent_owner:
                        poller.poll_audits()
                        self.worker.network_notice = poller.owner_error or poller.audit_error
                    else:
                        self.worker.network_notice = poller.poll()
                if entry:
                    continue
            except Exception as error:
                self.worker.network_notice = str(error)[:400]
                self.worker.quitting.wait(1)
            self.changed.wait(.1)
            self.changed.clear()

    def audit_service_snapshot(self):
        value = self.gpu.audit_service_view()
        # Read-only presentation binding. It does not combine jobs, confer an
        # audit credit, or change the resident queue's execution decisions.
        with self.gpu.condition:
            active = self.gpu.active
            context = dict(active.context) if active else {}
            cycle = context.get('work_cycle')
            catch_up = context.get('role') == 'audit' and not cycle
            if catch_up:
                cycle = context.get('audit_budget_cycle') or next((job.context.get('work_cycle') for job in self.gpu.search
                              if job.context.get('work_cycle')), None)
        if cycle:
            try:state = self.cycles.snapshot(cycle)
            except (KeyError, ValueError):
                value['cycle_notice'] = 'Work cycle presentation changed; awaiting next frame'
                cycle = None
        if cycle:
            totals = getattr(self, 'cycle_audit_positions', {})
            value['cycle'] = dict(schema='PGN_MINING_CYCLE_VIEW_V1', request_id=cycle,
                ticket_number=state['work']['ticket_number'], search_positions=state['work']['length'],
                active_index=2 if context.get('role') == 'search' else context.get('audit_position', 0),
                catch_up=catch_up, audits=[dict(state=(task or {}).get('stage', 'pending'),
                    positions=str(totals[(cycle, i)]) if (cycle, i) in totals else None)
                    for i, task in enumerate(state['audits'])])
        value["measurement_notice"] = self.audit_pressure_notice
        if getattr(self, 'report_poller', None) is not None:
            value['publication_maintenance'] = self.report_poller.snapshot()
        controlled = getattr(self, 'controlled_lab_audit_actors', False)
        value["controlled_lab_identities"] = controlled
        value["independent_economic_auditors"] = False if controlled else None
        return value

    def refresh_audit_pressure(self):
        # Read-only management I/O never runs in the GPU execution lane.
        while not self.worker.quitting.is_set():
            began = time.monotonic()
            try:
                state = self.worker.rpc.call("getpgnnetworkmetrics", [2, 60, 600])
                if state.get("schema") != "PGN_NETWORK_METRICS_V1" or state.get("genesis") != self.worker.args.genesis:
                    raise ValueError("Audit service measurement belongs to another chain or schema")
                queue = state["audit_queue"]
                waiting, assigned = queue["awaiting"], queue["assigned"]
                if type(waiting) is not int or type(assigned) is not int or waiting < 0 or assigned < 0:
                    raise ValueError("Invalid audit compute queue counts")
                self.gpu.update_audit_backlog(waiting + assigned, state["audit_enabled_operators"], queue["oldest_compute_pending_age_seconds"])
                self.audit_pressure_notice = None
            except Exception as error:
                # A failed read is not a zero backlog. Keep the last allocation.
                self.audit_pressure_notice = str(error)[:240]
            # Large histories can make this observation more expensive than
            # its old fixed pause. Keep its request duty at most25%, leaving
            # the chain lock available to assignment and publication RPCs.
            self.worker.quitting.wait(max(5.0, 3.0 * (time.monotonic()-began)))

    def audit_work(self, audit, position):
        while not self.worker.quitting.is_set():
            if not self.worker.running.wait(.2):
                continue
            try:
                pending = self.cycles.pending(position, retry_permissions=True)
                outcome = audit.process_one()
                audit.last_error = None
                if outcome == "none":
                    proposal = audit.last_unavailable or {}
                    # A prefetch during another calculation cannot waive the
                    # next cycle's audits before its actual service boundary.
                    with self.gpu.condition:
                        active = self.gpu.active
                        at_boundary = active is None or active.context.get("work_cycle") == pending
                    if pending and at_boundary and self.cycles.pending(position, retry_permissions=True) == pending:
                        reason = proposal.get("unavailable_reason")
                        if proposal.get("genesis") != self.worker.args.genesis:
                            raise ValueError("Audit availability belongs to another or unknown chain")
                        if reason == "no_eligible_independent_work":
                            # Availability and its anchor must come from the
                            # same Core read, not a later unrelated tip query.
                            anchor = proposal.get("parent")
                            self.cycles.attach(pending, position, dict(stage="unavailable", anchor=anchor, reason=reason), permission_verified=True)
                            self.gpu.wake()
                        elif reason in ("auditor_not_registered", "audit_permission_disabled"):
                            self.cycles.hold_permission(pending, reason)
                        elif reason:
                            self.cycles.hold(pending, proposal.get("reason", reason)[:240])
                    with self.gpu.condition:
                        self.gpu.condition.wait(5 if self.gpu.active is not None else .1)
            except Exception as error:
                if self.worker.quitting.is_set():
                    break
                self.worker.network_notice = str(error)[:400]
                audit.last_error = str(error)[:300]
                self.worker.quitting.wait(2)

    def startup_work(self):
        work=self.reassignments.next_work()
        if work is not None:return work,True
        try:return self.worker.assign(recover_only=True),False
        except ValueError as error:
            if str(error)!='Saved ticket no longer matches its live computation lease':raise
            saved=read(self.worker.root/'controller.json',262144)
            old=saved.get('work',{})
            entry=next((e for e in self.outbox._read()if
                e['work'].get('request_id')==old.get('request_id')and
                e['work'].get('ticket')==old.get('ticket')),None)
            if entry is None or not self.reassignments.stage(entry):raise
            return self.reassignments.next_work(),True

    def run(self):
        worker = self.worker
        with worker.network_lock:
            work,reassigned=self.startup_work()
            resumed = work is not None
            if work is not None:
                session = worker.root / "tickets" / work["ticket"] / "session.json"
                saved = read(session, 262144) if session.exists() else {}
                if saved.get("state") == "COMPLETE" or saved.get("schema") == COMPLETED_TICKET_SCHEMA:
                    worker.current = work
                    result = worker.source.run_ticket(self.job(work), worker.args.piece_limit)
                    # Publication and audit lanes must start even when older
                    # completed handoffs occupy every bounded outbox slot.
                    self.recovered_report = (work, result)
                    work = None
                    resumed = False
        if work is not None:
            native = work.get('native_audit_service', {}).get('enforced') is True
            legacy_assignment = resumed and not native and not self.cycles.contains(work["request_id"])
            if not legacy_assignment:
                self.prepare_cycle(work)
            self.promote(work, self.view_sequence)
            if reassigned:self.reassignments.adopted(work)
            journal = self.allocator.root / "controller.json"
            if journal.exists():
                value = read(journal)
                if value.get("work", {}).get("ticket") == work["ticket"]:
                    value["stage"] = "CONSUMED"
                    save(journal, value)
            self.gpu.submit(self.job(work), dict(role="search", work=work, prepared=False,
                            work_cycle=None if legacy_assignment else work["request_id"], legacy_assignment=legacy_assignment), worker.args.piece_limit)
            if resumed and self.view_sequence > 0:
                self.gpu.jobs[work["ticket"]].view_sequence = self.view_sequence
        try:
            # The existing preparation lane also obtains the first new ticket.
            # A native audit-duty refusal therefore leaves the execution and
            # audit lanes running instead of deadlocking before they start.
            self.prefetch.start()
            self.report_thread.start()
            self.owner_report_thread.start()
            if self.finalize_thread is not None:
                self.finalize_thread.start()
            self.pressure_thread.start()
            for thread in self.audit_threads:
                thread.start()
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
            for thread in [self.report_thread, self.owner_report_thread, self.finalize_thread, self.pressure_thread, *self.audit_threads]:
                if thread is not None and thread.ident is not None:
                    thread.join(5)
            worker.release_device()
            worker.source.close()
            self.cycles.close()
            self.closed = True
