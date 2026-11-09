"""Closed device model of the real bounded queue; no GPU or network claims."""
from pathlib import Path
import sys
import threading
import unittest
from unittest.mock import patch
from types import SimpleNamespace

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'src/engine/runtime_host'))
from horizon_gpu_queue import ResidentGpuQueue
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'src/protocol'))
from horizon_mining_flow import MiningFlow

class QueueServiceTests(unittest.TestCase):
    def test_expensive_management_read_rests_without_resetting_pressure(self):
        clock = [0.0]
        waits = []
        class Stop:
            def is_set(self):return bool(waits)
            def wait(self, seconds):waits.append(seconds)
        class Rpc:
            def call(self, method, parameters):
                clock[0] += 12.0
                return dict(schema='PGN_NETWORK_METRICS_V1',genesis='a'*64,audit_enabled_operators=3,
                            audit_queue=dict(awaiting=100,assigned=2,oldest_compute_pending_age_seconds=3600))
        flow=MiningFlow.__new__(MiningFlow)
        flow.gpu=ResidentGpuQueue(None,threading.Event(),threading.Event(),None,None,None,None)
        flow.worker=SimpleNamespace(rpc=Rpc(),args=SimpleNamespace(genesis='a'*64),quitting=Stop())
        with patch('horizon_mining_flow.time.monotonic',lambda:clock[0]):
            flow.refresh_audit_pressure()
        self.assertEqual(waits,[36.0])
        self.assertEqual(flow.gpu.audit_service_view()['target_audit_share'],.8)

    def test_public_queue_read_is_management_io_and_bad_data_retains_policy(self):
        class OneRound:
            stopped=False
            def is_set(self):return self.stopped
            def wait(self,seconds):self.stopped=True
        class Rpc:
            value=None
            def call(self,method,parameters):
                self.called=(method,parameters)
                if isinstance(self.value,Exception):raise self.value
                return self.value
        queue=ResidentGpuQueue(None,threading.Event(),threading.Event(),None,None,None,None)
        rpc=Rpc()
        flow=MiningFlow.__new__(MiningFlow)
        flow.gpu=queue
        flow.worker=SimpleNamespace(rpc=rpc,args=SimpleNamespace(genesis='a'*64),quitting=OneRound())
        rpc.value=dict(schema='PGN_NETWORK_METRICS_V1',genesis='a'*64,audit_enabled_operators=3,
                       audit_queue=dict(awaiting=100000,assigned=2,oldest_compute_pending_age_seconds=3600))
        flow.refresh_audit_pressure()
        self.assertEqual(rpc.called,('getpgnnetworkmetrics',[2,60,600]))
        self.assertEqual(queue.audit_service_view()['target_audit_share'],.8)
        before=queue.audit_service_view()
        for bad in [RuntimeError('Injected lost connection'),dict(rpc.value,genesis='b'*64),
                    dict(rpc.value,audit_queue=dict(awaiting=True,assigned=0,oldest_compute_pending_age_seconds=0))]:
            rpc.value=bad
            flow.worker.quitting=OneRound()
            flow.refresh_audit_pressure()
            self.assertEqual(queue.audit_service_view(),before)
            self.assertTrue(flow.audit_pressure_notice)

    def test_large_old_stock_drains_with_ordinary_searchers(self):
        clock=[0]
        enabled,stopping=threading.Event(),threading.Event()
        enabled.set()
        old_remaining=1000
        pending=[]
        completed=set()
        next_id=0
        search_count=audit_count=0
        maximum_jobs=0
        errors=[]
        class Engine:
            def run_ticket(self,ticket,maximum):
                clock[0]+=200_000_000_000 if ticket['role']=='search' else 2_000_000_000
                return dict(state='COMPLETE')
        def submit(role,old=False):
            nonlocal next_id,maximum_jobs
            next_id+=1
            ticket=dict(ticket_id=f'{next_id:064x}',start='150000000000000000000',end='150000200000000000000',role=role,old=old)
            queue.submit(ticket,dict(role=role),64_000_000_000_000)
            maximum_jobs=max(maximum_jobs,len(queue.jobs))
        def fill_audits():
            while pending and len(queue.audits)<10:
                submit('audit',pending.pop())
        def done(job,result):
            nonlocal old_remaining,search_count,audit_count
            self.assertNotIn(job.ticket['ticket_id'],completed)
            completed.add(job.ticket['ticket_id'])
            if job.context['role']=='search':
                search_count+=1
                pending.extend([False]*10) # Two five-partition audits per ticket.
                if len(queue.search)<2:submit('search')
            else:
                audit_count+=1
                if job.ticket['old']:old_remaining-=1
            if old_remaining==0:
                stopping.set()
            else:
                fill_audits()
                queue.update_audit_backlog(old_remaining+len(pending),3,3600)
        queue=ResidentGpuQueue(Engine(),enabled,stopping,lambda job:None,done,lambda:None,errors.append,
                               clock_ns=lambda:clock[0],policy_clock=lambda:clock[0]/1e9)
        pending.extend([True]*1000)
        fill_audits()
        submit('search');submit('search')
        queue.update_audit_backlog(1000,3,3600)
        queue.run()
        self.assertFalse(errors)
        self.assertEqual(old_remaining,0)
        self.assertGreater(search_count,0)
        self.assertGreater(audit_count,search_count*10)
        self.assertLessEqual(maximum_jobs,13)
        self.assertEqual(len(completed),search_count+audit_count)
        self.assertLessEqual(queue.audit_service_view()['target_audit_share'],.8)

    def test_no_eligible_audits_never_idles_search(self):
        enabled,stopping=threading.Event(),threading.Event();enabled.set()
        class Engine:
            def run_ticket(self,ticket,maximum):return dict(state='COMPLETE')
        queue=ResidentGpuQueue(Engine(),enabled,stopping,lambda job:None,lambda job,result:None,lambda:None,lambda error:None)
        queue.update_audit_backlog(10**12,1,86400)
        ticket=dict(ticket_id='a'*64,start='10',end='20')
        future=queue.submit(ticket,dict(role='search'),10)
        self.assertEqual(queue._next().future,future)
        self.assertIsNone(queue._next())

    def test_invalid_measurement_cannot_erase_pressure(self):
        queue=ResidentGpuQueue(None,threading.Event(),threading.Event(),None,None,None,None)
        queue.update_audit_backlog(10**9,1,3600)
        original=queue.audit_service_view()
        for values in [(True,1,1),(1,0,False),(-1,1,0),(1,1,float('nan'))]:
            with self.assertRaises(ValueError):queue.update_audit_backlog(*values)
            self.assertEqual(queue.audit_service_view(),original)

    def test_pause_preserves_exact_job_and_future(self):
        enabled,stopping=threading.Event(),threading.Event();enabled.set()
        calls=[];completed=[];errors=[];clock=[0]
        class Engine:
            def run_ticket(self,ticket,maximum):
                calls.append(ticket['ticket_id']);clock[0]+=1_000_000
                return dict(state='PAUSED' if len(calls)==1 else 'COMPLETE')
        def pause():
            enabled.set() # Simulate an explicit user resume after acknowledgement.
        def done(job,result):
            completed.append(job.ticket['ticket_id']);stopping.set()
        queue=ResidentGpuQueue(Engine(),enabled,stopping,lambda job:None,done,pause,errors.append,clock_ns=lambda:clock[0])
        ticket=dict(ticket_id='b'*64,start='10',end='20')
        future=queue.submit(ticket,dict(role='search'),10)
        queue.run()
        self.assertFalse(errors)
        self.assertEqual(calls,['b'*64,'b'*64])
        self.assertEqual(completed,['b'*64])
        self.assertEqual(future.result()['state'],'COMPLETE')

if __name__=='__main__':unittest.main()
