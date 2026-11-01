"""Canonical native Gold proof assembly; Core alone verifies and accepts payment."""
import base64
import json
import os
import subprocess
from pathlib import Path

def compact(number):
    if not 0 <= number < 1 << 64:
        raise ValueError("Invalid compact size")
    if number < 253:
        return bytes([number])
    size, prefix = (2, 253) if number <= 65535 else (4, 254) if number <= 0xffffffff else (8, 255)
    return bytes([prefix]) + number.to_bytes(size, "little")

def canonical_hex(text, length=None):
    value = bytes.fromhex(text)
    if value.hex() != text or (length is not None and len(value) != length):
        raise ValueError("Canonical bounded hex required")
    return value

def push(value):
    size = len(value)
    if size <= 75:
        return bytes([size]) + value
    if size <= 255:
        return b"\x4c" + bytes([size]) + value
    if size <= 65535:
        return b"\x4d" + size.to_bytes(2, "little") + value
    if size <= 4 * 1024 * 1024:
        return b"\x4e" + size.to_bytes(4, "little") + value
    raise ValueError("Proof script exceeds budget")

def assemble(work, evidence, signature):
    if evidence.get("verified") is not True or evidence.get("network_accepted") is not False:
        raise ValueError("Exact local gap evidence is required")
    root = canonical_hex(evidence["details_root"], 32)
    if root.hex() != work["report_root"]:
        raise ValueError("Evidence root differs from the audited details commitment")
    signed = base64.b64decode(signature, validate=True)
    if base64.b64encode(signed).decode() != signature or not 0 < len(signed) <= 10000:
        raise ValueError("Canonical bounded BIP322 signature required")
    segment = canonical_hex(evidence["segment_bytes"])
    scientific = canonical_hex(evidence["scientific_bytes"])
    siblings = [canonical_hex(s, 32) for s in evidence["siblings"]]
    if not 0 <= evidence["segment_index"] < 100 or len(siblings) > 32 or not 0 <= evidence["record_index"] < 100:
        raise ValueError("Invalid committed gap position")
    def field(value):
        return compact(len(value)) + value
    payload = (b"PGN/SCIENCE-BLOCK/1\0" + canonical_hex(work["request_id"], 32)[::-1] + root + field(signed) + field(segment) + evidence["segment_index"].to_bytes(4, "little") + compact(len(siblings)) + b"".join(siblings) + evidence["record_index"].to_bytes(8, "little") + field(scientific))
    return (b"\x6a" + push(b"PGNPROOF-UNAPPROVED-1") + push(payload)).hex()

def build(builder, work, computed, lower):
    builder = Path(builder)
    if not builder.is_file():
        raise RuntimeError("The native exact evidence builder is unavailable")
    request = dict(work=work, computed=computed, lower_prime=str(lower))
    result = subprocess.run([str(builder)], input=json.dumps(request).encode(), capture_output=True, timeout=40, creationflags=0x08000000 if os.name == "nt" else 0)
    if len(result.stdout) > 4 * 1024 * 1024 or len(result.stderr) > 16384:
        raise ValueError("Native proof output exceeds budget")
    evidence = json.loads(result.stdout)
    if result.returncode or evidence.get("verified") is not True:
        raise RuntimeError(evidence.get("error", "Exact gap proof was not produced"))
    return evidence
