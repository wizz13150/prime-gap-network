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
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
from horizon_mining_flow import MiningFlow, save
from horizon_coordination import native_header_hash
from horizon_cpu_bridge import LogicalMinerCpuResources
from horizon_cpu_cycle import digest


class MiningFlowCycleTests(unittest.TestCase):
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

    def exercise(self, reason, should_search, have_audits=False, permission_recovers=False):
        # Protocol-only synthetic engine/RPC data; no private ticket, chain,
        # wallet, hardware, runtime payload or prior scientific result is read.
        lo=150000000000000000000;length=200000000000000
        work=dict(genesis='f'*64,parent='a'*64,request_id='1'*64,ticket='2'*64,work_id='3'*64,
                  start=str(lo),length=str(length),ticket_number='1',target_numerator='32',target_denominator='1')
        from pgn_native_contract import fields
        body=dict(schema='PGN_HORIZON_COMPUTED_REPORT_V1',ticket_id=work['ticket'],start=str(lo),end=str(lo+length),
                  positions=str(length),policy_sha256=fields(lo,lo+length)['threshold_policy']['sha256'],counts=['0']*100,best=[])
        computed=dict(body=body,sha256=digest(body),network_accepted=False)
        work['ticket_number'] = str((int(work['start']) - 150000000000000000000) // 200000000000000 + 1)
        with tempfile.TemporaryDirectory() as temporary:
            class Rpc:
                def __init__(self): self.assigned = 0; self.jobs = {}; self.serial = 0; self.denials = 0
                def call(self, method, args=None, wallet=False):
                    args = args or []
                    if method == 'validateaddress':
                        return dict(isvalid=True, scriptPubKey='0014' + '1' * 40)
                    if method == 'getpgnworkview':
                        # The normal-flow fixture has no pre-existing assigned
                        # audits. It must answer the same anchored recovery read
                        # as Core before the controller requests new service.
                        return dict(schema='PGN_WORK_VIEW_V1', kind='audits',
                                    offset=args[1], limit=args[2],
                                    state=dict(genesis=work['genesis'],
                                               bestblockhash='a' * 64, height=100),
                                    reports=[], audits=[])
                    if method == 'signmessage': return 'fixture-signature'
                    if method == 'getpgnauditevent':
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
                    if method == 'getpgnscientificwork': return dict(target_numerator='32', target_denominator='1')
                    if method == 'getpgnauditstate':
                        return [dict(request_id=args[2], jobs=[dict(slot=0, attempt=0, **self.jobs[args[2]])])]
                    raise AssertionError(method)
            class Source:
                def __init__(self): self.calls = []; self.roles = []; self.manager = SimpleNamespace(shutdown=lambda: None)
                def run_ticket(self, job, limit):
                    time.sleep(.01)
                    self.calls.append(job)
                    context = flow.gpu.active.context
                    self.roles.append((context['role'], context.get('audit_position')))
                    if context['role'] == 'audit':
                        audit_body=dict(schema='PGN_HORIZON_COMPUTED_REPORT_V1',ticket_id=job['ticket_id'],
                            start=job['start'],end=job['end'],positions=str(int(job['end'])-int(job['start'])),
                            policy_sha256=job['threshold_policy']['sha256'],counts=['1']+['0']*99,best=[])
                        return dict(state='COMPLETE',body=audit_body,sha256=digest(audit_body),network_accepted=False)
                    (worker.root / 'tickets' / job['ticket_id']).mkdir(parents=True, exist_ok=True)
                    return copy.deepcopy(computed)
                def close(self): pass
            class Worker:
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
            worker.rpc = Rpc(); worker.source = Source(); worker.messages = []
            worker.running = threading.Event(); worker.running.set(); worker.quitting = threading.Event()
            worker.state_lock = threading.RLock(); worker.network_lock = threading.RLock()
            worker.sequence = 0; worker.device_lease = None; worker.audits = SimpleNamespace(presentation=None)
            cpu_stages={'CPU/ALEX/0':[], 'CPU/WIZZ/0':[]}
            cpu_requests={device:None for device in cpu_stages}
            def cpu_emit(frame):
                if frame['schema']!='PGN_HORIZON_CPU_COMMAND_V1':return True
                device=frame['device_id'];command=frame['command'];action=command['action']
                if action=='execute':
                    cpu_requests[device]=command['request'];cpu_stages[device].append(command['request']['stage'])
                request=cpu_requests[device]
                reply=dict(ok=True,status=dict(request_sha256=digest(request)if request else None))
                if action=='evidence':
                    reply['evidence']=dict(request=request,request_sha256=digest(request),executor=dict(host=device.split('/')[1],processor_group=0),
                        complete=False,counts100=['0']*100,best=[],frontiers=[],completed_positions='100',production_admitted=False,network_accepted=False)
                if action in('discard','ack_result'):cpu_requests[device]=None
                worker.source.accept_cpu_reply(device,frame['bridge_id'],reply);return True
            worker.source=LogicalMinerCpuResources(worker.source,worker.root,cpu_emit)
            for device in cpu_stages:worker.source.configure_cpu(device,dict(host=device.split('/')[1],processor_group=0),True,1,[0])
            flow = MiningFlow(worker)
            flow.prefetch.ready = work
            save(flow.allocator.root / 'controller.json', dict(work=work, stage='ASSIGNED', sequence=1))
            flow.prepared(work)
            completed = flow.gpu.on_result
            def finish(job, result):
                completed(job, result)
                if job.context['role'] == 'search': worker.quitting.set()
            flow.gpu.on_result = finish
            # Successful work stops on the actual search result above. The
            # watchdog is only a failure bound, not a two-second work quota:
            # durable audit journals can exceed that quota on a loaded CI host.
            timer = threading.Timer(10 if should_search else 2, lambda: (worker.quitting.set(), flow.gpu.wake()))
            timer.start()
            for thread in flow.audit_threads: thread.start()
            try:
                flow.gpu.run()
                for lane in worker.source.lanes.values():lane.wait_cpu(5)
                cycle = flow.cycles.snapshot(work['request_id'])
                if should_search:
                    if permission_recovers: self.assertGreater(worker.rpc.denials, 2)
                    self.assertEqual(len(worker.source.calls), 11 if have_audits else 1,
                                     getattr(worker,'network_notice',None))
                    self.assertEqual(cycle['stage'], 'completed')
                    self.assertEqual([task['stage'] for task in cycle['audits']], ['committed', 'committed'] if have_audits else ['unavailable', 'unavailable'])
                    self.assertEqual(flow.outbox.first()['work']['request_id'], work['request_id'])
                    if have_audits:
                        self.assertEqual(worker.source.roles, [('audit', 0)] * 5 + [('audit', 1)] * 5 + [('search', None)])
                        for device,stages in cpu_stages.items():
                            self.assertIn('audit1',stages);self.assertIn('audit2',stages);self.assertIn('search',stages)
                            self.assertLess(stages.index('audit1'),stages.index('audit2'));self.assertLess(stages.index('audit2'),stages.index('search'))
                            self.assertIsNone(worker.source.lanes[device].last_cpu_error)

                else:
                    self.assertFalse(worker.source.calls)
                    self.assertTrue(cycle['hold'])
                    self.assertEqual(cycle['stage'], 'prepared')
                    self.assertIsNone(flow.outbox.first())
            finally:
                worker.quitting.set(); worker.running.set(); flow.gpu.wake(); timer.cancel()
                for thread in flow.audit_threads:
                    thread.join(2); self.assertFalse(thread.is_alive())
                flow.cycles.close()


if __name__ == '__main__':unittest.main()
