"""Own-Core confirmed request service receipts, distinct from audit verdicts."""
import copy
import re
from horizon_core_rpc import CoreRpcError


class NativeAuditServiceRequired(RuntimeError):
    pass


def identity(value):
    return type(value)is str and re.fullmatch('[0-9a-f]{64}',value)is not None and value!='0'*64


def gate(rpc,signer,genesis):
    try:value=rpc.call('getpgnworkdutystatus',[signer])
    except CoreRpcError as error:
        if error.code==-32601:return None
        raise
    if (type(value)is not dict or value.get('schema')!='PGN_NATIVE_WORK_DUTY_STATUS_V1'
            or value.get('genesis')!=genesis or type(value.get('enforced'))is not bool
            or not identity(value.get('bestblockhash'))):
        raise ValueError('Native work-duty status belongs to another or unknown chain')
    if value['enforced']:
        if type(value.get('may_request'))is not bool:raise ValueError('Explicit native service permission required')
        if not value['may_request']:raise NativeAuditServiceRequired('Required audit service before the next search range: '+str(value.get('status','held'))[:80])
    return value


def context(value,work):
    keys={'schema','enforced','request_id','issuance_block','issuance_height','credits'}
    if (type(value)is not dict or set(value)!=keys or value['schema']!='PGN_NATIVE_ASSIGNED_AUDIT_SERVICE_V1'
            or value['enforced']is not True or value['request_id']!=work['request_id']
            or not identity(value['request_id'])or not identity(value['issuance_block'])
            or type(value['issuance_height'])is not int or not 0<value['issuance_height']<1<<31
            or type(value['credits'])is not list or len(value['credits'])>2):
        raise ValueError('Malformed confirmed native service context')
    seen=set()
    for ref in value['credits']:
        if (type(ref)is not dict or set(ref)!={'request_id','slot','attempt','commitment'}
                or not identity(ref['request_id'])or ref['request_id']==work['request_id']
                or type(ref['slot'])is not int or ref['slot']not in(0,1)
                or type(ref['attempt'])is not int or not 0<=ref['attempt']<1<<32
                or type(ref['commitment'])is not str or re.fullmatch('[0-9a-f]{64}',ref['commitment'])is None):
            raise ValueError('Invalid or reused confirmed audit credit')
        key=(ref['request_id'],ref['slot'],ref['attempt'])
        if key in seen:raise ValueError('Duplicate confirmed audit credit')
        seen.add(key)
    return copy.deepcopy(value)


def tasks(value,work):
    value=context(value,work);anchor=value['issuance_block']
    result=[dict(stage='committed',request=r['request_id'],slot=r['slot'],attempt=r['attempt'],block=anchor)
            for r in value['credits']]
    result.extend(dict(stage='unavailable',anchor=anchor,reason='no_eligible_independent_work')for _ in range(2-len(result)))
    return result


def observe(rpc,work,genesis,previous=None):
    # Never authorize from a saved JSON flag: re-read the actual owning Core.
    fresh=rpc.call('getpgnscientificwork',[work['request_id'],'0'*64])
    # Service belongs to the immutable request/range, not the current Gold
    # difficulty. Core legitimately reprices a scientific submission at a new
    # parent; submit_gold fetches that current target again before proof/signing.
    # Comparing it here would strand retained computation after a retarget.
    if fresh.get('genesis')!=genesis or fresh.get('accepted')is not False or any(fresh.get(k)!=work.get(k)for k in
            ('request_id','ticket','start','length')):
        raise ValueError('Native service receipt belongs to different issued work')
    value=fresh.get('native_audit_service')
    if (type(value)is not dict or set(value)!={'schema','enforced','request_id','view_parent','issuance_block','issuance_height','credits'}
            or value.get('schema')!='PGN_NATIVE_ASSIGNED_AUDIT_SERVICE_V1'or value.get('request_id')!=work['request_id']
            or type(value.get('enforced'))is not bool
            or value.get('view_parent')!=fresh.get('parent')or not identity(value.get('view_parent'))):
        raise ValueError('Core does not expose an authenticated assigned service receipt')
    if not value['enforced']:
        if value.get('credits')!=[]:raise ValueError('Inactive service policy cannot consume credits')
        if previous is not None:raise ValueError('Confirmed native service policy disappeared')
        return None
    normalized=context({k:v for k,v in value.items()if k!='view_parent'},work)
    if type(fresh.get('height'))is not int or normalized['issuance_height']>=fresh['height']:
        raise ValueError('Request service receipt is ahead of its own Core view')
    header=rpc.call('getblockheader',[normalized['issuance_block'],True])
    if (header.get('hash')!=normalized['issuance_block']or header.get('height')!=normalized['issuance_height']
            or type(header.get('confirmations'))is not int or header['confirmations']<=0
            or rpc.call('getblockhash',[normalized['issuance_height']])!=normalized['issuance_block']
            # fresh.height is the candidate height: its parent is one lower.
            # Normal descendants cannot change consumed service credits in
            # an immutable issuance block. A replaced observation block can.
            or rpc.call('getblockhash',[fresh['height']-1])!=value['view_parent']):
        raise RuntimeError('Native service receipt changed branches; retained work needs a fresh view')
    if previous is not None and previous!=normalized:
        if previous['issuance_block']==normalized['issuance_block']:
            raise ValueError('Confirmed service credits changed inside an immutable block')
        try:old=rpc.call('getblockheader',[previous['issuance_block'],True])
        except CoreRpcError as error:
            if error.code!=-5:raise
            old=dict(confirmations=-1)
        if type(old.get('confirmations'))is not int or old['confirmations']>0:
            raise ValueError('Canonical request service receipt cannot be replaced')
    return normalized
