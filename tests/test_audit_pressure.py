from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src/engine/runtime_host"))
from horizon_audit_pressure import AuditPressure, WeightedService, SCALE


class AuditPressureTests(unittest.TestCase):
    def test_normal_and_huge_backlog_bounds(self):
        policy = AuditPressure(clock=lambda: 0.)
        self.assertAlmostEqual(policy.share, 1 / 11)
        weights = policy.update(1_000_000_000_000, 10_000, 0.)
        self.assertEqual(weights["audit"], 800_000)
        self.assertEqual(weights["search"], 200_000)

    def test_age_detects_missing_registered_capacity(self):
        policy = AuditPressure(clock=lambda: 0.)
        policy.update(1, 100_000, 2400.)
        self.assertEqual(policy.weights()["audit"], 800_000)

    def test_recovery_is_gradual_and_clock_guarded(self):
        clock = [0.]
        policy = AuditPressure(clock=lambda: clock[0])
        policy.update(100_000, 10, 0.)
        clock[0] = 1.
        policy.update(0, 10, 0.)
        self.assertGreater(policy.share, .7)
        clock[0] = 600.
        policy.update(0, 10, 0.)
        self.assertLess(policy.share, .091)
        clock[0] = 599.
        with self.assertRaises(ValueError): policy.update(0, 10, 0.)

    def test_invalid_measurements_are_not_zero_backlog(self):
        policy = AuditPressure()
        for values in [(-1, 1, 0.), (1, -1, 0.), (True, 1, 0.), (1, 1, float("nan")),
                       (1 << 64, 1, 0.), (1, 1 << 32, 0.)]:
            with self.assertRaises(ValueError): policy.update(*values)

    def test_actual_time_fairness_with_unequal_job_sizes(self):
        service = WeightedService({"audit": 800_000, "search": 200_000})
        spent = {"audit": 0, "search": 0}
        for _ in range(20_000):
            role = service.choose(True, True)
            elapsed = 2 if role == "audit" else 200
            service.charge(role, elapsed)
            spent[role] += elapsed
        fraction = spent["audit"] / sum(spent.values())
        self.assertLess(abs(fraction - .8), .01)
        self.assertGreater(spent["search"], 0)

    def test_idle_lane_does_not_bank_a_history_of_priority(self):
        service = WeightedService({"audit": 500_000, "search": 500_000})
        for _ in range(100_000):
            service.choose(True, False)
            service.charge("search", 200)
        audits = 0
        while service.choose(True, True) == "audit":
            service.charge("audit", 2)
            audits += 1
            self.assertLessEqual(audits, 101)

    def test_only_available_lane_keeps_working(self):
        service = WeightedService({"audit": 800_000, "search": 200_000})
        self.assertIsNone(service.choose(False, False))
        self.assertEqual(service.choose(True, False), "search")
        self.assertEqual(service.choose(False, True), "audit")

    def test_safe_yield_decision(self):
        service = WeightedService({"audit": 800_000, "search": 200_000})
        self.assertTrue(service.should_yield("search", 1, True))
        self.assertFalse(service.should_yield("search", 1, False))
        with self.assertRaises(ValueError): service.charge("search", -1)

    def test_large_stock_drains_in_shared_work_model(self):
        # Closed work model: every search creates two audit obligations;
        # the ordinary-worker pool serves both classes. Assignment validity
        # is checked elsewhere. This is not a network load test.
        policy = AuditPressure(clock=lambda: 0.)
        service = WeightedService(policy.update(100_000, 12, 3600.))
        old = pending = 100_000
        searches = audits = 0
        for turn in range(200_000):
            if turn % 100 == 0:
                service.update_weights(policy.update(pending, 12, 3600. if old else 0.))
            role = service.choose(True, pending > 0)
            if role == "search":
                searches += 1; pending += 2; service.charge(role, 200)
            else:
                audits += 1; pending -= 1; old = max(0, old - 1); service.charge(role, 10)
            if not old: break
        self.assertEqual(old, 0)
        self.assertGreater(searches, 0)
        self.assertGreater(audits, searches * 2)
        self.assertLess(pending, 100_000)


if __name__ == "__main__": unittest.main()
