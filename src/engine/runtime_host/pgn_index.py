"""Disk-backed exact gap index. RAM bound is independent of run gap count."""

from bisect import bisect_right
from collections.abc import MutableMapping
import json
import sqlite3
import threading
from pgn_projection import Projection


def blob(number):
    return int(number).to_bytes(16, "big", signed=False)


def number(value):
    return int.from_bytes(value, "big", signed=False)


class GapIndex(MutableMapping):
    def __init__(self, path, session_id, configuration, configuration_hash):
        self._writer_thread = threading.get_ident()
        self.configuration = json.loads(json.dumps(configuration))
        self.db = sqlite3.connect(str(path))
        self.db.execute("PRAGMA journal_mode=WAL")
        self.db.execute("PRAGMA synchronous=FULL")
        self.db.execute("PRAGMA cache_size=-4096")  # At most4MiB page cache.
        self.db.execute("PRAGMA temp_store=FILE")
        self.db.executescript("""
          CREATE TABLE IF NOT EXISTS meta(k TEXT PRIMARY KEY,v TEXT NOT NULL);
          CREATE TABLE IF NOT EXISTS gaps(lo BLOB PRIMARY KEY,hi BLOB NOT NULL,gap INTEGER NOT NULL,seq INTEGER NOT NULL,bin INTEGER NOT NULL) WITHOUT ROWID;
          CREATE INDEX IF NOT EXISTS gaps_gap ON gaps(gap DESC,lo);
          CREATE INDEX IF NOT EXISTS gaps_recent ON gaps(seq DESC);
          CREATE TABLE IF NOT EXISTS chunks(idx INTEGER PRIMARY KEY,value TEXT NOT NULL);
        """)
        binding = {"session_id": session_id, "configuration_hash": configuration_hash}
        prior = self.meta("binding")
        if prior is not None and prior != binding:
            raise ValueError("SQLite/session input binding mismatch")
        self.put_meta("binding", binding)
        self.db.commit()
        start, end = (int(configuration[k]) for k in ("start", "end"))
        bins = int(configuration.get("bins", 100))
        self.bounds = [start + (end - start) * i // bins for i in range(bins + 1)]
        columns = {row[1] for row in self.db.execute("PRAGMA table_info(gaps)")}
        migrated = False
        for name in ("visible", "count_ok", "detail_ok"):
            if name not in columns:
                self.db.execute(
                    "ALTER TABLE gaps ADD COLUMN "
                    + name
                    + " INTEGER NOT NULL DEFAULT0".replace("DEFAULT0", "DEFAULT 0")
                )
                migrated = True
        self.db.executescript("""
          CREATE INDEX IF NOT EXISTS gaps_visible_count_rank ON gaps(visible,count_ok,gap DESC,lo);
          CREATE INDEX IF NOT EXISTS gaps_visible_detail_rank ON gaps(visible,detail_ok,gap DESC,lo);
          CREATE INDEX IF NOT EXISTS gaps_visible_count_recent ON gaps(visible,count_ok,seq DESC);
        """)
        self.projection = Projection(self, configuration, rebuild=migrated)

    def meta(self, key, default=None):
        row = self.db.execute("SELECT v FROM meta WHERE k=?", (key,)).fetchone()
        return json.loads(row[0]) if row else default

    def _assert_writer(self):
        if threading.get_ident() != self._writer_thread:
            raise RuntimeError(
                "SQLite mutations belong to the session coordinator only"
            )

    def put_meta(self, key, value):
        self._assert_writer()
        self.db.execute(
            "INSERT OR REPLACE INTO meta VALUES(?,?)",
            (key, json.dumps(value, separators=(",", ":"))),
        )

    def _insert_gap(self, low, high, sequence):
        if not self.projection.start <= low < self.projection.end or high <= low:
            raise ValueError("Gap lower prime is outside its logical ownership")
        prior = self.get(low)
        if prior is not None and prior != high:
            raise ValueError("Conflicting indexed gap")
        group = bisect_right(self.bounds, low) - 1
        count_ok, detail_ok = self.projection.eligibility(low, high - low)
        changed = self.db.execute(
            "INSERT OR IGNORE INTO gaps(lo,hi,gap,seq,bin,visible,count_ok,detail_ok) VALUES(?,?,?,?,?,?,?,?)",
            (
                blob(low),
                blob(high),
                high - low,
                sequence,
                group,
                int(low < int(self.projection.value["prefix"])),
                int(count_ok),
                int(detail_ok),
            ),
        ).rowcount
        return (
            self.projection.inserted(low, high, group, sequence)
            if changed
            else self.projection.value
        )

    def apply(self, event, offset):
        self._assert_writer()
        kind = event["type"]
        next_projection = self.projection.value
        with self.db:
            if kind == "gap":
                next_projection = self._insert_gap(
                    int(event["lower"]), int(event["upper"]), event["sequence"]
                )
            elif kind == "exact_prefix":
                from types import SimpleNamespace
                from pgn_exact_prefix import validate_structure
                from pgn_threshold_policy import at_position

                proof = event["certificate"]
                pin = self.configuration["threshold_policy"]
                start, end = int(proof["start"]), int(proof["end"])
                if (
                    self.meta("active_chunk") is not None
                    or start != int(self.projection.value["prefix"])
                    or end > self.projection.end
                ):
                    raise ValueError(
                        "Exact prefix does not continue the committed frontier"
                    )
                segment = at_position(pin, start)
                if end > int(segment["end"]):
                    raise ValueError("Exact prefix crosses its pinned threshold")
                piece = SimpleNamespace(
                    start=start,
                    end=end,
                    minimum_gap=segment["effective_gap_min"],
                    kind="exact_short_prefix",
                    policy_sha256=pin["sha256"],
                )
                validate_structure(proof, piece)
                if proof["owned_gap"]:
                    low, high = map(int, proof["owned_gap"])
                    self._insert_gap(low, high, event["sequence"])
                next_projection = self.projection.move(end)
                prior = self.meta("progress", {"positions": 0, "tests": 0})
                self.put_meta(
                    "progress",
                    {"positions": end - self.projection.start, "tests": prior["tests"]},
                )
                self.put_meta("last_exact_prefix", proof)
            elif kind in ("progress", "rewind"):
                next_projection = self.projection.move(
                    self.projection.start + event["positions"]
                )
                self.put_meta(
                    "progress",
                    {"positions": event["positions"], "tests": event["tests"]},
                )
            elif kind == "chunk_bind":
                self.put_meta(
                    "active_chunk",
                    {
                        "index": event["index"],
                        "identity": event["identity"],
                        "trusted_checkpoint": None,
                        "instance": None,
                    },
                )
            elif kind in ("instance_started", "checkpoint_trusted"):
                current = self.meta("active_chunk")
                if current is None:
                    raise ValueError("Journal chunk transition has no bound chunk")
                if kind == "instance_started":
                    current["instance"] = event["instance"]
                    self.put_meta("state", "RUNNING")
                else:
                    current["trusted_checkpoint"] = event["checkpoint"]
                self.put_meta("active_chunk", current)
            elif kind == "worker_relocated":
                current = self.meta("active_chunk")
                current["identity"]["worker"] = event["worker"]
                self.put_meta("active_chunk", current)
            elif kind == "next_range":
                self.put_meta("next_range", event["entry"])
            elif kind == "prepared_replacement":
                self.put_meta("prepared_replacement", event["plan"])
            elif kind == "state":
                self.put_meta("state", event["state"])
                timing = {
                    "active_seconds": event["active_seconds"],
                    "paused_seconds": event["paused_seconds"],
                }
                if "timing_complete" in event:
                    timing["timing_complete"] = event["timing_complete"]
                if event.get("rate_scope") is not None:
                    timing["rate_scope"] = event["rate_scope"]
                self.put_meta("timing", timing)
                current = self.meta("active_chunk")
                if event.get("instance_closed") and current and current.get("instance"):
                    current["instance"]["closed"] = True
                    self.put_meta("active_chunk", current)
                if event.get("work_closed") and current and current.get("instance"):
                    current["instance"]["work_closed"] = True
                    self.put_meta("active_chunk", current)
            elif kind == "chunk_closed":
                chunk = event["chunk"]
                self.db.execute(
                    "INSERT OR REPLACE INTO chunks VALUES(?,?)",
                    (chunk["index"], json.dumps(chunk)),
                )
                self.put_meta("active_chunk", None)
                self.put_meta("completed_chunk_count", chunk["index"] + 1)
            self.put_meta("journal_sequence", event["sequence"])
            self.put_meta("journal_offset", offset)
            self.put_meta("projection", next_projection)
        self.projection.value = next_projection

    def __getitem__(self, key):
        row = self.db.execute("SELECT hi FROM gaps WHERE lo=?", (blob(key),)).fetchone()
        if row is None:
            raise KeyError(key)
        return number(row[0])

    def __setitem__(self, key, value):
        raise RuntimeError("Insert only through the durable session journal")

    def __delitem__(self, key):
        raise RuntimeError("Durable gaps cannot be silently deleted")

    def __iter__(self):
        for (lo,) in self.db.execute("SELECT lo FROM gaps ORDER BY lo"):
            yield number(lo)

    def __len__(self):
        return self.db.execute("SELECT COUNT(*) FROM gaps").fetchone()[0]

    def items(self):
        for lo, hi in self.db.execute("SELECT lo,hi FROM gaps ORDER BY lo"):
            yield number(lo), number(hi)

    def _policy_column(self, role):
        if role not in ("count", "detail"):
            raise ValueError("Record role must be count or detail")
        return role + "_ok"

    def policy_rows(self, role, below):
        import math

        column = self._policy_column(role)
        below = min(int(below), int(self.projection.value["prefix"]))
        for lo, hi, gap in self.db.execute(
            "SELECT lo,hi,gap FROM gaps WHERE visible=1 AND "
            + column
            + "=1 AND lo<? ORDER BY lo",
            (blob(below),),
        ):
            low = number(lo)
            yield gap, gap / math.log(low), low

    def policy_count(self, role, below):
        column = self._policy_column(role)
        prefix = int(self.projection.value["prefix"])
        if int(below) >= prefix:
            return self.projection.value[role + "_total"]
        return self.db.execute(
            "SELECT COUNT(*) FROM gaps WHERE visible=1 AND " + column + "=1 AND lo<?",
            (blob(below),),
        ).fetchone()[0]

    def policy_top(self, role, below, limit=100):
        import math

        column = self._policy_column(role)
        prefix = int(self.projection.value["prefix"])
        if int(below) >= prefix and limit <= self.projection.limit:
            return [
                (g, g / math.log(int(lo)), int(lo))
                for g, lo, seq in self.projection.value[role + "_top"][:limit]
            ]
        return [
            (g, g / math.log(number(lo)), number(lo))
            for lo, g in self.db.execute(
                "SELECT lo,gap FROM gaps WHERE visible=1 AND "
                + column
                + "=1 AND lo<? ORDER BY gap DESC,lo LIMIT ?",
                (blob(min(int(below), prefix)), limit),
            )
        ]

    def policy_recent(self, role, below, limit=10):
        import math

        column = self._policy_column(role)
        prefix = int(self.projection.value["prefix"])
        if role == "count" and int(below) >= prefix and limit <= 30:
            return [
                (g, g / math.log(int(lo)), int(lo))
                for g, lo, seq in self.projection.value["recent"][:limit]
            ]
        return [
            (g, g / math.log(number(lo)), number(lo))
            for lo, g in self.db.execute(
                "SELECT lo,gap FROM gaps WHERE visible=1 AND "
                + column
                + "=1 AND lo<? ORDER BY seq DESC LIMIT ?",
                (blob(min(int(below), prefix)), limit),
            )
        ]

    def policy_bin_counts(self, role, below):
        column = self._policy_column(role)
        prefix = int(self.projection.value["prefix"])
        if role == "count" and int(below) >= prefix:
            return list(self.projection.value["counts"])
        counts = [0] * (len(self.bounds) - 1)
        for i, n in self.db.execute(
            "SELECT bin,COUNT(*) FROM gaps WHERE visible=1 AND "
            + column
            + "=1 AND lo<? GROUP BY bin",
            (blob(min(int(below), prefix)),),
        ):
            counts[i] = n
        return counts

    def iter_rows(self, threshold, below):
        import math

        for lo, hi, gap in self.db.execute(
            "SELECT lo,hi,gap FROM gaps WHERE lo<? AND gap>=? ORDER BY lo",
            (blob(below), threshold),
        ):
            low = number(lo)
            yield gap, gap / math.log(low), low

    def count_rows(self, threshold, below):
        value = self.projection.value
        if not self.projection.dynamic and below == int(value["prefix"]):
            if threshold == self.projection.count:
                return value["count_total"]
            if threshold == self.projection.detail:
                return value["detail_total"]
        return self.db.execute(
            "SELECT COUNT(*) FROM gaps WHERE lo<? AND gap>=?", (blob(below), threshold)
        ).fetchone()[0]

    def top(self, threshold, below, limit=100):
        import math

        value = self.projection.value
        name = (
            "count"
            if threshold == self.projection.count
            else "detail" if threshold == self.projection.detail else None
        )
        if (
            not self.projection.dynamic
            and below == int(value["prefix"])
            and name
            and limit <= self.projection.limit
        ):
            return [
                (gap, gap / math.log(int(lo)), int(lo))
                for gap, lo, seq in value[name + "_top"][:limit]
            ]
        if not self.projection.dynamic and below == int(value["prefix"]) and name:
            return [
                (gap, gap / math.log(number(lo)), number(lo))
                for lo, gap in self.db.execute(
                    "SELECT lo,gap FROM gaps WHERE visible=1 AND "
                    + name
                    + "_ok=1 ORDER BY gap DESC,lo LIMIT ?",
                    (limit,),
                )
            ]
        return [
            (gap, gap / math.log(number(lo)), number(lo))
            for lo, gap in self.db.execute(
                "SELECT lo,gap FROM gaps WHERE lo<? AND gap>=? ORDER BY gap DESC,lo LIMIT ?",
                (blob(below), threshold, limit),
            )
        ]

    def recent(self, threshold, below, limit=10):
        import math

        value = self.projection.value
        if (
            not self.projection.dynamic
            and below == int(value["prefix"])
            and threshold == self.projection.count
            and limit <= 30
        ):
            return [
                (gap, gap / math.log(int(lo)), int(lo))
                for gap, lo, seq in value["recent"][:limit]
            ]
        return [
            (gap, gap / math.log(number(lo)), number(lo))
            for lo, gap in self.db.execute(
                "SELECT lo,gap FROM gaps WHERE lo<? AND gap>=? ORDER BY seq DESC LIMIT ?",
                (blob(below), threshold, limit),
            )
        ]

    def bin_counts(self, threshold, below):
        if (
            not self.projection.dynamic
            and below == int(self.projection.value["prefix"])
            and threshold == self.projection.count
        ):
            return list(self.projection.value["counts"])
        counts = [0] * (len(self.bounds) - 1)
        for index, count in self.db.execute(
            "SELECT bin,COUNT(*) FROM gaps WHERE lo<? AND gap>=? GROUP BY bin",
            (blob(below), threshold),
        ):
            counts[index] = count
        return counts

    def close(self):
        self.db.close()


def read_metadata_snapshot(path):
    """Read committed metadata only. No schema init, migration or projection write."""
    from pathlib import Path

    path = Path(path).resolve()
    if not path.is_file():
        return {}
    connection = sqlite3.connect(
        path.as_uri() + "?mode=ro", uri=True, timeout=0.1, isolation_level=None
    )
    try:
        connection.execute("PRAGMA query_only=ON")
        connection.execute("BEGIN")
        keys = (
            "binding",
            "session_metadata",
            "journal_sequence",
            "progress",
            "state",
            "timing",
            "active_chunk",
            "next_range",
            "prepared_replacement",
            "completed_chunk_count",
            "projection",
        )
        placeholders = ",".join("?" for _ in keys)
        rows = connection.execute(
            "SELECT k,v FROM meta WHERE k IN (" + placeholders + ")", keys
        ).fetchall()
        result = {}
        for key, value in rows:
            if len(value) > 1048576:
                raise ValueError("Oversized committed session metadata")
            result[key] = json.loads(value)
        return result
    finally:
        connection.close()
