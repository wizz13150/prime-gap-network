"""Prove a changed matured assignment without discarding its former work.

A request can remain confirmed while the later block seeding its allocation
is reorganized. Request existence alone does not authenticate the old ticket.
This reader never signs, publishes, changes a journal, or grants audit credit.
"""
from horizon_compute_lease import computation_lease_active


def identity(value):
    return (type(value) is str and len(value) == 64 and value != '0' * 64
            and all(c in '0123456789abcdef' for c in value))


def prove_reassignment(worker, work):
    """Return a coherent own-Core replacement, or None for an unchanged view."""
    if (work.get('genesis') != worker.args.genesis
            or not all(identity(work.get(k)) for k in ('genesis', 'request_id', 'ticket', 'parent'))):
        raise ValueError('Canonical original work identity required for reassignment')
    rpc = worker.rpc
    fresh = rpc.call('getpgnscientificwork', [work['request_id'], '0' * 64])
    if (fresh.get('schema') != 'PGN_NATIVE_SCIENTIFIC_WORK1'
            or fresh.get('accepted') is not False or fresh.get('genesis') != work['genesis']
            or fresh.get('request_id') != work['request_id']
            or not identity(fresh.get('ticket')) or not identity(fresh.get('parent'))
            or type(fresh.get('height')) is not int or fresh['height'] <= 0
            or any(fresh.get(k) != work.get(k) for k in ('miner_script', 'reward_script'))):
        raise ValueError('Reassigned work differs from its authenticated request owner')
    if fresh['ticket'] == work['ticket']:
        if any(fresh.get(k) != work.get(k) for k in ('start', 'length', 'segments')):
            raise ValueError('An unchanged ticket cannot change its scientific interval')
        return None
    for key in ('start', 'length', 'ticket_number', 'target_numerator', 'target_denominator'):
        text = fresh.get(key)
        if (type(text) is not str or not text.isascii() or not text.isdecimal()
                or len(text) > 160 or str(int(text)) != text or int(text) <= 0):
            raise ValueError('Noncanonical reassigned work number: ' + key)
    if type(fresh.get('segments')) is not int or fresh['segments'] != work.get('segments'):
        raise ValueError('Reassigned work has another report segmentation')
    anchor = fresh['parent']
    view = rpc.call('getpgnworkview', ['tickets', 0, 1, work['request_id']])
    state = view.get('state', {})
    if (view.get('schema') != 'PGN_WORK_VIEW_V1' or view.get('kind') != 'tickets'
            or view.get('request_id') != work['request_id']
            or state.get('genesis') != work['genesis']
            or state.get('bestblockhash') != anchor
            or state.get('height') != fresh['height'] - 1):
        return None
    leases, reports = state.get('leases'), view.get('reports')
    if (type(leases) is not list or len(leases) != 1 or type(reports) is not list):
        raise ValueError('Explicit exact reassigned reservation and report lookup required')
    if reports:
        return None  # A committed report is never silently replaced.
    lease = leases[0]
    if (lease.get('request') != work['request_id'] or lease.get('state') != 'assigned'
            or lease.get('miner_script') != work['miner_script']
            or lease.get('reward_script') != work['reward_script']
            or lease.get('start') != fresh['start']
            or lease.get('end') != str(int(fresh['start']) + int(fresh['length']))
            or not computation_lease_active(state, lease)):
        raise ValueError('Replacement does not match its live scientific reservation')
    old = rpc.call('getblockheader', [work['parent'], True])
    if (old.get('hash') != work['parent'] or type(old.get('height')) is not int
            or not 0 < old['height'] <= state['height']
            or type(old.get('confirmations')) is not int):
        raise ValueError('Authenticated original assignment parent required')
    if old['confirmations'] >= 0:
        raise ValueError('Canonical original assignment cannot be replaced')
    canonical = rpc.call('getblockhash', [old['height']])
    if not identity(canonical) or canonical == work['parent']:
        raise ValueError('Original assignment parent is not proven off the active branch')
    if rpc.call('getbestblockhash') != anchor:
        return None
    return dict(schema='PGN_REASSIGNED_WORK_PROOF_V1', work=fresh,
                evidence=dict(anchor=anchor, height=state['height'],
                    previous_parent=work['parent'], previous_parent_height=old['height'],
                    canonical_at_previous_height=canonical, previous_ticket=work['ticket'],
                    request_id=work['request_id'], reason='matured_assignment_reorganized'))
