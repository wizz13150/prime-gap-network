"""Write-ahead handoff from an orphaned allocation to its current allocation.

Records retain the original report or inactive partial cycle. Only the small ready
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
        if value.get('retention')=='inactive_assignment':
            # The immutable ticket directory, session index, WAL, partitions
            # and any unsealed results keep their old ticket identity.
            session=value.get('retained_session')
            if session is not None:
                path=flow.worker.root/'tickets'/value['previous']['ticket']/'session.json'
                if hashlib.sha256(path.read_bytes()).hexdigest()!=session:
                    raise ValueError('Retained partial session changed during reassignment')
            old=value['previous_cycle']
            if old is None:
                if flow.cycles.contains(value['previous']['request_id']):
                    current=flow.cycles.snapshot(value['previous']['request_id'])
                    if current['work']!=flow.cycles.work_identity(value['work']):
                        raise ValueError('Another cycle appeared during startup reassignment')
                flow.cycles.prepare(value['work'],native_service=value['native_service'])
            else:
                flow.cycles.replace_inactive_assignment(value['previous'],value['work'],value['native_service'],value['evidence'])
            return
        if hashlib.sha256(_report(flow.outbox,entry)).hexdigest()!=value['retained_report_sha256']:
            raise ValueError('Retained scientific report changed during reassignment')
        flow.cycles.replace_completed_assignment(value['previous'],value['work'],value['native_service'],value['evidence'])
        with flow.outbox.condition:
            rows=flow.outbox._read()
            old=next((r for r in rows if r['work']['request_id']==entry['work']['request_id']),None)
            if old is not None:
                if old!=entry:raise ValueError('Another report cannot be retired by this handoff')
                flow.outbox._save([r for r in rows if r!=entry]);flow.outbox.condition.notify_all()

    def stage_inactive(self, previous):
        """Startup-only recovery; never retire a queued/running GPU job."""
        flow=self.flow;gpu=flow.gpu
        if gpu.active is not None or gpu.jobs or any(t.ident is not None for t in flow.audit_threads):
            raise RuntimeError('Partial assignment recovery requires an unstarted execution lane')
        if any(getattr(flow,name).ident is not None for name in ('report_thread','owner_report_thread','pressure_thread')):
            raise RuntimeError('Partial assignment recovery requires unstarted publication lanes')
        return self._stage_inactive(previous)

    def stage_unstarted(self, previous):
        """A ready recovery can itself be reorganized before GPU adoption."""
        flow=self.flow
        with flow.gpu.condition:
            if previous['ticket'] in flow.gpu.jobs:
                raise RuntimeError('Reassigned range is already owned by the executor')
        # Only the preparation writer obtains this recovery before it publishes
        # ready work. Never hold the GPU scheduling condition over Core I/O.
        cycle=flow.cycles.snapshot(previous['request_id'])
        if cycle['stage']!='prepared' or cycle['work']!=flow.cycles.work_identity(previous):
            raise RuntimeError('Only an unstarted prepared replacement can be reconciled')
        return self._stage_inactive(previous)

    def _stage_inactive(self, previous):
        flow=self.flow
        proof=prove_reassignment(flow.worker,previous)
        if proof is None:return False
        fresh=proof['work']
        with self.lock:
            cycle=flow.cycles.snapshot(previous['request_id']) if flow.cycles.contains(previous['request_id']) else None
            if cycle is not None and cycle['work']!=flow.cycles.work_identity(previous):
                raise ValueError('Partial assignment differs from its retained cycle')
            native=observe(flow.worker.rpc,fresh,flow.worker.args.genesis,None if cycle is None else cycle.get('native_service'))
            if native is None:raise ValueError('Startup reassignment requires confirmed native service accounting')
            session=flow.worker.root/'tickets'/previous['ticket']/'session.json'
            if session.exists() and session.stat().st_size>262144:raise ValueError('Retained partial session exceeds budget')
            key=hashlib.sha256(canonical_bytes([fresh['request_id'],previous['ticket'],fresh['ticket']])).hexdigest()
            path=self.folder/'records'/(key+'.json')
            value=dict(schema='PGN_REASSIGNMENT_HANDOFF_V1',retention='inactive_assignment',
                previous=previous,work=fresh,entry=None,previous_cycle=cycle,native_service=native,
                evidence=proof['evidence'],retained_session=hashlib.sha256(session.read_bytes()).hexdigest()if session.exists()else None)
            raw=canonical_bytes(value)+b'\n'
            if len(raw)>65536:raise ValueError('Startup reassignment handoff exceeds budget')
            ids=self.read()
            if key not in ids and len(ids)>=4:raise BufferError('Reassignment queue is full; old work remains retained')
            if path.exists():
                prior=self.record(key)
                if prior!=value:raise ValueError('Immutable startup reassignment receipt changed')
            else:
                path.parent.mkdir(parents=True,exist_ok=True);atomic_bytes(path,raw)
            if key not in ids:atomic_bytes(self.index,canonical_bytes([*ids,key])+b'\n')
            self.finish(value)
            return True

    def next_work(self):
        with self.lock:
            # Bound one turn even if the active branch keeps changing. The
            # durable index retains the next successor for a later retry.
            for _ in range(4):
                ids=self.read()
                if not ids:return None
                value=self.record(ids[0]);self.finish(value)
                if prove_reassignment(self.flow.worker,value['work']) is None:
                    return deepcopy(value['work'])
                if not self.stage_unstarted(value['work']):
                    raise RuntimeError('Replacement changed while its new allocation was observed')
                current=self.read()
                atomic_bytes(self.index,canonical_bytes([key for key in current if key!=ids[0]])+b'\n')
            raise RuntimeError('Assignment changed repeatedly; recovery retained for a fresh stable view')

    def adopted(self, work):
        with self.lock:
            ids=self.read()
            if not ids:raise ValueError('Adopted reassignment has no durable handoff')
            value=self.record(ids[0])
            if any(work.get(k)!=value['work'].get(k)for k in ('genesis','request_id','ticket','start','length')):
                raise ValueError('Adopted work differs from its reassignment handoff')
            atomic_bytes(self.index,canonical_bytes(ids[1:])+b'\n')
