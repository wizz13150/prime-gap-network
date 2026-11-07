"""Actual private SQLite queue/client; controlled RPC, no Core or devices."""
from pathlib import Path
from types import SimpleNamespace
from contextlib import closing
import hashlib
import json
import sqlite3
import sys
import tempfile
import unittest

P = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(P/'src/engine/runtime_host'), str(P/'src/protocol'), str(P/'tests')]
from horizon_owner_reports import OwnerReports
from horizon_network_audits import NetworkAudits
from report_commitment import canonical_bytes
from audit_publication_chain_fixture import ChainRPC


def identity(i):
    return f'{i:064x}'


class OwnerRPC(ChainRPC):
    def __init__(self):
        super().__init__()
        self.states = {}
        self.page_lengths = []

    def call(self, method, parameters=None, **kwargs):
        if method == 'getpgnauditstate':
            group = parameters[3]
            self.page_lengths.append(len(group))
            return [dict(request_id=request, **self.states[request]) for request in group if request in self.states]
        result = super().call(method, parameters, **kwargs)
        if method == 'getpgnworkview':
            result['audits'] = [dict(request_id=parameters[3], **self.states.get(parameters[3], {}))]
        return result


class OwnerReportQueueTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.rpc = OwnerRPC()
        self.sent = []
        self.worker = SimpleNamespace(root=self.root, rpc=self.rpc,
                                      submit_gold=lambda request, sealed, details: self.sent.append(request) or 'no_gold')
        self.client = NetworkAudits(self.worker)
        self.queue = self.client.owner_reports

    def count(self, waiting=None):
        with self.queue.database() as db:
            if waiting is None:
                return db.execute('SELECT COUNT(*) FROM reports').fetchone()[0]
            return db.execute('SELECT COUNT(*) FROM reports WHERE waiting=?', (waiting,)).fetchone()[0]

    def item(self, i):
        return dict(ticket=identity(i+10000), report_root=identity(i+20000))

    def completed(self, i):
        request, item = identity(i), self.item(i)
        body = dict(ticket=item['ticket'], computed=dict(counts=['1']+['0']*99, best=[]))
        item['report_root'] = hashlib.sha256(canonical_bytes(body)).hexdigest()
        folder = self.root/'tickets'/item['ticket']
        folder.mkdir(parents=True)
        sealed = dict(report_root=item['report_root'], body=body)
        (folder/'network-report.json').write_bytes(canonical_bytes(sealed))
        self.rpc.states[request] = dict(audited=True, details_root='a'*64, jobs=[])
        return request, item, sealed

    def test_5000_pending_owners_do_not_pin_later_ready_report(self):
        values = {identity(i):self.item(i) for i in range(1, 5001)}
        request, item, sealed = self.completed(5000)
        values[request] = item
        self.queue.register_batch(dict(list(values.items())[:4096]))
        self.queue.register_batch(dict(list(values.items())[4096:]))
        visited = set()
        original = self.client.states
        def states(requests):
            requests = list(requests)
            visited.update(requests)
            return original(requests)
        self.client.states = states
        for _ in range(79):
            self.client.reveal_owner_reports()
        self.assertEqual(visited, set(values))
        self.assertEqual(self.sent, [request])
        self.assertEqual(self.count(), 5000)
        self.assertEqual(self.count(1), 4999)
        self.assertLessEqual(max(self.rpc.page_lengths), 64)
        self.assertLess(self.queue.file.stat().st_size, 4*1024*1024)
        self.assertEqual(json.loads((self.root/'tickets'/item['ticket']/'network-report.json').read_bytes()), sealed)

    def test_registration_is_immutable_and_transactional(self):
        self.queue.register(identity(1), self.item(1))
        bad = self.item(1)
        bad['report_root'] = 'f'*64
        with self.assertRaises(ValueError):
            self.queue.register_batch({identity(2):self.item(2), identity(1):bad})
        self.assertEqual(self.count(), 1)

    def test_cursor_survives_restart_and_wraps_to_new_lower_id(self):
        self.queue.register_batch({identity(i):self.item(i) for i in range(2, 131)})
        first = self.queue.next_page()
        other = OwnerReports(self.root)
        second = other.next_page()
        self.assertFalse(set(dict(first)) & set(dict(second)))
        other.register(identity(1), self.item(1))
        third = dict(other.next_page())
        self.assertIn(identity(130), third)
        self.assertIn(identity(1), third)

    def test_legacy_import_is_idempotent_preserves_original_and_ack(self):
        source = self.root/'pending-owner-reports.json'
        raw = canonical_bytes({identity(1):self.item(1)})
        source.write_bytes(raw)
        self.queue.import_legacy()
        self.queue.acknowledge(identity(1), self.item(1))
        self.queue.import_legacy()
        self.assertEqual(self.count(), 1)
        self.assertEqual(self.count(1), 0)
        self.assertEqual(source.read_bytes(), raw)
        source.write_bytes(canonical_bytes({identity(1):self.item(1), identity(2):self.item(2)}))
        self.queue.import_legacy()
        self.assertEqual(self.count(), 2)
        self.assertEqual(self.count(1), 1)

    def test_reorg_requeues_acknowledged_identity_without_recomputing_or_deletion(self):
        self.queue.register(identity(1), self.item(1))
        self.queue.observe(self.rpc)
        self.queue.acknowledge(identity(1), self.item(1))
        self.rpc.extend()
        self.queue.observe(self.rpc)
        self.assertEqual(self.count(1), 0)
        self.rpc.fork()
        self.queue.observe(self.rpc)
        self.assertEqual(dict(self.queue.next_page()), {identity(1):self.item(1)})

    def test_wrong_network_and_invalid_cursor_do_not_acknowledge(self):
        self.queue.register(identity(1), self.item(1))
        self.queue.observe(self.rpc)
        self.rpc.chain[0] = 'f'*64
        with self.assertRaises(ValueError):
            self.queue.observe(self.rpc)
        self.assertEqual(self.count(1), 1)
        with self.queue.database() as db:
            db.execute("INSERT OR REPLACE INTO meta VALUES('waiting_cursor','../../foreign')")
        with self.assertRaises(ValueError):
            self.queue.next_page()

    def test_storage_budget_failure_preserves_prior_rows(self):
        self.queue = OwnerReports(self.root, max_pages=8)
        self.queue.register(identity(1), self.item(1))
        with self.assertRaises(sqlite3.DatabaseError):
            self.queue.register_batch({identity(i):self.item(i) for i in range(2, 1002)})
        self.assertEqual(self.count(), 1)
        self.assertEqual(dict(self.queue.next_page()), {identity(1):self.item(1)})
        self.assertLessEqual(self.queue.file.stat().st_size, 8*4096)

    def test_wrong_atomic_anchor_retains_ready_report_until_confirmed(self):
        request, item, sealed = self.completed(1)
        self.client.track_owner_report(dict(request_id=request, ticket=item['ticket']), sealed)
        self.rpc.view_tip = 'f'*64
        self.client.reveal_owner_reports()
        self.assertEqual(self.count(1), 1)
        del self.rpc.view_tip
        self.client.reveal_owner_reports()
        self.assertEqual(self.count(1), 0)

    def test_tampered_report_body_cannot_be_acknowledged_as_no_gold(self):
        request, item, sealed = self.completed(1)
        self.queue.register(request, item)
        sealed['body']['computed']['counts'][0] = '2'
        (self.root/'tickets'/item['ticket']/'network-report.json').write_bytes(canonical_bytes(sealed))
        with self.assertRaises(ValueError):
            self.client.reveal_owner_reports()
        self.assertEqual(self.sent, [])
        self.assertEqual(self.count(1), 1)

    def test_invalid_registration_and_empty_queue_do_not_send_rpc(self):
        for request, item in [('../bad', self.item(1)), (identity(0), self.item(1)), (identity(1), dict(ticket='../bad', report_root='a'*64)),
                              (identity(1), dict(ticket='a'*64, report_root='b'*64, extra='bad'))]:
            with self.assertRaises(ValueError):
                self.queue.register(request, item)
        self.client.reveal_owner_reports()
        self.assertEqual(self.rpc.calls, [])

    def test_foreign_sqlite_is_refused_without_mutation(self):
        with closing(sqlite3.connect(self.queue.file)) as db, db:
            db.execute('CREATE TABLE unrelated(value TEXT)')
            db.execute("INSERT INTO unrelated VALUES('keep')")
        before = self.queue.file.read_bytes()
        with self.assertRaises(ValueError):
            self.queue.register(identity(1), self.item(1))
        self.assertEqual(self.queue.file.read_bytes(), before)

    def test_noncanonical_page_size_does_not_bypass_byte_budget(self):
        with closing(sqlite3.connect(self.queue.file)) as db, db:
            db.execute('PRAGMA page_size=8192')
            db.execute(f'PRAGMA application_id={OwnerReports.application_id}')
            db.execute('CREATE TABLE meta(key TEXT PRIMARY KEY,value TEXT NOT NULL)')
        before = self.queue.file.read_bytes()
        with self.assertRaises(ValueError):
            self.queue.register(identity(1), self.item(1))
        self.assertEqual(self.queue.file.read_bytes(), before)


if __name__ == '__main__':
    unittest.main()
