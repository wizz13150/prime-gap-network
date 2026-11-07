"""Actual audit client/cycle journal; controlled RPC and computation, no live credit."""
from pathlib import Path
from types import SimpleNamespace
import hashlib
import json
import sys
import tempfile
import threading
import unittest
import subprocess

ROOT = Path(__file__).resolve().parents[1]
NATIVE_PROJECTION = Path(sys.argv.pop(1)).resolve() if len(sys.argv)>1 and not sys.argv[1].startswith('-') else None
sys.path.insert(0, str(ROOT / 'src/protocol'))
sys.path.insert(0, str(ROOT / 'src/engine/runtime_host'))
from horizon_network_audits import NetworkAudits
from horizon_work_cycle import WorkCycleJournal
from horizon_coordination import native_header_hash


class AuditCyclePublicationTests(unittest.TestCase):
    def test_restart_after_core_confirmation_adopts_exact_publication_without_recompute(self):
        self.exercise(crash=True)

    def test_pause_and_repricing_keep_the_same_audit_configuration(self):
        self.exercise(pause=True)

    def test_restart_refuses_a_different_committed_result(self):
        self.exercise(crash=True, mismatched=True)

    def test_missing_digest_reproduces_old_rpc_recovery_hold(self):
        self.exercise(crash=True, missing=True)

    def exercise(self, crash=False, pause=False, mismatched=False, missing=False):
        with tempfile.TemporaryDirectory() as temporary, WorkCycleJournal(Path(temporary) / 'cycles', 'f' * 64) as journal:
            root = Path(temporary) / 'runtime'; root.mkdir()
            parent = 'd' * 64
            journal.prepare(dict(genesis='f' * 64, request_id=parent, ticket='e' * 64,
                                 ticket_number='42', start='150000000000000000000', length='200000000000000',
                                 target_numerator='32', target_denominator='1'))
            class Rpc:
                stage = 'none'
                target = '32'
                work_calls = 0
                commitment = None
                active_hash = 'b' * 64
                def call(self, method, args=None, wallet=False):
                    args = args or []
                    if method == 'signmessage': return 'fixture-signature'
                    if method == 'getpgnauditevent':
                        action = args[0]
                        if action == 'assign' and self.stage != 'none':
                            return dict(available=False, unavailable_reason='no_eligible_independent_work')
                        ranges = [dict(index=i, start=str(150000000000000000000 + i * 2_000_000_000_000),
                                       end=str(150000000000000000000 + (i + 1) * 2_000_000_000_000)) for i in range(5)]
                        return dict(available=True, challenge='fixture', envelope=action,
                                    request_id='1' * 64, slot=0, attempt=0, ranges=ranges)
                    if method == 'getpgnreports': return [dict(ticket='2' * 64, report_root='3' * 64)]
                    if method == 'getpgnscientificwork':
                        self.work_calls += 1
                        return dict(target_numerator=self.target, target_denominator='1')
                    if method == 'getpgnauditstate':
                        if NATIVE_PROJECTION is not None:
                            result=subprocess.run([str(NATIVE_PROJECTION),'--project',self.stage,self.commitment or ''],
                                                  capture_output=True,timeout=5,check=True)
                            projected=json.loads(result.stdout)
                            if set(projected)-{'slot','attempt','state','commitment'}:
                                raise AssertionError('Private audit data in native public projection')
                            if missing:projected.pop('commitment',None)
                            return [dict(request_id='1'*64,jobs=[projected])]
                        if missing:return [dict(request_id='1'*64,jobs=[dict(slot=0,attempt=0,state=self.stage)])]
                        return [dict(request_id='1' * 64, jobs=[dict(slot=0, attempt=0, state=self.stage, commitment=self.commitment)])]
                    if method == 'getblockheader': return dict(hash=self.active_hash, height=7, confirmations=1)
                    if method == 'getblockhash': return self.active_hash
                    raise AssertionError(method)
            class Source:
                def __init__(self): self.jobs = []; self.paused_once = False
                def run_ticket(self, job, limit):
                    self.jobs.append(job)
                    if pause and len(self.jobs) == 2 and not self.paused_once:
                        self.paused_once = True
                        return dict(state='PAUSED')
                    return dict(state='COMPLETE', body=dict(counts=['1'] + ['0'] * 99))
            rpc, source = Rpc(), Source()
            worker = SimpleNamespace(root=root, rpc=rpc, source=source, network_lock=threading.RLock(),
                                     args=SimpleNamespace(piece_limit=64_000_000_000_000),
                                     running=threading.Event(), address=lambda: 'fixture-auditor')
            worker.running.set()
            service = None
            persisted_before_commit = []
            def ledger(envelopes, *, on_intent=None):
                action = envelopes[0]
                header = bytearray(112); header[4:36] = bytes.fromhex('b' * 64)[::-1]; header[80] = 2
                header[82] = 1 if action == 'assign' else 2
                block = bytes(header) + b'\1'
                intent = dict(parent='b' * 64, block=block.hex(), hash=native_header_hash(block),
                              operation_sha256=hashlib.sha256(json.dumps(envelopes, separators=(',', ':')).encode()).hexdigest())
                if on_intent:
                    on_intent(intent)
                    saved = json.loads((service.folder / 'current.json').read_bytes())
                    persisted_before_commit.append(saved['publication_intent']['hash'])
                if action == 'assign': rpc.stage = 'assigned'
                elif action == 'commit':
                    saved = json.loads((service.folder / 'current.json').read_bytes())
                    rpc.commitment = saved['answer']['commitment']; rpc.stage = 'committed'
                else: raise AssertionError(action)
                rpc.active_hash = intent['hash']
                return dict(block_hash=intent['hash'])
            worker.ledger = ledger
            def assigned(audit):
                journal.attach(parent, 0, dict(stage='assigned', request=audit['request_id'], slot=0, attempt=0))
            fail_once = [crash]
            def committed(audit, block):
                if fail_once[0]:
                    fail_once[0] = False
                    raise RuntimeError('Injected crash after Core confirmation')
                journal.published(parent, 0, audit['request_id'], 0, 0, block)
            service = NetworkAudits(worker, on_assigned=assigned, on_committed=committed)
            if pause:
                self.assertEqual(service.process_one(), 'paused')
                rpc.target = '31'
                self.assertEqual(service.process_one(), 'committed')
            elif crash:
                with self.assertRaisesRegex(RuntimeError, 'Injected crash'):
                    service.process_one()
                self.assertEqual(journal.snapshot(parent)['audits'][0]['stage'], 'assigned')
                count_before = len(source.jobs)
                if mismatched:rpc.commitment='f'*64
                service = NetworkAudits(worker, on_assigned=assigned, on_committed=committed)
                if mismatched or missing:
                    with self.assertRaisesRegex(RuntimeError,'does not expose' if missing else 'assignment changed'):
                        service.process_one()
                    self.assertEqual(len(source.jobs),count_before)
                    self.assertTrue((service.folder/'current.json').exists())
                    self.assertEqual(journal.snapshot(parent)['audits'][0]['stage'],'assigned')
                    return
                self.assertEqual(service.process_one(), 'none')
                self.assertEqual(len(source.jobs), count_before)
            else:
                self.assertEqual(service.process_one(), 'committed')
            self.assertEqual(journal.snapshot(parent)['audits'][0]['stage'], 'committed')
            self.assertFalse(journal.may_search(parent))
            self.assertEqual(len(persisted_before_commit), 1)
            self.assertEqual(rpc.work_calls, 1)
            self.assertTrue(all(job['target_merit_q16'] == 32 * 65536 for job in source.jobs))
            self.assertTrue(all(not {'expected', 'expected_counts', 'report_body'} & set(job) for job in source.jobs))


if __name__ == '__main__': unittest.main()
