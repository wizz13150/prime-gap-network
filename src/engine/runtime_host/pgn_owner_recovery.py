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


def _transport_v2(store, root, token, final):
    from pgn_output_spool import verified_segments, _encoded, _binding_name, BINDING_SCHEMA
    job = final.get('output_job')
    if not isinstance(job, dict):
        return None
    binding = job.get('binding')
    instance = store.meta['current_chunk'].get('instance') or {}
    if not isinstance(binding, dict) or binding.get('schema') != BINDING_SCHEMA or binding.get('token') != token or binding.get('nonce') != instance.get('job_nonce'):
        return None
    locator = instance.get('output_owner')
    if locator is not None:
        from pgn_output_spool import recovery_locator
        if locator != recovery_locator(root, binding):
            return None
    name = _binding_name(binding.get('sequence'), binding.get('nonce'))
    if hashlib.sha256(_encoded(binding)).hexdigest() != job.get('binding_sha256') or _json(root / name) != binding:
        return None
    identity_name = name[:-5] + '.identity.json'
    if binding.get('identity') != identity_name or _sha(root / identity_name).lower() != binding.get('identity_sha256'):
        return None
    if locator is not None and _json(root / identity_name).get('ticket_id') != store.folder.name:
        return None
    bootstrap = _json(root / 'bootstrap.json')
    wire = bootstrap.get('command', [])
    if wire.count('--resident-directory') != 1:
        return None
    receipt_path = Path(wire[wire.index('--resident-directory') + 1]) / f'{binding["sequence"]}.done.json'
    receipt = _json(receipt_path, 65536)
    if receipt != job.get('work_receipt') or _sha(receipt_path).lower() != job.get('work_receipt_sha256'):
        return None
    if (receipt.get('schema'), receipt.get('sequence'), receipt.get('nonce'), receipt.get('pid'), receipt.get('exit')) != ('SCOUT_RESIDENT_JOB_V1', binding['sequence'], binding['nonce'], instance.get('pid'), 0) or type(receipt.get('exit')) is not int:
        return None
    seal = final.get('output_spool')
    paths = verified_segments(root / 'output', seal, token)
    start = binding.get('output_start')
    if type(start) is not int or not seal['base_offset'] <= start <= seal['end_offset']:
        return None  # already ACKed output is not replayed or invented
    return dict(command=binding['command'], identity=root / identity_name,
                identity_sha256=binding['identity_sha256'], seal=seal, paths=paths, start=start)


def _output_lines(root, transport):
    if transport is None:
        with (root / 'stdout.log').open(encoding='utf-8') as stream:
            yield from stream
        return
    offset = transport['seal']['base_offset']
    boundary = offset == transport['start']
    for path in transport['paths']:
        with path.open('rb') as stream:
            while True:
                raw = stream.readline(65537)
                if not raw:
                    break
                if len(raw) > 65536 or not raw.endswith(b'\n'):
                    raise ValueError('Invalid segmented recovery line')
                before = offset
                offset += len(raw)
                if before < transport['start'] < offset:
                    raise ValueError('Bound output start is not a delivered line boundary')
                if offset == transport['start']:
                    boundary = True
                if before >= transport['start']:
                    boundary = True
                    yield raw.decode('utf-8')
    if not boundary or offset != transport['seal']['end_offset']:
        raise ValueError('Bound output recovery frontier changed')


def _bindings(store, root, token, identity, final, transport=None):
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
    command = transport['command'] if transport is not None else bootstrap.get("command")
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
    commitment = transport['identity'] if transport is not None else root / "committed-identity.json"
    commitment_sha = transport['identity_sha256'] if transport is not None else final.get('committed_identity_sha256')
    if (
        not commitment.is_file()
        or not commitment_sha
        or _sha(commitment) != str(commitment_sha).upper()
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
    if transport is None and (
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
    if not chunk:
        return False
    instance = chunk.get("instance") or {}
    # SessionStore.closed already committed this exact work and its trusted
    # checkpoint. Its normal owner receipt is not an orphan to replay.
    if instance.get("closed") is True and instance.get("work_closed") is True:
        return False
    locator = instance.get("output_owner")
    if not receipt.is_file() and locator is None:
        return False
    result = _json(receipt) if receipt.is_file() else dict(token=locator.get('token'))
    identity = chunk["identity"]
    token = result.get("token", "")
    if not re.fullmatch("[a-f0-9]{32}", token):
        raise ValueError("Invalid detached owner recovery token")
    if locator is not None:
        owner_ticket = locator.get('owner_ticket_id', '')
        if not re.fullmatch('[a-f0-9]{64}', owner_ticket) or locator.get('token') != token:
            raise ValueError('Invalid durable output owner locator')
        root = store.folder.parent / owner_ticket / ('native-owner-' + token)
        if root.resolve().parent.parent != store.folder.parent.resolve() or root.is_symlink() or root.parent.is_symlink():
            raise ValueError('Durable output owner escaped ticket namespace')
    else:
        root = store.folder / ("native-owner-" + token)
    if result.get("owner_folder", root.name) != root.name or not root.is_dir():
        raise ValueError("Recovery owner folder changed")
    if not (root / 'done.json').is_file():
        return False
    final = _json(root / "done.json")
    if final.get('schema') not in (None, 'PGN_OWNER_OUTPUT_V1', 'PGN_OWNER_OUTPUT_V2'):
        return False
    if final.get("error") is not None or final.get("started") is not True or final.get("exit") != 0:
        return False
    if final.get("closed") is not True or final.get("token") != token:
        raise ValueError("Detached owner has not safely closed this worker")
    # Storage backpressure can close the resident normally BEFORE the session
    # commits work_closed. Parent death is not a prerequisite for replaying its
    # exact sealed output. This path requires the durable V2 job locator and
    # all of the same receipt, image, nonce, session and prefix checks below.
    if final.get("orphan") is not True and not (
        final.get("orphan") is False
        and instance.get("closed") is True
        and locator is not None
        and final.get("schema") == "PGN_OWNER_OUTPUT_V2"
        and isinstance(final.get("output_job"), dict)
    ):
        raise ValueError("Detached owner has not safely closed this worker")
    if final["image_sha256"] != identity["worker_sha256"]:
        raise ValueError("Recovery worker image differs from the bound session")
    if locator is not None and (
        final.get('orphan') is True or final.get('recovery_ticket_id') is not None
    ) and final.get('recovery_ticket_id') != store.folder.name:
        return False
    transport = _transport_v2(store, root, token, final) if final.get('schema') == 'PGN_OWNER_OUTPUT_V2' else None
    if final.get('schema') == 'PGN_OWNER_OUTPUT_V2' and transport is None:
        return False
    if not _bindings(store, root, token, identity, final, transport):
        return False
    bootstrap = _json(root / "bootstrap.json")
    command = transport['command'] if transport is not None else bootstrap["command"]

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
        for line in _output_lines(root, transport):
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
    for line in _output_lines(root, transport):
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
    if transport is None:
        if _sha(root / 'stdout.log') != str(final['stdout_sha256']).upper():
            return False
    elif _transport_v2(store, root, token, final) != transport:
        return False
    # The owner seals/never rewrites its log after done. No mutations occur before all checks above.
    for line in _output_lines(root, transport):
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
    if receipt.is_file():
        receipt.rename(receipt.with_name("native-owner-recovered-" + token + ".json"))
    return True
