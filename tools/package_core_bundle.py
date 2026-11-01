"""Assemble the portable Core payload, without any wallet or chain data.

Inputs are explicit files and network parameters. The output is immutable:
repeating the same invocation verifies it; different contents require a new
output directory. This tool never starts Core or reads a user's data folder.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import tempfile
from pathlib import Path


def fingerprint(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def assemble(executable: Path, runtime_files: list[Path], output: Path, *,
             chain: str, genesis: str, rpc_port: int, peer_port: int,
             peers: list[str]) -> dict:
    if chain not in ("main", "test") or not re.fullmatch(r"[0-9a-fA-F]{64}", genesis):
        raise ValueError("Explicit network and genesis fingerprint required")
    if not (1024 <= rpc_port <= 65535 and 1024 <= peer_port <= 65535) or rpc_port == peer_port:
        raise ValueError("Separate RPC and peer ports required")
    if len(peers) > 64 or any(not peer or peer.startswith("-") or re.search(r"\s", peer) for peer in peers):
        raise ValueError("Invalid peer endpoints")
    sources = [executable.resolve(strict=True), *(item.resolve(strict=True) for item in runtime_files)]
    names: set[str] = set()
    entries = []
    for source in sources:
        name = source.name
        if not source.is_file() or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,127}", name):
            raise ValueError("Only explicit executable/library files may be packaged")
        with source.open("rb") as stream:
            signature = stream.read(4)
        if not (signature.startswith(b"MZ") or signature == b"\x7fELF"):
            raise ValueError("Core payload must be a Windows or Linux executable/shared library")
        if name.casefold() in names or name.casefold() == "core-template.json":
            raise ValueError("Duplicate/reserved bundle filename")
        if source != sources[0] and not (name.lower().endswith(".dll") or ".so" in name or name.lower().endswith(".dylib")):
            raise ValueError("Runtime payload accepts shared libraries only")
        size = source.stat().st_size
        if not 0 < size <= 128 * 1024 * 1024:
            raise ValueError("Individual Core payload exceeds its bound")
        names.add(name.casefold())
        entries.append({"path": name, "bytes": size, "sha256": fingerprint(source)})
    if len(entries) > 64 or sum(entry["bytes"] for entry in entries) > 256 * 1024 * 1024:
        raise ValueError("Core payload exceeds its bound")
    manifest = {
        "schema": "PGN_CORE_BUNDLE_V1",
        "deployment": {
            "product": "PGN Core", "chain": chain, "frontend": "daemon",
            "executable": sources[0].name, "executable_sha256": entries[0]["sha256"],
            "genesis_hash": genesis.lower(), "rpc_port": rpc_port,
            "peer_port": peer_port, "peers": peers,
        },
        "files": sorted(entries, key=lambda entry: entry["path"]),
    }
    encoded = json.dumps(manifest, ensure_ascii=True, sort_keys=True, indent=2).encode("utf-8") + b"\n"
    if len(encoded) > 32768:
        raise ValueError("Core manifest exceeds its bound")
    output = output.absolute()
    if output.exists():
        expected_names = {entry["path"] for entry in entries} | {"core-template.json"}
        if not output.is_dir() or {entry.name for entry in output.iterdir()} != expected_names:
            raise ValueError("Existing output is not this exact bundle; nothing overwritten")
        if (output / "core-template.json").read_bytes() != encoded or any(
            not (output / entry["path"]).is_file() or
            (output / entry["path"]).stat().st_size != entry["bytes"] or
            fingerprint(output / entry["path"]) != entry["sha256"] for entry in entries
        ):
            raise ValueError("Existing output differs; nothing overwritten")
        return manifest
    output.parent.mkdir(parents=True, exist_ok=True)
    # Temporary files are owned by this invocation; publish only a complete,
    # re-hashed payload. No prior output or source directory is ever removed.
    stage = Path(tempfile.mkdtemp(prefix=".core-bundle-", dir=output.parent)).resolve()
    assert stage.parent == output.parent.resolve()
    try:
        for source, entry in zip(sources, entries):
            target = stage / entry["path"]
            shutil.copyfile(source, target)
            if target.stat().st_size != entry["bytes"] or fingerprint(target) != entry["sha256"]:
                raise ValueError("Core input changed during packaging")
            target.chmod(source.stat().st_mode)
        (stage / "core-template.json").write_bytes(encoded)
        stage.rename(output)
    finally:
        if stage.exists():
            # This exact generated directory is inside the verified parent.
            assert stage.parent == output.parent.resolve() and stage.name.startswith(".core-bundle-")
            shutil.rmtree(stage)
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--runtime-file", action="append", default=[], type=Path)
    parser.add_argument("--output", required=True, type=Path, help="Destination Core directory")
    parser.add_argument("--chain", required=True, choices=("main", "test"))
    parser.add_argument("--genesis", required=True)
    parser.add_argument("--rpc-port", required=True, type=int)
    parser.add_argument("--peer-port", required=True, type=int)
    parser.add_argument("--peer", action="append", default=[])
    args = parser.parse_args()
    manifest = assemble(args.executable, args.runtime_file, args.output, chain=args.chain,
                        genesis=args.genesis, rpc_port=args.rpc_port,
                        peer_port=args.peer_port, peers=args.peer)
    print(json.dumps({"output": str(args.output), "files": len(manifest["files"]),
                      "bytes": sum(entry["bytes"] for entry in manifest["files"]),
                      "wallet_or_chain_data_included": False}))


if __name__ == "__main__":
    main()
