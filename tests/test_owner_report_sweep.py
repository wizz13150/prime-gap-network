"""Durable fair sweep state, transient failures and restart conservation."""
from pathlib import Path
import json
import sys
import tempfile
import unittest
P = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(P/'src/engine/runtime_host'), str(P/'src/protocol'), str(P/'tests')]
from horizon_owner_reports import OwnerReports
from horizon_network_audits import NetworkAudits
from report_commitment import canonical_bytes
import test_owner_report_queue as fixture


def anchor(i=1):
    return dict(hash=f'{i:064x}', height=i, genesis='a'*64)


class OwnerSweepTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.queue = OwnerReports(Path(self.tmp.name))

    def fill(self, first=1, last=501):
        values = {fixture.identity(i):dict(ticket=fixture.identity(i+10000), report_root=fixture.identity(i+20000)) for i in range(first, last)}
        rows = list(values.items())
        for offset in range(0, len(rows), 4096):
            self.queue.register_batch(dict(rows[offset:offset+4096]))
        return values

    def drain(self, context, limit=64):
        seen, calls = [], 0
        while True:
            rows, more = self.queue.next_on_anchor(context, limit)
            calls += 1
            self.assertLessEqual(len(rows), limit)
            seen.extend(request for request, _ in rows)
            if not more:
                return seen, calls
            self.assertLess(calls, 1000)

    def test_5000_rows_once_per_tip_not_continuous_full_scans(self):
        values = self.fill(last=5001)
        seen, calls = self.drain(anchor())
        self.assertEqual(len(seen), 5000)
        self.assertEqual(set(seen), set(values))
        self.assertEqual(calls, 79)
        for _ in range(10):
            self.assertEqual(self.queue.next_on_anchor(anchor()), ([], False))
        other, _ = self.drain(anchor(2))
        self.assertEqual(set(other), set(values))

    def test_wrap_from_nonzero_cursor_covers_exactly_one_circle(self):
        values = self.fill(last=131)
        self.queue.next_page(31)
        seen, _ = self.drain(anchor(), 16)
        self.assertEqual(len(seen), len(values))
        self.assertEqual(set(seen), set(values))

    def test_new_registration_reopens_same_tip_without_resetting_fairness(self):
        self.fill(10, 110)
        self.drain(anchor())
        self.queue.register(fixture.identity(1), dict(ticket='b'*64, report_root='c'*64))
        seen, _ = self.drain(anchor())
        self.assertIn(fixture.identity(1), seen)
        self.assertEqual(len(seen), 101)

    def test_changing_tip_every_step_does_not_pin_first_page(self):
        values = self.fill(last=501)
        seen = set()
        for i in range(1, 12):
            rows, _ = self.queue.next_on_anchor(anchor(i))
            seen.update(dict(rows))
        self.assertEqual(seen, set(values))

    def test_restart_continues_a_partial_sweep(self):
        self.fill(last=201)
        first, _ = self.queue.next_on_anchor(anchor())
        self.queue = OwnerReports(self.queue.root)
        second, _ = self.queue.next_on_anchor(anchor())
        self.assertFalse(set(dict(first)) & set(dict(second)))

    def test_bad_generation_and_cursor_fail_without_losing_rows(self):
        self.fill(last=3)
        with self.queue.database() as db:
            db.execute("INSERT OR REPLACE INTO meta VALUES('generation','-1')")
        with self.assertRaises(ValueError):
            self.queue.next_on_anchor(anchor())
        with self.queue.database() as db:
            self.assertEqual(db.execute('SELECT COUNT(*) FROM reports').fetchone()[0], 2)
            db.execute("INSERT OR REPLACE INTO meta VALUES('generation','1')")
            db.execute("INSERT OR REPLACE INTO meta VALUES('sweep','{}')")
        with self.assertRaises(ValueError):
            self.queue.next_on_anchor(anchor())

    def client_case(self):
        case = fixture.OwnerReportQueueTests()
        case.setUp()
        self.addCleanup(case.doCleanups)
        return case

    def test_client_restart_retries_page_scheduled_before_crash(self):
        case = self.client_case()
        request, item, _ = case.completed(1)
        case.queue.register(request, item)
        context = case.queue.observe(case.rpc)
        case.queue.next_on_anchor(context)  # Crash here, before any RPC/send.
        self.assertEqual(case.queue.next_on_anchor(context), ([], False))
        restarted = NetworkAudits(case.worker)
        restarted.reveal_owner_reports()
        self.assertEqual(case.sent, [request])
        self.assertEqual(case.count(1), 0)

    def test_transient_mixed_tip_view_retries_even_when_tip_returns(self):
        case = self.client_case()
        request, item, _ = case.completed(1)
        case.queue.register(request, item)
        original = case.client.states
        case.client.states = lambda requests: None
        case.client.reveal_owner_reports()
        self.assertTrue(case.client.owner_poll_more)
        case.client.states = original
        case.client.reveal_owner_reports()
        self.assertEqual(case.sent, [request])
        self.assertEqual(case.count(1), 0)

    def test_repaired_local_payload_is_retried_without_requiring_new_block(self):
        case = self.client_case()
        request, item, sealed = case.completed(1)
        case.queue.register(request, item)
        path = case.root/'tickets'/item['ticket']/'network-report.json'
        original = path.read_bytes()
        sealed['body']['computed']['counts'][0] = '2'
        path.write_bytes(canonical_bytes(sealed))
        with self.assertRaises(ValueError):
            case.client.reveal_owner_reports()
        path.write_bytes(original)
        case.client.reveal_owner_reports()
        self.assertEqual(case.sent, [request])
        self.assertEqual(case.count(1), 0)

    def test_no_repeated_state_page_lookup_on_unchanged_tip(self):
        case = self.client_case()
        case.queue.register(fixture.identity(1), case.item(1))
        case.client.reveal_owner_reports()
        calls = len(case.rpc.page_lengths)
        for _ in range(10):
            case.client.reveal_owner_reports()
        self.assertEqual(len(case.rpc.page_lengths), calls)


if __name__ == '__main__':
    unittest.main()
