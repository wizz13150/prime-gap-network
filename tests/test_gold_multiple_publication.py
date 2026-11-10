"""Per-discovery lifecycle, failure isolation and immutable owner migration."""
from pathlib import Path
import json
import sys
import unittest
from unittest.mock import patch
P = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(P/'src/engine/runtime_host'), str(P/'src/protocol'), str(P/'tests')]
from test_gold_publication_retry import GoldPublicationRetryTests
from test_owner_report_queue import OwnerReportQueueTests, identity
import horizon_network_worker as nw


class MultipleGoldTests(GoldPublicationRetryTests):
    def setUp(self):
        super().setUp()
        self.original_call = super().call
        self.built = []; self.block_for = {}; self.lower_for = {}; self.current_lower = None
        self.second = '150000000000000000101'
        self.sealed['body']['computed']['best'].append(dict(gap=1800, lower_prime=self.second))
        def build(builder, work, computed, lower):
            self.built.append(lower); self.current_lower = lower
            return dict(details_root='d'*64)
        patch.object(nw, 'build_gap_proof', side_effect=build).start()
    def call(self, method, parameters=None, **kw):
        if method == 'getpgnscientifictemplate':
            block = self.block_for.setdefault(self.current_lower, f'{len(self.block_for)+1:064x}')
            self.lower_for[block] = self.current_lower
            return dict(work_verified=True,winner_atomic='123',block=block,hash=block)
        if method == 'submitblock':
            self.sent += 1; self.confirmed[parameters[0]] = 1; return None
        return self.original_call(method, parameters, **kw)
    # The inherited single-discovery contract is covered by the unchanged
    # original class. This subclass exercises two distinct accepted facts.
    def test_retry_after_success_does_not_submit_twice(self):
        self.assertEqual(self.submit(), 'pending_gold')
        self.assertEqual(self.submit(), 'accepted')
        for _ in range(3): self.assertEqual(self.submit(), 'accepted')
        self.assertEqual(self.sent, 2); self.assertEqual(len(set(self.built)), 2)
    def test_crash_after_acceptance_recovers_from_intent(self):
        self.fail_after_submit = True
        with self.assertRaises(OSError): self.submit()
        self.assertEqual(self.submit(), 'accepted')
        self.assertEqual(self.sent, 2); self.assertEqual(len(self.built), 2)
    def test_orphaned_receipt_is_not_treated_as_active_acceptance(self):
        self.submit(); self.submit()
        first = next(iter(self.block_for)); self.confirmed[self.block_for[first]] = -1
        self.assertEqual(self.submit(), 'accepted'); self.assertEqual(self.sent, 3)
        self.assertEqual(self.built.count(first), 2)
    def test_failed_strongest_does_not_starve_next_discovery(self):
        with patch.object(nw, 'build_gap_proof', side_effect=RuntimeError('unsupported first proof')):
            with self.assertRaisesRegex(RuntimeError, 'unsupported'): self.submit()
        self.assertEqual(self.submit(), 'pending_gold')
        self.assertEqual(self.built, [self.second])
        self.assertEqual(self.submit(), 'accepted'); self.assertEqual(self.sent, 2)
    def test_unknown_header_is_not_a_false_acceptance(self):
        self.submit(); self.submit()
        original = self.call
        class Failed:
            def call(_, method, parameters=None, **kw):
                if method == 'getblockheader': raise RuntimeError('authentication unavailable')
                return original(method, parameters, **kw)
        self.worker.rpc = Failed()
        with self.assertRaisesRegex(RuntimeError, 'authentication'): self.submit()
        self.assertEqual(self.sent, 2)
    def test_foreign_lower_receipt_never_skips_a_discovery(self):
        self.submit(); path = self.folder/'network-acceptance.json'
        receipt = json.loads(path.read_bytes()); receipt['lower_prime'] = self.second
        path.write_text(json.dumps(receipt))
        with self.assertRaisesRegex(ValueError, 'discovery'): self.submit()
        self.assertEqual(self.sent, 1)
    def test_duplicate_declaration_is_not_published(self):
        self.sealed['body']['computed']['best'].append(self.sealed['body']['computed']['best'][0])
        with self.assertRaisesRegex(ValueError, 'Duplicate'): self.submit()
        self.assertEqual(self.sent, 0)


class OwnerGoldMigrationTests(OwnerReportQueueTests):
    def test_old_terminal_reports_reopen_only_once(self):
        request, item, sealed = self.completed(1)
        self.queue.register(request, item); self.queue.acknowledge(request, item)
        with self.queue.database() as db:
            db.execute("DELETE FROM meta WHERE key='gold_publication_version'")
        self.queue.reopen_for_gold_publications(); self.assertEqual(self.count(1), 1)
        self.queue.acknowledge(request, item)
        self.queue.reopen_for_gold_publications(); self.assertEqual(self.count(1), 0)
        self.assertEqual(self.count(), 1)
    def test_pending_discovery_does_not_acknowledge_report(self):
        request, item, sealed = self.completed(1); self.queue.register(request, item)
        self.worker.submit_gold = lambda *args: 'pending_gold'
        self.client.reveal_owner_reports(); self.assertEqual(self.count(1), 1)
        self.worker.submit_gold = lambda *args: 'accepted'
        self.client.reveal_owner_reports(); self.assertEqual(self.count(1), 0)


if __name__ == '__main__': unittest.main()
