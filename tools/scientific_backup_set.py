"""Private, bounded sets of independently verifiable scientific ZIP archives.

An index is the final commit marker. A missing index means an incomplete set,
never a successful backup. Selection is observed per ticket, not an atomic
snapshot of a running network. No source, chain or wallet is changed.
"""
from contextlib import contextmanager
import hashlib
import os
from pathlib import Path
import sqlite3
import stat
import tempfile
import zipfile

import export_scientific_results as single

SCHEMA = "PGN_PRIVATE_SCIENTIFIC_BACKUP_SET_V1"
INDEX = "index.json"
MAXIMUM_INDEX = 1024 * 1024
MAXIMUM_SHARDS = 4096
DEFAULT_BUDGET = 512 * 1024 * 1024
SCRATCH_BUDGET = 128 * 1024 * 1024
CHUNK_BYTES = 32 * 1024 * 1024
FLAGS = dict(private_backup=True, contains_private_audit_answers=True,
             network_acceptance_proven=False, keys_or_wallets_included=False,
             reset_or_token_conversion_authorized=False)
INDEX_KEYS = {"schema", "genesis", "attribution", "tickets", "sealed_reports",
              "decoded_bytes", "ticket_set_sha256", "selection", "shards"} | set(FLAGS)
SHARD_KEYS = {"path", "bytes", "sha256", "tickets", "sealed_reports", "decoded_bytes"}
SELECTION_KEYS = {"mode", "active_excluded", "other_entries_excluded"}


def _integer(value, minimum=0, maximum=2**53 - 1):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError("A bounded integer is required")
    return value


def _plain_path(path, directory=False):
    """Reject symbolic links and Windows junctions, including ancestors."""
    path = Path(path).absolute()
    for component in (path, *path.parents):
        metadata = component.lstat()
        if stat.S_ISLNK(metadata.st_mode) or getattr(metadata, "st_file_attributes", 0) & 0x400:
            raise ValueError("Redirected backup paths are not supported")
    if directory and not path.is_dir() or not directory and not path.is_file():
        raise ValueError("Backup path has the wrong file type")
    return path


def _sha(path):
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(65536), b""):
            value.update(block)
    return value.hexdigest()


@contextmanager
def _ids(parent):
    # Exact, disk-backed deduplication. SQLite's page ceiling is a scratch quota,
    # not a lifetime ticket limit. One shard's reports are the memory work unit.
    with tempfile.TemporaryDirectory(prefix=".pgn-science-ids-", dir=parent) as scratch:
        db = sqlite3.connect(str(Path(scratch) / "ids.sqlite"))
        try:
            db.execute("PRAGMA page_size=4096")
            db.execute("PRAGMA journal_mode=OFF")
            db.execute("PRAGMA temp_store=FILE")
            db.execute("PRAGMA cache_size=-1024")
            db.execute(f"PRAGMA max_page_count={SCRATCH_BUDGET // 4096}")
            db.execute("CREATE TABLE ids (id TEXT PRIMARY KEY) WITHOUT ROWID")
            yield db
        finally:
            db.close()


def _add(db, tickets):
    try:
        db.executemany("INSERT INTO ids VALUES (?)", ((ticket,) for ticket in tickets))
    except sqlite3.IntegrityError as error:
        raise ValueError("Duplicate ticket across scientific backup shards") from error


def _identity(db):
    value = hashlib.sha256()
    count = 0
    for (ticket,) in db.execute("SELECT id FROM ids ORDER BY id"):
        value.update((ticket + "\n").encode("ascii"))
        count += 1
    return count, value.hexdigest()


def _verified_shard(path, genesis, attribution):
    _plain_path(path)
    result = single.verify(path, genesis)
    with zipfile.ZipFile(path) as archive:
        manifest = single._decode(archive.read("manifest.json"))
    if manifest["attribution"] != attribution or _sha(path) != result["sha256"]:
        raise ValueError("Scientific shard changed or has different attribution")
    return result, manifest["tickets"]


class CompletedTickets:
    """Stream all recognized completed folders; malformed candidates fail closed."""
    def __init__(self, root):
        self.root = _plain_path(root, directory=True)
        if self.root.name != "tickets":
            raise ValueError("Select the retained tickets directory explicitly")
        self.selection = dict(mode="observed_completed_directory", active_excluded=0,
                              other_entries_excluded=0)
        self.used = False

    def __iter__(self):
        if self.used:
            raise ValueError("Ticket enumeration cannot be silently replayed")
        self.used = True
        with os.scandir(self.root) as entries:
            for entry in entries:
                if not single.HASH.fullmatch(entry.name):
                    self.selection["other_entries_excluded"] += 1
                    continue
                folder = _plain_path(entry.path, directory=True)
                marker = single.read_json(folder / "session.json", 262144)
                if not isinstance(marker, dict):
                    raise ValueError("Unrecognized retained ticket metadata")
                if marker.get("schema") == "PGN_HORIZON_COMPLETED_TICKET_V1":
                    yield folder
                elif marker.get("schema") == "PGN68_DURABLE_SESSION_V1":
                    if marker.get("state") == "COMPLETE":
                        yield folder
                    elif marker.get("state") in {"PREPARED", "LAUNCH_INTENT", "RUNNING", "PAUSING", "PAUSED"}:
                        self.selection["active_excluded"] += 1
                    else:
                        raise ValueError("Unknown retained ticket state; export not complete")
                else:
                    raise ValueError("Unknown retained ticket schema; export not complete")


def _publish(staging, output):
    # Reserve a new directory exclusively. Commit index last with no-clobber
    # links; a crash before it leaves a recognizably incomplete set. Never
    # replace an existing output, even an empty directory or a concurrent writer.
    output.mkdir()
    linked = []
    try:
        for source in sorted(staging.iterdir(), key=lambda p: (p.name == INDEX, p.name)):
            target = output / source.name
            os.link(source, target)
            linked.append((source, target))
    except BaseException:
        for source, target in reversed(linked):
            if target.exists() and os.path.samestat(source.stat(), target.lstat()):
                target.unlink()
        try:
            output.rmdir()
        except OSError:
            pass
        raise


def create_set(output, folders, genesis, public_name="", maximum_bytes=DEFAULT_BUDGET):
    if not single._hash(genesis):
        raise ValueError("Explicit source genesis fingerprint required")
    single._label(public_name)
    _integer(maximum_bytes, 1)
    output = Path(output).absolute()
    parent = _plain_path(output.parent, directory=True)
    if output.exists() or output.is_symlink():
        raise ValueError("A new backup directory is required")
    attribution = dict(public_name=public_name, source="operator_label_not_network_identity_proof")
    selection = folders.selection if isinstance(folders, CompletedTickets) else dict(
        mode="explicit_completed_tickets", active_excluded=0, other_entries_excluded=0)
    with tempfile.TemporaryDirectory(prefix=".pgn-science-set-", dir=parent) as temporary:
        staging = Path(temporary)
        shards, stored, decoded, sealed = [], 0, 0, 0
        with _ids(parent) as ids:
            batch, payload_bytes = [], 0

            def flush():
                nonlocal stored, decoded, sealed
                if len(shards) >= MAXIMUM_SHARDS:
                    raise ValueError("Backup set exceeds its shard quota")
                name = f"part-{len(shards) + 1:06d}.zip"
                single.create(staging / name, batch, genesis, public_name)
                result, tickets = _verified_shard(staging / name, genesis, attribution)
                _add(ids, tickets)
                stored += result["bytes"]
                decoded += result["decoded_bytes"]
                sealed += result["sealed_reports"]
                if stored > maximum_bytes or decoded > maximum_bytes:
                    raise ValueError("Backup set exceeds its storage or decoded quota")
                shards.append({key: result[key] for key in SHARD_KEYS - {"path"}} | {"path": name})

            for folder in folders:
                folder = _plain_path(folder, directory=True)
                # Preflight decoded size without retaining previous reports.
                # create() rereads/revalidates each completed ticket when sealed.
                _, entries = single.selected_ticket(folder, genesis)
                size = sum(map(len, entries.values()))
                if batch and (len(batch) == single.MAXIMUM_TICKETS or payload_bytes + size > CHUNK_BYTES):
                    flush()
                    batch, payload_bytes = [], 0
                batch.append(folder)
                payload_bytes += size
            if batch:
                flush()
            count, fingerprint = _identity(ids)
        if count == 0:
            raise ValueError("No completed ticket was selected")
        manifest = dict(schema=SCHEMA, genesis=genesis, attribution=attribution, **FLAGS,
                        tickets=count, sealed_reports=sealed, decoded_bytes=decoded,
                        ticket_set_sha256=fingerprint, selection=selection, shards=shards)
        raw = single.canonical_bytes(manifest) + b"\n"
        if len(raw) > MAXIMUM_INDEX or stored + len(raw) > maximum_bytes or decoded + len(raw) > maximum_bytes:
            raise ValueError("Backup set index or aggregate quota exceeded")
        with (staging / INDEX).open("xb") as stream:
            stream.write(raw)
            stream.flush()
            os.fsync(stream.fileno())
        result = verify_set(staging, genesis, maximum_bytes)
        _publish(staging, output)
        return result | {"output": str(output)}


def verify_set(directory, genesis=None, maximum_bytes=DEFAULT_BUDGET):
    _integer(maximum_bytes, 1)
    root = _plain_path(directory, directory=True)
    index = _plain_path(root / INDEX)
    if index.stat().st_size > MAXIMUM_INDEX:
        raise ValueError("Scientific backup set index is oversized")
    with index.open("rb") as stream:
        raw = stream.read(MAXIMUM_INDEX + 1)
    if len(raw) > MAXIMUM_INDEX:
        raise ValueError("Scientific backup set index is oversized")
    manifest = single._decode(raw)
    index_size, index_hash = len(raw), hashlib.sha256(raw).hexdigest()
    if (not isinstance(manifest, dict) or set(manifest) != INDEX_KEYS
            or manifest.get("schema") != SCHEMA or not single._hash(manifest.get("genesis"))
            or any(manifest.get(key) is not value for key, value in FLAGS.items())):
        raise ValueError("Unknown private scientific backup set contract")
    if genesis is not None and genesis != manifest["genesis"]:
        raise ValueError("Scientific backup set has a different genesis")
    attribution, selection, shards = (manifest[key] for key in ("attribution", "selection", "shards"))
    if (not isinstance(attribution, dict) or set(attribution) != {"public_name", "source"}
            or attribution["source"] != "operator_label_not_network_identity_proof"):
        raise ValueError("Unsupported backup set attribution")
    single._label(attribution["public_name"])
    if (not isinstance(selection, dict) or set(selection) != SELECTION_KEYS
            or selection["mode"] not in {"explicit_completed_tickets", "observed_completed_directory"}):
        raise ValueError("Unsupported backup selection contract")
    for key in ("active_excluded", "other_entries_excluded"):
        _integer(selection[key])
    if selection["mode"] == "explicit_completed_tickets" and any(selection[key] for key in SELECTION_KEYS - {"mode"}):
        raise ValueError("Explicit selection cannot claim excluded directory entries")
    for key in ("tickets", "sealed_reports", "decoded_bytes"):
        _integer(manifest[key], 1 if key != "sealed_reports" else 0)
    if not single._hash(manifest["ticket_set_sha256"]) or not isinstance(shards, list) or not 1 <= len(shards) <= MAXIMUM_SHARDS:
        raise ValueError("Invalid scientific backup set inventory")
    names = {INDEX}
    for number, row in enumerate(shards, 1):
        if not isinstance(row, dict) or set(row) != SHARD_KEYS or row["path"] != f"part-{number:06d}.zip":
            raise ValueError("Invalid scientific backup shard name or fields")
        for key in ("bytes", "tickets", "sealed_reports", "decoded_bytes"):
            _integer(row[key], 0 if key == "sealed_reports" else 1)
        if not single._hash(row["sha256"]):
            raise ValueError("Invalid scientific backup shard fingerprint")
        names.add(row["path"])
    # Stream the actual directory inventory, stopping at the first unknown entry.
    observed = 0
    with os.scandir(root) as entries:
        for entry in entries:
            if entry.name not in names:
                raise ValueError("Undeclared scientific backup set entry")
            _plain_path(entry.path)
            observed += 1
    if observed != len(names):
        raise ValueError("Scientific backup set is incomplete")
    if sum(row["bytes"] for row in shards) + index_size > maximum_bytes or manifest["decoded_bytes"] + index_size > maximum_bytes:
        raise ValueError("Backup set exceeds its declared storage or decoded quota")
    stored = decoded = index_size
    sealed = 0
    # Verification can read a backup on read-only media. Its bounded scratch
    # database belongs in the OS temporary directory, never beside the backup.
    with _ids(None) as ids:
        for row in shards:
            path = _plain_path(root / row["path"])
            if path.stat().st_size != row["bytes"]:
                raise ValueError("Scientific shard storage size changed")
            result, tickets = _verified_shard(path, manifest["genesis"], attribution)
            if any(result[key] != row[key] for key in SHARD_KEYS - {"path"}):
                raise ValueError("Scientific shard differs from the committed index")
            _add(ids, tickets)
            stored += result["bytes"]
            decoded += result["decoded_bytes"]
            sealed += result["sealed_reports"]
            if stored > maximum_bytes or decoded > maximum_bytes:
                raise ValueError("Backup set exceeds its storage or decoded quota")
        count, fingerprint = _identity(ids)
    if (count != manifest["tickets"] or fingerprint != manifest["ticket_set_sha256"]
            or sealed != manifest["sealed_reports"] or decoded - index_size != manifest["decoded_bytes"]
            or _sha(index) != index_hash):
        raise ValueError("Global scientific backup conservation check failed")
    return dict(PASS=True, schema=SCHEMA, genesis=manifest["genesis"], tickets=count,
                shards=len(shards), sealed_reports=sealed, bytes=stored, decoded_bytes=decoded,
                ticket_set_sha256=fingerprint, index_sha256=index_hash, selection=selection,
                contains_private_audit_answers=True, network_acceptance_proven=False,
                reset_performed=False, source_modified=False, wallets_included=False)
