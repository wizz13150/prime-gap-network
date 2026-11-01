"""Read a bounded testnet presence/synchronization snapshot from local Core RPC.

Coordinator tool only. It does not create a wallet, start mining, enroll an
operator, publish an event, or expose RPC beyond the local machine.
"""

import argparse
import base64
import json
from pathlib import Path
import urllib.request
import uuid


GENESIS = "74530feede52b8cda4fa19077747deededef0562844762a43cce462efd29102d"


def inspect(port, cookie):
    if not 1 <= port <= 65535 or not cookie.is_file() or cookie.stat().st_size > 4096:
        raise ValueError("A local RPC port and bounded authentication cookie are required")
    authorization = base64.b64encode(cookie.read_bytes().strip()).decode("ascii")

    def rpc(method, parameters=None):
        identity = str(uuid.uuid4())
        body = json.dumps({"jsonrpc": "2.0", "id": identity, "method": method,
                           "params": parameters or []}).encode("utf-8")
        request = urllib.request.Request(
            "http://127.0.0.1:" + str(port) + "/", data=body,
            headers={"Content-Type": "application/json", "Authorization": "Basic " + authorization},
        )
        with urllib.request.urlopen(request, timeout=10) as response:
            encoded = response.read(2 * 1024 * 1024 + 1)
        if len(encoded) > 2 * 1024 * 1024:
            raise ValueError("RPC response exceeds the diagnostic budget")
        value = json.loads(encoded)
        if value.get("id") != identity or value.get("error"):
            raise ValueError("Local Core RPC rejected the presence query")
        return value["result"]

    if rpc("getblockhash", [0]) != GENESIS:
        raise ValueError("Foreign chain refused")
    chain = rpc("getblockchaininfo")
    network = rpc("getnetworkinfo")
    peers = rpc("getpeerinfo")
    if not isinstance(chain, dict) or not isinstance(network, dict) or not isinstance(peers, list):
        raise ValueError("Unexpected Core presence response")
    if len(peers) > 256:
        raise ValueError("Use a paged monitor for more than 256 peers")
    peer_rows = []
    for peer in peers:
        if not isinstance(peer, dict):
            raise ValueError("Malformed peer presence row")
        peer_rows.append({
            key: peer.get(key) for key in (
                "id", "connection_type", "version", "subver", "inbound", "conntime",
                "lastrecv", "lastsend", "bytesrecv", "bytessent", "timeoffset",
                "pgn_announced_height", "pgn_syncing",
            )
        })
    return {
        "schema": "PGN_TESTNET_PRESENCE_V1",
        "read_only": True,
        "genesis_verified": True,
        "ledger": {key: chain.get(key) for key in (
            "blocks", "headers", "initialblockdownload", "verificationprogress",
        )},
        "network": {key: network.get(key) for key in (
            "networkactive", "connections", "connections_in", "connections_out",
        )},
        "peers": peer_rows,
        "note": "Peer presence is not proof of independent audit-operator identity or completed work.",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, type=int)
    parser.add_argument("--cookie", required=True, type=Path)
    arguments = parser.parse_args()
    try:
        result = inspect(arguments.port, arguments.cookie)
    except Exception as error:
        # Never include authentication bytes or an HTTP request object in output.
        raise SystemExit(type(error).__name__ + ": presence query failed") from None
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
