"""Versioned exact retained-merit policy with two-integer gap transitions.

Logical policy intervals end at a threshold or bit boundary. The native work
planner must additionally split them at physical word and allocation limits.
"""
import hashlib
import json
from functools import lru_cache

from pgn_retention_rule import DOMAIN_START, DOMAIN_END, minimum_gap, next_threshold_integer

POLICY_ID = "POINTWISE_RETAINED_MERIT_20_5"
SCHEMA = "PGN_THRESHOLD_POLICY_V2"
MINIMUM_GAP = minimum_gap(DOMAIN_START)


def _sha(value):
    raw = json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(raw).hexdigest().upper()


def _requested(value):
    if str(value).strip().lower() == "auto":
        return "auto"
    if isinstance(value, bool):
        raise ValueError("MIN_GAP must be Auto or an integer")
    number = int(value)
    if str(value).strip() != str(number) or not MINIMUM_GAP <= number <= 2147483647:
        raise ValueError(f"Manual MIN_GAP must be an integer{MINIMUM_GAP}..2147483647")
    return number


def pin_range(start, end, value="auto", count_override=None, detail_override=None):
    start, end = int(start), int(end)
    if not DOMAIN_START <= start < end <= DOMAIN_END:
        raise ValueError("Increasing67..95-bit interval required")
    value = _requested(value)
    overrides = [_requested(x) if x is not None else None for x in (count_override, detail_override)]
    if "auto" in overrides:
        raise ValueError("COUNT/DETAIL override must be an integer or blank")
    cursor = start
    segments = []
    while cursor < end:
        bits = cursor.bit_length()
        base = minimum_gap(cursor) if value == "auto" else value
        stop = min(end, 1 << bits)
        if value == "auto":
            stop = min(stop, next_threshold_integer(base))
        count = base if overrides[0] is None else overrides[0]
        detail = base if overrides[1] is None else overrides[1]
        segments.append(dict(start=str(cursor), end=str(stop), bits=bits,
                             base_gap=base, count_gap=count, detail_gap=detail,
                             effective_gap_min=min(base, count, detail)))
        cursor = stop
    pin = dict(schema=SCHEMA, policy_id=POLICY_ID, requested=value,
               count_override=overrides[0], detail_override=overrides[1],
               start=str(start), end=str(end), segments=segments)
    pin["sha256"] = _sha(pin)
    return pin


@lru_cache(maxsize=64)
def _verify_encoded(encoded):
    """Immutable encoding makes reuse safe even when callers hold mutable dictionaries."""
    pin = json.loads(encoded)
    expected = pin_range(pin["start"], pin["end"], pin["requested"],
                         pin["count_override"], pin["detail_override"])
    if expected != pin:
        raise ValueError("Pointwise threshold coverage or values changed")


def verify_pin(pin, start=None, end=None):
    body = dict(pin)
    digest = body.pop("sha256", None)
    if body.get("schema") != SCHEMA or body.get("policy_id") != POLICY_ID or digest != _sha(body):
        raise ValueError("Pointwise threshold pin changed")
    if start is not None and int(body["start"]) != int(start):
        raise ValueError("Threshold start changed")
    if end is not None and int(body["end"]) != int(end):
        raise ValueError("Threshold end changed")
    if not isinstance(body.get("segments"), list) or not 1 <= len(body["segments"]) <= 256:
        raise ValueError("Invalid pointwise threshold segment count")
    # Always check the digest above; reconstruct each distinct immutable value once.
    _verify_encoded(json.dumps(pin, sort_keys=True, separators=(",", ":")))
    return pin
