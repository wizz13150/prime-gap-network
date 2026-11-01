"""Bounded, transactional audit coordination projected from confirmed reports.

Callers must authenticate participants and source report metadata from their core.
This queue cannot accept a scientific result or authorize a monetary payment.
"""
from contextlib import contextmanager
import json
from pathlib import Path
import sqlite3

from report_commitment import audit_partitions, canonical_bytes, decimal, digest, hash_text
from report_commitment import verify_audit_reveal, verify_report_reveal


class AuditHeld(RuntimeError):
    pass


class AuditQueue:
    def __init__(self, file, main_limit=24 * 1024 * 1024):
        self.file = Path(file).resolve()
        self.file.parent.mkdir(parents=True, exist_ok=True)
        if main_limit < 1024 * 1024 or main_limit > 24 * 1024 * 1024:
            raise ValueError("Explicit bounded audit projection required")
        if self.file.exists() and self.file.stat().st_size > main_limit:
            raise AuditHeld("Existing audit projection exceeds its admitted budget")
        self.db = sqlite3.connect(self.file, timeout=2, isolation_level=None)
        self.db.row_factory = sqlite3.Row
        identity = self.db.execute("PRAGMA application_id").fetchone()[0]
        if identity not in (0, 0x50474E41):
            self.db.close()
            raise AuditHeld("Foreign SQLite application identity")
        self.db.execute("PRAGMA application_id=1346850369")
        self.db.execute("PRAGMA journal_mode=DELETE")
        self.db.execute("PRAGMA synchronous=FULL")
        self.db.execute("PRAGMA foreign_keys=ON")
        page = self.db.execute("PRAGMA page_size").fetchone()[0]
        self.db.execute("PRAGMA max_page_count=" + str(main_limit // page))
        self.db.executescript("""
        CREATE TABLE IF NOT EXISTS reports(
          request TEXT PRIMARY KEY, root TEXT NOT NULL, ticket TEXT NOT NULL,
          owner TEXT NOT NULL, start TEXT NOT NULL, length TEXT NOT NULL,
          block TEXT NOT NULL, height INTEGER NOT NULL, seed TEXT NOT NULL,
          state TEXT NOT NULL, expected TEXT);
        CREATE TABLE IF NOT EXISTS jobs(
          request TEXT NOT NULL REFERENCES reports(request), slot INTEGER NOT NULL,
          attempt INTEGER NOT NULL, actor TEXT NOT NULL, parts TEXT NOT NULL,
          issued INTEGER NOT NULL, deadline INTEGER NOT NULL, state TEXT NOT NULL,
          commitment TEXT, reveal TEXT, PRIMARY KEY(request,slot,attempt));
        CREATE INDEX IF NOT EXISTS audit_report_state ON reports(state,height);
        CREATE INDEX IF NOT EXISTS audit_job_state ON jobs(request,slot,state);
        """)

    @contextmanager
    def transaction(self):
        self.db.execute("BEGIN IMMEDIATE")
        try:
            yield
            self.db.execute("COMMIT")
        except BaseException:
            if self.db.in_transaction:
                self.db.execute("ROLLBACK")
            raise

    def close(self):
        self.db.close()

    def register_confirmed(self, report, owner, work, seed_block):
        """Project an authenticated commitment; the caller supplies no expected counts."""
        request, root, ticket = (hash_text(report[key]) for key in ("request_id", "report_root", "ticket"))
        hash_text(owner)
        seed_block = hash_text(seed_block)
        block = hash_text(report["block"])
        if type(report["height"]) is not int or report["height"] < 1:
            raise ValueError("Confirmed report height required")
        if work["request_id"] != request or work["ticket"] != ticket:
            raise ValueError("Wrong issued work for report")
        start, length = decimal(work["start"]), decimal(work["length"])
        if length < 100 or start + length > 1 << 95:
            raise ValueError("Invalid report range")
        with self.transaction():
            old = self.db.execute("SELECT * FROM reports WHERE request=?", (request,)).fetchone()
            if old:
                immutable = (old["root"], old["ticket"], old["owner"], old["start"], old["length"])
                if immutable != (root, ticket, owner, str(start), str(length)):
                    raise AuditHeld("Confirmed report identity changed; reconcile the branch first")
                if old["block"] != block or old["seed"] != seed_block:
                    raise AuditHeld("Changed confirmation or seed requires a new audit generation")
                if old["state"] == "ORPHANED":
                    states = [self.db.execute("SELECT state FROM jobs WHERE request=? AND slot=? ORDER BY attempt DESC LIMIT 1", (request, slot)).fetchone() for slot in range(2)]
                    state = "AUDITED" if all(row and row[0] == "PASSED" for row in states) else "DISPUTED" if any(row and row[0] == "FAILED" for row in states) else "PENDING_AUDITS"
                    self.db.execute("UPDATE reports SET state=? WHERE request=?", (state, request))
                return False
            self.db.execute("INSERT INTO reports VALUES(?,?,?,?,?,?,?,?,?,?,NULL)",
                            (request, root, ticket, owner, str(start), str(length), block,
                             report["height"], seed_block, "PENDING_AUDITS"))
            return True

    def pending(self):
        return [dict(row) for row in self.db.execute(
            "SELECT request,root,ticket,height,state FROM reports WHERE state NOT IN ('ORPHANED','AUDITED') ORDER BY height,request")]

    def _report(self, request):
        row = self.db.execute("SELECT * FROM reports WHERE request=?", (hash_text(request),)).fetchone()
        if not row or row["state"] == "ORPHANED":
            raise AuditHeld("Report is not on the confirmed branch")
        return row

    def claim(self, actor, height, lease_blocks=12):
        """Atomically reserve one audit after participant authentication.

        The same independent auditor may run both disjoint tasks; it never audits
        its own report. Two tasks are not advertised as two distinct operators.
        """
        actor = hash_text(actor)
        if type(height) is not int or height < 0 or type(lease_blocks) is not int or not 1 <= lease_blocks <= 144:
            raise ValueError("Block-height audit lease required")
        with self.transaction():
            for report in self.db.execute("SELECT * FROM reports WHERE state='PENDING_AUDITS' AND owner<>? ORDER BY height,request", (actor,)).fetchall():
                if height <= report["height"]:
                    continue
                partitions = audit_partitions(report["root"], report["seed"])
                for slot in range(2):
                    previous = self.db.execute("SELECT * FROM jobs WHERE request=? AND slot=? ORDER BY attempt DESC LIMIT 1", (report["request"], slot)).fetchone()
                    if previous and previous["state"] != "EXPIRED":
                        continue
                    attempt = 0 if previous is None else previous["attempt"] + 1
                    parts = partitions[slot]
                    self.db.execute("INSERT INTO jobs VALUES(?,?,?,?,?,?,?,?,NULL,NULL)",
                                    (report["request"], slot, attempt, actor, json.dumps(parts), height,
                                     height + lease_blocks, "ASSIGNED"))
                    ranges = [dict(index=i, start=str(int(report["start"]) + int(report["length"]) * i // 100),
                                   end=str(int(report["start"]) + int(report["length"]) * (i + 1) // 100)) for i in parts]
                    identity = dict(request_id=report["request"], report_root=report["root"], ticket=report["ticket"],
                                    slot=slot, attempt=attempt, actor=actor, partitions=list(parts), ranges=ranges,
                                    issued_height=height, deadline_height=height + lease_blocks)
                    # No owner body, nonce, positional counts or expected answer.
                    return identity
        return None

    def _job(self, request, slot, attempt, actor):
        if type(slot) is not int or slot not in (0, 1) or type(attempt) is not int or attempt < 0:
            raise ValueError("Invalid audit generation")
        row = self.db.execute("SELECT * FROM jobs WHERE request=? AND slot=? AND attempt=? AND actor=?",
                              (hash_text(request), slot, attempt, hash_text(actor))).fetchone()
        if not row:
            raise AuditHeld("Audit is not assigned to this authenticated participant")
        return row

    def commit(self, request, slot, attempt, actor, commitment, height):
        commitment = hash_text(commitment)
        if type(height) is not int or height < 0:
            raise ValueError("Consensus block height required")
        with self.transaction():
            self._report(request)
            job = self._job(request, slot, attempt, actor)
            if job["state"] == "COMMITTED" and job["commitment"] == commitment:
                return False
            if job["state"] != "ASSIGNED" or height >= job["deadline"]:
                raise AuditHeld("Audit lease is closed; stale work cannot replace a new attempt")
            self.db.execute("UPDATE jobs SET state='COMMITTED',commitment=? WHERE request=? AND slot=? AND attempt=?",
                            (commitment, request, slot, attempt))
            return True

    def _all_committed(self, request):
        for slot in range(2):
            row = self.db.execute("SELECT state FROM jobs WHERE request=? AND slot=? ORDER BY attempt DESC LIMIT 1", (request, slot)).fetchone()
            if not row or row["state"] not in ("COMMITTED", "PASSED", "FAILED"):
                return False
        return True

    def reveal_owner_report(self, request, sealed):
        with self.transaction():
            report = self._report(request)
            if not self._all_committed(request):
                raise AuditHeld("Expected answers remain hidden until both audits commit")
            body = verify_report_reveal(sealed, report["root"])
            computed = body["computed"]
            if body["request_id"] != request or body["ticket"] != report["ticket"] or computed["start"] != report["start"] or int(computed["end"]) != int(report["start"]) + int(report["length"]):
                raise ValueError("Report reveal does not match its confirmed work")
            expected = json.dumps(computed["counts"], separators=(",", ":"))
            if report["expected"] is not None and report["expected"] != expected:
                raise AuditHeld("Immutable report answers changed")
            self.db.execute("UPDATE reports SET expected=? WHERE request=?", (expected, request))

    def reveal_audit(self, request, slot, attempt, actor, reveal):
        if len(canonical_bytes(reveal)) > 4096:
            raise ValueError("Audit reply exceeds capacity")
        with self.transaction():
            report = self._report(request)
            job = self._job(request, slot, attempt, actor)
            if not self._all_committed(request) or report["expected"] is None:
                raise AuditHeld("Report and audit reveal stage is not open")
            if job["state"] not in ("COMMITTED", "PASSED", "FAILED"):
                raise AuditHeld("Audit has no immutable answer commitment")
            encoded = canonical_bytes(reveal).decode()
            if job["reveal"] is not None:
                if job["reveal"] != encoded:
                    raise AuditHeld("Audit answer already revealed")
                return job["state"] == "PASSED"
            parts = json.loads(job["parts"])
            counts = json.loads(report["expected"])
            passed = verify_audit_reveal(reveal, job["commitment"], report["ticket"], attempt, parts, [counts[i] for i in parts])
            self.db.execute("UPDATE jobs SET state=?,reveal=? WHERE request=? AND slot=? AND attempt=?",
                            ("PASSED" if passed else "FAILED", encoded, request, slot, attempt))
            if not passed:
                self.db.execute("UPDATE reports SET state='DISPUTED' WHERE request=?", (request,))
            elif all(self.db.execute("SELECT state FROM jobs WHERE request=? AND slot=? ORDER BY attempt DESC LIMIT 1", (request, s)).fetchone()[0] == "PASSED" for s in range(2)):
                self.db.execute("UPDATE reports SET state='AUDITED' WHERE request=?", (request,))
            return passed

    def expire(self, height):
        if type(height) is not int or height < 0:
            raise ValueError("Consensus block height required")
        with self.transaction():
            return self.db.execute("UPDATE jobs SET state='EXPIRED' WHERE state='ASSIGNED' AND deadline<=?", (height,)).rowcount

    def reconcile_branch(self, confirmed_requests):
        """Withdraw orphaned jobs without deleting their evidence or inventing credit."""
        confirmed = {hash_text(value) for value in confirmed_requests}
        with self.transaction():
            for row in self.db.execute("SELECT request FROM reports WHERE state<>'ORPHANED'").fetchall():
                if row["request"] not in confirmed:
                    self.db.execute("UPDATE reports SET state='ORPHANED' WHERE request=?", (row["request"],))

    def status(self, request):
        report = self._report(request)
        jobs = [dict(row) for row in self.db.execute("SELECT slot,attempt,actor,state,deadline FROM jobs WHERE request=? ORDER BY slot,attempt", (request,))]
        return dict(request_id=request, state=report["state"], jobs=jobs,
                    scientific_accepted=False, payment_authorized=False)
