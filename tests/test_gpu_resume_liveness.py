"""A paused task must retain its Future without blocking eligible independent work."""
from pathlib import Path
from collections import Counter
import sys
import threading
import unittest

runtime=Path(__file__).resolve().parents[1]/'src/engine/runtime_host'
if '--runtime-root' in sys.argv:
    index=sys.argv.index('--runtime-root');runtime=Path(sys.argv[index+1]);del sys.argv[index:index+2]
sys.path.insert(0,str(runtime))
from horizon_gpu_queue import ResidentGpuQueue

class ResumeLivenessTests(unittest.TestCase):
    def test_blocked_paused_search_yields_to_the_audit_that_releases_it(self):
        enabled=threading.Event();enabled.set();stopped=threading.Event();done=threading.Event()
        admitted={'search':True};calls=[];completed=[];errors=[]
        class Engine:
            def run_ticket(self,ticket,limit):
                name=ticket['ticket_id'];calls.append(name)
                if name=='search' and calls.count(name)==1:
                    admitted['search']=False
                    return {'state':'PAUSED'}
                return {'state':'COMPLETE','ticket_id':name}
        def result(job,value):
            completed.append(job.ticket['ticket_id'])
            if job.context['role']=='audit':admitted['search']=True
            else:done.set();stopped.set()
        queue=ResidentGpuQueue(Engine(),enabled,stopped,lambda j:None,result,lambda:None,errors.append,
                              may_start=lambda j:j.context['role']=='audit' or admitted['search'])
        search=queue.submit({'ticket_id':'search'},{'role':'search'},100)
        audit=queue.submit({'ticket_id':'audit'},{'role':'audit'},100)
        # Select search once before the independent audit; no timing-dependent weights.
        queue.resume_job=queue.search.popleft()
        thread=threading.Thread(target=queue.run);thread.start()
        try:
            self.assertTrue(done.wait(2),'Paused search prevented the audit needed to release it')
            self.assertEqual(completed,['audit','search'])
            self.assertEqual(Counter(calls),Counter(search=2,audit=1))
            self.assertEqual(search.result()['ticket_id'],'search')
            self.assertEqual(audit.result()['ticket_id'],'audit')
            self.assertFalse(errors)
        finally:stopped.set();queue.wake();thread.join(2)
        self.assertFalse(thread.is_alive());self.assertFalse(queue.jobs)

    def test_two_paused_jobs_are_both_retained_and_resume_once(self):
        enabled=threading.Event();enabled.set();stopped=threading.Event();both_paused=threading.Event();done=threading.Event()
        allowed={'first':True,'second':True};calls=Counter();completed=[];errors=[]
        class Engine:
            def run_ticket(self,ticket,limit):
                name=ticket['ticket_id'];calls[name]+=1
                if calls[name]==1:
                    allowed[name]=False
                    if calls['first'] and calls['second']:both_paused.set()
                    return {'state':'PAUSED'}
                return {'state':'COMPLETE','ticket_id':name}
        def result(job,value):
            completed.append(job.ticket['ticket_id'])
            if len(completed)==2:done.set();stopped.set()
        queue=ResidentGpuQueue(Engine(),enabled,stopped,lambda j:None,result,lambda:None,errors.append,
                              may_start=lambda j:allowed[j.ticket['ticket_id']])
        first=queue.submit({'ticket_id':'first'},{'role':'search'},100)
        second=queue.submit({'ticket_id':'second'},{'role':'audit'},100)
        queue.resume_job=queue.search.popleft()
        thread=threading.Thread(target=queue.run);thread.start()
        try:
            self.assertTrue(both_paused.wait(2),'A deferred resume starved the other task')
            self.assertIs(queue.jobs['first'].future,first)
            self.assertIs(queue.jobs['second'].future,second)
            allowed.update(first=True,second=True);queue.wake()
            self.assertTrue(done.wait(2),'A later PAUSED result overwrote the retained task')
            self.assertEqual(set(completed),{'first','second'});self.assertEqual(len(completed),2)
            self.assertEqual(calls,Counter(first=2,second=2));self.assertFalse(errors)
            self.assertEqual(first.result()['ticket_id'],'first');self.assertEqual(second.result()['ticket_id'],'second')
        finally:stopped.set();queue.wake();thread.join(2)
        self.assertFalse(thread.is_alive())

if __name__=='__main__':unittest.main()
