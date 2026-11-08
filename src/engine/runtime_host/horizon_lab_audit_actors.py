"""Explicit lab-only audit signing identities, sharing the normal visible GPU queue.

No identities are shipped or generated here. They are not independent economic
auditors and cannot enable this mode on mainnet or change Core admission rules.
"""
from pathlib import Path
import json


def load_lab_audit_actors(file, rpc, genesis, *, pilot_coordination):
    if file is None:
        return None
    if not pilot_coordination or rpc.call('getblockchaininfo').get('chain') != 'test':
        raise ValueError('Controlled laboratory audit identities require the explicit testnet pilot')
    path = Path(file)
    if not path.is_file() or path.is_symlink() or path.stat().st_size > 8192:
        raise ValueError('Laboratory audit identity profile is missing or exceeds 8192 bytes')
    value = json.loads(path.read_bytes())
    if (not isinstance(value, dict) or value.get('schema') != 'PGN_CONTROLLED_LAB_AUDIT_ACTORS_V1'
            or value.get('genesis') != genesis or value.get('scope') != 'controlled-testnet-laboratory'
            or value.get('independent_economic_auditors') is not False):
        raise ValueError('Explicit laboratory scope and matching testnet genesis are required')
    actors = value.get('actors')
    if (not isinstance(actors, list) or len(actors) != 2
            or any(type(a) is not str or not 26 <= len(a) <= 120 or not a.isascii() or not a.isalnum() for a in actors)
            or len(set(actors)) != 2):
        raise ValueError('Two distinct public laboratory audit addresses are required')
    for actor in actors:
        info = rpc.call('getaddressinfo', [actor], wallet=True)
        if info.get('ismine') is not True:
            raise ValueError('The configured wallet cannot sign for a laboratory audit identity')
    return tuple(actors)
