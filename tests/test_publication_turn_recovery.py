"""Actual durable owner queue and audit publisher under interrupted sends."""
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
import json
import sys
import threading
import tempfile
import unittest

P = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(P/'src/engine/runtime_host'), str(P/'src/protocol'), str(P/'tests')]
from horizon_network_audits import NetworkAudits
from horizon_coordination import CoordinationParentChanged, PublicationPending
from horizon_network_audits import save
from report_commitment import audit_commitment
from test_owner_report_queue import OwnerReportQueueTests


class OwnerTurnTests(OwnerReportQueueTests):
    # Reuse real SQLite and sealed reports, not a model of their implementation.
    def populate(self, count):
        for number in range(1, count+1):
            request, item, _ = self.completed(number)
            self.queue.register(request, item)

    def test_unresolved_high_merit_does_not_starve_deferred_turn_after_restart(self):
        self.populate(10)
        self.worker.submit_gold = lambda request, *args: self.sent.append(request) or 'pending_gold'
        self.client.reveal_owner_reports()
        self.assertEqual(len(self.sent), 4)
        self.assertEqual(len(self.queue.deferred_publication_turn()), 6)
        self.client = NetworkAudits(self.worker)
        self.queue = self.client.owner_reports
        self.client.reveal_owner_reports()
        self.assertEqual(len(self.sent), 8)
        self.client.reveal_owner_reports()
        self.assertEqual(len(set(self.sent)), 10)
        self.assertEqual(self.count(1), 10)
        self.assertTrue(all((self.root/'tickets'/self.item(i)['ticket']/'network-report.json').exists()
                            for i in range(1, 11)))

    def test_slow_send_yields_and_retains_every_unvisited_report(self):
        self.populate(3)
        clock = [0.0]
        def slow(request, *args):
            self.sent.append(request)
            clock[0] += 31.0
            return 'pending_gold'
        self.worker.submit_gold = slow
        with patch('horizon_network_audits.time.monotonic', lambda: clock[0]):
            self.client.reveal_owner_reports()
        self.assertEqual(len(self.sent), 1)
        self.assertEqual(len(self.queue.deferred_publication_turn()), 2)
        self.assertTrue(self.client.owner_poll_more)
        self.assertEqual(self.count(1), 3)

    def test_confirmation_only_acknowledges_visited_reports(self):
        self.populate(9)
        originals = {p: p.read_bytes() for p in (self.root/'tickets').glob('*/network-report.json')}
        for expected in (5, 1, 0):
            self.client.reveal_owner_reports()
            self.assertEqual(self.count(1), expected)
        self.assertEqual(len(set(self.sent)), 9)
        self.assertTrue(all(p.read_bytes() == raw for p, raw in originals.items()))

    def test_failed_deferred_write_cannot_acknowledge_unvisited_results(self):
        self.populate(6)
        self.worker.submit_gold = lambda *args: 'pending_gold'
        with patch.object(self.queue, 'retain_publication_turn', side_effect=OSError('disk full')):
            with self.assertRaises(OSError):
                self.client.reveal_owner_reports()
        self.assertEqual(self.count(1), 6)
        self.client = NetworkAudits(self.worker)
        self.client.reveal_owner_reports()
        self.assertEqual(self.count(1), 6)

    def test_deferred_identity_tamper_is_not_sent(self):
        self.populate(5)
        self.worker.submit_gold = lambda *args: 'pending_gold'
        self.client.reveal_owner_reports()
        with self.queue.database() as db:
            raw = json.loads(db.execute("SELECT value FROM meta WHERE key='publication_turn'").fetchone()[0])
            raw[0][1]['report_root'] = 'f'*64
            db.execute("UPDATE meta SET value=? WHERE key='publication_turn'", (json.dumps(raw),))
        with self.assertRaisesRegex(ValueError, 'immutable'):
            self.client.reveal_owner_reports()
        self.assertEqual(self.count(1), 5)


class AuditSemanticRetryTests(unittest.TestCase):
    def test_completed_audit_retries_publication_without_reentering_any_gpu_partition(self):
        with tempfile.TemporaryDirectory() as temporary:
            running = threading.Event(); running.set()
            def no_compute(*args): self.fail('completed audit was recomputed')
            worker = SimpleNamespace(root=Path(temporary), running=running, quitting=threading.Event(),
                network_lock=threading.RLock(), args=SimpleNamespace(piece_limit=1),
                source=SimpleNamespace(run_ticket=no_compute))
            client = NetworkAudits(worker)
            answer = audit_commitment('b'*64, 0, list(range(5)), ['7']*5, 'c'*64)
            audit = dict(request_id='a'*64, ticket='b'*64, report_root='d'*64, slot=0, attempt=0,
                         ranges=[dict(index=i, start=str(100+20*i), end=str(120+20*i)) for i in range(5)], counts=[7]*5,
                         target_merit_q16=21*65536, answer=answer)
            save(client.folder/'current.json', audit)
            client.state = lambda _: dict(jobs=[dict(slot=0, attempt=0, state='assigned')])
            calls = []
            def send(*args, **kwargs):
                calls.append(args)
                if len(calls) == 1: raise PublicationPending('reply lost')
                return dict(available=True)
            client.apply = send
            with self.assertRaises(PublicationPending): client.process_one()
            self.assertTrue((client.folder/'current.json').exists())
            original = (client.folder/('a'*64+'-0-0.json')).read_bytes()
            self.assertEqual(client.process_one(), 'committed')
            self.assertFalse((client.folder/'current.json').exists())
            self.assertEqual((client.folder/('a'*64+'-0-0.json')).read_bytes(), original)
            self.assertEqual(calls[0], calls[1])

    def fixture(self):
        class Lane:
            entries = 0
            depth = 0
            def __enter__(self):
                self.entries += 1
                self.depth += 1
            def __exit__(self, *args): self.depth -= 1
        lock = Lane()
        running = threading.Event(); running.set()
        worker = SimpleNamespace(network_lock=lock, running=running, quitting=threading.Event())
        audit = NetworkAudits.__new__(NetworkAudits); audit.worker = worker
        events = []
        def event(*args):
            self.assertEqual(lock.depth, 1)
            events.append(args)
            return dict(available=True, envelope='fresh-'+str(len(events)))
        audit.event = event
        return audit, worker, lock, events

    def test_parent_race_reauthorizes_same_semantic_body_in_new_lane_turn(self):
        audit, worker, lock, events = self.fixture()
        sends = []
        def ledger(envelopes, **kwargs):
            sends.append(envelopes)
            if len(sends) == 1: raise CoordinationParentChanged('parent advanced')
            return dict(block_hash='b'*64)
        worker.ledger = ledger
        result = audit.apply('commit', 'a'*64, 0, 2, 'c'*64)
        self.assertEqual(result['confirmed_block'], 'b'*64)
        self.assertEqual(events[0], events[1])
        self.assertEqual(sends, [['fresh-1'], ['fresh-2']])
        self.assertEqual(lock.entries, 2)
        self.assertEqual(lock.depth, 0)

    def test_continuous_parent_changes_yield_after_two_attempts(self):
        audit, worker, lock, events = self.fixture()
        def changed(*args, **kwargs): raise CoordinationParentChanged('parent advanced')
        worker.ledger = changed
        with self.assertRaises(CoordinationParentChanged): audit.apply('commit', 'a'*64, 0, 2, 'c'*64)
        self.assertEqual(len(events), 2)
        self.assertEqual(lock.depth, 0)

    def test_off_cancels_parent_retry_without_a_false_ack(self):
        audit, worker, lock, events = self.fixture()
        def changed(*args, **kwargs):
            worker.running.clear()
            raise CoordinationParentChanged('parent advanced')
        worker.ledger = changed
        with self.assertRaises(CoordinationParentChanged): audit.apply('commit', 'a'*64, 0, 2, 'c'*64)
        self.assertEqual(len(events), 1)

    def test_unknown_submission_is_not_blindly_retried_as_new_operation(self):
        audit, worker, lock, events = self.fixture()
        def unknown(*args, **kwargs): raise PublicationPending('reply lost; candidate retained')
        worker.ledger = unknown
        with self.assertRaises(PublicationPending): audit.apply('commit', 'a'*64, 0, 2, 'c'*64)
        self.assertEqual(len(events), 1)

    def test_permanent_rejection_is_not_relabelled_as_parent_race(self):
        audit, worker, lock, events = self.fixture()
        def rejected(*args, **kwargs): raise ValueError('wrong signature')
        worker.ledger = rejected
        with self.assertRaisesRegex(ValueError, 'wrong signature'): audit.apply('commit', 'a'*64, 0, 2, 'c'*64)
        self.assertEqual(len(events), 1)


if __name__ == '__main__': unittest.main()
