"""Adversarial report/audit lifecycle tests with an actual engine report fixture."""
from pathlib import Path
import concurrent.futures
import copy
import hashlib
import json
import sqlite3
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src/protocol"))
from report_commitment import seal_report, verify_report_reveal, audit_partitions, audit_commitment, digest
from audit_queue import AuditQueue, AuditHeld


def identity(label):
    return hashlib.sha256(label.encode()).hexdigest()


class Reports(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.computed = json.loads((ROOT / "tests/fixtures/network/computed-report.json").read_text())
        cls.work = json.loads((ROOT / "tests/fixtures/network/issued-work.json").read_text())

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.file = Path(self.temp.name) / "audits.sqlite"
        self.queue = AuditQueue(self.file)
        self.owner = identity("owner")
        self.actor = identity("independent auditor")
        self.sealed = seal_report(self.computed, self.work, nonce="11" * 32)
        self.confirmed = dict(request_id=self.work["request_id"], report_root=self.sealed["report_root"],
                              ticket=self.work["ticket"], block=identity("confirmation"), height=5)
        self.queue.register_confirmed(self.confirmed, self.owner, self.work, identity("future block 6"))
        self.addCleanup(self.temp.cleanup)
        self.addCleanup(lambda: self.queue.close())

    def claims(self):
        return [self.queue.claim(self.actor, 6), self.queue.claim(self.actor, 6)]

    def answers(self, jobs, wrong=False):
        counts = self.computed["body"]["counts"]
        result = []
        for job in jobs:
            values = [counts[i] for i in job["partitions"]]
            if wrong and job["slot"] == 0:
                values[0] = str(int(values[0]) + 1)
            result.append(audit_commitment(job["ticket"], job["attempt"], job["partitions"], values, "22" * 32))
        return result

    def commit_all(self, jobs, answers):
        for job, answer in zip(jobs, answers):
            self.queue.commit(job["request_id"], job["slot"], job["attempt"], self.actor, answer["commitment"], 7)

    def test_actual_report_and_hiding_nonce(self):
        self.assertEqual(sum(map(int, self.computed["body"]["counts"])), 731)
        self.assertEqual(len(self.computed["body"]["best"]), 100)
        self.assertEqual(verify_report_reveal(self.sealed, self.sealed["report_root"])["computed"], self.computed["body"])
        second = seal_report(self.computed, self.work, nonce="33" * 32)
        self.assertNotEqual(second["report_root"], self.sealed["report_root"])
        changed = copy.deepcopy(self.sealed)
        changed["body"]["computed"]["counts"][0] = "8"
        with self.assertRaises(ValueError):
            verify_report_reveal(changed, self.sealed["report_root"])

    def test_semantic_input_validation_beyond_hash_check(self):
        for mutate in (lambda b: b["counts"].pop(), lambda b: b["counts"].__setitem__(0, "-1"),
                       lambda b: b["best"].append(copy.deepcopy(b["best"][0])),
                       lambda b: b["best"][0].__setitem__("lower_prime", b["end"])):
            changed = copy.deepcopy(self.computed)
            mutate(changed["body"])
            changed["sha256"] = digest(changed["body"])
            with self.assertRaises(ValueError):
                seal_report(changed, self.work)

    def test_ten_unique_reproducible_partitions(self):
        for number in range(1000):
            seed = identity(str(number))
            parts = audit_partitions(self.sealed["report_root"], seed)
            self.assertEqual(parts, audit_partitions(self.sealed["report_root"], seed))
            self.assertEqual([len(p) for p in parts], [5, 5])
            self.assertEqual(len(set(parts[0] + parts[1])), 10)
            self.assertTrue(all(0 <= n < 100 for group in parts for n in group))

    def test_single_miner_remains_pending_and_has_no_own_audits(self):
        self.assertIsNone(self.queue.claim(self.owner, 6))
        state = self.queue.status(self.work["request_id"])
        self.assertEqual(state["state"], "PENDING_AUDITS")
        self.assertFalse(state["scientific_accepted"])
        self.assertFalse(state["payment_authorized"])

    def test_assignments_expose_no_expected_answers(self):
        jobs = self.claims()
        self.assertIsNone(self.queue.claim(identity("third auditor"), 6))
        self.assertEqual(len({i for job in jobs for i in job["partitions"]}), 10)
        for job in jobs:
            self.assertFalse({"counts", "expected", "computed", "commitment_nonce"} & set(job))
            self.assertEqual(len(job["ranges"]), 5)
            self.assertTrue(all(int(r["end"]) > int(r["start"]) for r in job["ranges"]))

    def test_both_commit_before_any_owner_answer_is_revealed(self):
        jobs = self.claims()
        answers = self.answers(jobs)
        with self.assertRaises(AuditHeld):
            self.queue.reveal_owner_report(self.work["request_id"], self.sealed)
        first = jobs[0]
        self.queue.commit(first["request_id"], first["slot"], first["attempt"], self.actor, answers[0]["commitment"], 7)
        with self.assertRaises(AuditHeld):
            self.queue.reveal_owner_report(self.work["request_id"], self.sealed)
        second = jobs[1]
        self.queue.commit(second["request_id"], second["slot"], second["attempt"], self.actor, answers[1]["commitment"], 7)
        self.queue.reveal_owner_report(self.work["request_id"], self.sealed)
        for job, answer in zip(jobs, answers):
            self.assertTrue(self.queue.reveal_audit(job["request_id"], job["slot"], job["attempt"], self.actor, answer))
        state = self.queue.status(self.work["request_id"])
        self.assertEqual(state["state"], "AUDITED")
        self.assertFalse(state["scientific_accepted"])
        self.assertFalse(state["payment_authorized"])

    def test_wrong_answer_is_disputed_and_cannot_be_overwritten(self):
        jobs = self.claims()
        answers = self.answers(jobs, wrong=True)
        self.commit_all(jobs, answers)
        self.queue.reveal_owner_report(self.work["request_id"], self.sealed)
        job = jobs[0]
        self.assertFalse(self.queue.reveal_audit(job["request_id"], 0, 0, self.actor, answers[0]))
        self.assertEqual(self.queue.status(job["request_id"])["state"], "DISPUTED")
        with self.assertRaises(AuditHeld):
            self.queue.reveal_audit(job["request_id"], 0, 0, self.actor, self.answers(jobs)[0])

    def test_restart_preserves_commitments_and_no_double_completion(self):
        jobs = self.claims()
        answers = self.answers(jobs)
        self.commit_all(jobs, answers)
        self.queue.close()
        self.queue = AuditQueue(self.file)
        self.assertFalse(self.queue.commit(jobs[0]["request_id"], 0, 0, self.actor, answers[0]["commitment"], 8))
        self.queue.reveal_owner_report(self.work["request_id"], self.sealed)
        for job, answer in zip(jobs, answers):
            self.assertTrue(self.queue.reveal_audit(job["request_id"], job["slot"], 0, self.actor, answer))
            self.assertTrue(self.queue.reveal_audit(job["request_id"], job["slot"], 0, self.actor, answer))
        self.assertEqual(len(self.queue.status(self.work["request_id"])["jobs"]), 2)
        self.assertEqual(self.queue.db.execute("PRAGMA integrity_check").fetchone()[0], "ok")

    def test_expiry_reassigns_without_accepting_old_attempt(self):
        old = self.queue.claim(self.actor, 6, lease_blocks=2)
        self.assertEqual(self.queue.expire(7), 0)
        self.assertEqual(self.queue.expire(8), 1)
        new = self.queue.claim(identity("replacement auditor"), 8)
        self.assertEqual(new["slot"], old["slot"])
        self.assertEqual(new["attempt"], 1)
        with self.assertRaises(AuditHeld):
            self.queue.commit(old["request_id"], old["slot"], 0, self.actor, identity("late answer"), 8)

    def test_orphan_is_withdrawn_and_same_branch_can_be_restored(self):
        self.queue.reconcile_branch([])
        self.assertIsNone(self.queue.claim(self.actor, 6))
        with self.assertRaises(AuditHeld):
            self.queue.status(self.work["request_id"])
        self.assertFalse(self.queue.register_confirmed(self.confirmed, self.owner, self.work, identity("future block 6")))
        self.assertIsNotNone(self.queue.claim(self.actor, 6))

    def test_concurrent_claims_are_exclusive(self):
        def claim(number):
            queue = AuditQueue(self.file)
            try:
                return queue.claim(identity("auditor " + str(number)), 6)
            finally:
                queue.close()
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
            jobs = [job for job in pool.map(claim, range(8)) if job]
        self.assertEqual(len(jobs), 2)
        self.assertEqual(len({(j["slot"], j["attempt"]) for j in jobs}), 2)

    def test_disk_full_rolls_back_without_losing_existing_reports(self):
        before = self.queue.pending()
        pages = self.queue.db.execute("PRAGMA page_count").fetchone()[0]
        self.queue.db.execute("PRAGMA max_page_count=" + str(pages))
        with self.assertRaises(sqlite3.OperationalError):
            with self.queue.transaction():
                for n in range(10000):
                    self.queue.db.execute("INSERT INTO reports VALUES(?,?,?,?,?,?,?,?,?,?,NULL)",
                        (identity(str(n)), identity("root"), identity("ticket"), self.owner, "2", "100", identity("block"), 5, identity("seed"), "PENDING_AUDITS"))
        self.assertEqual(self.queue.pending(), before)
        self.assertLessEqual(self.file.stat().st_size, pages * self.queue.db.execute("PRAGMA page_size").fetchone()[0])


if __name__ == "__main__":
    unittest.main()
