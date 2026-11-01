"""Recover a detached owner's closed, exact output prefix into the session WAL."""

from pathlib import Path
import json, re, struct, time, hashlib
from pgn_native_contract import checked_checkpoint


def _sha(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for block in iter(lambda: f.read(65536), b""):
            h.update(block)
    return h.hexdigest().upper()


def _json(path, maximum=1048576):
    path = Path(path)
    if path.is_symlink() or not path.is_file() or path.stat().st_size > maximum:
        raise ValueError("Unsealed recovery metadata")
    return json.loads(path.read_text(encoding="utf-8"))


def _bindings(store, root, token, identity, final):
    bootstrap = _json(root / "bootstrap.json")
    ready = _json(root / "ready.json")
    if bootstrap.get("token") != token or ready.get("token") != token:
        return False
    expected = identity["worker_sha256"].upper()
    if (
        str(bootstrap.get("image_sha256", "")).upper() != expected
        or str(ready.get("image_sha256", "")).upper() != expected
    ):
        return False
    instance = store.meta["current_chunk"].get("instance") or {}
    if (ready.get("pid"), str(ready.get("birth"))) != (
        instance.get("pid"),
        str(instance.get("birth")),
    ):
        return False
    command = bootstrap.get("command")
    if (
        not isinstance(command, list)
        or not command
        or Path(command[0]).name != Path(identity["worker"]).name
    ):
        return False

    def value(flag):
        if command.count(flag) != 1:
            raise ValueError("Duplicate/missing recovery control flag: " + flag)
        i = command.index(flag)
        if i + 1 >= len(command):
            raise ValueError("Missing recovery flag value")
        return command[i + 1]

    if int(value("--start")) != int(identity["start"]) or int(value("--length")) != int(
        identity["length"]
    ):
        return False
    alias = re.search(r"-k(8|16|32|64)(?:\.exe)?$", Path(command[0]).name)
    if alias and int(alias.group(1)) != identity["profile_k"]:
        return False
    if (
        int(value("--gap-min")) != identity["gap_min"]
        or int(value("--emit-min-gap")) != identity["emit_min_gap"]
    ):
        return False
    if value("--engine") != "walk" or value("--device") != "0":
        return False
    if (
        Path(value("--state")).resolve() != store.folder / "native-checkpoint.bin"
        or Path(value("--stop-file")).resolve() != store.folder / "stop.request"
    ):
        return False
    commitment = root / "committed-identity.json"
    if (
        not commitment.is_file()
        or not final.get("committed_identity_sha256")
        or _sha(commitment) != str(final["committed_identity_sha256"]).upper()
    ):
        return False
    saved = _json(commitment, 32768)
    checks = (
        saved.get("pid") == instance.get("pid"),
        str(saved.get("birth")) == str(instance.get("birth")),
        str(saved.get("image_sha256", "")).upper() == expected,
        str(saved.get("configuration_sha256", "")).upper() == store.configuration_hash().upper(),
        saved.get("session_id") == store.meta["session_id"],
        saved.get("profile_k") == identity["profile_k"],
        saved.get("emit_min_gap") == identity["emit_min_gap"],
    )
    if (
        not all(checks)
        or type(saved.get("wal_sequence")) is not int
        or not 0 <= saved["wal_sequence"] <= store.sequence
    ):
        return False
    log = root / "stdout.log"
    if (
        not log.is_file()
        or type(final.get("stdout_bytes")) is not int
        or log.stat().st_size != final["stdout_bytes"]
        or _sha(log) != str(final.get("stdout_sha256", "")).upper()
    ):
        return False
    checkpoint = store.folder / "native-checkpoint.bin"
    if checkpoint.exists() and (
        not final.get("checkpoint_sha256")
        or _sha(checkpoint) != str(final["checkpoint_sha256"]).upper()
    ):
        return False
    return True


def recover(store):
    receipt = store.folder / "native-owner-recovery.json"
    chunk = store.meta.get("current_chunk")
    if not receipt.is_file() or not chunk:
        return False
    result = _json(receipt)
    identity = chunk["identity"]
    token = result.get("token", "")
    if not re.fullmatch("[a-f0-9]{32}", token):
        raise ValueError("Invalid detached owner recovery token")
    root = store.folder / ("native-owner-" + token)
    if result.get("owner_folder") != root.name or not root.is_dir():
        raise ValueError("Recovery owner folder changed")
    final = _json(root / "done.json")
    if final.get("error") is not None or final.get("started") is not True or final.get("exit") != 0:
        return False
    if not final.get("closed") or not final.get("orphan") or final.get("token") != token:
        raise ValueError("Detached owner has not safely closed this worker")
    if final["image_sha256"] != identity["worker_sha256"]:
        raise ValueError("Recovery worker image differs from the bound session")
    if not _bindings(store, root, token, identity, final):
        return False
    bootstrap = _json(root / "bootstrap.json")
    command = bootstrap["command"]

    def value(flag):
        return command[command.index(flag) + 1]

    if int(value("--start")) != int(identity["start"]) or int(value("--length")) != int(
        identity["length"]
    ):
        raise ValueError("Recovery input differs from the bound chunk")
    gap = int(value("--gap-min"))
    state = store.folder / "native-checkpoint.bin"
    length = int(identity["length"])
    first = int(identity["start"])
    if state.exists():
        data = state.read_bytes()
        if len(data) != 64:
            raise ValueError("Detached owner checkpoint size changed")
        magic, low, high, size, positions, tests, _, _ = struct.unpack("<8s7Q", data)
        if (magic, low, high, size) != (
            b"PGNS" + struct.pack("<I", gap),
            first % 2**64,
            first >> 64,
            length,
        ):
            raise ValueError("Detached owner checkpoint/input/threshold changed")
        checked_checkpoint(data, identity, positions, tests)
        if not 0 <= positions <= length:
            raise ValueError("Detached owner checkpoint frontier invalid")
        complete = positions == length
    else:
        positions = tests = 0
        complete = False
        with (root / "stdout.log").open(encoding="utf-8") as stream:
            for line in stream:
                if len(line) > 65536:
                    raise ValueError("Oversized recovery publication")
                match = re.search(r"\[pgn-counters\].* positions=(\d+) gaps=\d+ tests=(\d+)", line)
                if match:
                    positions, tests = map(int, match.groups())
                if "[phase0-gpu] --- end ---" in line:
                    complete = True
        if final.get("exit") != 0 or not complete or positions != length:
            # The existing trusted CP remains authoritative after a forced crash.
            return False
    if final.get("exit") != 0:
        return False
    last_counter = None
    end_seen = False
    previous = (-1, -1)
    with (root / "stdout.log").open(encoding="utf-8") as stream:
        for line in stream:
            if len(line) > 65536:
                raise ValueError("Oversized recovery publication")
            match = re.search(r"GAP merit=[^ ]+ gap=(\d+) lower=(\d+) upper=(\d+) verified=1", line)
            if "GAP merit=" in line and not match:
                raise ValueError("Malformed/unverified detached gap publication")
            if match:
                width, lower, upper = map(int, match.groups())
                if upper - lower != width:
                    raise ValueError("Recovery endpoint conflict")
                if not first <= lower < first + length or width < identity["emit_min_gap"]:
                    raise ValueError("Recovery publication outside bound ownership/emission floor")
            match = re.search(r"\[pgn-counters\].* positions=(\d+) gaps=\d+ tests=(\d+)", line)
            if match:
                progress, number = map(int, match.groups())
                if not previous[0] <= progress <= length or number < previous[1]:
                    raise ValueError("Recovery counters retreated or escaped the chunk")
                previous = (progress, number)
                if progress <= positions:
                    last_counter = (progress, number)
            if "[phase0-gpu] --- end ---" in line:
                end_seen = True
    if last_counter != (positions, tests):
        raise ValueError("Checkpoint has no matching durable output-prefix counter")
    if complete and not end_seen:
        raise ValueError("Complete detached output has no end marker")
    if _sha(root / "stdout.log") != str(final["stdout_sha256"]).upper():
        return False
    # The owner seals/never rewrites its log after done. No mutations occur before all checks above.
    with (root / "stdout.log").open(encoding="utf-8") as stream:
        for line in stream:
            match = re.search(r"GAP merit=[^ ]+ gap=(\d+) lower=(\d+) upper=(\d+) verified=1", line)
            if match:
                width, lower, upper = map(int, match.groups())
                if first <= lower < first + length:
                    store.record_gap(lower, upper, "Native_verified_detached_recovery")
    # Publish trusted CP only AFTER all rows are durable, and before advancing the visible frontier.
    if not complete:
        store.capture_closed_checkpoint(state, positions, tests)
    total_position = int(identity["base_positions"]) + positions
    total_tests = int(identity["base_tests"]) + tests
    if total_position < store.meta["positions"] or total_tests < store.meta["tests"]:
        store.recovery_rewind(total_position, total_tests)
    else:
        store.accepted_progress(total_position, total_tests)
    store.meta.update(timing_complete=False, rate_scope="SINCE_RESUME_UNKNOWN_CRASH_WALL")
    store.closed(
        "CHUNK_COMPLETE" if complete else "PAUSED",
        store.meta.get("active_seconds", 0),
        store.meta.get("paused_seconds", 0),
        chunk_complete=complete,
    )
    receipt.rename(receipt.with_name("native-owner-recovered-" + token + ".json"))
    return True
