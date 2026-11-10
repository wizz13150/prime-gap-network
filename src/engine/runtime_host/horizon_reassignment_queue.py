"""Write-ahead handoff from an orphaned allocation to its current allocation.

Records retain the original completed report and cycle. Only the small ready
index changes; adopting another ticket never retags scientific results.
"""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import threading
from pgn_session import atomic_bytes
from report_commitment import canonical_bytes
from horizon_report_reconciliation import _report
from horizon_native_work_duty import observe
from horizon_reassigned_work import prove_reassignment, identity


class ReassignmentQueue:
    def __init__(self, flow):
        self.flow=flow
        self.folder=flow.worker.root/'reassigned-work'
        self.index=self.folder/'ready.json'
        self.lock=threading.RLock()

    def read(self):
        if not self.index.exists():return []
        if self.index.stat().st_size>1024:raise ValueError('Reassignment ready index exceeds budget')
        value=json.loads(self.index.read_bytes())
        if type(value)is not list or len(value)>4 or len(set(value))!=len(value) or not all(identity(k)for k in value):
            raise ValueError('Invalid bounded reassignment index')
        return value

    def record(self, key):
        if not identity(key):raise ValueError('Canonical reassignment receipt identity required')
        path=self.folder/'records'/(key+'.json')
        if path.stat().st_size>65536:raise ValueError('Reassignment receipt exceeds budget')
        value=json.loads(path.read_bytes())
        if value.get('schema')!='PGN_REASSIGNMENT_HANDOFF_V1':raise ValueError('Invalid reassignment receipt')
        expected=hashlib.sha256(canonical_bytes([value['previous']['request_id'],value['previous']['ticket'],value['work']['ticket']])).hexdigest()
        if key!=expected:raise ValueError('Reassignment receipt differs from its index')
        return value

    def stage(self, entry):
        worker=self.flow.worker
        proof=prove_reassignment(worker,entry['work'])
        if proof is None:return False
        fresh=proof['work']
        with self.lock:
            # A record from a retired code version remains exact input, not an
            # instruction to accept a report or to change its computed range.
            cycle=self.flow.cycles.snapshot(entry['work']['request_id'])
            if cycle['work']!=self.flow.cycles.work_identity(entry['work']) or cycle['stage']!='completed':
                raise ValueError('Only an exact completed allocation can enter reassignment')
            native=observe(worker.rpc,fresh,worker.args.genesis,cycle.get('native_service'))
            if native is None:raise ValueError('Reassignment requires confirmed native service accounting')
            report=_report(self.flow.outbox,entry)
            key=hashlib.sha256(canonical_bytes([fresh['request_id'],entry['work']['ticket'],fresh['ticket']])).hexdigest()
            path=self.folder/'records'/(key+'.json')
            value=dict(schema='PGN_REASSIGNMENT_HANDOFF_V1',previous=entry['work'],work=fresh,
                entry=entry,previous_cycle=cycle,native_service=native,evidence=proof['evidence'],
                retained_report_sha256=hashlib.sha256(report).hexdigest())
            raw=canonical_bytes(value)+b'\n'
            if len(raw)>65536:raise ValueError('Reassignment handoff exceeds budget')
            ids=self.read()
            if key not in ids and len(ids)>=4:raise BufferError('Reassignment queue is full; old work remains retained')
            if path.exists():
                prior=self.record(key)
                if prior['entry']!=entry or prior['previous_cycle']!=cycle:
                    raise ValueError('Immutable reassignment receipt changed')
                value=prior
            else:
                path.parent.mkdir(parents=True,exist_ok=True)
                atomic_bytes(path,raw)
            if key not in ids:atomic_bytes(self.index,canonical_bytes([*ids,key])+b'\n')
            self.finish(value)
            return True

    def finish(self, value):
        """Idempotent roll-forward after an interrupted write-ahead handoff."""
        flow=self.flow;entry=value['entry']
        if hashlib.sha256(_report(flow.outbox,entry)).hexdigest()!=value['retained_report_sha256']:
            raise ValueError('Retained scientific report changed during reassignment')
        flow.cycles.replace_completed_assignment(value['previous'],value['work'],value['native_service'],value['evidence'])
        with flow.outbox.condition:
            rows=flow.outbox._read()
            old=next((r for r in rows if r['work']['request_id']==entry['work']['request_id']),None)
            if old is not None:
                if old!=entry:raise ValueError('Another report cannot be retired by this handoff')
                flow.outbox._save([r for r in rows if r!=entry]);flow.outbox.condition.notify_all()

    def next_work(self):
        with self.lock:
            ids=self.read()
            if not ids:return None
            value=self.record(ids[0]);self.finish(value)
            return deepcopy(value['work'])

    def adopted(self, work):
        with self.lock:
            ids=self.read()
            if not ids:raise ValueError('Adopted reassignment has no durable handoff')
            value=self.record(ids[0])
            if any(work.get(k)!=value['work'].get(k)for k in ('genesis','request_id','ticket','start','length')):
                raise ValueError('Adopted work differs from its reassignment handoff')
            atomic_bytes(self.index,canonical_bytes(ids[1:])+b'\n')
