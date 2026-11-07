"""Durable queue crash boundaries and actual small owned CPU executions."""
from pathlib import Path
import ctypes
import json
import os
import sys
import tempfile
import unittest
from unittest.mock import patch

EXE = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else None
ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / p) for p in (
    'src/engine/runtime_host', 'src/engine/shared_composite', 'src/protocol')]
import horizon_preparation_queue as queue
import horizon_preparation_process as executor
from pgn_session import SessionControllerLock
from pgn_owned import _kernel, windows_birth

N = 10000019**2
SMALL = [(N - 120, N + 120)]
LARGE = [(150000000000000000000, 150000000000000000030)]


class PreparationQueueTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name) / 'source queue'
        self.q = queue.PreparationQueue(self.root)

    def require_worker(self):
        if os.name != 'nt' or EXE is None:
            self.skipTest('Pass the qualified Windows producer for owned execution')

    def test_duplicate_input_is_idempotent_across_restart(self):
        identity = self.q.enqueue(SMALL)
        self.assertEqual(queue.PreparationQueue(self.root).enqueue(SMALL), identity)
        self.assertEqual(len(self.q.entries()), 1)
        self.assertEqual(self.q.entries()[0]['attempt'], 0)

    def closed_launch(self):
        identity=self.q.enqueue(SMALL);self.q._update(identity,state='failed',attempt=1)
        attempt=self.q._job(identity)/'attempt-1';attempt.mkdir()
        request=dict(schema='PGN_CPU_PREPARATION_REQUEST_V1',image_sha256=queue.PRODUCER_SHA,args=['never-resumed'])
        owner=dict(schema='PGN_CPU_PREPARATION_OWNER_V1',role='cpu_preparation',state='OWNED_PROCESS_CLOSED',
                   configuration_sha256=queue.hashlib.sha256(queue.canonical(request)).hexdigest())
        (attempt/'request.json').write_bytes(queue.canonical(request))
        (attempt/'producer-owner.json').write_bytes(queue.canonical(owner))
        return identity,attempt,owner

    def test_drained_constructor_failure_can_retry_bounded_new_attempt(self):
        identity,attempt,owner=self.closed_launch();before=(attempt/'request.json').read_bytes()
        with patch.object(queue,'run_preparation',side_effect=InterruptedError('Stopped before new execution'))as execute:
            with self.assertRaises(InterruptedError):self.q.pump('unused',logical_cpu=4)
        self.assertEqual(execute.call_args.args[1].name,'attempt-2')
        self.assertEqual((attempt/'request.json').read_bytes(),before)
        self.assertTrue(json.loads((attempt/'abandoned.json').read_bytes())['owned_job_drained'])
        self.assertEqual(self.q.entries()[0]['attempt'],2)
        with patch.object(queue,'run_preparation',side_effect=AssertionError('Third attempt forbidden')):
            with self.assertRaises(queue.PreparationRecoveryRequired):self.q.pump('unused',logical_cpu=4)

    def test_closed_launch_recovery_refuses_identity_output_or_changed_marker(self):
        identity,attempt,owner=self.closed_launch()
        for name in ('producer-identity.json','producer-receipt.json','output'):
            file=attempt/name;file.write_text('{}')
            self.assertFalse(queue.PreparationQueue._closed_before_identity(attempt));file.unlink()
        for key,value in [('pid',123),('configuration_sha256','0'*64),('state','SUSPENDED_ASSIGNED_JOB')]:
            (attempt/'producer-owner.json').write_bytes(queue.canonical(dict(owner,**{key:value})))
            self.assertFalse(queue.PreparationQueue._closed_before_identity(attempt))
        (attempt/'producer-owner.json').write_bytes(queue.canonical(owner))
        self.assertTrue(queue.PreparationQueue._closed_before_identity(attempt))

    def test_actual_long_output_path_completion_and_receipt_reuse(self):
        self.require_worker();folder=self.root/('long-'+110*'a')/(110*'b')/'attempt'
        self.assertGreater(len(str(folder)),260)
        bundle,receipt=executor.run_preparation(EXE,folder,SMALL,logical_cpu=4,memory_mib=128,timeout=20)
        self.assertEqual(receipt['exit'],0);self.assertTrue(receipt['args'][2].startswith(chr(92)*2+'?'+chr(92)))
        with patch.object(executor,'WindowsOwnedProcess',side_effect=AssertionError('Completed source recomputed')):
            adopted,again=executor.run_preparation(EXE,folder,SMALL,logical_cpu=4,memory_mib=128,timeout=20)
        self.assertEqual(adopted.receipt_sha256,bundle.receipt_sha256);self.assertEqual(again,receipt)

    def test_invalid_or_changed_scopes_never_enter_the_queue(self):
        for scopes in ([], [(3, 3)], [(True, 5)], [(3, 8), (7, 9)], [(3, 10**21 + 1)]):
            with self.subTest(scopes=scopes), self.assertRaises(ValueError):
                self.q.enqueue(scopes)
        identity = self.q.enqueue(SMALL)
        file = self.q._job(identity) / 'scopes.json'
        doc = json.loads(file.read_bytes())
        doc['scopes'][0][1] = str(N + 121)
        file.write_text(json.dumps(doc), encoding='utf-8')
        with self.assertRaises(ValueError):
            self.q.enqueue(SMALL)

    def test_capacity_preserves_prior_work(self):
        for i in range(queue.MAX_ITEMS):
            self.q.enqueue([(N + i * 1000, N + i * 1000 + 10)])
        before = self.q.index.read_bytes()
        with self.assertRaises(BufferError):
            self.q.enqueue([(N + 100000, N + 100010)])
        self.assertEqual(self.q.index.read_bytes(), before)
        with patch.object(queue, 'MAX_RETAINED_BYTES', 10), self.assertRaises(BufferError):
            queue.PreparationQueue(self.root / 'other').enqueue(SMALL)

    def test_busy_consumer_and_cancel_do_not_execute(self):
        self.q.enqueue(SMALL)
        held = SessionControllerLock(self.q.consumer)
        try:
            with patch.object(queue, 'run_preparation', side_effect=AssertionError('Duplicate consumer')):
                self.assertIsNone(self.q.pump('unused', logical_cpu=4))
        finally:
            held.release()
        with patch.object(queue, 'run_preparation', side_effect=AssertionError('Cancelled work ran')):
            with self.assertRaises(InterruptedError):
                self.q.pump('unused', logical_cpu=4, cancelled=lambda: True)
        self.assertEqual(self.q.entries()[0]['attempt'], 0)

    def test_ready_is_not_a_permission_to_recompute_missing_outputs(self):
        identity = self.q.enqueue(SMALL)
        self.q._update(identity, state='ready', attempt=1)
        with patch.object(queue, 'run_preparation', side_effect=AssertionError('Hidden execution')):
            with self.assertRaises(queue.PreparationRecoveryRequired):
                self.q.ready(identity, 'unused', logical_cpu=4)

    def test_alive_owned_identity_cannot_be_abandoned_or_killed(self):
        self.require_worker()
        identity = self.q.enqueue(SMALL)
        self.q._update(identity, state='running', attempt=1)
        attempt = self.q._job(identity) / 'attempt-1'
        attempt.mkdir()
        request = dict(image_sha256=queue.PRODUCER_SHA, args=['controlled-alive-process'])
        (attempt / 'request.json').write_text(json.dumps(request))
        kernel, types = _kernel()
        before = os.getpid()
        owner = dict(schema='PGN_CPU_PREPARATION_OWNER_V1', role='cpu_preparation', pid=before,
                     birth=windows_birth(kernel, types, kernel.GetCurrentProcess()),
                     image_sha256=queue.PRODUCER_SHA, command=request['args'],
                     configuration_sha256=queue.hashlib.sha256(queue.canonical(request)).hexdigest())
        (attempt / 'producer-owner.json').write_text(json.dumps(owner))
        with patch.object(queue, 'run_preparation', side_effect=AssertionError('Alive attempt duplicated')):
            with self.assertRaises(queue.PreparationRecoveryRequired):
                self.q.pump(EXE, logical_cpu=4)
        self.assertEqual(os.getpid(), before)
        self.assertFalse((attempt / 'abandoned.json').exists())
        self.assertEqual(self.q.entries()[0]['state'], 'failed')

    def test_actual_completion_restart_handoff_and_no_second_process(self):
        self.require_worker()
        identity = self.q.enqueue(SMALL)
        self.assertEqual(self.q.pump(EXE, logical_cpu=4, memory_mib=128, timeout=20), identity)
        q = queue.PreparationQueue(self.root)
        with patch.object(executor, 'WindowsOwnedProcess', side_effect=AssertionError('Completed source reran')):
            bundle, receipt = q.ready(identity, EXE, logical_cpu=4)
            self.assertEqual(len(bundle.witnesses[0]), 1)
            self.assertTrue(receipt['all_owned_jobs_drained'])
            self.assertFalse(receipt['GPU_used'])
            self.assertIsNone(q.pump(EXE, logical_cpu=4))
            q.acknowledge(identity)
            q.acknowledge(identity)
            self.assertEqual(q.entries(), [])
            self.assertTrue((q._job(identity) / 'attempt-1/output/COMPLETE.json').exists())
            self.assertEqual(q.enqueue(SMALL), identity)
            self.assertEqual(q.pump(EXE, logical_cpu=4), identity)

    def test_crash_after_receipt_before_index_recovers_without_execution(self):
        self.require_worker()
        identity = self.q.enqueue(SMALL)
        update = self.q._update
        def crash(identity, **changes):
            if changes.get('state') == 'ready':
                raise RuntimeError('Controlled crash after durable producer receipt')
            update(identity, **changes)
        with patch.object(self.q, '_update', side_effect=crash):
            with self.assertRaises(RuntimeError):
                self.q.pump(EXE, logical_cpu=4, memory_mib=128, timeout=20)
        with patch.object(executor, 'WindowsOwnedProcess', side_effect=AssertionError('Crash recovery duplicated CPU work')):
            self.assertEqual(queue.PreparationQueue(self.root).pump(EXE, logical_cpu=4), identity)
        self.assertEqual(self.q.entries()[0]['attempt'], 1)
        self.assertEqual(self.q.entries()[0]['state'], 'ready')

    def cancel_owned_attempt(self,identity):
        number=self.q.entries()[0]['attempt']+1
        attempt=self.q._job(identity)/('attempt-'+str(number))
        with self.assertRaises(executor.PreparationCancelled):
            self.q.pump(EXE,logical_cpu=4,memory_mib=128,timeout=20,
                        cancelled=lambda:(attempt/'producer-identity.json').exists())
        self.assertTrue(self.q._cancelled_attempt(attempt))
        owner=json.loads((attempt/'producer-owner.json').read_bytes())
        kernel,types=_kernel()
        self.assertFalse(queue._recorded_alive(kernel,types,owner))
        return attempt

    def test_three_owned_cancellations_then_completion_and_reuse(self):
        self.require_worker();identity=self.q.enqueue(SMALL);retained=[]
        for number in (1,2,3):
            attempt=self.cancel_owned_attempt(identity)
            self.assertEqual(self.q.entries()[0]['attempt'],number)
            retained.append((attempt/'cancelled.json',(attempt/'cancelled.json').read_bytes()))
        self.assertEqual(queue.PreparationQueue(self.root).pump(EXE,logical_cpu=4,memory_mib=128,timeout=20),identity)
        self.assertEqual(self.q.entries()[0]['attempt'],4)
        self.assertEqual(self.q.entries()[0]['state'],'ready')
        for file,data in retained:self.assertEqual(file.read_bytes(),data)
        # The real provider's terminal handoff must also understand every
        # cancelled attempt; otherwise successful recovery leaks a full cache.
        from horizon_preparation_retention import PreparationRetention
        retention=PreparationRetention(self.root.parent)
        plan='a'*64;plan_folder=retention.root/'plans'/plan;plan_folder.mkdir(parents=True)
        (plan_folder/'input.json').write_bytes(b'controlled immutable plan')
        (plan_folder/'job.json').write_bytes(queue.canonical(dict(job=identity)))
        targets=dict(plan=plan_folder,job=self.q._job(identity))
        files,_=retention._manifest(targets)
        self.assertTrue(any(name.endswith('attempt-3/cancelled.json')for name in files))

        with patch.object(executor,'WindowsOwnedProcess',side_effect=AssertionError('Completed work reran')):
            self.q.ready(identity,EXE,logical_cpu=4)

    def test_cancellation_does_not_erase_two_timeout_failures(self):
        self.require_worker();identity=self.q.enqueue(LARGE)
        self.cancel_owned_attempt(identity)
        for number in (2,3):
            with self.assertRaises(TimeoutError):
                self.q.pump(EXE,logical_cpu=4,memory_mib=128,timeout=.08)
            self.assertEqual(self.q.entries()[0]['attempt'],number)
        with patch.object(queue,'run_preparation',side_effect=AssertionError('Failure limit bypassed')):
            with self.assertRaises(queue.PreparationRecoveryRequired):self.q.pump(EXE,logical_cpu=4)
        self.assertEqual(self.q.entries()[0]['state'],'failed')

    def test_cancellation_receipt_requires_exact_retained_identity(self):
        self.require_worker();identity=self.q.enqueue(SMALL)
        attempt=self.cancel_owned_attempt(identity);marker=attempt/'cancelled.json'
        original=marker.read_bytes();doc=json.loads(original)
        for key,value in [('owner_sha256','0'*64),('request_sha256','1'*64),('schema','UNKNOWN')]:
            marker.write_bytes(queue.canonical(dict(doc,**{key:value})))
            self.assertFalse(self.q._cancelled_attempt(attempt))
        marker.write_bytes(original)
        owner=attempt/'producer-owner.json';owner.write_bytes(owner.read_bytes()+b' ')
        self.assertFalse(self.q._cancelled_attempt(attempt))

    def test_cancellations_remain_bounded_by_retained_attempt_count(self):
        self.require_worker();identity=self.q.enqueue(SMALL)
        with patch.object(queue,'MAX_ATTEMPT_RECORDS',2):
            self.cancel_owned_attempt(identity);self.cancel_owned_attempt(identity)
            with patch.object(queue,'run_preparation',side_effect=AssertionError('Unbounded restart')):
                with self.assertRaises(queue.PreparationRecoveryRequired):self.q.pump(EXE,logical_cpu=4)
        self.assertEqual(self.q.entries()[0]['state'],'failed')

    def test_two_real_timeouts_are_bounded_and_other_inputs_can_continue(self):
        self.require_worker()
        identity = self.q.enqueue(LARGE)
        added = []
        def enqueue_while_running():
            if not added:
                added.append(self.q.enqueue(SMALL))
            return False
        for expected_attempt in (1, 2):
            with self.assertRaises(TimeoutError):
                self.q.pump(EXE, logical_cpu=4, memory_mib=128, timeout=.08,
                            cancelled=enqueue_while_running)
            row = self.q.entries()[0]
            self.assertEqual(row['attempt'], expected_attempt)
            attempt = self.q._job(identity) / ('attempt-' + str(expected_attempt))
            owner = json.loads((attempt / 'producer-owner.json').read_bytes())
            self.assertEqual(owner['state'], 'OWNED_PROCESS_CLOSED')
            self.assertFalse((attempt / 'producer-receipt.json').exists())
        with self.assertRaises(queue.PreparationRecoveryRequired):
            self.q.pump(EXE, logical_cpu=4)
        self.assertEqual(self.q.entries()[0]['state'], 'failed')
        self.assertTrue((self.q._job(identity) / 'attempt-1/abandoned.json').exists())
        self.assertEqual(self.q.pump(EXE, logical_cpu=4, memory_mib=128, timeout=20), added[0])


if __name__ == '__main__':
    unittest.main()
