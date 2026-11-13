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



    def test_no_eligible_audits_never_idles_search(self):
        enabled,stopping=threading.Event(),threading.Event();enabled.set()
        class Engine:
            def run_ticket(self,ticket,maximum):return dict(state='COMPLETE')
        queue=ResidentGpuQueue(Engine(),enabled,stopping,lambda job:None,lambda job,result:None,lambda:None,lambda error:None)
        ticket=dict(ticket_id='a'*64,start='10',end='20')
        future=queue.submit(ticket,dict(role='search'),10)
        self.assertEqual(queue._next().future,future)
        self.assertIsNone(queue._next())


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
