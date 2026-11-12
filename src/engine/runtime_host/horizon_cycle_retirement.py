"""Conserve inactive native-service cycles before releasing hot CPU scheduling slots."""
import copy,json
from report_commitment import canonical_bytes
from pgn_session import atomic_bytes
from horizon_work_cycle import WorkCycleJournal,identity


def observe_inactive(rpc,cycle,genesis):
    """Absence is usable only in an exact, coherent owned-Core work view.

    This observation is local scheduling evidence, never a network verdict.
    A submitted report or an extant lease keeps its current recovery path.
    """
    work=cycle['work'];request=work['request_id']
    view=rpc.call('getpgnworkview',['tickets',0,1,request])
    state=view.get('state',{})
    if (view.get('schema')!='PGN_WORK_VIEW_V1' or view.get('kind')!='tickets'
        or view.get('request_id')!=request or state.get('genesis')!=genesis
        or not identity(state.get('bestblockhash')) or type(state.get('height'))is not int or state['height']<0
        or type(state.get('leases'))is not list or type(view.get('reports'))is not list):
        raise ValueError('Exact owned-Core work view required')
    if len(state['leases'])>1 or any(v.get('request')!=request for v in state['leases']):
        raise ValueError('Work view returned another reservation')
    if state['leases'] or view['reports']:return None
    anchor=state['bestblockhash']
    if rpc.call('getbestblockhash')!=anchor:return None
    return dict(schema='PGN_INACTIVE_LOCAL_CYCLE_V1',genesis=genesis,request_id=request,
        previous_ticket=work['ticket'],anchor=anchor,height=state['height'],reason='no_current_lease_or_report')


def retain_inactive(journal,request,evidence,*,protected_requests,expected_cycle):
    """Commit an immutable conservation record before releasing one hot slot.

    Caller owns/stops the executors involved and supplies their current protected
    requests. No result, source frontier, report, wallet or controller is touched.
    Native receipts remain immutable and their accounting stays with Core.
    """
    with journal.lock:
        if journal.owner.fd is None:raise RuntimeError('Work-cycle writer is closed')
        if request in protected_requests:raise RuntimeError('Cycle still belongs to an executor or preparation lane')
        current=next((c for c in journal.value['cycles']if c['work']['request_id']==request),None)
        if current is None:return False
        if current!=expected_cycle:raise ValueError('Cycle changed after inactivity observation')
        if (current['stage'] not in('prepared','running') or current['hold']
            or 'native_service'not in current or not WorkCycleJournal.ready(current)):
            raise ValueError('Only an unheld native-service cycle can be retired by this retirement path')
        required={'schema','genesis','request_id','previous_ticket','anchor','height','reason'}
        if (type(evidence)is not dict or set(evidence)!=required
            or evidence['schema']!='PGN_INACTIVE_LOCAL_CYCLE_V1' or evidence['genesis']!=journal.value['genesis']
            or evidence['request_id']!=request or evidence['previous_ticket']!=current['work']['ticket']
            or not identity(evidence['anchor']) or type(evidence['height'])is not int or evidence['height']<0
            or evidence['reason']!='no_current_lease_or_report'):
            raise ValueError('Inactivity evidence differs from its cycle')
        raw=canonical_bytes(dict(cycle=current,evidence=evidence,extra_audits=journal.extra_audits(request)))
        with journal.history.database(create=True)as db:
            db.execute('CREATE TABLE IF NOT EXISTS inactive_native_cycles(request TEXT NOT NULL,ticket TEXT NOT NULL,receipt BLOB NOT NULL CHECK(length(receipt)<=65536),PRIMARY KEY(request,ticket)) WITHOUT ROWID')
            prior=db.execute('SELECT receipt FROM inactive_native_cycles WHERE request=? AND ticket=?',(request,current['work']['ticket'])).fetchone()
            if prior is not None:
                retained=json.loads(prior[0])
                if retained['cycle']!=current or retained['extra_audits']!=journal.extra_audits(request):
                    raise ValueError('Immutable inactive cycle changed')
            db.execute('INSERT OR IGNORE INTO inactive_native_cycles VALUES(?,?,?)',(request,current['work']['ticket'],raw))
        # A crash here leaves both copies. Restart can repeat the same observation
        # and write, while the existing hot journal remains authoritative.
        candidate=copy.deepcopy(journal.value)
        candidate['cycles']=[c for c in candidate['cycles']if c['work']['request_id']!=request]
        journal.validate(candidate)
        atomic_bytes(journal.file,canonical_bytes(candidate)+b'\n')
        journal.value=candidate
        return True


def reconcile_cpu_capacity(flow,incoming):
    """Preparation-lane integration; no network I/O in executor lock."""
    if getattr(flow.worker,'cpu_only',False)is not True:return False
    if flow.cycles.contains(incoming['request_id']):return False
    with flow.cycles.lock:
        if len(flow.cycles.value['cycles'])<flow.cycles.maximum_cycles:return False
        if any(c['stage']=='completed'and not c['hold']for c in flow.cycles.value['cycles']):return False
        candidates=copy.deepcopy(flow.cycles.value['cycles'])
    def protected():
        requests={incoming['request_id']}
        for job in flow.gpu.jobs.values():
            requests.update(job.context.get(k)for k in('work_cycle','audit_budget_cycle'))
        return requests
    for cycle in candidates:
        request=cycle['work']['request_id']
        if cycle['stage']not in('prepared','running')or cycle['hold']or 'native_service'not in cycle or not WorkCycleJournal.ready(cycle):continue
        with flow.gpu.condition:
            if request in protected():continue
        with flow.worker.network_lock:
            proof=observe_inactive(flow.worker.rpc,cycle,flow.worker.args.genesis)
        if proof is None:continue
        # The scheduler uses condition -> cycle lock too; retain that order.
        with flow.gpu.condition:
            if request in protected():continue
            return retain_inactive(flow.cycles,request,proof,protected_requests=protected(),expected_cycle=cycle)
    return False
