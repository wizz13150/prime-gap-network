"""Plan or apply one explicit testnet admission through an already loaded wallet.

The default is read-only. --apply requires a state directory and the native
authority's owned signing address. One operator ID must cover all that person's
address aliases. This tool never creates/loads a wallet or exports a key.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src/engine/runtime_host"))
from horizon_core_rpc import CoreRpc
from horizon_coordination import confirm_candidate
from pgn_session import SessionControllerLock, atomic_bytes

CAPACITY = 262144


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")


def save(file, value):
    encoded = canonical(value) + b"\n"
    if len(encoded) > CAPACITY:
        raise ValueError("Enrollment journal exceeds its byte budget")
    atomic_bytes(file, encoded)


def enroll(rpc, authority, participant, operator_id, allow_audits, state_root=None, *, update_audit_permission=False):
    if state_root is None:
        return _enroll(rpc, authority, participant, operator_id, allow_audits, None, update_audit_permission)
    folder = Path(state_root)
    folder.mkdir(parents=True, exist_ok=True)
    lock = SessionControllerLock(folder)
    try:
        return _enroll(rpc, authority, participant, operator_id, allow_audits, folder, update_audit_permission)
    finally:
        lock.release()


def _enroll(rpc, authority, participant, operator_id, allow_audits, state_root, update_audit_permission):
    genesis=rpc.genesis
    if type(genesis)is not str or not re.fullmatch('[0-9a-f]{64}',genesis)or genesis=='0'*64:
        raise ValueError('Explicit expected testnet genesis required')
    if not re.fullmatch("[0-9a-f]{64}", operator_id) or operator_id == "0" * 64:
        raise ValueError("A canonical nonzero operator identifier is required")
    if any(not address or len(address) > 128 for address in (authority, participant)):
        raise ValueError("Bounded native signing addresses are required")
    rpc.verify()
    if rpc.call('getblockchaininfo').get('chain')!='test':
        raise ValueError('Operator enrollment is restricted to testnet')
    summary = rpc.call("getpgnparticipants")
    if summary.get("genesis") != genesis:
        raise ValueError("Foreign participant state refused")
    signing = rpc.call("validateaddress", [authority])
    member = rpc.call("validateaddress", [participant])
    if signing.get("isvalid") is not True or member.get("isvalid") is not True:
        raise ValueError("Native signing addresses are required")
    if signing.get("scriptPubKey") != summary.get("authority_script"):
        raise ValueError("The selected signer is not the native testnet authority")
    body = {"schema": "PGN_TESTNET_PARTICIPANT_V1", "signing_script": member["scriptPubKey"],
            "operator_id": operator_id, "may_audit": bool(allow_audits)}
    binding = {"genesis": genesis, "authority_request": summary["authority_request"], "body": body}
    identity = hashlib.sha256(canonical(binding)).hexdigest()
    journal = Path(state_root) / (identity + ".json") if state_root is not None else None

    def present():
        snapshot = rpc.call("getpgnparticipants", [0, 1, participant])
        if snapshot.get("genesis") != genesis:
            raise ValueError("Foreign membership lookup refused")
        values = snapshot.get("participants")
        if not isinstance(values, list) or len(values) > 1:
            raise ValueError("Malformed native membership lookup")
        if values:
            expected = {key: body[key] for key in ("signing_script", "operator_id", "may_audit")}
            if not isinstance(values[0], dict) or set(values[0]) != set(expected) or not isinstance(values[0].get("may_audit"), bool):
                raise ValueError("Malformed existing membership")
            if values[0] != expected:
                same_identity = all(values[0].get(key) == expected[key] for key in ("signing_script", "operator_id"))
                if not same_identity or not update_audit_permission:
                    raise ValueError("Existing membership differs; explicit policy review is required")
                return None
        return snapshot if values else None

    confirmed = present()
    result = {"schema": "PGN_TESTNET_ADMISSION_V1", "operation": identity,
              "binding": binding, "participant": participant, "applied": False,
              "read_only": journal is None, "already_present": confirmed is not None,
              "audit_permission_update_authorized": bool(update_audit_permission)}
    if journal is None:
        return result
    if not rpc.wallet or rpc.wallet not in rpc.call("listwallets"):
        raise ValueError("Select an already loaded authority wallet")
    owned = rpc.call("getaddressinfo", [authority], wallet=True)
    if owned.get("ismine") is not True or owned.get("iswatchonly") is True:
        raise ValueError("The loaded wallet must own the authority signing key")
    journal.parent.mkdir(parents=True, exist_ok=True)
    previous = None
    if journal.exists():
        if journal.stat().st_size > CAPACITY:
            raise ValueError("Oversized enrollment journal")
        previous = json.loads(journal.read_bytes())
        if previous.get("binding") != binding:
            raise ValueError("Enrollment journal belongs to another operation")
    if confirmed:
        result.update(applied=True, confirmation_tip=confirmed["bestblockhash"])
        save(journal, result)
        return result
    if previous and previous.get("candidate"):
        # Preserve unresolved exact work. Never discard an ambiguous or orphaned
        # event and silently issue a different admission.
        confirm_candidate(rpc, previous["candidate"])
        confirmed = present()
        if not confirmed:
            raise ValueError("Confirmed candidate has not established the desired membership")
        result.update(applied=True, confirmation_tip=confirmed["bestblockhash"], candidate=previous["candidate"])
        save(journal, result)
        return result
    save(journal, result)
    parameters = ["enroll", authority, summary["authority_request"], 0, 0, canonical(body).hex()]
    for _ in range(3):
        proposal = rpc.call("getpgnauditevent", parameters)
        signature = rpc.call("signmessage", [authority, proposal["challenge"]], wallet=True)
        try:
            signed = rpc.call("getpgnauditevent", parameters + [signature])
            template = rpc.call("getpgnbootstraptemplate", [[signed["envelope"]]])
        except RuntimeError:
            if rpc.call("getbestblockhash") != proposal["parent"]:
                continue
            raise
        block = bytearray.fromhex(template["block"])
        if template["genesis"] != genesis or template["parent"] != signed["parent"]:
            continue
        target = int(template["numeric_target"], 16)
        for nonce in range(1000000):
            block[82:90] = nonce.to_bytes(8, "little")
            digest = hashlib.sha256(hashlib.sha256(block[:112]).digest()).digest()
            if int.from_bytes(digest, "little") <= target:
                break
        else:
            raise RuntimeError("Bounded native coordination work exhausted")
        candidate = {"block": block.hex(), "parent": template["parent"], "hash": digest[::-1].hex()}
        result["candidate"] = candidate
        save(journal, result)
        confirm_candidate(rpc, candidate)
        confirmed = present()
        if not confirmed:
            raise ValueError("Native membership is not confirmed at the active tip")
        result.update(applied=True, confirmation_tip=confirmed["bestblockhash"])
        save(journal, result)
        return result
    raise RuntimeError("The native parent kept changing; admission plan retained for retry")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--cookie", type=Path, required=True)
    parser.add_argument("--genesis",required=True,help="Expected genesis from the selected testnet bundle")
    parser.add_argument("--wallet", default="")
    parser.add_argument("--authority", required=True)
    parser.add_argument("--participant", required=True)
    parser.add_argument("--operator-id", required=True)
    parser.add_argument("--allow-audits", action="store_true")
    parser.add_argument("--update-audit-permission", action="store_true",
                        help="Explicitly change audit permission of an existing address; operator identity cannot change")
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--state-root", type=Path)
    args = parser.parse_args()
    if not 1 <= args.port <= 65535 or not args.cookie.is_file() or args.cookie.stat().st_size > 4096:
        parser.error("Local Core authentication is required")
    if args.apply and args.state_root is None:
        parser.error("--apply requires an explicit --state-root")
    rpc = CoreRpc(args.port, args.cookie, args.genesis, args.wallet)
    result = enroll(rpc, args.authority, args.participant, args.operator_id,
                    args.allow_audits, args.state_root if args.apply else None,
                    update_audit_permission=args.update_audit_permission)
    # The public receipt omits the exact block and journal details.
    print(json.dumps({key: value for key, value in result.items() if key != "candidate"}, indent=2))


if __name__ == "__main__":
    main()
