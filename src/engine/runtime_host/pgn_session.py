"""Durable session journal and native-checkpoint binding. No GPU dependencies."""

from pathlib import Path
import datetime as dt
import hashlib
import json
import os
import struct
import uuid
import shutil
import threading, time, weakref, functools
from pgn_index import GapIndex, read_metadata_snapshot
from pgn_native_contract import (
    PROTOCOL,
    require_new_configuration,
    checked_checkpoint,
    module_metadata,
    file_sha,
)


class SessionControllerLock:
    """One metadata/journal writer for a session; never inherited by a child."""

    def __init__(self, folder):
        folder = Path(folder)
        if not folder.is_dir():
            raise ValueError("Saved session folder does not exist")
        self.fd = os.open(folder / ".controller.lock", os.O_RDWR | os.O_CREAT, 0o600)
        os.set_inheritable(self.fd, False)
        try:
            if os.fstat(self.fd).st_size == 0:
                os.write(self.fd, b"0")
            os.lseek(self.fd, 0, os.SEEK_SET)
            if os.name == "nt":
                import msvcrt

                msvcrt.locking(self.fd, msvcrt.LK_NBLCK, 1)
            else:
                import fcntl

                fcntl.flock(self.fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            os.close(self.fd)
            self.fd = None
            raise RuntimeError(
                "This session already has a controller; use its existing window"
            )

    def release(self):
        if self.fd is None:
            return
        os.lseek(self.fd, 0, os.SEEK_SET)
        if os.name == "nt":
            import msvcrt

            msvcrt.locking(self.fd, msvcrt.LK_UNLCK, 1)
        else:
            import fcntl

            fcntl.flock(self.fd, fcntl.LOCK_UN)
        os.close(self.fd)
        self.fd = None


_locks_guard = threading.Lock()
_folder_locks = weakref.WeakValueDictionary()
_writers = weakref.WeakValueDictionary()


def folder_lock(folder):
    key = os.path.normcase(str(Path(folder).resolve()))
    with _locks_guard:
        lock = _folder_locks.get(key)
        if lock is None:
            lock = threading.RLock()
            _folder_locks[key] = lock
    return lock


def coordinator_only(method):
    @functools.wraps(method)
    def checked(self, *args, **kwargs):
        if threading.get_ident() != self._writer_thread:
            raise RuntimeError(
                "Session mutations belong to the coordinator only; use read_session_metadata for UI reads"
            )
        return method(self, *args, **kwargs)

    return checked


def atomic_bytes(path, data):
    path = Path(path)
    with folder_lock(path.parent):
        temporary = path.with_name(
            path.name
            + ".pending-"
            + str(os.getpid())
            + "-"
            + str(threading.get_ident())
            + "-"
            + uuid.uuid4().hex
        )
        try:
            with temporary.open("xb") as stream:
                stream.write(data)
                stream.flush()
                os.fsync(stream.fileno())
            for attempt in range(5):
                try:
                    os.replace(temporary, path)
                    break
                except OSError as error:
                    if (
                        getattr(error, "winerror", None) not in (5, 32, 33)
                        or attempt == 4
                    ):
                        raise
                    time.sleep(0.005 * (2**attempt))
            if os.name != "nt":
                fd = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
                try:
                    os.fsync(fd)
                finally:
                    os.close(fd)
        finally:
            # Never unlink the old shared .pending or any other writer's temp.
            try:
                temporary.unlink(missing_ok=True)
            except OSError:
                pass


def read_session_metadata(folder):
    """Committed read-only snapshot; never SessionStore/GapIndex construction."""
    folder = Path(folder).resolve()
    values = read_metadata_snapshot(folder / "session-index.sqlite3")
    metadata = values.get("session_metadata")
    if metadata is None:
        path = folder / "session.json"
        if path.stat().st_size > 1048576:
            raise ValueError("Oversized session metadata cache")
        metadata = json.loads(path.read_text(encoding="utf-8"))
    if (
        not isinstance(metadata, dict)
        or metadata.get("schema") != "PGN68_DURABLE_SESSION_V1"
    ):
        raise ValueError("Unsupported session schema")
    binding = values.get("binding")
    digest = hashlib.sha256(
        json.dumps(
            metadata["configuration"], sort_keys=True, separators=(",", ":")
        ).encode()
    ).hexdigest()
    if binding is not None and binding != {
        "session_id": metadata["session_id"],
        "configuration_hash": digest,
    }:
        raise ValueError("SQLite/session input binding mismatch")
    metadata = dict(metadata)
    if "progress" in values:
        metadata.update(values["progress"])
    if "state" in values:
        metadata["state"] = values["state"]
    if "timing" in values:
        metadata.update(values["timing"])
    for source, target in (
        ("active_chunk", "current_chunk"),
        ("next_range", "next_range"),
        ("prepared_replacement", "prepared_replacement"),
        ("completed_chunk_count", "completed_chunk_count"),
        ("journal_sequence", "journal_sequence"),
    ):
        if source in values:
            metadata[target] = values[source]
    return metadata


def file_hash(path):
    return file_sha(path)


class SessionStore:
    SCHEMA = "PGN68_DURABLE_SESSION_V1"

    def __init__(self, folder, metadata):
        self.folder = Path(folder).resolve()
        self.path = self.folder / "session.json"
        self.journal = self.folder / "session-events.jsonl"
        self.meta = metadata
        require_new_configuration(metadata["configuration"])
        self._writer_thread = threading.get_ident()
        self.cache_error = None
        self._writer_key = os.path.normcase(str(self.folder))
        self._configuration_hash = self.configuration_hash()
        config = metadata["configuration"]
        self._original_floor = int(
            config.get(
                "emit_min_gap",
                min(
                    int(config.get("count_gap", config.get("min_gap", 960))),
                    int(config.get("detail_gap", config.get("min_gap", 960))),
                ),
            )
        )
        if not 30 <= self._original_floor <= 2147483647:
            raise ValueError("Invalid immutable session emission floor")
        with _locks_guard:
            if _writers.get(self._writer_key) is not None:
                raise RuntimeError(
                    "This session already has an authoritative writer; UI must use a read-only snapshot"
                )
            _writers[self._writer_key] = self
        try:
            self.gaps = GapIndex(
                self.folder / "session-index.sqlite3",
                metadata["session_id"],
                metadata["configuration"],
                self._configuration_hash,
            )
            self.sequence = int(self.gaps.meta("journal_sequence", 0))
            self._recover_journal()
        except BaseException:
            with _locks_guard:
                if _writers.get(self._writer_key) is self:
                    _writers.pop(self._writer_key, None)
            if hasattr(self, "gaps"):
                self.gaps.close()
            raise

    @classmethod
    def create(cls, folder, configuration, device, session_id=None):
        require_new_configuration(configuration)
        folder = Path(folder).resolve()
        folder.mkdir(parents=True, exist_ok=True)
        if cls.exists(folder):
            raise FileExistsError("Session already exists; use resume explicitly")
        if session_id is not None and (
            len(session_id) != 32
            or any(c not in "0123456789abcdef" for c in session_id)
        ):
            raise ValueError("Invalid stable child session ID")
        metadata = {
            "schema": cls.SCHEMA,
            "session_id": session_id or uuid.uuid4().hex,
            "created_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
            "configuration": configuration,
            "device": device,
            "state": "PREPARED",
            "positions": 0,
            "tests": 0,
            "active_seconds": 0.0,
            "paused_seconds": 0.0,
            "timing_complete": True,
            "current_chunk": None,
            "closed_chunks": [],
            "journal_sequence": 0,
        }
        result = cls(folder, metadata)
        result.append(
            "session",
            session_id=metadata["session_id"],
            configuration_hash=result.configuration_hash(),
            device_uuid=device.get("uuid"),
        )
        result.save()  # Durable identity exists BEFORE any worker launch.
        return result

    @classmethod
    def load(cls, folder):
        metadata = read_session_metadata(folder)
        require_new_configuration(metadata["configuration"])
        if metadata.get("schema") != cls.SCHEMA:
            raise ValueError("Unsupported session schema")
        result = cls(folder, metadata)
        if metadata.get("state") in ("LAUNCH_INTENT", "RUNNING", "PAUSING"):
            metadata["timing_complete"] = False
            metadata["rate_scope"] = "SINCE_RESUME_UNKNOWN_CRASH_WALL"
            metadata["rate_origin_position"] = metadata.get("positions", 0)
        return result

    @classmethod
    def exists(cls, folder):
        folder = Path(folder)
        return (folder / "session.json").is_file() or (
            folder / "session-index.sqlite3"
        ).is_file()

    @coordinator_only
    def save(self):
        if self.configuration_hash() != self._configuration_hash:
            raise ValueError(
                "Current session configuration is immutable; change Settings for a new session"
            )
        self.meta["journal_sequence"] = self.sequence
        data = (json.dumps(self.meta, indent=2) + "\n").encode("utf-8")
        with folder_lock(self.folder):
            # Full identity/configuration is authoritative in SQLite before the
            # optional JSON cache. Do not catch journal/DB/CP/owner failures.
            with self.gaps.db:
                self.gaps.put_meta("session_metadata", self.meta)
            try:
                atomic_bytes(self.path, data)
            except OSError as error:
                self.cache_error = "Session JSON cache deferred: " + str(error)[:500]
                return False
            self.cache_error = None
        return True

    def configuration_hash(self):
        return hashlib.sha256(
            json.dumps(
                self.meta["configuration"], sort_keys=True, separators=(",", ":")
            ).encode()
        ).hexdigest()

    @property
    def original_floor(self):
        return self._original_floor

    def _recover_journal(self):
        if not self.journal.exists():
            return
        valid_bytes = int(self.gaps.meta("journal_offset", 0))
        torn = False
        with self.journal.open("rb") as stream:
            stream.seek(valid_bytes)
            while True:
                line = stream.readline(1024 * 1024 + 1)
                if not line:
                    break
                if len(line) > 1024 * 1024:
                    raise ValueError("Oversized session journal record")
                if not line.endswith(b"\n"):
                    # Preserve an interrupted final write separately, then keep
                    # the durable complete prefix. Never ignore a bad full row.
                    self.meta["truncated_journal_tail"] = True
                    torn = True
                    break
                event = json.loads(line)
                if event["sequence"] != self.sequence + 1:
                    raise ValueError("Session journal sequence is discontinuous")
                self.sequence += 1
                valid_bytes += len(line)
                if event["type"] == "session":
                    if (
                        event["session_id"] != self.meta["session_id"]
                        or event["configuration_hash"] != self.configuration_hash()
                    ):
                        raise ValueError(
                            "Session metadata does not match its durable journal binding"
                        )
                self.gaps.apply(event, valid_bytes)
        if torn:
            backup = self.folder / ("journal-interrupted-" + uuid.uuid4().hex + ".bin")
            with self.journal.open("rb") as source:
                source.seek(valid_bytes)
                interrupted = source.read(1024 * 1024 + 1)
            if len(interrupted) > 1024 * 1024:
                raise ValueError("Oversized interrupted journal tail")
            backup.write_bytes(interrupted)
            with self.journal.open("r+b") as stream:
                stream.truncate(valid_bytes)
                stream.flush()
                os.fsync(stream.fileno())
        progress = self.gaps.meta("progress")
        if progress:
            self.meta.update(progress)
        if self.gaps.meta("state") is not None:
            self.meta["state"] = self.gaps.meta("state")
        if self.gaps.meta("timing") is not None:
            self.meta.update(self.gaps.meta("timing"))
        if self.gaps.meta("active_chunk", "ABSENT") != "ABSENT":
            self.meta["current_chunk"] = self.gaps.meta("active_chunk")
        if self.gaps.meta("next_range", "ABSENT") != "ABSENT":
            self.meta["next_range"] = self.gaps.meta("next_range")
        if self.gaps.meta("prepared_replacement", "ABSENT") != "ABSENT":
            self.meta["prepared_replacement"] = self.gaps.meta("prepared_replacement")
        self.meta["completed_chunk_count"] = self.gaps.meta("completed_chunk_count", 0)

    @coordinator_only
    def append(self, kind, **values):
        event = {"sequence": self.sequence + 1, "type": kind, **values}
        data = (json.dumps(event, separators=(",", ":")) + "\n").encode("utf-8")
        with self.journal.open("ab") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
            offset = stream.tell()
        self.gaps.apply(event, offset)
        self.sequence += 1

    @coordinator_only
    def record_gap(self, lower, upper, source):
        lower, upper = int(lower), int(upper)
        start, end = (int(self.meta["configuration"][k]) for k in ("start", "end"))
        if not start <= lower < end or upper <= lower:
            raise ValueError("Journal gap lies outside lower-prime ownership")
        prior = self.gaps.get(lower)
        if prior is not None:
            if prior != upper:
                raise ValueError("Conflicting pair for a durable lower prime")
            return False
        if upper - lower < self.original_floor:
            return False
        self.append(
            "gap",
            lower=str(lower),
            upper=str(upper),
            source=source,
            emit_min_gap=self.original_floor,
        )
        return True

    @coordinator_only
    def bind_chunk(
        self,
        *,
        start,
        length,
        worker,
        worker_sha256,
        profile_k,
        base_positions=0,
        base_tests=0,
        gap_min=None,
        threshold_policy_sha256=None,
        worker_source_sha256=None,
    ):
        if profile_k not in (8, 16, 32, 64) or length <= 0:
            raise ValueError("Invalid native chunk/profile")
        f = require_new_configuration(self.meta["configuration"])
        from pgn_native_contract import piece_fields

        local = piece_fields(
            f["threshold_policy"], int(start), int(start) + int(length)
        )
        if (
            int(start) != int(self.meta["configuration"]["start"]) + int(base_positions)
            or int(base_positions) < 0
            or int(base_tests) < 0
        ):
            raise ValueError("Native child frontier differs from its logical range")
        if (
            gap_min != local["effective_gap_min"]
            or threshold_policy_sha256 != f["threshold_policy"]["sha256"]
            or not worker_source_sha256
        ):
            raise ValueError("Native threshold/source binding is required")
        identity = {
            "native_protocol": PROTOCOL,
            "gap_min": int(gap_min),
            "threshold_policy_sha256": threshold_policy_sha256,
            "worker_source_sha256": worker_source_sha256,
            "start": str(start),
            "length": str(length),
            "worker": str(Path(worker).resolve()),
            "worker_sha256": worker_sha256.upper(),
            "profile_k": profile_k,
            "emit_min_gap": self.original_floor,
            "base_positions": base_positions,
            "base_tests": base_tests,
        }
        current = self.meta.get("current_chunk")
        if current and current.get("identity") != identity:
            raise ValueError("Resume cannot change input, worker hash or K profile")
        if not current:
            if (int(base_positions), int(base_tests)) != (
                int(self.meta["positions"]),
                int(self.meta["tests"]),
            ):
                raise ValueError("New native child must start at the accepted frontier")
            self.meta["current_chunk"] = {
                "index": self.meta.get("completed_chunk_count", 0),
                "identity": identity,
                "trusted_checkpoint": None,
                "instance": None,
            }
            self.append(
                "chunk_bind",
                identity=identity,
                index=self.meta["current_chunk"]["index"],
            )
        self.meta["state"] = "LAUNCH_INTENT"
        self.append(
            "state",
            state="LAUNCH_INTENT",
            active_seconds=self.meta.get("active_seconds", 0),
            paused_seconds=self.meta.get("paused_seconds", 0),
        )
        self.save()
        return self.meta["current_chunk"]

    @coordinator_only
    def before_launch(self):
        chunk = self.meta["current_chunk"]
        if chunk is None:
            raise ValueError("Native launch requires a bound chunk")
        if file_hash(chunk["identity"]["worker"]) != chunk["identity"]["worker_sha256"]:
            raise ValueError("Worker hash changed; no resume/launch")
        f = require_new_configuration(self.meta["configuration"])
        identity = chunk["identity"]
        from pgn_native_contract import piece_fields

        local = piece_fields(
            f["threshold_policy"],
            int(identity["start"]),
            int(identity["start"]) + int(identity["length"]),
        )
        if (
            identity.get("native_protocol") != PROTOCOL
            or identity.get("gap_min") != local["effective_gap_min"]
            or identity.get("threshold_policy_sha256")
            != f["threshold_policy"]["sha256"]
        ):
            raise ValueError("Chunk threshold identity changed")
        m = module_metadata(identity["worker"])
        if m["source_manifest_sha256"] != identity["worker_source_sha256"]:
            raise ValueError("Native source fingerprint changed")
        if m["profile_k"] != identity["profile_k"]:
            raise ValueError("Native argv0/profile identity changed")
        self.meta["state"] = "LAUNCH_INTENT"
        self.append(
            "state",
            state="LAUNCH_INTENT",
            active_seconds=self.meta.get("active_seconds", 0),
            paused_seconds=self.meta.get("paused_seconds", 0),
        )
        self.save()

    @coordinator_only
    def trusted_worker_path(self, path):
        chunk = self.meta["current_chunk"]
        if file_hash(path) != chunk["identity"]["worker_sha256"]:
            raise ValueError("Packaged worker no longer matches the saved session hash")
        path = str(Path(path).resolve())
        if chunk["identity"]["worker"] != path:
            self.append("worker_relocated", worker=path)
            chunk["identity"]["worker"] = path
            self.save()

    @coordinator_only
    def started(self, pid, birth, job_nonce=None):
        if job_nonce is not None and (
            len(job_nonce)!=32 or any(c not in "0123456789abcdef" for c in job_nonce)
        ):
            raise ValueError("Invalid resident job nonce")
        self.meta["current_chunk"]["instance"] = {
            "pid": pid,
            "birth": birth,
            "closed": False,
        }
        if job_nonce is not None:
            self.meta["current_chunk"]["instance"]["job_nonce"] = job_nonce
        self.append("instance_started", instance=self.meta["current_chunk"]["instance"])
        self.meta["state"] = "RUNNING"
        self.save()

    @coordinator_only
    def complete_exact_prefix(self, piece, certificate, find_exact_pair):
        from pgn_exact_prefix import verify
        from pgn_threshold_policy import at_position

        f = require_new_configuration(self.meta["configuration"])
        pin = f["threshold_policy"]
        frontier = int(self.meta["configuration"]["start"]) + int(
            self.meta["positions"]
        )
        if (
            self.meta.get("current_chunk") is not None
            or piece.start != frontier
            or piece.end > int(self.meta["configuration"]["end"])
        ):
            raise ValueError("Exact prefix must continue a closed native frontier")
        segment = at_position(pin, piece.start)
        if (
            piece.policy_sha256 != pin["sha256"]
            or piece.minimum_gap != segment["effective_gap_min"]
            or piece.end > int(segment["end"])
        ):
            raise ValueError("Exact prefix policy differs from the logical range")
        verify(certificate, piece, find_exact_pair)
        self.append("exact_prefix", certificate=certificate)
        self.meta["positions"] = piece.end - int(self.meta["configuration"]["start"])
        self.save()
        return self.meta["positions"]

    @coordinator_only
    def accepted_progress(self, positions, tests):
        if positions < self.meta["positions"] or tests < self.meta["tests"]:
            raise ValueError("Accepted session progress cannot retreat")
        self.append("progress", positions=positions, tests=tests)
        self.meta.update(positions=positions, tests=tests)
        self.save()

    @coordinator_only
    def recovery_rewind(self, positions, tests, unknown_wall=True):
        self.append("rewind", positions=positions, tests=tests)
        self.meta.update(positions=positions, tests=tests)
        if unknown_wall:
            self.meta.update(
                timing_complete=False, rate_scope="SINCE_RESUME_UNKNOWN_CRASH_WALL"
            )
        self.save()

    @coordinator_only
    def capture_closed_checkpoint(self, path, native_position, native_tests):
        chunk = self.meta["current_chunk"]
        identity = chunk["identity"]
        data = Path(path).read_bytes()
        position, tests = checked_checkpoint(
            data, identity, native_position, native_tests
        )
        backup = self.folder / "trusted-current-checkpoint.bin"
        atomic_bytes(backup, data)
        chunk["trusted_checkpoint"] = {
            "file": backup.name,
            "sha256": hashlib.sha256(data).hexdigest(),
            "native_positions": position,
            "native_tests": tests,
        }
        self.append("checkpoint_trusted", checkpoint=chunk["trusted_checkpoint"])
        self.save()

    @coordinator_only
    def retire_unstarted_chunk(self):
        """Archive a closed binding with no accepted native checkpoint."""
        chunk = self.meta.get("current_chunk")
        if not chunk or chunk.get("trusted_checkpoint") is not None:
            raise ValueError(
                "Only an unstarted binding can be retired without a checkpoint"
            )
        instance = chunk.get("instance") or {}
        if instance and not instance.get("closed"):
            raise ValueError("Cannot retire a live Native instance")
        identity = chunk["identity"]
        if (self.meta["positions"], self.meta["tests"]) != (
            identity["base_positions"],
            identity["base_tests"],
        ):
            raise ValueError("Unstarted binding differs from the accepted frontier")
        saved = json.loads(json.dumps(chunk))
        saved.update(
            accepted_prefix_positions=0,
            accepted_prefix_tests=0,
            close_reason="REPLACED_UNSTARTED_BINDING",
        )
        self.append("chunk_closed", chunk=saved)
        self.meta["closed_chunks"] = (self.meta.get("closed_chunks", []) + [saved])[
            -10:
        ]
        self.meta["completed_chunk_count"] = saved["index"] + 1
        self.meta["current_chunk"] = None
        self.meta["state"] = "PAUSED"
        self.save()
        return saved

    @coordinator_only
    def retire_closed_prefix(self, native_positions, native_tests):
        """Close only a committed prefix; remaining positions bind a new job."""
        from pgn_native_contract import checked_checkpoint

        chunk = self.meta.get("current_chunk")
        if not chunk:
            raise ValueError("No bound chunk prefix to retire")
        instance = chunk.get("instance") or {}
        if instance and not instance.get("closed"):
            raise ValueError("Cannot resegment a live Native instance")
        identity = chunk["identity"]
        trusted = chunk.get("trusted_checkpoint")
        if trusted is None:
            raise ValueError("A committed checkpoint is required before resegmentation")
        data = (self.folder / trusted["file"]).read_bytes()
        if hashlib.sha256(data).hexdigest() != trusted["sha256"]:
            raise ValueError("Closed prefix checkpoint changed")
        checked_checkpoint(data, identity, native_positions, native_tests)
        if (self.meta["positions"], self.meta["tests"]) != (
            identity["base_positions"] + native_positions,
            identity["base_tests"] + native_tests,
        ):
            raise ValueError("Prefix frontier is not the accepted session frontier")
        saved = json.loads(json.dumps(chunk))
        name = f"trusted-prefix-{chunk['index']}.bin"
        existing = self.folder / name
        if existing.exists() and existing.read_bytes() != data:
            raise ValueError("Prefix archive collision")
        atomic_bytes(existing, data)
        saved["trusted_checkpoint"]["file"] = name
        saved["accepted_prefix_positions"] = native_positions
        saved["accepted_prefix_tests"] = native_tests
        saved["close_reason"] = "RESEGMENTED_COMMITTED_PREFIX"
        # Existing replay semantics close a Native binding; progress is a
        # separate journal frontier. Original input length is never rewritten.
        self.append("chunk_closed", chunk=saved)
        self.meta["closed_chunks"] = (self.meta.get("closed_chunks", []) + [saved])[
            -10:
        ]
        self.meta["completed_chunk_count"] = saved["index"] + 1
        self.meta["current_chunk"] = None
        self.meta["state"] = "PAUSED"
        self.save()
        return saved

    @coordinator_only
    def restore_checkpoint(self, destination):
        chunk = self.meta["current_chunk"]
        trusted = chunk["trusted_checkpoint"]
        # Ignore an unacknowledged newer native file: its emitted rows may have
        # been in a pipe when the frontend crashed. Replay from the trusted copy.
        if trusted is None:
            Path(destination).unlink(missing_ok=True)
            return 0, 0
        if trusted.get("file") != "trusted-current-checkpoint.bin":
            raise ValueError("Invalid trusted checkpoint path")
        data = (self.folder / trusted["file"]).read_bytes()
        if hashlib.sha256(data).hexdigest() != trusted["sha256"]:
            raise ValueError("Trusted checkpoint bytes changed")
        checked_checkpoint(
            data,
            chunk["identity"],
            trusted["native_positions"],
            trusted["native_tests"],
        )
        atomic_bytes(Path(destination), data)
        return trusted["native_positions"], trusted["native_tests"]

    @coordinator_only
    def closed(self, state, active_seconds, paused_seconds, chunk_complete=False,
               instance_closed=True, work_receipt=None):
        chunk = self.meta.get("current_chunk")
        if not instance_closed:
            from pgn_resident import WorkReceipt
            if not isinstance(work_receipt,WorkReceipt) or not chunk or not work_receipt.matches(chunk):
                raise ValueError("A live resident needs an exact verified work-completion receipt")
        if chunk and chunk.get("instance"):
            chunk["instance"]["closed"] = bool(instance_closed)
            if work_receipt is not None:
                chunk["instance"]["work_closed"] = True
        self.meta.update(
            state=state, active_seconds=active_seconds, paused_seconds=paused_seconds
        )
        self.append(
            "state",
            state=state,
            active_seconds=active_seconds,
            paused_seconds=paused_seconds,
            instance_closed=bool(instance_closed),
            work_closed=work_receipt is not None,
            timing_complete=self.meta.get("timing_complete", True),
            rate_scope=self.meta.get("rate_scope"),
        )
        if chunk_complete:
            self.append("chunk_closed", chunk=chunk)
            self.meta["closed_chunks"] = (self.meta["closed_chunks"] + [chunk])[-10:]
            self.meta["completed_chunk_count"] = chunk["index"] + 1
            self.meta["current_chunk"] = None
        self.save()

    def assert_no_live_instance(self, is_live):
        chunk = self.meta.get("current_chunk")
        instance = chunk.get("instance") if chunk else None
        if instance and not instance.get("closed") and is_live(instance):
            raise RuntimeError(
                "Previous bound worker is still active; resume would create a second worker"
            )

    @coordinator_only
    def close(self):
        try:
            self.gaps.close()
        finally:
            with _locks_guard:
                if _writers.get(self._writer_key) is self:
                    _writers.pop(self._writer_key, None)
