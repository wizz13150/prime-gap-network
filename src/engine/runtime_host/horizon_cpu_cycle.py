"""CPU resources execute work from the existing logical-miner normal cycle.

The current CPU coverage pin is pending. Mixed operation conserves the qualified
GPU report and retains CPU counts/best/frontiers as independently bound evidence.
There is no CPU audit allocator, reward identity, or audit-only product mode.
"""
from pathlib import Path
import copy
import hashlib
import json
import os
import queue
import subprocess
import threading
import time
from pgn_session import atomic_bytes
from horizon_work_cycle import WorkCycleJournal

def digest(value):
    return hashlib.sha256(json.dumps(value,sort_keys=True,separators=(',',':')).encode()).hexdigest()

class CpuCycleClient:
    """One owned resident service; controls are serialized and replies bounded."""
    def __init__(self, command, *, timeout=60):
        self.timeout=timeout; self.lock=threading.RLock(); self.events=queue.Queue(maxsize=64)
        self.sequence=0; self.generation=0; self.closed=False
        self.process=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,
            text=True,bufsize=1,creationflags=(0x08000000|0x00004000)if os.name=='nt'else 0)
        def reader():
            try:
                while True:
                    line=self.process.stdout.readline(32769)
                    if not line: raise RuntimeError('Owned CPU service disconnected')
                    if len(line.encode())>32768 or not line.endswith('\n'):raise ValueError('CPU reply exceeds32KiB')
                    self.events.put(json.loads(line))
            except BaseException as error:self.events.put(error)
        self.reader=threading.Thread(target=reader,name='cpu-cycle-replies',daemon=True);self.reader.start()
        ready=self._read()
        if ready.get('event')!='ready' or ready.get('schema')!='PGN_CPU_JSONLINES_V1':
            self.close(force=True);raise ValueError('Owned CPU service did not become ready')

    def _read(self):
        try:value=self.events.get(timeout=self.timeout)
        except queue.Empty:raise TimeoutError('CPU boundary acknowledgement timed out')
        if isinstance(value,BaseException):raise value
        return value

    def call(self, action, **fields):
        with self.lock:
            if self.closed:raise RuntimeError('CPU executor closed')
            self.sequence+=1; request=dict(id=self.sequence,action=action,**fields)
            data=json.dumps(request,separators=(',',':'))+'\n'
            if len(data.encode())>32768:raise ValueError('CPU request exceeds32KiB')
            self.process.stdin.write(data);self.process.stdin.flush()
            while True:
                value=self._read()
                if value.get('id')!=self.sequence:
                    if value.get('event')=='worker_error':raise RuntimeError(value.get('message','CPU worker failed'))
                    raise ValueError('CPU reply identity changed')
                if value.get('ok')is not True:raise ValueError(value.get('error',{}).get('message','CPU request refused'))
                return value

    def configure(self, enabled, threads, affinity):
        self.generation+=1
        return self.call('configure',enabled=enabled,threads=threads,affinity=affinity,generation=self.generation)

    def pause(self):
        self.generation+=1
        return self.call('pause',generation=self.generation)

    def close(self, force=False):
        if self.closed:return
        try:
            if not force:self.call('close',drain=True)
        finally:
            self.closed=True
            self.process.stdin.close()
            try:self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:self.process.terminate();self.process.wait(timeout=5)
            self.process.stdout.close()

def cycle_request(ticket, context, cycle):
    """Validate a grant against the same journal used by MiningFlow admission."""
    key=context.get('work_cycle')
    if not key or context.get('legacy_assignment') or cycle['work']['request_id']!=key or cycle['hold']:
        raise ValueError('CPU execution requires a live normal-ticket cycle')
    role=context.get('role')
    if role=='search':
        if cycle['stage']!='running' or not WorkCycleJournal.ready(cycle) or ticket['ticket_id']!=cycle['work']['ticket']:
            raise ValueError('CPU search cannot precede the two normal audit stages')
        stage='search'
    elif role=='audit':
        position=context.get('audit_position')
        if type(position)is not int or position not in(0,1) or cycle['stage']!='prepared':
            raise ValueError('Bound normal audit stage required')
        task=cycle['audits'][position]
        if not task or task['stage']!='assigned' or not all(x and x['stage']in('committed','unavailable')for x in cycle['audits'][:position]):
            raise ValueError('CPU audit stage cannot bypass normal-cycle ordering')
        if (task['request'],task['slot'],task['attempt'])!=(context.get('audit_request_id'),context.get('audit_slot'),context.get('audit_attempt')):
            raise ValueError('CPU audit belongs to another logical-miner assignment')
        stage='audit'+str(position+1)
    else:raise ValueError('CPU work needs a normal cycle stage')
    return dict(schema='PGN_CPU_CYCLE_WORK_V1',work_cycle=key,normal_ticket=cycle['work']['ticket'],
        stage=stage,ticket_id=ticket['ticket_id'],start=ticket['start'],end=ticket['end'])

class MixedNormalSource:
    """Existing GPU engine plus optional CPU work, under one MiningFlow grant.

    CPU source coverage is not promoted by agreement or by a local enable flag.
    The GPU remains the exhaustive conservation path. No speed gain is claimed.
    A complete CPU result is compared, and partial evidence remains explicit.
    """
    def __init__(self, gpu, cpu, root, *, threads=1, affinity=None, observer=lambda value:None,resource_identity=None):
        self.gpu=gpu;self.cpu=cpu;self.root=Path(root);self.observer=observer
        self.threads=threads;self.affinity=affinity;self.enabled=False;self.grant=None
        self.control_lock=threading.RLock();self.cpu_thread=None;self.cpu_done=None;self.last_cpu_error=None
        self.resource_identity=copy.deepcopy(resource_identity)

    def __getattr__(self,name):return getattr(self.gpu,name)

    def configure_cpu(self, enabled, threads, affinity):
        if type(enabled)is not bool or type(threads)is not int or type(affinity)is not list or len(affinity)!=threads:
            raise ValueError('CPU ON/OFF and explicit thread/affinity budget required')
        with self.control_lock:
            self.enabled=enabled;self.threads=threads;self.affinity=list(affinity)
            # ON is a resource choice. The service cannot create a work request.
            return self.cpu.configure(enabled,threads,affinity)

    def bind_cycle(self,ticket,context,cycle):
        request=cycle_request(ticket,context,cycle)
        with self.control_lock:self.grant=copy.deepcopy(request)

    def clear_cycle(self):
        with self.control_lock:self.grant=None

    def _retain(self,evidence,gpu_report,grant):
        request=evidence['request']
        if request!=grant or evidence['request_sha256']!=digest(request):raise ValueError('CPU evidence ownership changed')
        if self.resource_identity is not None and evidence.get('executor')!={k:self.resource_identity[k]for k in('host','processor_group')}:
            raise ValueError('CPU executor evidence belongs to another host/device')
        if evidence.get('production_admitted')is not False or evidence.get('network_accepted')is not False:
            raise ValueError('Unqualified CPU evidence cannot claim production admission')
        body=gpu_report.get('body')
        compared=False
        provisional=getattr(self.gpu,'scientific_raw_report_provisional',False)
        if body is not None and evidence['complete']and not provisional:
            if evidence['counts100']!=body['counts'] or evidence['best']!=body['best']:
                raise ValueError('Complete CPU computation disagrees with qualified GPU conservation report')
            compared=True
        receipt=dict(schema='PGN_MIXED_NORMAL_CPU_RECEIPT_V1',cpu=evidence,gpu_report_sha256=gpu_report.get('sha256'),
            resource_identity=self.resource_identity,
            full_result_compared=compared,publication_source=('gpu_pending_scientific_finalization'if provisional else'qualified_gpu'),cpu_publication_admitted=False,
            cost='CPU computation overlaps/repeats qualified GPU coverage; no performance gain established')
        folder=self.root/'tickets'/request['ticket_id'];folder.mkdir(parents=True,exist_ok=True)
        raw=json.dumps(receipt,sort_keys=True,separators=(',',':')).encode()+b'\n'
        if len(raw)>32768:raise ValueError('Mixed CPU receipt exceeds32KiB')
        atomic_bytes(folder/'cpu-cycle-evidence.json',raw)
        # Each resource retains at most32 completed/partial receipts. These are
        # evidence files owned by this lane, never user reports or checkpoints.
        receipts=sorted(self.root.glob('tickets/*/cpu-cycle-evidence.json'),key=lambda p:p.stat().st_mtime_ns)
        for old in receipts[:-32]:
            if old.parent.parent.parent.resolve()!=self.root.resolve():raise ValueError('CPU receipt retention escaped its owned root')
            old.unlink()
            try:old.parent.rmdir()
            except OSError:pass
        return receipt

    def _qualification(self,grant,done,outcome):
        started=False
        try:
            with self.control_lock:
                if not self.enabled:return
                status=self.cpu.call('status')['status']
                if status.get('request_sha256')not in(None,digest(grant)):
                    raise ValueError('CPU retains another cycle; explicit reconciliation required')
                if status.get('request_sha256')is None:self.cpu.call('execute',request=grant)
                self.cpu.configure(True,self.threads,self.affinity);started=True
            # Only this owned CPU management lane waits. The GPU publication
            # lane never waits for CPU computation, comparison, or draining.
            while not done.wait(.5):
                status=self.cpu.call('status')['status']
                self.observer(dict(type='telemetry',status=status,resource_enabled=self.enabled))
            self.cpu.pause()
            self.observer(dict(type='telemetry',status=self.cpu.call('status')['status'],resource_enabled=self.enabled))
            evidence=self.cpu.call('evidence')['evidence']
            report=outcome.get('report',dict(state='PAUSED'))
            receipt=self._retain(evidence,report,grant)
            if report.get('state')!='PAUSED':
                self.cpu.call('ack_result'if evidence['complete']else'discard',request_sha256=digest(grant))
            self.observer(dict(type='cpu_normal_work',active=False,completed_positions=evidence['completed_positions'],
                complete=evidence['complete'],full_result_compared=receipt['full_result_compared'],publication_admitted=False))
        except Exception as error:
            self.last_cpu_error=str(error)[:300]
            with self.control_lock:self.enabled=False
            if started:
                try:self.cpu.pause()
                except Exception:pass
            self.observer(dict(type='cpu_notice',active=False,message='CPU qualification paused: '+self.last_cpu_error,
                publication_admitted=False,resource_enabled=False))

    def run_ticket(self,ticket,maximum_piece):
        grant=copy.deepcopy(self.grant);done=threading.Event();outcome={}
        use_cpu=self.enabled and grant is not None and grant['ticket_id']==ticket['ticket_id']
        if use_cpu and (self.cpu_thread is None or not self.cpu_thread.is_alive()):
            lo,hi=int(ticket['start']),int(ticket['end'])
            if (1<<67)<=lo<hi<=(1<<68):
                self.cpu_done=done
                self.cpu_thread=threading.Thread(target=self._qualification,args=(grant,done,outcome),name='normal-cycle-cpu-qualification',daemon=True)
                self.cpu_thread.start()
            else:self.observer(dict(type='cpu_notice',active=False,reason_code='unsupported_domain',message='CPU inactive: Owned68 domain only; qualified GPU continues the normal cycle'))
        try:
            result=self.gpu.run_ticket(ticket,maximum_piece);outcome['report']=result
            return result
        finally:done.set()

    def wait_cpu(self,timeout=60):
        if self.cpu_thread:self.cpu_thread.join(timeout)
        if self.cpu_thread and self.cpu_thread.is_alive():raise TimeoutError('CPU qualification boundary remains in flight')

    def close(self):
        try:
            if self.cpu_done:self.cpu_done.set()
            self.wait_cpu()
            self.cpu.close()
        except Exception as error:
            self.last_cpu_error=str(error)[:300]
            self.observer(dict(type='cpu_notice',active=False,resource_enabled=False,message='CPU close retained its last durable frontier: '+self.last_cpu_error))
        finally:self.gpu.close()


from horizon_cpu_source import CpuOnlyNormalSource
