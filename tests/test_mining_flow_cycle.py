"""Real controller/queue/outbox/journal with controlled RPC and engine fixtures."""
from pathlib import Path
from types import SimpleNamespace
import copy
import hashlib
import json
import sys
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src/protocol'))
sys.path.insert(0, str(ROOT / 'src/engine/runtime_host'))
from horizon_mining_flow import MiningFlow, save
from horizon_coordination import native_header_hash


class MiningFlowCycleTests(unittest.TestCase):
    def test_ticket_prefetch_refusal_does_not_hide_unfinished_audit_verification(self):
        messages=[];flow=object.__new__(MiningFlow)
        flow.worker=SimpleNamespace(message=lambda text,phase:messages.append((text,phase)))
        flow.gpu=SimpleNamespace(search_waiting_for_audits=False)
        flow.prefetch=SimpleNamespace(error='Required audit service before next range')
        flow.outbox=SimpleNamespace(capacity_available=lambda:True)
        flow.audit_lanes=[SimpleNamespace(current_audit={'ranges':[{'index':12},{'index':30}], 'counts':[]}),
                          SimpleNamespace(current_audit={'ranges':[{'index':24},{'index':31}], 'counts':[7,8]})]
        flow.waiting();text,phase=messages[-1]
        self.assertEqual(phase,'Completing audits')
        self.assertIn('Audit 1/2: verifying computed results',text)
        self.assertIn('Audit 2/2: result publication pending',text)
        self.assertNotIn('partition',text)
        self.assertNotIn('Required audit service',text)
        flow.audit_lanes[0].current_audit=None;flow.audit_lanes[1].current_audit=None
        flow.waiting();self.assertEqual(messages[-1],(flow.prefetch.error,'Waiting for next ticket'))

    def test_readonly_visual_cycle_carries_two_audits_and_search_without_mutating_state(self):
        from collections import deque
        state=dict(work=dict(ticket_number='42',length='200000000000000'),audits=[dict(stage='committed'),dict(stage='assigned')])
        original=copy.deepcopy(state)
        flow=object.__new__(MiningFlow);flow.gpu=SimpleNamespace(condition=threading.Condition(),active=SimpleNamespace(context=dict(role='audit',work_cycle='a'*64,audit_position=1)),search=deque(),audit_service_view=lambda:dict(active_role='audit'))
        flow.cycles=SimpleNamespace(snapshot=lambda request:copy.deepcopy(state));flow.cycle_audit_positions={('a'*64,0):10**13,('a'*64,1):10**13};flow.audit_pressure_notice=None
        view=flow.audit_service_snapshot()['cycle'];self.assertEqual(view['active_index'],1);self.assertEqual(view['ticket_number'],'42');self.assertEqual(view['audits'][1]['positions'],'10000000000000');self.assertEqual(state,original)
        flow.gpu.active.context=dict(role='audit',audit_position=0);flow.gpu.search.append(SimpleNamespace(context=dict(work_cycle='a'*64)))
        self.assertTrue(flow.audit_service_snapshot()['cycle']['catch_up']);self.assertEqual(state,original)
        def retired(request):raise ValueError('No retained cycle')
        flow.cycles.snapshot=retired;self.assertNotIn('cycle',flow.audit_service_snapshot())

    def test_real_empty_queue_acknowledges_pause_before_first_gpu_job(self):
        from horizon_gpu_queue import ResidentGpuQueue
        stopped=threading.Event();enabled=threading.Event();ack=threading.Event();events=[]
        def message(text,phase):events.append(('status',phase));ack.set()
        worker=SimpleNamespace(current=None,audits=SimpleNamespace(presentation=None),
            release_device=lambda:events.append(('released',None)),message=message,
            progress=lambda value:self.fail('No current ticket exists to present'))
        flow=object.__new__(MiningFlow);flow.worker=worker
        def unexpected(*args):self.fail('An empty disabled queue must not execute GPU work')
        queue=ResidentGpuQueue(SimpleNamespace(run_ticket=unexpected),enabled,stopped,unexpected,unexpected,flow.paused,unexpected)
        thread=threading.Thread(target=queue.run);thread.start()
        try:self.assertTrue(ack.wait(2));self.assertEqual(events,[('released',None),('status','Paused')])
        finally:stopped.set();thread.join(2)
        self.assertFalse(thread.is_alive())

    def test_empty_eligible_pool_runs_search_and_keeps_report_in_outbox(self):
        self.exercise('no_eligible_independent_work', should_search=True)

    def test_unregistered_auditor_does_not_silently_skip_required_service(self):
        self.exercise('auditor_not_registered', should_search=False)

    def test_controller_sends_two_complete_audits_then_computes_its_range(self):
        self.exercise('no_eligible_independent_work', should_search=True, have_audits=True)

    def test_pending_registration_resumes_after_fresh_core_permission_reply(self):
        self.exercise('auditor_not_registered', should_search=True, permission_recovers=True)

    def test_disabled_permission_resumes_after_fresh_core_permission_reply(self):
        self.exercise('audit_permission_disabled', should_search=True, permission_recovers=True)

    def test_required_audits_keep_order_with_deferred_report_finalization(self):
        self.exercise('no_eligible_independent_work', should_search=True, have_audits=True, preseal=True)

    def test_native_initial_allocation_waits_for_audits_without_deadlocking_gpu(self):
        self.exercise('no_eligible_independent_work', should_search=True, have_audits=True, full_run=True, native=True)

    def test_lab_actors_retire_from_computation_when_native_service_is_enforced(self):
        self.exercise('no_eligible_independent_work', should_search=True, have_audits=True,
                      full_run=True, native=True, lab=True)

    def test_lab_actors_keep_legacy_computation_before_native_service(self):
        self.exercise('no_eligible_independent_work', should_search=True, have_audits=True,
                      full_run=True, native=False, lab=True)

    def test_native_no_eligible_work_bootstraps_without_fabricating_two_audits(self):
        self.exercise('no_eligible_independent_work', should_search=True, full_run=True, native=True)

    def test_legacy_initial_ticket_still_runs_two_audits_before_search(self):
        self.exercise('no_eligible_independent_work', should_search=True, have_audits=True, full_run=True)

    def test_stop_while_native_gate_is_held_drains_all_resident_lanes(self):
        self.exercise('no_eligible_independent_work', should_search=False, full_run=True, native=True, held=True)

    def exercise(self, reason, should_search, have_audits=False, permission_recovers=False, preseal=False,
                 full_run=False, native=False, held=False, lab=False):
        computed = json.loads((ROOT / 'tests/fixtures/network/computed-report.json').read_bytes())
        work = json.loads((ROOT / 'tests/fixtures/network/issued-work.json').read_bytes())
        work['ticket_number'] = str((int(work['start']) - 150000000000000000000) // 200000000000000 + 1)
        with tempfile.TemporaryDirectory() as temporary:
            class Rpc:
                def __init__(self): self.assigned = 0; self.jobs = {}; self.serial = 0; self.denials = 0; self.audit_actors=set()
                def cancel_all(self): pass
                def native_receipt(self):
                    return dict(schema='PGN_NATIVE_ASSIGNED_AUDIT_SERVICE_V1',enforced=True,
                        request_id=work['request_id'],view_parent='a'*64,issuance_block='c'*64,issuance_height=16,
                        credits=[dict(request_id=k,slot=0,attempt=0,commitment=v['commitment'])
                                 for k,v in sorted(self.jobs.items())if v['state']=='committed'])
                def call(self, method, args=None, wallet=False):
                    args = args or []
                    if method == 'getblockchaininfo': return dict(chain='test')
                    if method == 'getaddressinfo': return dict(ismine=args[0] in lab_actors)
                    if method == 'getpgnworkdutystatus':
                        return dict(schema='PGN_NATIVE_WORK_DUTY_STATUS_V1',genesis=work['genesis'],
                            bestblockhash='a'*64,enforced=native,may_request=not held and
                            (not have_audits or sum(v['state']=='committed'for v in self.jobs.values())==2),
                            status='audit_work_required')
                    if method == 'getblockheader':return dict(hash=args[0],height=16,confirmations=5)
                    if method == 'getblockhash':return 'c'*64
                    if method == 'signmessage': return 'fixture-signature'
                    if method == 'getpgnauditevent':
                        self.audit_actors.add(args[1])
                        action = args[0]
                        if not have_audits or action == 'assign' and self.assigned >= 2:
                            unavailable = reason
                            if permission_recovers:
                                self.denials += 1
                                if self.denials > 2: unavailable = 'no_eligible_independent_work'
                            return dict(available=False, unavailable_reason=unavailable, reason=unavailable, genesis=work['genesis'], parent='a' * 64)
                        request = f'{100 + self.assigned:064x}' if action == 'assign' else args[2]
                        ranges = [dict(index=i, start=str(160000000000000000000 + i * 2_000_000_000_000),
                                       end=str(160000000000000000000 + (i + 1) * 2_000_000_000_000)) for i in range(5)]
                        envelope = json.dumps(dict(action=action, request=request, body=args[5])).encode().hex()
                        return dict(available=True, challenge='fixture', envelope=envelope,
                                    request_id=request, slot=0, attempt=0, ranges=ranges)
                    if method == 'getbestblockhash': return 'a' * 64
                    if method == 'getpgnreports': return [dict(ticket='3' * 64, report_root='4' * 64)]
                    if method == 'getpgnscientificwork':
                        if native and args[0]==work['request_id']:
                            return dict(work,accepted=False,parent='a'*64,height=21,native_audit_service=self.native_receipt())
                        return dict(target_numerator='32', target_denominator='1')
                    if method == 'getpgnauditstate':
                        return [dict(request_id=args[2], jobs=[dict(slot=0, attempt=0, **self.jobs[args[2]])])]
                    raise AssertionError(method)
            class Source:
                def __init__(self): self.calls = []; self.roles = []; self.manager = SimpleNamespace(shutdown=lambda: None)
                def require_report_finalization(self): self.retention_required = True
                def run_ticket(self, job, limit):
                    self.calls.append(job)
                    context = flow.gpu.active.context
                    self.roles.append((context['role'], context.get('audit_position')))
                    if context['role'] == 'audit':
                        from report_commitment import canonical_bytes
                        body=dict(schema='PGN_HORIZON_COMPUTED_REPORT_V1',ticket_id=job['ticket_id'],
                            start=job['start'],end=job['end'],positions=str(int(job['end'])-int(job['start'])),
                            policy_sha256=job['threshold_policy']['sha256'],counts=['1']+['0']*99,best=[])
                        return dict(state='COMPLETE',body=body,sha256=hashlib.sha256(canonical_bytes(body)).hexdigest(),
                                    network_accepted=False)
                    (worker.root / 'tickets' / job['ticket_id']).mkdir(parents=True, exist_ok=True)
                    return copy.deepcopy(computed)
                def close(self): self.closed=True
            class Worker:
                def assign(self,recover_only=False):
                    if recover_only:return None
                    if native:
                        from horizon_native_work_duty import gate
                        gate(self.rpc,self.address(),work['genesis'])
                    issued=dict(work,native_audit_service=self.rpc.native_receipt())if native else work
                    save(self.root/'controller.json',dict(stage='ASSIGNED',work=issued,sequence=1))
                    return issued
                def address(self): return 'fixture-auditor'
                def ledger(self, envelopes, *, on_intent=None):
                    if not have_audits: raise AssertionError('No available audit should be published')
                    event = json.loads(bytes.fromhex(envelopes[0]))
                    self.rpc.serial += 1
                    header = bytearray(112); header[4:36] = bytes.fromhex('a' * 64)[::-1]; header[80] = 2
                    header[82] = self.rpc.serial
                    block = bytes(header) + b'\1'
                    intent = dict(parent='a' * 64, block=block.hex(), hash=native_header_hash(block),
                                  operation_sha256=hashlib.sha256(json.dumps(envelopes, separators=(',', ':')).encode()).hexdigest())
                    if on_intent: on_intent(intent)
                    if event['action'] == 'assign':
                        self.rpc.jobs[event['request']] = dict(state='assigned')
                        self.rpc.assigned += 1
                    elif event['action'] == 'commit':
                        self.rpc.jobs[event['request']] = dict(state='committed', commitment=event['body'])
                    else: raise AssertionError(event)
                    return dict(block_hash=intent['hash'])
                def acquire_device(self): self.device_lease = 'controlled-engine'
                def release_device(self): self.device_lease = None
                def message(self, text, phase): self.messages.append((text, phase))
                def progress(self, event): pass
            worker = Worker(); worker.root = Path(temporary)
            worker.args = SimpleNamespace(genesis=work['genesis'], piece_limit=64000000000000)
            lab_actors=['tpgn'+'A'*30,'tpgn'+'B'*30]
            if lab:
                profile=worker.root/'controlled-lab-actors.json'
                profile.write_text(json.dumps(dict(schema='PGN_CONTROLLED_LAB_AUDIT_ACTORS_V1',genesis=work['genesis'],scope='controlled-testnet-laboratory',independent_economic_auditors=False,actors=lab_actors)))
                worker.args.controlled_lab_audit_actors=profile;worker.args.pilot_coordination=True
            worker.rpc = Rpc(); worker.source = Source(); worker.messages = []
            worker.running = threading.Event(); worker.running.set(); worker.quitting = threading.Event()
            worker.state_lock = threading.RLock(); worker.network_lock = threading.RLock()
            worker.sequence = 0; worker.device_lease = None; worker.audits = SimpleNamespace(presentation=None)
            flow = MiningFlow(worker, finalize=(lambda work, report: report) if preseal else None)
            self.assertEqual([lane.worker.address()for lane in flow.audit_lanes],lab_actors if lab and not native else ['fixture-auditor']*2)
            if preseal:self.assertTrue(worker.source.retention_required)
            if not full_run:
                flow.prefetch.ready = work
                save(flow.allocator.root / 'controller.json', dict(work=work, stage='ASSIGNED', sequence=1))
                flow.prepared(work)
            completed = flow.gpu.on_result
            def finish(job, result):
                completed(job, result)
                if job.context['role'] == 'search': worker.quitting.set()
            flow.gpu.on_result = finish
            # This is a deadlock guard, not a throughput deadline. Idle/pinned
            # validation may be descheduled by interactive Windows activity.
            timer = threading.Timer(1 if held else 10, lambda: (worker.quitting.set(), flow.gpu.wake()))
            timer.start()
            if not full_run:
                for thread in flow.audit_threads: thread.start()
            try:
                (flow.run if full_run else flow.gpu.run)()
                if full_run:
                    self.assertTrue(flow.closed);self.assertTrue(worker.source.closed)
                    for thread in [flow.prefetch.thread,flow.report_thread,flow.pressure_thread,*flow.audit_threads]:
                        self.assertFalse(thread.is_alive())
                cycle = flow.cycles.snapshot(work['request_id']) if flow.cycles.contains(work['request_id'])else None
                if should_search:
                    if lab:
                        self.assertEqual(worker.rpc.audit_actors,set(lab_actors) if not native else {'fixture-auditor'})
                        service=flow.audit_service_snapshot();self.assertEqual(service['controlled_lab_identities'],not native)
                        if not native:self.assertFalse(service['independent_economic_auditors'])
                    if permission_recovers: self.assertGreater(worker.rpc.denials, 2)
                    self.assertEqual(len(worker.source.calls), 11 if have_audits else 1,
                                     (getattr(worker,'network_notice',None),flow.prefetch.error,worker.messages[-3:]))
                    self.assertEqual(cycle['stage'], 'completed')
                    self.assertEqual([task['stage'] for task in cycle['audits']], ['committed', 'committed'] if have_audits else ['unavailable', 'unavailable'])
                    if preseal:
                        self.assertIsNone(flow.outbox.first())
                        self.assertEqual(len(flow.preseal.entries()),1)
                        self.assertTrue(flow.preseal.pump(flow.finalizer,flow.outbox))
                    self.assertEqual(flow.outbox.first()['work']['request_id'], work['request_id'])
                    if have_audits:
                        if native:
                            self.assertEqual(worker.source.roles[-1],('search',None))
                            self.assertEqual(worker.source.roles[:10].count(('audit',0)),5)
                            self.assertEqual(worker.source.roles[:10].count(('audit',1)),5)
                            self.assertIn('native_service',cycle)
                        else:self.assertEqual(worker.source.roles, [('audit', 0)] * 5 + [('audit', 1)] * 5 + [('search', None)])
                else:
                    self.assertFalse(worker.source.calls)
                    if held:
                        self.assertIsNone(cycle);self.assertIn('Required audit service',flow.prefetch.error)
                    else:
                        self.assertTrue(cycle['hold'])
                        self.assertEqual(cycle['stage'], 'prepared')
                    self.assertIsNone(flow.outbox.first())
            finally:
                worker.quitting.set(); worker.running.set(); flow.gpu.wake(); timer.cancel()
                for thread in flow.audit_threads:
                    thread.join(2); self.assertFalse(thread.is_alive())
                flow.cycles.close()


if __name__ == '__main__': unittest.main()
