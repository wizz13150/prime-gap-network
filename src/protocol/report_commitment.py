"""Canonical private range reports and blind audit commitments.

Only the report hash is published before every assigned audit has committed.
Report publication, scientific verification and payment are separate decisions.
"""
from __future__ import annotations
import hashlib
import json
import secrets

MAX_REPORT_BYTES = 65536
SEGMENTS = 100
AUDIT_PARTITIONS = 10
AUDIT_JOBS = 2


def canonical_bytes(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True, allow_nan=False).encode("ascii")


def digest(value):
    return hashlib.sha256(canonical_bytes(value)).hexdigest()


def decimal(value, maximum=(1 << 95)):
    if not isinstance(value, str) or not value or not value.isascii() or not value.isdecimal():
        raise ValueError("Canonical unsigned decimal string required")
    if len(value) > 29 or (len(value) > 1 and value[0] == "0"):
        raise ValueError("Noncanonical or oversized integer")
    number = int(value)
    if number > maximum:
        raise ValueError("Integer exceeds the protocol domain")
    return number


def hash_text(value):
    if not isinstance(value, str) or len(value) != 64 or any(c not in "0123456789abcdef" for c in value):
        raise ValueError("Canonical SHA-256 text required")
    return value


def validate_computed_report(report, work):
    if len(canonical_bytes(report)) > MAX_REPORT_BYTES or report.get("network_accepted") is not False:
        raise ValueError("A computed report is bounded and has no network acceptance")
    body = report["body"]
    if digest(body) != report["sha256"] or body["schema"] != "PGN_HORIZON_COMPUTED_REPORT_V1":
        raise ValueError("Computed report integrity mismatch")
    start = decimal(body["start"])
    end = decimal(body["end"])
    if start != decimal(work["start"]) or end - start != decimal(work["length"]) or end <= start:
        raise ValueError("Report does not cover the issued half-open ticket")
    if decimal(body["positions"]) != end - start or body["ticket_id"] != hash_text(work["ticket"]):
        raise ValueError("Incomplete report or wrong ticket")
    if not isinstance(body["policy_sha256"], str):
        raise ValueError("Invalid engine policy fingerprint")
    hash_text(body["policy_sha256"].lower())
    counts = body["counts"]
    if not isinstance(counts, list) or len(counts) != SEGMENTS:
        raise ValueError("Exactly one hundred positional counts required")
    total = sum(decimal(count, (1 << 64) - 1) for count in counts)
    if len(body["best"]) > min(100, total):
        raise ValueError("Detailed results exceed report limits")
    lowers = set()
    for gap in body["best"]:
        lower, upper = decimal(gap["lower_prime"]), decimal(gap["upper_prime"], (1 << 95) + (1 << 32))
        width = gap["gap"]
        if type(width) is not int or width <= 0 or width > (1 << 32) or width % 2:
            raise ValueError("Invalid larger-gap length")
        if not start <= lower < end or lower % 2 != 1 or upper != lower + width or lower in lowers:
            raise ValueError("Invalid or duplicated lower-endpoint ownership")
        lowers.add(lower)
    return body


def seal_report(report, work, nonce=None):
    body = validate_computed_report(report, work)
    nonce = secrets.token_hex(32) if nonce is None else hash_text(nonce)
    sealed = dict(schema="PGN_NETWORK_RANGE_REPORT_V1", genesis=hash_text(work["genesis"]),
                  request_id=hash_text(work["request_id"]), ticket=hash_text(work["ticket"]),
                  issue_context=hash_text(work["work_id"]), target_numerator=work["target_numerator"],
                  target_denominator=work["target_denominator"], commitment_nonce=nonce, computed=body)
    if len(canonical_bytes(sealed)) > MAX_REPORT_BYTES:
        raise ValueError("Network report exceeds capacity")
    return dict(body=sealed, report_root=digest(sealed), scientific_accepted=False)


def verify_report_reveal(sealed, expected_root):
    hash_text(expected_root)
    body = sealed["body"]
    if len(canonical_bytes(sealed)) > MAX_REPORT_BYTES or body["schema"] != "PGN_NETWORK_RANGE_REPORT_V1":
        raise ValueError("Unsupported or oversized report reveal")
    hash_text(body["commitment_nonce"])
    if sealed["report_root"] != expected_root or digest(body) != expected_root:
        raise ValueError("Report reveal differs from the immutable commitment")
    computed = body["computed"]
    work = dict(start=computed["start"], length=str(decimal(computed["end"]) - decimal(computed["start"])),
                ticket=body["ticket"])
    validate_computed_report(dict(body=computed, sha256=digest(computed), network_accepted=False), work)
    return body


def audit_partitions(report_root, seed_block):
    """Ten distinct partitions; the seed must be fixed after report confirmation.

    Rejection sampling avoids modulo bias for uniform seed material. A cheap
    testnet block hash is reproducible, not a production unbiased beacon.
    """
    prefix = b"PGN/audit-partitions/1\0" + bytes.fromhex(hash_text(report_root)) + bytes.fromhex(hash_text(seed_block))
    remaining = list(range(SEGMENTS))
    selected, counter = [], 0
    while len(selected) < AUDIT_PARTITIONS:
        value = int.from_bytes(hashlib.sha256(prefix + counter.to_bytes(8, "big")).digest(), "big")
        counter += 1
        bound = (1 << 256) - (1 << 256) % len(remaining)
        if value < bound:
            selected.append(remaining.pop(value % len(remaining)))
    return tuple(tuple(sorted(selected[i:i + 5])) for i in (0, 5))


def audit_commitment(ticket_id, attempt, indices, counts, nonce):
    if type(attempt) is not int or not 0 <= attempt < (1 << 32):
        raise ValueError("Invalid audit attempt")
    if len(indices) != 5 or len(set(indices)) != 5 or tuple(indices) != tuple(sorted(indices)):
        raise ValueError("Five distinct sorted audit partitions required")
    if any(type(i) is not int or not 0 <= i < SEGMENTS for i in indices) or len(counts) != 5:
        raise ValueError("Invalid audit partition result")
    values = [str(decimal(value, (1 << 64) - 1)) for value in counts]
    body = dict(schema="PGN_AUDIT_ANSWER_V1", ticket=hash_text(ticket_id), attempt=attempt,
                partitions=list(indices), counts=values, nonce=hash_text(nonce))
    return dict(body=body, commitment=digest(body))


def verify_audit_reveal(reveal, commitment, ticket_id, attempt, indices, expected):
    body = reveal["body"]
    reconstructed = audit_commitment(ticket_id, attempt, indices, body["counts"], body["nonce"])
    if body != reconstructed["body"] or reconstructed["commitment"] != hash_text(commitment):
        raise ValueError("Audit reveal differs from its prior commitment")
    return all(decimal(actual, (1 << 64) - 1) == decimal(wanted, (1 << 64) - 1)
               for actual, wanted in zip(body["counts"], expected, strict=True))
