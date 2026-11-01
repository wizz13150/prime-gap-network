"""Owned snapshot clock tests; no Core, GPU, wallet or filesystem writes."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src/engine/runtime_host"))
from horizon_compute_lease import computation_lease_active
from horizon_ticket_request import signed_request_parameters


class ComputeLeaseClock(unittest.TestCase):
    def setUp(self):
        self.page = {"height": 101, "median_time_past": 1001}
        self.height = {"state": "assigned", "issued": 100, "deadline": 244}
        self.timed = {"state": "assigned", "issued": 100,
                      "deadline_clock": "median_chain_time", "issued_time": 1000,
                      "deadline_time": 87400}

    def test_legacy_boundary(self):
        self.assertTrue(computation_lease_active({"height": 243}, self.height))
        self.assertFalse(computation_lease_active({"height": 244}, self.height))

    def test_explicit_legacy_clock(self):
        self.height["deadline_clock"] = "ledger_height"
        self.assertTrue(computation_lease_active(self.page, self.height))

    def test_fast_records_do_not_expire_time_lease(self):
        self.page["height"] = (1 << 31) - 1
        self.assertTrue(computation_lease_active(self.page, self.timed))

    def test_time_boundary(self):
        self.page["median_time_past"] = 87399
        self.assertTrue(computation_lease_active(self.page, self.timed))
        self.page["median_time_past"] = 87400
        self.assertFalse(computation_lease_active(self.page, self.timed))

    def test_abandoned_for_fifteen_days(self):
        self.page["median_time_past"] = 1000 + 15 * 86400
        self.assertFalse(computation_lease_active(self.page, self.timed))

    def test_missing_time_has_no_height_substitute(self):
        del self.page["median_time_past"]
        with self.assertRaises(ValueError):
            computation_lease_active(self.page, self.timed)

    def test_unknown_clock(self):
        self.timed["deadline_clock"] = "unknown"
        with self.assertRaises(ValueError):
            computation_lease_active(self.page, self.timed)

    def test_invalid_counters(self):
        for value in (True, -1, 1.0, "101", None, 1 << 63):
            with self.subTest(value=value), self.assertRaises(ValueError):
                computation_lease_active({"height": value}, self.height)

    def test_future_issue_rejected(self):
        with self.assertRaises(ValueError):
            computation_lease_active({"height": 99}, self.height)
        self.page["median_time_past"] = 999
        with self.assertRaises(ValueError):
            computation_lease_active(self.page, self.timed)

    def test_nonpositive_duration_rejected(self):
        self.timed["deadline_time"] = self.timed["issued_time"]
        with self.assertRaises(ValueError):
            computation_lease_active(self.page, self.timed)

    def test_submitted_report_is_not_a_computation_lease(self):
        report = dict(self.timed, state="submitted_pending_audits")
        before = copy.deepcopy(report)
        self.page["median_time_past"] = (1 << 63) - 1
        self.assertFalse(computation_lease_active(self.page, report))
        self.assertEqual(report, before)


class SignedRequestAnchor(unittest.TestCase):
    def setUp(self):
        self.request = {"schema": "PGN_TICKET_REQUEST_RPC_V2", "anchor": "12" * 32,
                        "request_body_version": 2, "deadline_clock": "median_chain_time",
                        "lease_seconds": 86400}

    def test_original_transport_unchanged(self):
        self.assertEqual(signed_request_parameters({}, "miner", "reward", "sig"), ["miner", "reward", "sig"])

    def test_anchor_is_reused(self):
        self.assertEqual(signed_request_parameters(self.request, "miner", "reward", "sig"), ["miner", "reward", "sig", "12" * 32])

    def test_new_rpc_can_pin_legacy_body(self):
        self.request.update(request_body_version=1, deadline_clock="ledger_height", lease_seconds=0)
        self.assertEqual(signed_request_parameters(self.request, "m", "r", "s")[-1], "12" * 32)

    def test_incomplete_or_future_contract_rejected(self):
        for changed in (dict(self.request, schema=None), dict(self.request, schema="future")):
            with self.assertRaises(ValueError):
                signed_request_parameters(changed, "m", "r", "s")

    def test_wrong_clock_or_body_rejected(self):
        for changed in (dict(self.request, deadline_clock="ledger_height"), dict(self.request, request_body_version=True), dict(self.request, lease_seconds=True), dict(self.request, lease_seconds=0)):
            with self.assertRaises(ValueError):
                signed_request_parameters(changed, "m", "r", "s")

    def test_anchor_is_exact_canonical_identity(self):
        for anchor in (None, "0" * 64, "AB" * 32, "12" * 31):
            with self.subTest(anchor=anchor), self.assertRaises(ValueError):
                signed_request_parameters(dict(self.request, anchor=anchor), "m", "r", "s")


if __name__ == "__main__":
    unittest.main()
