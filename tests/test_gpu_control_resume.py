"""Real ResidentGpuQueue, controlled thread barriers; no GPU/native launch."""
from pathlib import Path
import sys,threading,unittest
ROOT=Path(__file__).resolve().parents[1];sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
from horizon_gpu_queue import ResidentGpuQueue

class DesiredResumeTests(unittest.TestCase):
 def controlled(self,latest_on,change_at_ack=None,fail=False):
  enabled,stopping=threading.Event(),threading.Event();enabled.set()
  entered,release,ack,ack_release=threading.Event(),threading.Event(),threading.Event(),threading.Event()
  calls=[];started=[];done=[];errors=[];at_ack=[]
  class Engine:
   def run_ticket(inner,ticket,maximum):
    calls.append((ticket,maximum))
    if len(calls)==1:
     entered.set()
     if not release.wait(2):raise AssertionError('test did not release first job')
     if fail:raise ValueError('injected semantic failure')
     return {'state':'PAUSED'}
    return {'state':'COMPLETE'}
  def pause():
   at_ack.append(enabled.is_set());ack.set()
   if change_at_ack is not None:
    if not ack_release.wait(2):raise AssertionError('test did not release pause ACK')
   if not enabled.is_set():stopping.set()
  def complete(job,result):done.append(job);stopping.set()
  q=ResidentGpuQueue(Engine(),enabled,stopping,lambda job:started.append(job),complete,pause,errors.append)
  ticket={'ticket_id':'a'*64,'start':'10','end':'20'};future=q.submit(ticket,{'role':'search'},10)
  thread=threading.Thread(target=q.run,daemon=True);thread.start()
  try:
   self.assertTrue(entered.wait(2),'real queue did not enter fake engine')
   q.submit({'ticket_id':'b'*64},{'role':'audit'},1)
   # The exact saved search must resume before this newly ready audit job.
   enabled.clear()  # actual controls OFF semantic, while engine owns old job
   if latest_on:enabled.set()  # ON arrives BEFORE old job returns PAUSED
   release.set();self.assertTrue(ack.wait(2),'real queue did not ACK PAUSED')
   if change_at_ack is not None:
    enabled.set() if change_at_ack else enabled.clear();ack_release.set()
   thread.join(2);self.assertFalse(thread.is_alive(),'bounded queue did not reach terminal fixture condition')
   return dict(calls=calls,started=started,done=done,errors=errors,at_ack=at_ack,future=future,enabled=enabled.is_set(),queue=q)
  finally:
   stopping.set();release.set();ack_release.set();enabled.set();q.wake();thread.join(2)
 def test_OFF_ON_before_old_PAUSED_keeps_latest_intent_and_exact_future(self):
  r=self.controlled(True);self.assertEqual(r['at_ack'],[True]);self.assertFalse(r['errors']);self.assertEqual(len(r['calls']),2)
  self.assertEqual(r['calls'][0],r['calls'][1]);self.assertIs(r['started'][0],r['started'][1]);self.assertIs(r['started'][0].future,r['future']);self.assertEqual(r['future'].result()['state'],'COMPLETE');self.assertEqual(len(r['done']),1)
 def test_latest_OFF_stays_OFF_and_retains_current_job(self):
  r=self.controlled(False);self.assertEqual(r['at_ack'],[False]);self.assertEqual(len(r['calls']),1);self.assertFalse(r['done']);self.assertIsNotNone(r['queue'].resume_job);self.assertIs(r['queue'].resume_job.future,r['future'])
 def test_ON_during_paused_callback_is_retained(self):
  r=self.controlled(False,True);self.assertEqual(len(r['calls']),2);self.assertEqual(len(r['done']),1);self.assertFalse(r['errors'])
 def test_OFF_during_pause_callback_wins_over_prior_ON(self):
  r=self.controlled(True,False);self.assertEqual(len(r['calls']),1);self.assertFalse(r['done']);self.assertFalse(r['enabled'])
 def test_semantic_failure_still_fail_closed_and_retains_work(self):
  r=self.controlled(True,fail=True);self.assertEqual(len(r['calls']),1);self.assertEqual([str(e) for e in r['errors']],['injected semantic failure']);self.assertFalse(r['enabled']);self.assertFalse(r['done']);self.assertIs(r['queue'].search[0],r['started'][0])

if __name__=='__main__':unittest.main()
