"""Prepare a public operator-enrollment proposal through local authenticated RPC.

Coordinator tool: this does not sign, publish, allocate work or change a wallet.
Use one stable operator identifier for all signing-address aliases belonging to
the same operator. Distinct addresses alone do not establish independence.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src/engine/runtime_host"))
from horizon_core_rpc import CoreRpc

GENESIS = "74530feede52b8cda4fa19077747deededef0562844762a43cce462efd29102d"


def prepare(port, cookie, authority, authority_request, participant, operator_id, allow_audits):
    if not 1 <= port <= 65535 or not cookie.is_file() or cookie.stat().st_size > 4096:
        raise ValueError("Bounded local Core authentication is required")
    for identity in (authority_request, operator_id):
        if not re.fullmatch("[0-9a-f]{64}", identity) or identity == "0" * 64:
            raise ValueError("Canonical nonzero request and operator identifiers are required")
    if any(not address or len(address) > 128 for address in (authority, participant)):
        raise ValueError("Bounded public signing addresses are required")
    rpc = CoreRpc(port, cookie, GENESIS, "")
    rpc.verify()
    address = rpc.call("validateaddress", [participant])
    script = address.get("scriptPubKey")
    if address.get("isvalid") is not True or not isinstance(script, str) or not re.fullmatch("[0-9a-f]{2,20000}", script) or len(script) % 2:
        raise ValueError("Participant must have a valid native signing address")
    body = {"schema": "PGN_TESTNET_PARTICIPANT_V1", "signing_script": script,
            "operator_id": operator_id, "may_audit": bool(allow_audits)}
    encoded = json.dumps(body, sort_keys=True, separators=(",", ":")).encode("utf-8").hex()
    proposal = rpc.call("getpgnauditevent", ["enroll", authority, authority_request, 0, 0, encoded])
    if proposal.get("available") is not True or proposal.get("accepted") is not False:
        raise ValueError("No authority enrollment proposal is available")
    return {
        "schema": "PGN_TESTNET_ENROLLMENT_PLAN_V1", "read_only": True,
        "genesis": GENESIS, "parent": proposal["parent"],
        "authority": authority, "authority_request": authority_request,
        "participant": participant, "body": body, "body_hex": encoded,
        "challenge_sha256": hashlib.sha256(proposal["challenge"].encode("utf-8")).hexdigest(),
        "signed": False, "published": False, "accepted": False,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--cookie", type=Path, required=True)
    parser.add_argument("--authority", required=True)
    parser.add_argument("--authority-request", required=True)
    parser.add_argument("--participant", required=True)
    parser.add_argument("--operator-id", required=True)
    parser.add_argument("--allow-audits", action="store_true")
    arguments = parser.parse_args()
    try:
        result = prepare(arguments.port, arguments.cookie, arguments.authority,
                         arguments.authority_request, arguments.participant,
                         arguments.operator_id, arguments.allow_audits)
    except Exception as error:
        raise SystemExit(type(error).__name__ + ": enrollment plan unavailable") from None
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
