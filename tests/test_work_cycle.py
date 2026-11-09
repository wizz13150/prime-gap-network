"""Durable normal-cycle ordering only; no RPC, GPU, audit or reward acceptance."""
from pathlib import Path
import copy
import json
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src/engine/runtime_host'))
sys.path.insert(0, str(ROOT / 'src/protocol'))
from horizon_work_cycle import WorkCycleJournal


def work(number=1):
    return dict(genesis='a' * 64, request_id=f'{number:064x}', ticket=f'{number + 100:064x}',
                ticket_number=str(number), start='150000000000000000000', length='200000000000000',
                target_numerator='1846001', target_denominator='65536')


def task(number=200, slot=0):
    return dict(stage='assigned', request=f'{number:064x}', slot=slot, attempt=0)


class WorkCycleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        self.journal = WorkCycleJournal(self.folder, 'a' * 64)
        self.addCleanup(lambda: self.journal.close())
        self.work = work()
        self.request = self.work['request_id']
        self.journal.prepare(self.work)

    def attach_pair(self):
        for index in range(2):
            self.journal.attach(self.request, index, task(200 + index, index))

    def publish(self, index):
        item = task(200 + index, index)
        self.journal.published(self.request, index, item['request'], item['slot'], item['attempt'], f'{300 + index:064x}')

    def test_audit_one_then_two_then_search(self):
        self.attach_pair()
        self.assertTrue(self.journal.may_audit(self.request, 0))
        self.assertFalse(self.journal.may_audit(self.request, 1))
        self.assertFalse(self.journal.may_search(self.request))
        with self.assertRaises(ValueError): self.journal.advance(self.request, 'running')
        self.publish(0)
        self.assertTrue(self.journal.may_audit(self.request, 1))
        self.assertFalse(self.journal.may_search(self.request))
        self.publish(1)
        self.assertTrue(self.journal.may_search(self.request))
        self.journal.advance(self.request, 'running')
        self.journal.advance(self.request, 'completed')

    def test_catch_up_is_two_whole_audits_not_partitions_and_survives_restart(self):
        self.attach_pair(); self.publish(0); self.publish(1)
        self.journal.advance(self.request, 'running')
        first, second, third = ('c' * 64, 'd' * 64, 'e' * 64)
        for _ in range(5):
            self.journal.reserve_extra_audit(self.request, first)
        self.journal.reserve_extra_audit(self.request, second)
        self.assertFalse(self.journal.may_extra_audit(self.request, third))
        with self.assertRaises(BufferError): self.journal.reserve_extra_audit(self.request, third)
        self.journal.close(); self.journal = WorkCycleJournal(self.folder, 'a' * 64)
        self.assertTrue(self.journal.may_extra_audit(self.request, first))
        self.assertEqual(self.journal.extra_audit_cycle(first), self.request)
        self.assertFalse(self.journal.may_extra_audit(self.request, third))
        self.journal.prepare(work(2))
        self.assertTrue(self.journal.may_extra_audit(work(2)['request_id'], third))

    def test_lab_empty_regular_slots_allow_two_regular_plus_two_catch_up(self):
        empty = dict(stage='unavailable', anchor='b' * 64, reason='no_eligible_independent_work')
        for index in range(2): self.journal.attach(self.request, index, empty)
        for number in range(4): self.journal.reserve_extra_audit(self.request, f'{700 + number:064x}')
        self.assertFalse(self.journal.may_extra_audit(self.request, f'{704:064x}'))
        self.assertTrue(self.journal.may_search(self.request))

    def test_budget_write_failure_does_not_spend_or_erase_audit_allowance(self):
        before = self.journal.file.read_bytes()
        with patch('horizon_work_cycle.atomic_bytes', side_effect=OSError('Disk full')):
            with self.assertRaises(OSError): self.journal.reserve_extra_audit(self.request, 'c' * 64)
        self.assertEqual(self.journal.file.read_bytes(), before)
        self.assertNotIn('extra_audits', self.journal.snapshot(self.request))
        self.assertEqual(self.journal.extra_audits(self.request), [])

    def test_admission_keeps_released_cycle_wire_format_and_no_partition_rewrites(self):
        before = self.journal.file.read_bytes()
        self.journal.reserve_extra_audit(self.request, 'c' * 64)
        self.assertEqual(self.journal.file.read_bytes(), before)
        with patch('horizon_work_cycle.atomic_bytes', side_effect=AssertionError('No partition rewrite')):
            self.journal.reserve_extra_audit(self.request, 'c' * 64)
        # Contract of released engine41, independent of the new reader.
        for cycle in json.loads(self.journal.file.read_bytes())['cycles']:
            self.assertIn(set(cycle), ({'work','stage','audits','hold'},
                                      {'work','stage','audits','hold','native_service'}))

    def test_private42_budget_migration_survives_interrupted_cycle_rewrite(self):
        from horizon_work_cycle import atomic_bytes as real_atomic
        self.journal.close()
        saved = json.loads(self.journal.file.read_bytes())
        saved['cycles'][0]['extra_audits'] = ['c' * 64, 'd' * 64]
        self.journal.file.write_text(json.dumps(saved))
        def fail_cycle(path, data):
            if path == self.journal.file: raise OSError('Interrupted after budget commit')
            return real_atomic(path, data)
        with patch('horizon_work_cycle.atomic_bytes', side_effect=fail_cycle):
            with self.assertRaises(OSError): WorkCycleJournal(self.folder, 'a' * 64)
        self.journal = WorkCycleJournal(self.folder, 'a' * 64)
        self.assertEqual(self.journal.extra_audits(self.request), ['c' * 64, 'd' * 64])
        self.assertFalse(self.journal.may_extra_audit(self.request, 'e' * 64))
        self.assertNotIn('extra_audits', json.loads(self.journal.file.read_bytes())['cycles'][0])

    def test_foreign_sidecar_cannot_reset_an_existing_budget(self):
        self.journal.reserve_extra_audit(self.request, 'c' * 64)
        self.journal.close()
        path = self.folder / 'audit-execution.json'
        value = json.loads(path.read_bytes()); value['genesis'] = 'b' * 64
        path.write_text(json.dumps(value))
        with self.assertRaises(ValueError): WorkCycleJournal(self.folder, 'a' * 64)

    def test_recovery_keeps_first_confirmation_and_second_binding(self):
        self.attach_pair(); self.publish(0); self.journal.close()
        self.journal = WorkCycleJournal(self.folder, 'a' * 64)
        self.assertFalse(self.journal.prepare(self.work))
        self.assertEqual(self.journal.snapshot(self.request)['audits'][1], task(201, 1))
        self.assertTrue(self.journal.may_audit(self.request, 1))
        self.publish(0); self.publish(1)
        self.assertTrue(self.journal.may_search(self.request))

    def test_network_error_or_permission_denial_cannot_become_no_work(self):
        for reason in ('connection_lost', 'auditor_not_registered', 'audit_permission_disabled'):
            with self.subTest(reason=reason), self.assertRaises(ValueError):
                self.journal.attach(self.request, 0, dict(stage='unavailable', reason=reason, anchor='b' * 64))
        self.assertEqual(self.journal.snapshot(self.request)['audits'], [None, None])

    def test_explicit_empty_eligible_pool_allows_bootstrap_without_audit_credit(self):
        unavailable = dict(stage='unavailable', anchor='b' * 64, reason='no_eligible_independent_work')
        for index in range(2): self.journal.attach(self.request, index, unavailable)
        self.assertTrue(self.journal.may_search(self.request))
        self.assertTrue(all(item['stage'] != 'committed' for item in self.journal.snapshot(self.request)['audits']))

    def test_own_report_duplicate_report_and_changed_attempt_are_refused(self):
        with self.assertRaises(ValueError): self.journal.attach(self.request, 0, task(1))
        self.journal.attach(self.request, 0, task())
        with self.assertRaises(ValueError): self.journal.attach(self.request, 1, task(slot=1))
        with self.assertRaises(ValueError): self.journal.attach(self.request, 0, dict(task(), attempt=1))
        with self.assertRaises(ValueError): self.journal.published(self.request, 0, task()['request'], 0, 1, 'b' * 64)

    def test_changed_work_and_wrong_chain_never_rewrite_retained_work(self):
        before = self.journal.file.read_bytes()
        for changed in (dict(self.work, start='150000000000000000002'), dict(self.work, genesis='b' * 64)):
            with self.assertRaises(ValueError): self.journal.prepare(changed)
        self.assertEqual(self.journal.file.read_bytes(), before)

    def test_ambiguous_journal_and_foreign_genesis_fail_closed(self):
        self.journal.close()
        before = self.journal.file.read_bytes()
        with self.assertRaises(ValueError): WorkCycleJournal(self.folder, 'b' * 64)
        self.journal.file.write_bytes(before.replace(b'"cycles":', b'"cycles":[],"cycles":', 1))
        with self.assertRaises(ValueError): WorkCycleJournal(self.folder, 'a' * 64)
        self.journal.file.write_bytes(before)

    def test_disk_write_failure_preserves_memory_and_disk(self):
        before = self.journal.file.read_bytes()
        with patch('horizon_work_cycle.atomic_bytes', side_effect=OSError('Injected disk full')):
            with self.assertRaises(OSError): self.journal.attach(self.request, 0, task())
        self.assertEqual(self.journal.file.read_bytes(), before)
        self.assertEqual(self.journal.snapshot(self.request)['audits'], [None, None])

    def test_single_writer_and_closed_writer(self):
        with self.assertRaises(RuntimeError): WorkCycleJournal(self.folder, 'a' * 64)
        self.journal.close()
        with self.assertRaises(RuntimeError): self.journal.prepare(work(2))

    def test_capacity_backpressure_keeps_unfinished_cycles(self):
        for number in range(2, 5): self.journal.prepare(work(number))
        before = self.journal.file.read_bytes()
        with self.assertRaises(BufferError): self.journal.prepare(work(5))
        self.assertEqual(self.journal.file.read_bytes(), before)

    def test_hold_after_start_blocks_completion_and_survives_restart(self):
        self.attach_pair(); self.publish(0); self.publish(1)
        self.journal.advance(self.request, 'running')
        self.journal.hold(self.request, 'Audit confirmation changed branch')
        self.journal.close(); self.journal = WorkCycleJournal(self.folder, 'a' * 64)
        with self.assertRaises(ValueError): self.journal.advance(self.request, 'completed')
        self.assertEqual(self.journal.snapshot(self.request)['hold'], 'Audit confirmation changed branch')

    def test_only_public_cycle_metadata_is_persisted(self):
        changed = dict(work(2), expected_counts=[123] * 100, private_key='must not persist')
        self.journal.prepare(changed)
        raw = self.journal.file.read_text()
        self.assertNotIn('expected_counts', raw); self.assertNotIn('private_key', raw)
        self.assertLess(len(raw.encode()), self.journal.maximum_bytes)

    def test_permission_wait_survives_restart_and_requires_fresh_verified_binding(self):
        self.journal.hold_permission(self.request, 'auditor_not_registered')
        self.journal.close(); self.journal = WorkCycleJournal(self.folder, 'a' * 64)
        self.assertIsNone(self.journal.pending(0))
        self.assertEqual(self.journal.pending(0, retry_permissions=True), self.request)
        self.assertFalse(self.journal.may_search(self.request))
        with self.assertRaises(RuntimeError): self.journal.attach(self.request, 0, task())
        self.journal.attach(self.request, 0, task(), permission_verified=True)
        self.assertTrue(self.journal.may_audit(self.request, 0))
        self.assertFalse(self.journal.may_search(self.request))

    def test_branch_reconciliation_hold_cannot_be_downgraded_to_permission_wait(self):
        self.journal.hold(self.request, 'Audit confirmation changed branch')
        before = self.journal.file.read_bytes()
        self.assertIsNone(self.journal.pending(0, retry_permissions=True))
        with self.assertRaises(RuntimeError): self.journal.hold_permission(self.request, 'audit_permission_disabled')
        with self.assertRaises(RuntimeError): self.journal.attach(self.request, 0, task(), permission_verified=True)
        self.assertEqual(self.journal.file.read_bytes(), before)

    def test_permission_retry_does_not_turn_transport_error_into_empty_work(self):
        self.journal.hold_permission(self.request, 'audit_permission_disabled')
        before = self.journal.file.read_bytes()
        with self.assertRaises(ValueError):
            self.journal.attach(self.request, 0, dict(stage='unavailable', reason='connection_lost', anchor='b' * 64), permission_verified=True)
        self.assertEqual(self.journal.file.read_bytes(), before)
        self.assertFalse(self.journal.may_search(self.request))

    def test_failed_permission_recovery_write_retains_the_hold(self):
        self.journal.hold_permission(self.request, 'auditor_not_registered')
        before = self.journal.file.read_bytes()
        with patch('horizon_work_cycle.atomic_bytes', side_effect=OSError('Injected disk full')):
            with self.assertRaises(OSError): self.journal.attach(self.request, 0, task(), permission_verified=True)
        self.assertEqual(self.journal.file.read_bytes(), before)
        self.assertTrue(self.journal.snapshot(self.request)['hold'])


if __name__ == '__main__': unittest.main()
