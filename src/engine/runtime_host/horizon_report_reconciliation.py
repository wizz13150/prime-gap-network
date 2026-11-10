"""Preserve orphaned scientific work without blocking independent publication."""
from pathlib import Path
import json,re
from pgn_session import atomic_bytes
from report_commitment import canonical_bytes,verify_report_reveal

SCHEMA='PGN_ORPHAN_REPORT_RECONCILIATION_V1'

def _index(outbox):
    path=outbox.root/'report-reconciliation-index.json'
    if not path.exists():return dict(items=[],cursor=0)
    if path.stat().st_size>262144:raise ValueError('Report reconciliation index exceeds budget')
    value=json.loads(path.read_bytes());items=value.get('items');cursor=value.get('cursor')
    if (not isinstance(items,list) or len(items)>2048 or len(set(items))!=len(items)
            or any(not isinstance(i,str) or not re.fullmatch('[0-9a-f]{64}',i)for i in items)
            or type(cursor)is not int or cursor<0):
        raise ValueError('Invalid bounded report reconciliation index')
    return value

def _save_index(outbox,value):
    atomic_bytes(outbox.root/'report-reconciliation-index.json',canonical_bytes(value)+b'\n')

def _report(outbox,entry):
    work=entry['work'];ticket=work['ticket']
    if not re.fullmatch('[0-9a-f]{64}',ticket):raise ValueError('Canonical retained ticket required')
    path=outbox.root/'tickets'/ticket/'network-report.json'
    if path.stat().st_size>262144:raise ValueError('Retained report exceeds budget')
    raw=path.read_bytes();sealed=json.loads(raw);body=verify_report_reveal(sealed,entry['report_root'])
    if body['request_id']!=work['request_id'] or body['ticket']!=ticket or body['genesis']!=work['genesis']:
        raise ValueError('Retained report differs from reconciliation identity')
    return raw

def _record_path(outbox,request):
    if not isinstance(request,str) or not re.fullmatch('[0-9a-f]{64}',request):raise ValueError('Canonical retained request required')
    return outbox.root/'report-reconciliation'/f'{request}.json'

def orphan_evidence(worker,entry):
    rpc=worker.rpc;work=entry['work'];request=work['request_id']
    chain=rpc.call('getblockchaininfo');anchor=chain.get('bestblockhash');height=chain.get('blocks')
    if not isinstance(anchor,str) or not re.fullmatch('[0-9a-f]{64}',anchor) or type(height)is not int:
        raise ValueError('Authenticated chain anchor required for orphan recovery')
    view=rpc.call('getpgnworkview',['tickets',0,1,request]);state=view.get('state',{})
    if (view.get('schema')!='PGN_WORK_VIEW_V1' or view.get('kind')!='tickets' or view.get('request_id')!=request
            or state.get('genesis')!=work['genesis'] or state.get('bestblockhash')!=anchor
            or state.get('height')!=height):
        return None
    leases=state.get('leases');reports=view.get('reports')
    if not isinstance(leases,list) or not isinstance(reports,list):raise ValueError('Explicit reservation/report lookup required')
    if leases or reports:return None
    issuance=work.get('native_audit_service',{}).get('issuance_block') or work.get('parent')
    if not isinstance(issuance,str) or not re.fullmatch('[0-9a-f]{64}',issuance):return None
    header=rpc.call('getblockheader',[issuance,True]);old_height=header.get('height');confirmations=header.get('confirmations')
    if (header.get('hash')!=issuance or type(old_height)is not int or not 0<old_height<=height
            or type(confirmations)is not int or confirmations>=0):return None
    canonical=rpc.call('getblockhash',[old_height])
    if not isinstance(canonical,str) or not re.fullmatch('[0-9a-f]{64}',canonical):raise ValueError('Canonical block identity required')
    if canonical==issuance or rpc.call('getbestblockhash')!=anchor:return None
    return dict(anchor=anchor,height=height,issuance=issuance,issuance_height=old_height,
                canonical_at_issuance=canonical,reason='issuance_not_on_active_chain')

def park_orphaned_report(worker,outbox,entry):
    evidence=orphan_evidence(worker,entry)
    if evidence is None:return False
    with outbox.condition:
        entries=outbox._read();current=next((e for e in entries if e['work']['request_id']==entry['work']['request_id']),None)
        if current is None:return True # A crash/retry may already have completed the handoff.
        if current!=entry:raise ValueError('Retained publication changed during reconciliation')
        _report(outbox,entry)
        path=_record_path(outbox,entry['work']['request_id']);path.parent.mkdir(parents=True,exist_ok=True)
        if path.exists():
            if path.stat().st_size>32768:raise ValueError('Reconciliation record exceeds budget')
            previous=json.loads(path.read_bytes())
            if previous.get('schema')!=SCHEMA or previous.get('entry')!=entry:raise ValueError('Immutable orphaned report identity changed')
        index=_index(outbox);request=entry['work']['request_id']
        if request not in index['items']:
            if len(index['items'])>=2048:raise BufferError('Retained orphan report catalogue is full; active report preserved')
            index['items'].append(request)
        # Receipt + discoverable index precede freeing the active queue slot.
        # Crash at any earlier boundary leaves the original queue retryable.
        atomic_bytes(path,canonical_bytes(dict(schema=SCHEMA,status='orphaned_issuance',entry=entry,evidence=evidence))+b'\n')
        _save_index(outbox,index)
        outbox._save([e for e in entries if e!=entry]);outbox.condition.notify_all()
    return True

def retry_one_parked_report(worker,outbox):
    if not (outbox.root/'report-reconciliation-index.json').exists():return False
    with outbox.lock:
        if not outbox.capacity_available():return False
        index=_index(outbox)
        if not index['items']:return False
        position=index['cursor']%len(index['items']);request=index['items'][position]
        index['cursor']=(position+1)%len(index['items']);_save_index(outbox,index)
        path=_record_path(outbox,request)
        if path.stat().st_size>32768:raise ValueError('Reconciliation record exceeds budget')
        value=json.loads(path.read_bytes())
        if value.get('schema')!=SCHEMA or value.get('entry',{}).get('work',{}).get('request_id')!=request:raise ValueError('Reconciliation record identity mismatch')
        entry=value['entry'];_report(outbox,entry)
    rpc=worker.rpc;anchor=rpc.call('getbestblockhash');view=rpc.call('getpgnworkview',['tickets',0,1,request]);state=view.get('state',{});work=entry['work']
    if (view.get('schema')!='PGN_WORK_VIEW_V1' or view.get('kind')!='tickets' or view.get('request_id')!=request
            or state.get('genesis')!=work['genesis'] or state.get('bestblockhash')!=anchor):return False
    lease=next((l for l in state.get('leases',[])if l.get('request')==request),None)
    if not lease or lease.get('start')!=work['start'] or int(lease['end'])-int(lease['start'])!=int(work['length']):return False
    fresh=rpc.call('getpgnscientificwork',[request,'0'*64])
    if any(fresh.get(k)!=work.get(k)for k in ['genesis','request_id','ticket','start','length']) or rpc.call('getbestblockhash')!=anchor:return False
    with outbox.condition:
        entries=outbox._read();old=next((e for e in entries if e['work']['request_id']==request),None)
        if old is not None and old!=entry:raise ValueError('Restored report identity differs from active queue')
        if old is None:
            if len(entries)>=outbox.maximum:return False
            outbox._save([*entries,entry])
        index=_index(outbox);index['items']=[i for i in index['items']if i!=request]
        value['status']='restored_for_publication';value['restored_anchor']=anchor
        atomic_bytes(path,canonical_bytes(value)+b'\n');_save_index(outbox,index);outbox.condition.notify_all()
    return True
