"""Retire a stale scheduling cursor, preserving every byte of private work."""
from pathlib import Path
import hashlib,json,re
from pgn_session import atomic_bytes


def retain_changed_assignment(folder, audit, state, anchor, rpc, callback):
    if not isinstance(anchor,str) or not re.fullmatch('[0-9a-f]{64}',anchor):
        raise ValueError('Explicit reconciliation anchor required')
    source=Path(folder)/'current.json'
    raw=source.read_bytes()
    if len(raw)>262144 or json.loads(raw)!=audit:
        raise ValueError('Current audit changed before retention')
    if rpc.call('getbestblockhash')!=anchor:
        raise RuntimeError('Chain changed during audit retirement; current work retained')
    digest=hashlib.sha256(raw).hexdigest()
    destination=Path(folder)/'retained-assignments'/f'{digest}.json'
    destination.parent.mkdir(parents=True,exist_ok=True)
    if destination.exists():
        if destination.read_bytes()!=raw:raise ValueError('Retained audit identity changed')
    else:atomic_bytes(destination,raw)
    # The callback can release only the exact pending local scheduling slot.
    # It does not grant a commitment, service credit, verdict or reward.
    callback(audit,anchor)
    if source.read_bytes()!=raw:raise ValueError('Audit changed during retirement')
    source.unlink()
    return destination
