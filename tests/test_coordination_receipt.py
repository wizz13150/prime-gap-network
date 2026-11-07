"""Retained publication receipts, not a block-validation or mining fixture."""
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
import hashlib
import json
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src/protocol'))
sys.path.insert(0, str(ROOT / 'src/engine/runtime_host'))
from horizon_network_worker import NetworkWorker, save


class CoordinationReceiptTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.worker = NetworkWorker.__new__(NetworkWorker)
        self.worker.root = Path(self.temp.name)
        self.worker.args = SimpleNamespace(pilot_coordination=True)
        self.calls = []
        def call(method, params):
            self.calls.append(method)
            return dict(block=(b'\0' * 112 + b'\1').hex(), numeric_target='ff' * 32, parent='b' * 64)
        self.worker.rpc = SimpleNamespace(call=call)

    def retained(self, envelopes, confirmed=True):
        digest = hashlib.sha256(json.dumps(envelopes, separators=(',', ':')).encode()).hexdigest()
        saved = dict(hash='a' * 64, parent='b' * 64, block='00' * 113,
                     operation_sha256=digest, confirmed=confirmed)
        save(self.worker.root / 'coordination-intent.json', saved)
        return saved

    def test_confirmed_signed_operation_reuses_exact_block_after_revalidation(self):
        original = self.retained(['6a01'])
        with patch('horizon_network_worker.confirm_candidate') as confirm:
            result = self.worker.ledger(['6a01'])
        self.assertEqual(result['block_hash'], original['hash'])
        self.assertEqual(confirm.call_count, 1)
        self.assertEqual(self.calls, [])

    def test_failed_revalidation_keeps_same_candidate_unconfirmed(self):
        original = self.retained(['6a01'])
        with patch('horizon_network_worker.confirm_candidate', side_effect=RuntimeError('Changed active branch')):
            with self.assertRaises(RuntimeError): self.worker.ledger(['6a01'])
        retained = json.loads((self.worker.root / 'coordination-intent.json').read_bytes())
        self.assertEqual(retained['hash'], original['hash'])
        self.assertEqual(retained['block'], original['block'])
        self.assertFalse(retained['confirmed'])
        self.assertEqual(self.calls, [])

    def test_confirmed_empty_tick_still_advances_instead_of_reusing_forever(self):
        original = self.retained([])
        with patch('horizon_network_worker.confirm_candidate'):
            result = self.worker.ledger([])
        self.assertNotEqual(result['block_hash'], original['hash'])
        self.assertEqual(self.calls, ['getpgnbootstraptemplate'])

    def test_unconfirmed_empty_tick_is_recovered_exactly_once(self):
        original = self.retained([], confirmed=False)
        with patch('horizon_network_worker.confirm_candidate'):
            result = self.worker.ledger([])
        self.assertEqual(result['block_hash'], original['hash'])
        self.assertEqual(self.calls, [])

    def test_new_operations_callback_never_adopts_another_retained_operation(self):
        previous = self.retained(['6a01'], confirmed=False)
        snapshots = []
        with patch('horizon_network_worker.confirm_candidate') as confirm:
            result = self.worker.ledger(['6a02'], on_intent=lambda value: snapshots.append(dict(value)))
        self.assertEqual(confirm.call_count, 2)
        self.assertEqual(len(snapshots), 1)
        self.assertNotEqual(snapshots[0]['operation_sha256'], previous['operation_sha256'])
        self.assertEqual(snapshots[0]['hash'], result['block_hash'])


if __name__ == '__main__': unittest.main()
