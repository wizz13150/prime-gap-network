"""Bounded durable report handoff; network submission runs outside the GPU lane."""
from pathlib import Path
import json
import threading
from pgn_session import atomic_bytes
from report_commitment import canonical_bytes, seal_report, verify_report_reveal, validate_computed_report

class ReportOutbox:
    def __init__(self, root, maximum=4, *, discoverer=None):
        self.root = Path(root)
        self.file = self.root / "report-outbox.json"
        self.maximum = maximum
        self.discoverer = discoverer
        self.lock = threading.RLock()
        self.condition = threading.Condition(self.lock)

    def _read(self):
        with self.lock:
            if not self.file.exists():
                return []
            if self.file.stat().st_size > 32768:
                raise ValueError("Report outbox exceeds its byte budget")
            entries = json.loads(self.file.read_bytes())
            if not isinstance(entries, list) or len(entries) > self.maximum:
                raise ValueError("Report outbox exceeds its item budget")
            ids = [entry["work"]["request_id"] for entry in entries]
            if len(ids) != len(set(ids)):
                raise ValueError("Duplicated report outbox identity")
            return entries

    def _save(self, entries):
        raw = canonical_bytes(entries) + b"\n"
        if len(raw) > 32768:
            raise ValueError("Report outbox exceeds its byte budget")
        atomic_bytes(self.file, raw)

    def put(self, work, computed):
        # Seal once before exposing the handoff. Retries retain the same nonce.
        computed_body = validate_computed_report(computed, work)
        ticket = work["ticket"]
        if len(ticket) != 64 or any(c not in "0123456789abcdef" for c in ticket):
            raise ValueError("Canonical completed ticket required")
        folder = self.root / "tickets" / ticket
        file = folder / "network-report.json"
        if file.exists():
            if file.stat().st_size > 65536:
                raise ValueError("Sealed report exceeds capacity")
            sealed = json.loads(file.read_bytes())
            verify_report_reveal(sealed, sealed["report_root"])
            if sealed["body"]["computed"] != computed_body:
                raise ValueError("Repeated computation differs from its retained report")
            if sealed["body"]["request_id"] != work["request_id"] or sealed["body"]["ticket"] != ticket:
                raise ValueError("Retained report belongs to another request")
        else:
            name = self.discoverer() if self.discoverer is not None else None
            sealed = seal_report(computed, work, discoverer=name)
            atomic_bytes(file, canonical_bytes(sealed) + b"\n")
        entry = dict(work=work, report_root=sealed["report_root"])
        with self.condition:
            entries = self._read()
            old = next((e for e in entries if e["work"]["request_id"] == work["request_id"]), None)
            if old:
                if old != entry:
                    raise ValueError("Completed report handoff changed")
                return entry
            if len(entries) >= self.maximum:
                raise BufferError("Network report queue is full; completed work remains on disk")
            entries.append(entry)
            self._save(entries)
            self.condition.notify_all()
        return entry

    def first(self):
        with self.lock:
            entries = self._read()
            return entries[0] if entries else None

    def acknowledge(self, request_id, confirmed_root):
        with self.condition:
            entries = self._read()
            old = next((e for e in entries if e["work"]["request_id"] == request_id), None)
            if old is None:
                return
            if old["report_root"] != confirmed_root:
                raise ValueError("Confirmed report root differs from durable handoff")
            self._save([e for e in entries if e["work"]["request_id"] != request_id])
            self.condition.notify_all()

    def capacity_available(self):
        with self.lock:
            return len(self._read()) < self.maximum
