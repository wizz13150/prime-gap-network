"""Bound a signed request to its original Core anchor, never a chosen range."""
import re


def signed_request_parameters(request, miner, reward, signature):
    if not isinstance(signature, str) or not signature:
        raise ValueError("Native ticket signature is missing")
    schema = request.get("schema")
    if schema is None:
        if any(key in request for key in ("request_body_version", "deadline_clock", "lease_seconds")):
            raise ValueError("Incomplete native ticket request contract")
        # Explicit compatibility with the original three-argument RPC. This
        # affects request transport only, never engine selection or arithmetic.
        return [miner, reward, signature]
    if schema != "PGN_TICKET_REQUEST_RPC_V2":
        raise ValueError("Unsupported native ticket request RPC contract")
    body = request.get("request_body_version")
    seconds = request.get("lease_seconds")
    if type(body) is not int or body not in (1, 2) or type(seconds) is not int:
        raise ValueError("Invalid native ticket request clock metadata")
    expected_clock = "ledger_height" if body == 1 else "median_chain_time"
    if request.get("deadline_clock") != expected_clock or (seconds != 0 if body == 1 else not 0 < seconds < (1 << 32)):
        raise ValueError("Native ticket request clock does not match its signed body")
    anchor = request.get("anchor")
    if not isinstance(anchor, str) or not re.fullmatch(r"[0-9a-f]{64}", anchor) or anchor == "0" * 64:
        raise ValueError("Canonical original ticket anchor is required")
    return [miner, reward, signature, anchor]
