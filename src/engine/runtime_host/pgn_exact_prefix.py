"""Exact coverage certificate for a physical fragment shorter than its retained gap.

Every wholly internal gap is too short to retain. The unique gap crossing the
exclusive end is checked by the existing exact adjacent-prime oracle. Progress
may be credited only after this certificate and its owned gap are committed.
"""

import hashlib
import json


def _digest(fields):
    payload = json.dumps(fields, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(payload).hexdigest().upper()


def certify(piece, find_exact_pair):
    """The oracle returns the exact adjacent pair P < end <= Q, including an end prime."""
    if piece.kind != "exact_short_prefix":
        raise ValueError("An exact prefix work piece is required")
    start, end, minimum = int(piece.start), int(piece.end), int(piece.minimum_gap)
    if not 0 < end - start < 30 or minimum < 30:
        raise ValueError(
            "Exact prefix requires fewer than30 positions and minimum gap at least30"
        )
    if start < 2 or start >> 64 != (end - 1) >> 64:
        raise ValueError("Exact prefix must stay within one numerical word")
    policy = piece.policy_sha256
    if (
        not isinstance(policy, str)
        or len(policy) != 64
        or any(ch not in "0123456789abcdefABCDEF" for ch in policy)
    ):
        raise ValueError("Exact prefix requires its immutable policy digest")
    lower, upper = map(int, find_exact_pair(end))
    if not lower < end <= upper or lower < 2:
        raise ValueError("Exact boundary oracle returned an invalid adjacent pair")
    fields = {
        "schema": "SCOUT_EXACT_PREFIX_COVERAGE_V1",
        "start": str(start),
        "end": str(end),
        "minimum_gap": minimum,
        "policy_sha256": policy,
        "lower_prime": str(lower),
        "upper_prime": str(upper),
        "positions": end - start,
        "native_tests": 0,
        "owned_gap": (
            [str(lower), str(upper)]
            if start <= lower and upper - lower >= minimum
            else None
        ),
    }
    return {**fields, "sha256": _digest(fields)}


def validate_structure(certificate, piece):
    """Check integrity/ownership only; the coordinator proves adjacency before journaling."""
    if not isinstance(certificate, dict):
        raise ValueError("Invalid exact prefix certificate")
    fields = {k: v for k, v in certificate.items() if k != "sha256"}
    if certificate.get("sha256") != _digest(fields):
        raise ValueError("Exact prefix certificate digest mismatch")
    pair = (int(certificate["lower_prime"]), int(certificate["upper_prime"]))
    if certificate != certify(piece, lambda end: pair):
        raise ValueError("Exact prefix structure or policy changed")
    return certificate


def verify(certificate, piece, find_exact_pair):
    """Recovery rechecks the pair before accepting an archived coverage frontier."""
    if not isinstance(certificate, dict):
        raise ValueError("Invalid exact prefix certificate")
    fields = {key: value for key, value in certificate.items() if key != "sha256"}
    if certificate.get("sha256") != _digest(fields):
        raise ValueError("Exact prefix certificate digest mismatch")
    if certificate != certify(piece, find_exact_pair):
        raise ValueError("Exact prefix geometry, policy or adjacent pair changed")
    return certificate
