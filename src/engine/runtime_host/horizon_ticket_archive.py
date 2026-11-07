"""Compact immutable completed tickets; active/recoverable work is never pruned."""
from pathlib import Path
from contextlib import nullcontext
import datetime as dt
import gzip
import hashlib
import json
import zlib
import heapq
import os
import re

from pgn_session import SessionControllerLock, atomic_bytes, read_session_metadata

SCHEMA = "PGN_HORIZON_COMPLETED_TICKET_V1"
MAXIMUM_BYTES = 128 * 1024
ARCHIVE_NAME = "completed-ticket.json.gz"
WORK_FILES = (
    "session-events.jsonl",
    "session-index.sqlite3",
    "session-index.sqlite3-wal",
    "session-index.sqlite3-shm",
)
BATCH_CURSOR_SCHEMA = "PGN_HORIZON_RETENTION_CURSOR_V1"
OWNER_RETIREMENT_SCHEMA = "PGN_ARCHIVED_OWNER_OUTPUT_V1"
MAXIMUM_OWNER_JOBS = 4096


def ticket_folder(folder):
    folder = Path(folder).resolve()
    if (folder.parent.name != "tickets" or len(folder.name) != 64
            or any(c not in "0123456789abcdef" for c in folder.name)):
        raise ValueError("An exact Horizon ticket folder is required")
    return folder


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode("utf-8")


def read_json(path, maximum=MAXIMUM_BYTES):
    path = Path(path)
    if path.is_symlink() or not path.is_file() or path.stat().st_size > maximum:
        raise ValueError("Completed-ticket metadata is unavailable or oversized")
    return json.loads(path.read_bytes())


def validate(body, ticket):
    configuration = body["configuration"]
    report = body["report"]
    computed = report["body"]
    frame = body["frame"]
    if (body.get("schema") != SCHEMA or configuration["ticket_id"] != ticket
            or body["configuration_sha256"] != hashlib.sha256(canonical(configuration)).hexdigest()
            or report.get("network_accepted") is not False
            or report["sha256"] != hashlib.sha256(canonical(computed)).hexdigest()
            or computed.get("schema") != "PGN_HORIZON_COMPUTED_REPORT_V1"
            or computed["ticket_id"] != ticket or frame["ticket_id"] != ticket
            or computed["start"] != configuration["start"]
            or computed["end"] != configuration["end"]
            or int(computed["positions"]) != int(computed["end"]) - int(computed["start"])
            or computed["policy_sha256"] != configuration["threshold_policy"]["sha256"]
            or frame["start"] != computed["start"] or frame["end"] != computed["end"]
            or frame["positions"] != computed["positions"]
            or len(computed["counts"]) != 100 or len(computed["best"]) > 100
            or frame.get("phase") != "Report ready"):
        raise ValueError("Completed-ticket identity, configuration or result changed")
    statistics = frame["scopes"]["ticket"]["statistics"]
    for count in computed["counts"]:
        if (not isinstance(count, str) or not count.isascii() or not count.isdecimal()
                or len(count) > 20 or len(count) > 1 and count[0] == "0"
                or int(count) > (1 << 64) - 1):
            raise ValueError("Completed-ticket count is not a canonical bounded integer")
    if int(statistics["count_total"]) != sum(int(n) for n in computed["counts"]):
        raise ValueError("Completed-ticket counts disagree with the retained statistics")


def _load(folder,verify_audit_attribution):
    """Return None for ordinary work; corrupt compact work fails closed."""
    folder = ticket_folder(folder)
    marker = read_json(folder / "session.json", 262144)
    if marker.get("schema") != SCHEMA:
        return None
    if marker.get("archive") != ARCHIVE_NAME or marker.get("ticket_id") != folder.name:
        raise ValueError("Completed-ticket archive binding changed")
    archive = folder / ARCHIVE_NAME
    if archive.is_symlink() or archive.stat().st_size > MAXIMUM_BYTES:
        raise ValueError("Completed-ticket archive exceeds its storage budget")
    encoded = archive.read_bytes()
    if hashlib.sha256(encoded).hexdigest() != marker.get("sha256"):
        raise ValueError("Completed-ticket archive fingerprint changed")
    decoder = zlib.decompressobj(16 + zlib.MAX_WBITS)
    raw = decoder.decompress(encoded, MAXIMUM_BYTES + 1)
    if (len(raw) > MAXIMUM_BYTES or not decoder.eof
            or decoder.unused_data or decoder.unconsumed_tail):
        raise ValueError("Completed-ticket archive exceeds its decoded budget")
    body = json.loads(raw)
    validate(body, folder.name)
    if 'scientific_gap_archive' in body:
        from horizon_scientific_gaps import check_descriptor
        check_descriptor(folder,body['scientific_gap_archive'],body['configuration'])
    if 'scientific_finalization' in body:
        from horizon_archive_repair import verify_archived
        verify_archived(folder,body)
    if verify_audit_attribution:_audit_attribution(folder,body)
    return body


def load(folder):
    """Checked result/presentation read; partial audit attribution fails closed."""
    return _load(folder,True)


def audit_handoff_context(folder):
    """Only immutable context for replaying an interrupted attribution write.

    Gzip/marker, configuration, report and scientific correction remain checked.
    The caller must restore the exact local audit receipt before presenting it.
    """
    return _load(folder,False)


def _audit_attribution(folder,body):
    from horizon_audit_partition_archive import NAME,read
    present=any((folder/name).exists()for name in (NAME,NAME+'.sha256'))
    declared=body['frame'].get('work_role')=='audit' or body.get('scientific_finalization',{}).get('handoff_kind')=='audit_count'
    if present or declared:
        read(folder,body['configuration'],body['report'])
        if body['frame'].get('work_role') not in(None,'audit'):
            raise ValueError('Audit archive cannot claim personal search attribution')
        return True
    return False


def result(folder, configuration):
    body = load(folder)
    if body is None:
        return None
    if body["configuration"] != configuration:
        raise ValueError("Issued work differs from the immutable completed ticket")
    return body["report"]


def frame(folder):
    body = load(folder)
    if body is None:
        return None
    value = dict(body["frame"])
    if _audit_attribution(Path(folder),body):
        value.update(work_role='audit',record_pairs=[],
                     validation_state='Independent audit computation; original discovery attribution retained')
    value["source_observed_utc"] = value.get("observed_utc")
    value["observed_utc"] = dt.datetime.now(dt.timezone.utc).isoformat()
    value["archived"] = True
    value['scientific_data_state']=('exact_local_observations_preserved'
        if 'scientific_gap_archive'in body else 'legacy_counts_and_best_only')
    value["source_age_ns"] = None
    for key in ("speed_rate", "average_rate", "compute_tests_rate", "eta_rate"):
        value[key] = None
    return value


def _scientific_retention_hold(folder,body):
    if body['configuration'].get('preseal_required')is not True:return None
    if 'scientific_gap_archive'not in body:
        return 'Legacy compact archive lacks exact scientific gap pairs; remaining source retained'
    from horizon_scientific_gaps import verify
    verify(folder,body['scientific_gap_archive'],body['configuration'])
    return None


def _purge_work_files(folder,body=None):
    body=load(folder)if body is None else body
    if body is None:raise ValueError('Complete archive proof is required before work cleanup')
    hold=_scientific_retention_hold(folder,body)
    if hold:raise ValueError(hold)
    removed = 0
    for name in WORK_FILES:
        original = folder / name
        file = original.resolve()
        if original.is_symlink() or file.parent != folder or file.name != name:
            raise ValueError("Completed-ticket cleanup escaped its exact folder")
        if file.exists():
            size = file.stat().st_size
            file.unlink()
            removed += size
    return removed


def _finalization_hold(folder, configuration, report):
    required = configuration.get('preseal_required', False)
    if type(required) is not bool:
        raise ValueError('Invalid scientific finalization retention policy')
    if not required:
        return None
    from horizon_audit_partition_archive import NAME as AUDIT_HANDOFF,completed as audit_completed
    if (folder/AUDIT_HANDOFF).exists():
        if audit_completed(folder,configuration,report)is None:
            return 'Audit count handoff pending; complete source index retained'
        return None
    from horizon_preseal_queue import PresealQueue, MAX_FILE_BYTES, validate_final
    from report_commitment import canonical_bytes, validate_computed_report, verify_report_reveal
    paths = [folder / name for name in ('preseal-input.json', 'preseal-final.json', 'network-report.json')]
    if not all(p.exists() for p in paths):
        return 'Scientific finalization pending; complete source index retained'
    source = PresealQueue.read(paths[0], MAX_FILE_BYTES)
    final = PresealQueue.read(paths[1], MAX_FILE_BYTES)
    sealed = PresealQueue.read(paths[2], 65536)
    if hashlib.sha256(canonical_bytes(source)).hexdigest() != final.get('input_sha256'):
        raise ValueError('Finalization input binding changed; complete index retained')
    work, original, corrected = source['work'], source['computed'], final['computed']
    if work['ticket'] != folder.name:
        raise ValueError('Finalization belongs to another ticket')
    validate_computed_report(original, work)
    validate_final(original, corrected, work)
    verify_report_reveal(sealed, sealed['report_root'])
    if (sealed['body']['request_id'] != work['request_id']
            or sealed['body']['ticket'] != folder.name
            or sealed['body']['computed'] != corrected['body']):
        raise ValueError('Finalization differs from the sealed handoff')
    if original != report and corrected != report:
        return 'Corrected scientific projection awaits archival reconciliation; complete source index retained'
    if original!=corrected and not all((folder/name).is_file()for name in ('source-finalization.json','source-finalization.sha256')):
        return 'Corrected scientific projection awaits archival reconciliation; complete source index retained'
    return None


def compact(folder, dry_run=False):
    """Commit a checked archive before deleting any redundant work file.

    An old host rejects the new session schema rather than creating an empty
    index. An interruption after marker publication can only leave extra files;
    retrying finishes their deletion without recomputing or changing results.
    """
    folder = ticket_folder(folder)
    lock = SessionControllerLock(folder)
    try:
        existing = load(folder)
        if existing is not None:
            hold = _finalization_hold(folder, existing['configuration'], existing['report'])
            if hold:
                return {"compacted": False, "reason": hold}
            hold=_scientific_retention_hold(folder,existing)
            if hold:return {"compacted":False,"reason":hold}
            return {"compacted": True, "reclaimed_bytes": 0 if dry_run else _purge_work_files(folder,existing),
                    "already_compacted": True}
        metadata = read_session_metadata(folder)
        if metadata.get("state") != "COMPLETE" or metadata.get("current_chunk"):
            return {"compacted": False, "reason": "Active or recoverable work retained"}
        if metadata['configuration'].get('preseal_required', False) is not False:
            hold = _finalization_hold(folder, metadata['configuration'], read_json(folder / 'report.json', 65536))
            if hold:
                return {"compacted": False, "reason": hold}
        # A resident can still write in the folder that hosted its first job.
        # Its signed/durable closure must exist before touching that workspace.
        for owner in folder.glob("native-owner-*"):
            if not owner.is_dir():
                continue
            done = owner / "done.json"
            if not done.is_file():
                return {"compacted": False, "reason": "Resident owner retained"}
            closed = read_json(done, 32768)
            if closed.get("closed") is not True or closed.get("exit") != 0 or closed.get("error") is not None:
                return {"compacted": False, "reason": "Unsettled owner retained"}
        from horizon_status_stream import ticket_frame
        body = {
            "schema": SCHEMA,
            "configuration": metadata["configuration"],
            "configuration_sha256": hashlib.sha256(canonical(metadata["configuration"])).hexdigest(),
            "report": read_json(folder / "report.json", 65536),
            "frame": ticket_frame(folder),
        }
        if metadata['configuration'].get('preseal_required',False):
            from horizon_audit_partition_archive import NAME as AUDIT_HANDOFF,completed as audit_completed
            from horizon_preseal_queue import MAX_FILE_BYTES
            audit=audit_completed(folder,metadata['configuration'],body['report'])if(folder/AUDIT_HANDOFF).exists()else None
            if audit is not None:
                original,corrected,receipt=audit
                from horizon_archive_repair import reconcile
                body['report'],body['frame'],body['scientific_finalization']=reconcile(
                    folder,metadata['configuration'],original,corrected,body['frame'])
                body['scientific_finalization'].update(handoff_kind='audit_count',audit_receipt_sha256=receipt)
            else:
                source=read_json(folder/'preseal-input.json',MAX_FILE_BYTES)
                final=read_json(folder/'preseal-final.json',MAX_FILE_BYTES)
            if audit is None and source['computed']!=final['computed']:
                if body['report']!=source['computed']:
                    raise ValueError('Original source report changed before archival reconciliation')
                from horizon_archive_repair import reconcile
                body['report'],body['frame'],body['scientific_finalization']=reconcile(
                    folder,metadata['configuration'],source['computed'],final['computed'],body['frame'])
        if _audit_attribution(folder,body):
            body['frame'].update(work_role='audit',record_pairs=[])
        validate(body, folder.name)
        sealed = folder / "network-report.json"
        if sealed.exists():
            network = read_json(sealed, 65536)
            if (network["body"]["computed"] != body["report"]["body"]
                    or hashlib.sha256(canonical(network["body"])).hexdigest() != network["report_root"]):
                raise ValueError("Sealed audit report differs; original workspace retained")
        raw = canonical(body)
        if len(raw) > MAXIMUM_BYTES:
            raise ValueError("Completed-ticket state exceeds its archive budget; original retained")
        encoded = gzip.compress(raw, mtime=0)
        if dry_run:
            return {"compacted": True, "dry_run": True, "archive_bytes": len(encoded),
                    "reclaimable_bytes": sum((folder / name).stat().st_size
                                             for name in WORK_FILES if (folder / name).is_file())}
        if metadata['configuration'].get('preseal_required')is True:
            from horizon_scientific_gaps import create
            body['scientific_gap_archive']=create(folder,metadata['configuration'])
            raw=canonical(body)
            if len(raw)>MAXIMUM_BYTES:
                raise ValueError('Scientific archive descriptor exceeds compact-state budget; originals retained')
            encoded=gzip.compress(raw,mtime=0)
        archive = folder / ARCHIVE_NAME
        atomic_bytes(archive, encoded)
        if gzip.decompress(archive.read_bytes()) != raw:
            raise ValueError("Completed-ticket archive did not survive its write")
        marker = {"schema": SCHEMA, "ticket_id": folder.name, "archive": ARCHIVE_NAME,
                  "sha256": hashlib.sha256(encoded).hexdigest()}
        # This is the commit point. Both new readers use the immutable archive;
        # previous readers reject its schema before opening an empty index.
        atomic_bytes(folder / "session.json", canonical(marker) + b"\n")
        load(folder)
        reclaimed = _purge_work_files(folder,body)
        return {"compacted": True, "reclaimed_bytes": reclaimed, "archive_bytes": len(encoded)}
    finally:
        lock.release()


def _plain_directory(path):
    # Junctions are reparse points on Windows but are not necessarily symlinks.
    return (path.is_dir() and not path.is_symlink()
            and not getattr(path.lstat(), "st_file_attributes", 0) & 0x400)


def _plain_file(path):
    return (path.is_file() and not path.is_symlink()
            and not getattr(path.lstat(), "st_file_attributes", 0) & 0x400)


def retire_completed_owner_output(owner, dry_run=False):
    """Retire only an ACKed, closed stream whose every job is archived.

    A resident can serve other tickets than the folder containing its output.
    A closed first ticket, process exit, or reader EOF alone is insufficient.
    The immutable retirement receipt is committed before unlinking exact sealed
    segments. An interrupted deletion is idempotent; reports, checkpoints, job
    bindings and recovery receipts are never removed by this operation.
    """
    from pgn_output_spool import SCHEMA as SPOOL_SCHEMA, BINDING_SCHEMA, verified_segments, _binding_name
    owner = Path(owner)
    token = owner.name.removeprefix("native-owner-")
    if (not re.fullmatch(r"[0-9a-f]{32}", token)
            or not _plain_directory(owner) or not _plain_directory(owner.parent)
            or not _plain_directory(owner.parent.parent)
            or owner.parent.parent.name != "tickets"):
        raise ValueError("An exact plain ticket output owner is required")
    folder = ticket_folder(owner.parent)
    if not _plain_file(folder / "session.json"):
        raise ValueError("Owner ticket marker is redirected")
    if load(folder) is None:
        return {"retired": False, "reason": "Owner ticket is not archived"}
    output = owner / "output"
    if not _plain_directory(output):
        return {"retired": False, "reason": "No current bounded output stream"}

    def document(path, maximum=65536):
        if not _plain_file(path):
            raise ValueError("Owner output metadata must be a plain file")
        value = read_json(path, maximum)
        if not isinstance(value, dict):
            raise ValueError("Owner output metadata must be an object")
        return value

    if not (owner / "done.json").exists():
        return {"retired": False, "reason": "Resident is still active"}
    final = document(owner / "done.json")
    if (final.get("schema") != "PGN_OWNER_OUTPUT_V2" or final.get("token") != token
            or final.get("closed") is not True or final.get("started") is not True
            or final.get("orphan") is not False or type(final.get("exit")) is not int
            or final["exit"] != 0 or final.get("error") is not None):
        return {"retired": False, "reason": "Unsettled or historical owner retained"}
    seal = document(output / "index.json")
    if (seal != final.get("output_spool") or seal.get("schema") != SPOOL_SCHEMA
            or seal.get("token") != token or seal.get("finished") is not True):
        raise ValueError("Closed owner output seal changed")
    segments = seal.get("segments")
    if not isinstance(segments, list):
        raise ValueError("Closed owner output segment table changed")
    if not segments:
        return {"retired": False, "reason": "No sealed output remains"}
    if len(segments) > 128 or any(not isinstance(s, dict) for s in segments):
        raise ValueError("Closed owner output segment table exceeds its bounded shape")
    ack = document(output / "ack.json") if (output / "ack.json").exists() else {}
    last = segments[-1]
    if (ack.get("schema") != SPOOL_SCHEMA or ack.get("token") != token
            or type(ack.get("end_offset")) is not int
            or ack["end_offset"] != seal.get("end_offset")
            or any(ack.get(k) != last.get(k) for k in ("index", "sha256", "link"))):
        return {"retired": False, "reason": "Unacknowledged output retained"}
    # Bound the catalogue before loading job bodies. Never follow a supplied path.
    names = []
    with os.scandir(owner) as entries:
        for entry in entries:
            if re.fullmatch(r"job-[0-9]{12}-[0-9a-f]{32}\.json", entry.name):
                names.append(entry.name)
                if len(names) > MAXIMUM_OWNER_JOBS:
                    raise ValueError("Owner job catalogue exceeds maintenance budget")
    names.sort()
    if not names:
        return {"retired": False, "reason": "No bound job archive authority"}
    bindings, archives = [], {}
    for name in names:
        binding = document(owner / name)
        if (binding.get("schema") != BINDING_SCHEMA or binding.get("token") != token
                or name != _binding_name(binding.get("sequence"), binding.get("nonce"))
                or binding.get("identity") != name[:-5] + ".identity.json"):
            raise ValueError("Archived owner job binding changed")
        identity_path = owner / binding["identity"]
        identity = document(identity_path)
        if hashlib.sha256(identity_path.read_bytes()).hexdigest() != binding.get("identity_sha256"):
            raise ValueError("Archived owner job identity changed")
        ticket = identity.get("ticket_id", "")
        if not isinstance(ticket, str) or not re.fullmatch(r"[0-9a-f]{64}", ticket):
            return {"retired": False, "reason": "Historical job lacks exact ticket identity"}
        target = folder.parent / ticket
        if not _plain_directory(target):
            raise ValueError("Archived owner job ticket is unavailable or redirected")
        marker_path = target / "session.json"
        if not _plain_file(marker_path):
            raise ValueError("Archived owner ticket marker is redirected")
        if (target / ARCHIVE_NAME).exists() and not _plain_file(target / ARCHIVE_NAME):
            raise ValueError("Archived owner ticket archive is redirected")
        body = load(target)
        if body is None:
            return {"retired": False, "reason": "Another bound ticket still needs recovery"}
        hold=_scientific_retention_hold(target,body)
        if hold:return {"retired":False,"reason":hold}
        if str(identity.get("configuration_sha256", "")).lower() != body["configuration_sha256"]:
            raise ValueError("Archived owner job configuration changed")
        archives[ticket] = hashlib.sha256(marker_path.read_bytes()).hexdigest()
        bindings.append(hashlib.sha256(canonical(binding)).hexdigest())
    executed = final.get("output_job") or {}
    if (not isinstance(executed, dict) or executed.get("binding") != binding
            or executed.get("binding_sha256") != bindings[-1]):
        raise ValueError("Final executed output job differs from the archived catalogue")
    receipt = dict(schema=OWNER_RETIREMENT_SCHEMA, token=token, archives=archives,
                   bindings=bindings, seal=seal, done_sha256=hashlib.sha256(canonical(final)).hexdigest())
    raw = canonical(receipt)
    if len(raw) > 1024 * 1024:
        raise ValueError("Owner retirement receipt exceeds its metadata budget")
    marker = owner / "archived-output.json"
    if marker.exists() or marker.is_symlink():
        if document(marker, 1024 * 1024) != receipt:
            raise ValueError("Owner retirement authority changed")
        # The original complete seal was verified before receipt publication.
        # It may now have missing segments after an interrupted unlink.
    else:
        verified_segments(output, seal, token)
    reclaimed = 0
    for segment in segments:
        name = f'{segment["index"]:012d}.log'
        path = output / name
        if path.exists() or path.is_symlink():
            if (not _plain_file(path) or path.resolve().parent != output.resolve()
                    or path.stat().st_size != segment["bytes"]
                    or hashlib.sha256(path.read_bytes()).hexdigest() != segment["sha256"]):
                raise ValueError("Retiring output segment changed")
            reclaimed += path.stat().st_size
    if not dry_run:
        if not marker.exists():
            atomic_bytes(marker, raw + b"\n")
        for segment in segments:
            (output / f'{segment["index"]:012d}.log').unlink(missing_ok=True)
    return {"retired": True, "reclaimed_bytes": 0 if dry_run else reclaimed,
            "reclaimable_bytes": reclaimed if dry_run else 0}


def _batch_names(tickets, after, limit):
    """Scan names only, retaining at most one batch rather than the catalogue."""
    def eligible(entries):
        for entry in entries:
            name = entry.name
            if (len(name) == 64 and all(c in "0123456789abcdef" for c in name)
                    and (after is None or name > after)
                    and entry.is_dir(follow_symlinks=False)
                    and not getattr(entry.stat(follow_symlinks=False), "st_file_attributes", 0) & 0x400):
                yield name
    with os.scandir(tickets) as entries:
        return heapq.nsmallest(limit, eligible(entries))


def compact_batch(root, limit=512, dry_run=False, *, _ticket_guard=None):
    """Resume bounded maintenance fairly; repeat work safely after interruption.

    The cursor only selects candidates. Each ticket still has to pass compact's
    full identity, completeness, owner-closure and archive conservation checks.
    A separate maintenance lock never takes the running host's controller lock.
    """
    if not 1 <= limit <= 4096:
        raise ValueError("Batch size must be between 1 and 4096")
    root = Path(root).resolve()
    tickets = root / "tickets"
    if not _plain_directory(tickets):
        raise ValueError("An existing plain Horizon tickets directory is required")
    maintenance = root / ".retention-maintenance"
    lock = None
    if not dry_run:
        maintenance.mkdir(exist_ok=True)
    if maintenance.exists():
        if not _plain_directory(maintenance):
            raise ValueError("Retention maintenance directory must not be redirected")
        if not dry_run:
            lock = SessionControllerLock(maintenance)
    summary = {"schema": "PGN_HORIZON_RETENTION_RESULT_V1", "dry_run": dry_run,
               "visited": 0, "compacted": 0, "retained": 0, "deferred": 0,
               "reclaimed_bytes": 0, "reclaimable_bytes": 0, "owners_retired": 0, "errors": []}
    try:
        cursor = maintenance / "cursor.json"
        after = None
        if cursor.exists() or cursor.is_symlink():
            saved = read_json(cursor, 2048)
            if not isinstance(saved, dict):
                raise ValueError("Retention cursor must be an object")
            after = saved.get("after")
            if (set(saved) != {"schema", "tickets", "after"}
                    or saved.get("schema") != BATCH_CURSOR_SCHEMA
                    or saved.get("tickets") != str(tickets)
                    or not isinstance(after, str) or len(after) != 64
                    or any(c not in "0123456789abcdef" for c in after)):
                raise ValueError("Retention cursor is invalid or belongs to another catalogue")
        names = _batch_names(tickets, after, limit)
        wrapped = after is not None and not names
        if wrapped:
            names = _batch_names(tickets, None, limit)
        summary.update(resumed_after=after, wrapped=wrapped)
        for name in names:
            folder = tickets / name
            summary["visited"] += 1
            try:
                guard = _ticket_guard(folder) if _ticket_guard else nullcontext(True)
                with guard as allowed:
                    if not allowed:
                        summary["retained"] += 1
                    else:
                        outcome = compact(folder, dry_run=dry_run)
                        summary["compacted" if outcome["compacted"] else "retained"] += 1
                        summary["reclaimed_bytes"] += outcome.get("reclaimed_bytes", 0)
                        summary["reclaimable_bytes"] += outcome.get("reclaimable_bytes", 0)
                        if outcome["compacted"]:
                            for owner in folder.glob("native-owner-*"):
                                if owner.is_dir():
                                    retired = retire_completed_owner_output(owner, dry_run=dry_run)
                                    summary["owners_retired"] += int(retired["retired"])
                                    summary["reclaimed_bytes"] += retired.get("reclaimed_bytes", 0)
                                    summary["reclaimable_bytes"] += retired.get("reclaimable_bytes", 0)
            except (OSError, ValueError, KeyError, RuntimeError) as error:
                summary["deferred"] += 1
                if len(summary["errors"]) < 8:
                    summary["errors"].append({"ticket": name, "reason": str(error)[:240]})
            # Persist only after this exact candidate was considered. A crash
            # before this write repeats an already-idempotent compact operation.
            # Deferred/active candidates are revisited on the next full cycle.
            if not dry_run:
                atomic_bytes(cursor, canonical({"schema": BATCH_CURSOR_SCHEMA,
                                                "tickets": str(tickets), "after": name}) + b"\n")
        summary["next_after"] = names[-1] if names else after
        return summary
    finally:
        if lock is not None:
            lock.release()


def main(argv=None):
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compact-completed", type=Path, required=True)
    parser.add_argument("--limit", type=int, default=512)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args(argv)
    try:
        summary = compact_batch(args.compact_completed, args.limit, args.dry_run)
    except (OSError, ValueError, RuntimeError) as error:
        parser.error(str(error))
    print(json.dumps(summary), flush=True)
    return 0
