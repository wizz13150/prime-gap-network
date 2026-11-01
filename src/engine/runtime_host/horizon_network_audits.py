"""Execute authority-assigned partitions without access to expected counts."""
from pathlib import Path
import hashlib
import json
import secrets
from pgn_native_contract import fields
from pgn_session import atomic_bytes
from report_commitment import audit_commitment, canonical_bytes

def save(file, value):
    atomic_bytes(Path(file), canonical_bytes(value) + b"\n")

class NetworkAudits:
    def __init__(self, worker):
        self.worker = worker
        self.folder = worker.root / "audits"
        self.folder.mkdir(exist_ok=True)
        self.presentation = None

    def event(self, action, request=None, slot=None, attempt=None, body=None):
        owner = self.worker.address()
        args = [action, owner, request, slot, attempt, body]
        proposal = self.worker.rpc.call("getpgnauditevent", args)
        if not proposal.get("available"):
            return proposal
        signature = self.worker.rpc.call("signmessage", [owner, proposal["challenge"]], wallet=True)
        return self.worker.rpc.call("getpgnauditevent", args + [signature])

    def apply(self, action, request=None, slot=None, attempt=None, body=None):
        with self.worker.network_lock:
            proposal = self.event(action, request, slot, attempt, body)
            if proposal.get("available"):
                self.worker.ledger([proposal["envelope"]])
            return proposal

    def state(self, request):
        values = self.worker.rpc.call("getpgnauditstate", [0, 1, request])
        return values[0] if values else {}

    def states(self, requests):
        """One authenticated reconstruction per bounded group, pinned to one tip."""
        requests = list(dict.fromkeys(requests))
        if len(requests) > 4096 or any(len(r) != 64 or any(c not in "0123456789abcdef" for c in r) for r in requests):
            raise ValueError("Bounded canonical audit lookups required")
        if not requests:
            return {}
        before = self.worker.rpc.call("getbestblockhash")
        result = {}
        for offset in range(0, len(requests), 100):
            group = requests[offset:offset + 100]
            rows = self.worker.rpc.call("getpgnauditstate", [0, 100, None, group])
            if not isinstance(rows, list) or len(rows) > len(group):
                raise ValueError("Audit lookup returned an oversized page")
            for row in rows:
                request = row.get("request_id")
                if request not in group or request in result:
                    raise ValueError("Audit lookup returned a different or duplicate request")
                result[request] = row
        # A changed branch can be retried later. Never disclose from a mixed view.
        if self.worker.rpc.call("getbestblockhash") != before:
            return None
        return result

    def reveal_committed(self):
        # One bounded, indexed journal per assigned audit. No history database.
        index = self.folder / "pending.json"
        if not index.is_file():
            return
        if index.stat().st_size > 1024 * 1024:
            raise ValueError("Pending audit index exceeds budget")
        pending = json.loads(index.read_bytes())
        if not isinstance(pending, list) or len(pending) > 4096:
            raise ValueError("Pending audit index exceeds its item budget")
        saved = []
        for name in pending:
            if not isinstance(name, str) or Path(name).name != name:
                raise ValueError("Invalid saved audit identity")
            file = self.folder / (name + ".json")
            if file.stat().st_size > 262144:
                raise ValueError("Saved audit exceeds budget")
            saved.append((name, json.loads(file.read_bytes())))
        states = self.states(audit["request_id"] for _, audit in saved)
        if states is None:
            return
        keep = []
        for name, audit in saved:
            state = states.get(audit["request_id"], {})
            jobs = state.get("jobs", [])
            job = next((j for j in jobs if j["slot"] == audit["slot"] and j["attempt"] == audit["attempt"]), None)
            if not job:
                # A reorganization can temporarily hide this assignment.
                keep.append(name)
                continue
            if job["state"] in ("passed", "failed"):
                continue
            if job["state"] != "committed" or not state.get("details_root"):
                keep.append(name)
                continue
            self.apply("reveal", audit["request_id"], audit["slot"], audit["attempt"], canonical_bytes(audit["answer"]["body"]).hex())
        if keep != pending:
            save(index, keep)

    def track_owner_report(self, work, sealed):
        file = self.worker.root / "pending-owner-reports.json"
        if file.is_file() and file.stat().st_size > 1024 * 1024:
            raise ValueError("Owner report index exceeds budget")
        pending = json.loads(file.read_bytes()) if file.is_file() else {}
        old = pending.get(work["request_id"])
        if old and old["report_root"] != sealed["report_root"]:
            raise ValueError("Owner report commitment changed")
        pending[work["request_id"]] = dict(ticket=work["ticket"], report_root=sealed["report_root"])
        if len(pending) > 4096:
            raise ValueError("Too many owner reports awaiting audit")
        save(file, pending)

    def reveal_owner_reports(self):
        file = self.worker.root / "pending-owner-reports.json"
        if not file.is_file():
            return
        if file.stat().st_size > 1024 * 1024:
            raise ValueError("Owner report index exceeds budget")
        pending = json.loads(file.read_bytes())
        if not isinstance(pending, dict) or len(pending) > 4096:
            raise ValueError("Owner report index exceeds its item budget")
        states = self.states(pending)
        if states is None:
            return
        changed = False
        for request, item in list(pending.items()):
            state = states.get(request, {})
            jobs = state.get("jobs", [])
            if not state.get("audited") and (state.get("details_root") or len(jobs) != 2 or any(j["state"] == "assigned" for j in jobs)):
                continue
            source = self.worker.root / "tickets" / item["ticket"] / "network-report.json"
            if source.stat().st_size > 262144:
                raise ValueError("Owner report exceeds budget")
            sealed = json.loads(source.read_bytes())
            if sealed["report_root"] != item["report_root"]:
                raise ValueError("Owner report differs from committed index")
            if not state.get("details_root") and len(jobs) == 2 and all(j["state"] != "assigned" for j in jobs):
                self.apply("reveal_report", request, 0, 0, canonical_bytes(sealed["body"]).hex())
                state = self.state(request)
            if state.get("audited"):
                outcome = self.worker.submit_gold(request, sealed, state["details_root"])
                if outcome in ("accepted", "no_gold"):
                    del pending[request]
                    changed = True
        if changed:
            save(file, pending)

    def process_one(self):
        journal = self.folder / "current.json"
        if journal.is_file():
            if journal.stat().st_size > 262144:
                raise ValueError("Current audit exceeds budget")
            audit = json.loads(journal.read_bytes())
            current = self.state(audit["request_id"])
            job = next((j for j in current.get("jobs", []) if j["slot"] == audit["slot"] and j["attempt"] == audit["attempt"]), None)
            if not job or job["state"] != "assigned":
                journal.unlink()
                audit = None
        else:
            audit = None
        if audit is None:
            proposal = self.apply("assign")
            if not proposal.get("available"):
                return "none"
            reports = self.worker.rpc.call("getpgnreports", [0, 1, proposal["request_id"]])
            if len(reports) != 1:
                raise ValueError("Assigned audit has no immutable report")
            report = reports[0]
            audit = dict(schema="PGN_HORIZON_ASSIGNED_AUDIT_V1", request_id=proposal["request_id"], ticket=report["ticket"], report_root=report["report_root"], slot=proposal["slot"], attempt=proposal["attempt"], ranges=proposal["ranges"], counts=[])
            save(journal, audit)
        work = self.worker.rpc.call("getpgnscientificwork", [audit["request_id"], audit["report_root"]])
        for i in range(len(audit["counts"]), len(audit["ranges"])):
            if not self.worker.running.is_set():
                return "paused"
            part = audit["ranges"][i]
            start, end = int(part["start"]), int(part["end"])
            identity = hashlib.sha256(canonical_bytes(dict(domain="PGN_HORIZON_AUDIT_PARTITION_V1", request=audit["request_id"], slot=audit["slot"], attempt=audit["attempt"], index=part["index"]))).hexdigest()
            self.presentation = dict(folder=self.worker.root / "tickets" / identity, request_id=audit["request_id"], title="Audit " + str(audit["slot"] + 1) + " · partition " + str(part["index"]))
            job = dict(schema="PGN_HORIZON_ENGINE_TICKET_V1", ticket_id=identity, start=str(start), end=str(end), threshold_policy=fields(start, end)["threshold_policy"], target_merit_q16=int(work["target_numerator"]) * 65536 // int(work["target_denominator"]))
            result = self.worker.source.run_ticket(job, self.worker.args.piece_limit)
            if result.get("state") == "PAUSED":
                return "paused"
            audit["counts"].append(sum(int(n) for n in result["body"]["counts"]))
            save(journal, audit)
        with self.worker.network_lock:
            answer = audit.get("answer")
            if answer is None:
                answer = audit_commitment(audit["ticket"], audit["attempt"], [r["index"] for r in audit["ranges"]], [str(n) for n in audit["counts"]], secrets.token_hex(32))
                audit["answer"] = answer
                save(journal, audit)
            name = audit["request_id"] + "-" + str(audit["slot"]) + "-" + str(audit["attempt"])
            save(self.folder / (name + ".json"), audit)
            index = self.folder / "pending.json"
            pending = json.loads(index.read_bytes()) if index.is_file() else []
            if name not in pending:
                if len(pending) >= 4096:
                    raise ValueError("Pending audit count exceeds budget")
                pending.append(name)
            save(index, pending)
            self.apply("commit", audit["request_id"], audit["slot"], audit["attempt"], answer["commitment"])
            journal.unlink()
            self.presentation = None
            return "committed"
