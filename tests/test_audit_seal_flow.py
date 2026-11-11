"""A terminal seal conflict must not become an invisible retrying GPU wait."""
from pathlib import Path
from types import SimpleNamespace
import sys
import threading
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'src/engine/runtime_host'), str(ROOT / 'src/protocol')]
from horizon_audit_answers import AuditSealRecoveryPending, AuditSealRecoveryRequired
from horizon_mining_flow import MiningFlow


class AuditSealFlowTests(unittest.TestCase):
    def flow(self):
        flow = object.__new__(MiningFlow)
        running, quitting = threading.Event(), threading.Event()
        running.set()
        self.messages = []
        self.notified = threading.Event()
        def message(detail, phase):
            self.messages.append((detail, phase))
            self.notified.set()
        flow.worker = SimpleNamespace(running=running, quitting=quitting, message=message)
        flow.cycles = SimpleNamespace(pending=lambda *_a, **_kw: 'cycle')
        return flow

    def test_unrecoverable_original_parks_only_lane_without_retrying(self):
        flow = self.flow()
        calls = []
        def process():
            calls.append('attempt')
            raise AuditSealRecoveryRequired('Original saved audit response missing; results retained')
        lane = SimpleNamespace(process_one=process)
        thread = threading.Thread(target=flow.audit_work, args=(lane, 0))
        thread.start()
        try:
            self.assertTrue(self.notified.wait(2), 'Recovery failure was not shown')
            self.assertEqual(self.messages[-1][1], 'Needs attention')
            self.assertIn('Original saved audit response missing', lane.last_error)
            self.assertTrue(flow.worker.running.is_set(), 'Sibling lanes were stopped')
            self.assertFalse(flow.worker.quitting.wait(.35))
            self.assertEqual(calls, ['attempt'], 'Terminal failure was retried')
            flow.worker.running.clear()
        finally:
            flow.worker.quitting.set()
            thread.join(2)
        self.assertFalse(thread.is_alive(), 'OFF/shutdown could not release the parked lane')

    def test_bounded_archive_recovery_continues_until_original_is_found(self):
        flow = self.flow()
        calls = []
        def process():
            calls.append('attempt')
            if len(calls) == 1:
                raise AuditSealRecoveryPending('Checking retained audit responses')
            flow.worker.quitting.set()
            return 'committed'
        lane = SimpleNamespace(process_one=process)
        thread = threading.Thread(target=flow.audit_work, args=(lane, 1))
        thread.start()
        thread.join(2)
        try:
            self.assertFalse(thread.is_alive())
            self.assertEqual(len(calls), 2)
            self.assertIsNone(lane.last_error)
            self.assertIsNone(lane.recovery_pending)
        finally:
            flow.worker.quitting.set()
            thread.join(2)

    def test_failure_before_current_assignment_keeps_real_cause_visible(self):
        flow = self.flow()
        flow.audit_lanes = [SimpleNamespace(current_audit=None, last_error='Original response conflicts with its report')]
        flow.waiting()
        self.assertEqual(self.messages, [('Audit 1: Original response conflicts with its report', 'Needs attention')])

    def test_recovery_before_current_assignment_has_explicit_status(self):
        flow = self.flow()
        flow.audit_lanes = [SimpleNamespace(current_audit=None, last_error=None,
                                           recovery_pending='Restoring retained response')]
        flow.waiting()
        self.assertEqual(self.messages, [('Audit 1: Restoring retained response', 'Recovering saved audit')])


if __name__ == '__main__':
    unittest.main()
