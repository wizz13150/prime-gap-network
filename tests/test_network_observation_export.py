from pathlib import Path
import copy
import importlib.util
import json
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('observation_export', ROOT / 'tools/export_network_observations.py')
export = importlib.util.module_from_spec(spec)
spec.loader.exec_module(export)
GENESIS, TIP = 'a' * 64, 'b' * 64


def snapshot():
    return dict(schema='PGN_NETWORK_METRICS_V1', genesis=GENESIS, bestblockhash=TIP,
                height=12, median_time_past=1000, window_seconds=7200, span_seconds=86400,
                rate_scope='reported_search_positions_per_chain_second_excludes_audits',
                audit_validation_scope='historical_reports_qualified_at_snapshot_tip',
                report_count=7, audited_report_count=0, reported_positions='9007199254740993',
                audited_positions='0', reporting_operators_in_window=1, audit_enabled_operators=1,
                audit_queue=dict(awaiting=14, assigned=0, committed=0, passed=0, failed=0,
                                 awaiting_seed=0, compute_positions_pending='140000000000000',
                                 oldest_compute_pending_age_seconds=100),
                points=[dict(time=900, height=10, hash='c' * 64, merit_target_q16=29 * 65536,
                             gold_count=0, window_seconds=7200, reported_positions='9007199254740993',
                             audited_positions='0')])


class Rpc:
    def __init__(self):
        self.value = snapshot()
        self.calls = []
        self.moving = False

    def call(self, method, arguments=None):
        self.calls.append(method)
        if method == 'getpgnnetworkmetrics':
            return copy.deepcopy(self.value)
        if method == 'getbestblockhash':
            return 'd' * 64 if self.moving else TIP
        raise AssertionError('Observation performed a non-read operation: ' + method)


class ObservationExportTests(unittest.TestCase):
    def setUp(self):
        self.rpc = Rpc()

    def test_zero_audited_work_is_unknown_not_zero_hardware_capacity(self):
        result = export.collect(self.rpc, GENESIS)
        self.assertEqual(result['qualification']['independent_capacity_signal'], 'UNKNOWN')
        self.assertEqual(result['native_metrics']['reported_positions'], '9007199254740993')
        self.assertFalse(result['qualification']['computation_time_authenticated'])
        self.assertFalse(result['mining_or_consensus_changed'])

    def test_snapshot_tip_changes_cannot_promote_mixed_history(self):
        self.rpc.moving = True
        with self.assertRaises(RuntimeError):
            export.collect(self.rpc, GENESIS)
        self.assertEqual(self.rpc.calls.count('getpgnnetworkmetrics'), 3)

    def test_foreign_chain_and_unknown_scope_are_rejected(self):
        self.rpc.value['genesis'] = 'e' * 64
        with self.assertRaises(ValueError):
            export.collect(self.rpc, GENESIS)
        self.rpc.value = snapshot()
        self.rpc.value['rate_scope'] = 'includes_audits_and_cpu_shadow'
        with self.assertRaises(ValueError):
            export.collect(self.rpc, GENESIS)

    def test_float_or_noncanonical_positions_cannot_lose_precision(self):
        for value in (9007199254740993, 9.0, '01', '1e20', '-1', True):
            self.rpc.value = snapshot()
            self.rpc.value['reported_positions'] = value
            with self.assertRaises(ValueError):
                export.collect(self.rpc, GENESIS)

    def test_audited_volume_or_count_cannot_exceed_claims(self):
        self.rpc.value['audited_report_count'] = 8
        with self.assertRaises(ValueError):
            export.collect(self.rpc, GENESIS)
        self.rpc.value = snapshot()
        self.rpc.value['points'][0]['audited_positions'] = '9007199254740994'
        with self.assertRaises(ValueError):
            export.collect(self.rpc, GENESIS)

    def test_unknown_private_fields_are_not_exported(self):
        self.rpc.value['wallet_private_key'] = 'MUST_NOT_LEAVE'
        self.rpc.value['points'][0]['expected_audit_counts'] = 'MUST_NOT_LEAVE'
        self.rpc.value['audit_queue']['cookie'] = 'MUST_NOT_LEAVE'
        result = export.collect(self.rpc, GENESIS)
        self.assertNotIn('MUST_NOT_LEAVE', json.dumps(result))

    def test_export_never_overwrites_an_existing_user_file(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'receipt.json'
            path.write_text('existing user data')
            with self.assertRaises(FileExistsError):
                export.write_export(path, export.collect(self.rpc, GENESIS))
            self.assertEqual(path.read_text(), 'existing user data')
            self.assertEqual(len(list(Path(directory).iterdir())), 1)

    def test_published_receipt_is_complete_and_bounded(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'receipt.json'
            export.write_export(path, export.collect(self.rpc, GENESIS))
            self.assertEqual(json.loads(path.read_bytes())['snapshot_tip'], TIP)
            self.assertLess(path.stat().st_size, 32768)
            self.assertEqual(len(list(Path(directory).iterdir())), 1)


if __name__ == '__main__':
    unittest.main()
