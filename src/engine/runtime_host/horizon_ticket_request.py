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


def retire_orphaned_signed_request(worker, journal, saved):
    """Retain an unissued request whose signed anchor left the active branch.

    No timeout/error means absence. The own-Core coherent lookup must show no
    lease/report, and the known old anchor must be explicitly noncanonical.
    A fresh request still passes the ordinary native audit-service gate.
    """
    from pathlib import Path
    import json
    from pgn_session import atomic_bytes
    from report_commitment import canonical_bytes
    from horizon_reassigned_work import identity
    request=saved.get('request',{})
    if saved.get('stage')!='REQUEST_SIGNED' or request.get('schema')!='PGN_TICKET_REQUEST_RPC_V2':
        return False
    if (request.get('genesis')!=worker.args.genesis or not identity(request.get('request_id'))
            or not identity(request.get('anchor')) or type(request.get('anchor_height'))is not int
            or request['anchor_height']<0):
        raise ValueError('Canonical retained signed request required')
    rpc=worker.rpc
    old=rpc.call('getblockheader',[request['anchor'],True])
    if (old.get('hash')!=request['anchor']or old.get('height')!=request['anchor_height']
            or type(old.get('confirmations'))is not int):
        raise ValueError('Known signed request anchor required for retirement')
    if old['confirmations']>=0:return False
    view=rpc.call('getpgnworkview',['tickets',0,1,request['request_id']]);state=view.get('state',{})
    if (view.get('schema')!='PGN_WORK_VIEW_V1'or view.get('kind')!='tickets'
            or view.get('request_id')!=request['request_id']or state.get('genesis')!=worker.args.genesis
            or not identity(state.get('bestblockhash'))or type(state.get('height'))is not int
            or state['height']<old['height']):
        return False
    leases,reports=state.get('leases'),view.get('reports')
    if type(leases)is not list or type(reports)is not list:
        raise ValueError('Explicit signed request absence required')
    if leases or reports:return False
    canonical=rpc.call('getblockhash',[old['height']])
    if not identity(canonical)or canonical==request['anchor']:
        raise ValueError('Signed request anchor is not proven off the active branch')
    if rpc.call('getbestblockhash')!=state['bestblockhash']:return False
    path=worker.root/'retired-ticket-requests'/(request['request_id']+'.json')
    evidence=dict(anchor=state['bestblockhash'],height=state['height'],
        old_anchor=request['anchor'],old_anchor_height=old['height'],canonical_at_old_height=canonical,
        reason='unissued_request_anchor_reorganized')
    value=dict(schema='PGN_RETIRED_SIGNED_REQUEST_V1',request=request,evidence=evidence)
    raw=canonical_bytes(value)+b'\n'
    if len(raw)>32768:raise ValueError('Retired signed request exceeds budget')
    with worker.state_lock:
        journal=Path(journal)
        if journal.stat().st_size>262144 or json.loads(journal.read_bytes())!=saved:
            raise ValueError('Signed request journal changed during retirement')
        if path.exists():
            if path.stat().st_size>32768 or json.loads(path.read_bytes()).get('request')!=request:
                raise ValueError('Immutable retired signed request changed')
        else:
            path.parent.mkdir(parents=True,exist_ok=True);atomic_bytes(path,raw)
        atomic_bytes(journal,canonical_bytes(dict(stage='REQUEST_ORPHANED',sequence=saved.get('sequence',0),
            previous_request=request['request_id'],retained_request=path.relative_to(worker.root).as_posix(),
            verification_anchor=state['bestblockhash']))+b'\n')
    return True
