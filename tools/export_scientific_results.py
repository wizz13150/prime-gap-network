"""Create/verify a bounded private scientific backup, without resetting a chain.

Only explicitly selected, completed tickets are read. This is not a wallet
backup, network acceptance certificate or a conversion of test coins.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import stat
import sys
import tempfile
import zipfile

PROJECT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(PROJECT / "src/engine/runtime_host"), str(PROJECT / "src/protocol")]
from horizon_ticket_archive import load as compact_ticket
from report_commitment import canonical_bytes, digest, validate_computed_report, verify_report_reveal
from public_attribution import public_name as canonical_public_name

SCHEMA = "PGN_PRIVATE_SCIENTIFIC_BACKUP_V1"
MAXIMUM_TICKETS = 512
MAXIMUM_BYTES = 64 * 1024 * 1024
MAXIMUM_ENTRY = 128 * 1024
HASH = re.compile(r"^[0-9a-f]{64}$")
REPORT_KEYS = {"schema", "ticket_id", "start", "end", "positions", "policy_sha256", "counts", "best"}
SEALED_KEYS = {"schema", "genesis", "request_id", "ticket", "issue_context", "target_numerator", "target_denominator", "commitment_nonce", "computed"}
MANIFEST_KEYS = {"schema", "genesis", "private_backup", "contains_private_audit_answers", "network_acceptance_proven", "keys_or_wallets_included", "reset_or_token_conversion_authorized", "attribution", "tickets", "members"}


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("Duplicate JSON object key")
        result[key] = value
    return result


def _decode(raw):
    def invalid_constant(value):
        raise ValueError("Nonfinite JSON constant")
    return json.loads(raw, object_pairs_hook=_unique_object, parse_constant=invalid_constant)


def read_json(path, maximum):
    path = Path(path)
    if path.is_symlink() or not path.is_file() or path.stat().st_size > maximum:
        raise ValueError("Scientific metadata is unavailable or oversized")
    return _decode(path.read_bytes())


def _hash(value):
    return isinstance(value, str) and HASH.fullmatch(value) is not None


def _merit_integer(value):
    # Native work receipts use canonical decimal strings; earlier local report
    # fixtures use JSON integers. Preserve either scalar representation exactly.
    if isinstance(value, str) and re.fullmatch(r"[1-9][0-9]{0,9}", value):
        value = int(value)
    if type(value) is not int or not 1 <= value <= 2**32 - 1:
        raise ValueError("Canonical bounded merit integer required")
    return value


def checked_report(report, ticket):
    if not isinstance(report, dict) or set(report) != {"body", "sha256", "network_accepted"}:
        raise ValueError("Unsupported computed-report envelope")
    body = report["body"]
    if not isinstance(body, dict) or set(body) != REPORT_KEYS or body.get("ticket_id") != ticket:
        raise ValueError("Unsupported computed-report fields or ticket")
    if any(not isinstance(row, dict) or set(row) != {"gap", "lower_prime", "upper_prime"} for row in body["best"]):
        raise ValueError("Unsupported detailed gap fields")
    work = dict(start=body["start"], length=str(int(body["end"]) - int(body["start"])), ticket=ticket)
    validate_computed_report(report, work)
    return body


def checked_network(network, computed, genesis):
    if not isinstance(network, dict) or set(network) != {"body", "report_root", "scientific_accepted"} or network["scientific_accepted"] is not False:
        raise ValueError("Unsupported local sealed report envelope")
    body = network["body"]
    if not isinstance(body, dict) or set(body) not in (SEALED_KEYS, SEALED_KEYS | {"discoverer"}):
        raise ValueError("Unsupported sealed report fields")
    for key in ("genesis", "request_id", "ticket", "issue_context", "commitment_nonce"):
        if not _hash(body[key]):
            raise ValueError("Canonical sealed report fingerprint required")
    if not _hash(network["report_root"]):
        raise ValueError("Canonical report root required")
    numerator, denominator = _merit_integer(body["target_numerator"]), _merit_integer(body["target_denominator"])
    if not denominator <= numerator <= denominator * 64:
        raise ValueError("Canonical bounded merit ratio required")
    verify_report_reveal(network, network["report_root"])
    if body["computed"] != computed or body["ticket"] != computed["ticket_id"] or body["genesis"] != genesis:
        raise ValueError("Sealed report belongs to a different result or chain")


def selected_ticket(folder, genesis):
    original = Path(folder).absolute()
    folder = original.resolve(strict=True)
    if original.is_symlink() or folder.parent.name != "tickets" or not HASH.fullmatch(folder.name):
        raise ValueError("An exact existing ticket folder is required")
    marker = read_json(folder / "session.json", 262144)
    compact = compact_ticket(folder)
    if compact is not None:
        report = compact["report"]
    else:
        if marker.get("state") != "COMPLETE" or marker.get("current_chunk"):
            raise ValueError("Active or recoverable work cannot be exported as complete")
        report = read_json(folder / "report.json", 65536)
        configuration = marker["configuration"]
        if (configuration["ticket_id"] != folder.name or configuration["start"] != report["body"]["start"]
                or configuration["end"] != report["body"]["end"]
                or configuration["threshold_policy"]["sha256"] != report["body"]["policy_sha256"]):
            raise ValueError("Completed report differs from its durable configuration")
    computed = checked_report(report, folder.name)
    members = {"computed.json": canonical_bytes(report) + b"\n"}
    sealed = folder / "network-report.json"
    if sealed.exists():
        network = read_json(sealed, 65536)
        checked_network(network, computed, genesis)
        members["sealed-report.json"] = canonical_bytes(network) + b"\n"
    # Atomic session publication during inspection must not produce a mixed
    # snapshot. No source file is modified, compacted or pruned by this tool.
    if read_json(folder / "session.json", 262144) != marker:
        raise ValueError("Selected ticket changed during backup inspection")
    return folder.name, members


def _label(name):
    if not isinstance(name, str):
        raise ValueError("Public label must be text")
    # Existing archives used a separate 32-character ASCII operator label.
    # Keep those readable; new profile names use the same canonical contract
    # as the immutable report. Neither label establishes a verified identity.
    if len(name) <= 32 and (not name or re.fullmatch(r"[A-Za-z0-9 ._+-]+", name)):
        return name
    if canonical_public_name(name) != name:
        raise ValueError("Public label must use its canonical profile spelling")
    return name


def create(output, folders, genesis, public_name=""):
    if not _hash(genesis):
        raise ValueError("Explicit source genesis fingerprint required")
    _label(public_name)
    if not 1 <= len(folders) <= MAXIMUM_TICKETS:
        raise ValueError("Select between 1 and 512 completed tickets per backup")
    output = Path(output).absolute()
    parent = output.parent.resolve(strict=True)
    if output.is_symlink() or output.suffix.lower() != ".zip" or output.exists():
        raise ValueError("A new ZIP output path is required; existing backups are never overwritten")
    members, tickets, size = {}, [], 0
    for folder in folders:
        ticket, entries = selected_ticket(folder, genesis)
        if ticket in tickets:
            raise ValueError("Duplicate selected ticket")
        tickets.append(ticket)
        for name, data in entries.items():
            size += len(data)
            if len(data) > MAXIMUM_ENTRY or size > MAXIMUM_BYTES - 512 * 1024:
                raise ValueError("Scientific backup exceeds its byte budget; select a smaller batch")
            members[f"tickets/{ticket}/{name}"] = data
    manifest = dict(schema=SCHEMA, genesis=genesis, private_backup=True,
                    contains_private_audit_answers=True, network_acceptance_proven=False,
                    keys_or_wallets_included=False, reset_or_token_conversion_authorized=False,
                    attribution=dict(public_name=public_name, source="operator_label_not_network_identity_proof"),
                    tickets=sorted(tickets),
                    members=[dict(path=name, bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
                             for name, data in sorted(members.items())])
    manifest_bytes = canonical_bytes(manifest) + b"\n"
    if len(manifest_bytes) > 512 * 1024:
        raise ValueError("Scientific backup manifest exceeds its budget")
    descriptor, temporary = tempfile.mkstemp(prefix=".pgn-science-", suffix=".zip", dir=parent)
    os.close(descriptor)
    try:
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            archive.writestr("manifest.json", manifest_bytes)
            for name, data in sorted(members.items()):
                archive.writestr(name, data)
        with open(temporary, "r+b") as stream:
            os.fsync(stream.fileno())
        result = verify(temporary, genesis)
        if Path(temporary).stat().st_size > MAXIMUM_BYTES:
            raise ValueError("Scientific ZIP exceeds its storage budget")
        # Same-filesystem no-clobber publication; concurrent identical output
        # requests cannot replace one another or expose a partially written ZIP.
        os.link(temporary, parent / output.name)
        result["output"] = str(parent / output.name)
        return result
    finally:
        Path(temporary).unlink(missing_ok=True)


def verify(path, genesis=None):
    path = Path(path)
    if path.is_symlink() or not path.is_file() or path.stat().st_size > MAXIMUM_BYTES:
        raise ValueError("Scientific backup is unavailable or oversized")
    with zipfile.ZipFile(path, "r") as archive:
        infos = archive.infolist()
        if len(infos) > MAXIMUM_TICKETS * 2 + 1:
            raise ValueError("Too many scientific backup members")
        names, total = set(), 0
        for entry in infos:
            total += entry.file_size
            if entry.filename in names or entry.flag_bits & 1 or stat.S_ISLNK(entry.external_attr >> 16):
                raise ValueError("Duplicate, encrypted or redirected scientific backup member")
            names.add(entry.filename)
            budget = 512 * 1024 if entry.filename == "manifest.json" else MAXIMUM_ENTRY
            if entry.file_size > budget or entry.file_size < 0 or total > MAXIMUM_BYTES:
                raise ValueError("Scientific backup decoded budget exceeded")
        manifest = _decode(archive.read("manifest.json"))
        if (not isinstance(manifest, dict) or set(manifest) != MANIFEST_KEYS
                or manifest.get("schema") != SCHEMA or manifest.get("private_backup") is not True
                or manifest.get("contains_private_audit_answers") is not True
                or manifest.get("network_acceptance_proven") is not False
                or manifest.get("keys_or_wallets_included") is not False
                or manifest.get("reset_or_token_conversion_authorized") is not False
                or not _hash(manifest.get("genesis"))):
            raise ValueError("Unknown scientific backup contract")
        if genesis is not None and genesis != manifest["genesis"]:
            raise ValueError("Scientific backup comes from a different network genesis")
        expected = manifest.get("members")
        if not isinstance(expected, list) or not 1 <= len(expected) <= MAXIMUM_TICKETS * 2:
            raise ValueError("Invalid scientific backup inventory")
        if any(not isinstance(item, dict) or set(item) != {"path", "bytes", "sha256"}
               or not isinstance(item["path"], str) for item in expected):
            raise ValueError("Unsupported scientific member metadata")
        attribution = manifest["attribution"]
        tickets = manifest["tickets"]
        if (not isinstance(attribution, dict) or set(attribution) != {"public_name", "source"}
                or not isinstance(tickets, list) or not 1 <= len(tickets) <= MAXIMUM_TICKETS
                or not all(_hash(ticket) for ticket in tickets) or tickets != sorted(set(tickets))):
            raise ValueError("Unsupported scientific identity metadata")
        if names != {"manifest.json"} | {item["path"] for item in expected} or len(names) != len(expected) + 1:
            raise ValueError("Scientific backup inventory is incomplete or contains undeclared members")
        reports, seals = {}, {}
        for entry in expected:
            match = re.fullmatch(r"tickets/([0-9a-f]{64})/(computed|sealed-report)\.json", entry["path"])
            if match is None or type(entry.get("bytes")) is not int or not HASH.fullmatch(entry.get("sha256", "")):
                raise ValueError("Invalid scientific backup member identity")
            raw = archive.read(entry["path"])
            if len(raw) != entry["bytes"] or hashlib.sha256(raw).hexdigest() != entry["sha256"]:
                raise ValueError("Scientific backup member fingerprint changed")
            value = _decode(raw)
            if match[2] == "computed":
                reports[match[1]] = checked_report(value, match[1])
            else:
                seals[match[1]] = value
        if sorted(reports) != manifest.get("tickets") or not seals.keys() <= reports.keys():
            raise ValueError("Scientific backup ticket inventory differs from the results")
        for ticket, network in seals.items():
            checked_network(network, reports[ticket], manifest["genesis"])
        if manifest["attribution"].get("source") != "operator_label_not_network_identity_proof":
            raise ValueError("Unsupported scientific attribution proof")
        _label(manifest["attribution"]["public_name"])
    fingerprint = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(65536), b""):
            fingerprint.update(block)
    return dict(PASS=True, schema=SCHEMA, genesis=manifest["genesis"], tickets=len(reports),
                sealed_reports=len(seals), bytes=path.stat().st_size,
                sha256=fingerprint.hexdigest(),
                decoded_bytes=total, network_acceptance_proven=False,
                reset_performed=False, source_modified=False, wallets_included=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--output", type=Path)
    action.add_argument("--verify", type=Path)
    action.add_argument("--output-directory", type=Path, help="New private backup set, automatically sharded")
    action.add_argument("--verify-directory", type=Path, help="Verify a complete private backup set")
    parser.add_argument("--ticket", action="append", type=Path, default=[])
    parser.add_argument("--tickets-root", type=Path, help="Explicit retained tickets directory; completed work only")
    parser.add_argument("--genesis", required=True)
    parser.add_argument("--public-name", default="")
    parser.add_argument("--maximum-bytes", type=int, default=512 * 1024 * 1024,
                        help="Backup set storage and decoded quota, default 512 MiB")
    args = parser.parse_args()
    if args.tickets_root and (args.ticket or not args.output_directory):
        parser.error("--tickets-root requires --output-directory and cannot be combined with --ticket")
    if args.output_directory or args.verify_directory:
        sys.path.insert(0, str(PROJECT / "tools"))
        from scientific_backup_set import CompletedTickets, create_set, verify_set
        if args.verify_directory:
            result = verify_set(args.verify_directory, args.genesis, args.maximum_bytes)
        else:
            folders = CompletedTickets(args.tickets_root) if args.tickets_root else iter(args.ticket)
            result = create_set(args.output_directory, folders, args.genesis, args.public_name, args.maximum_bytes)
    else:
        result = verify(args.verify, args.genesis) if args.verify else create(args.output, args.ticket, args.genesis, args.public_name)
    print(json.dumps(result, sort_keys=True))


if __name__ == "__main__":
    main()
