import copy,json,pathlib,sys,tempfile,unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent))
from contracts import validate,SCHEMA,PROBE,SOURCE,COVERAGE
from merit_policy import minimum_gap,next_piece,next_threshold_integer
from service import Service,validate_reply
START=199833824235735044953
def audit():
 return dict(schema=SCHEMA,ticket_id='a'*64,ticket_start=str(START),ticket_end=str(START+200000000000000),partitions=[dict(index=i,start=str(START+i*2000000000000),end=str(START+(i+1)*2000000000000))for i in[0,9,20,40,99]])
def reply(task,cpu=0):
 o,a,b,k,seq,g=task
 return dict(type='result',state='COMPLETE',backend='cpu_shared_gpu_lineage68',job_id=seq,start=str(a),end=str(b),positions=str(b-a),minimum_gap=g,pinned_cpu=cpu,affinity_verified=True,canonical_source_manifest_sha256=SOURCE,coverage_contract=COVERAGE,production_admitted=False,implementation='shared_scalar',sieve_layout='canonical_tiled',count='1',seconds=.001)
class Tests(unittest.TestCase):
 def test_audit_contract(self):
  self.assertEqual(len(validate(audit())),5)
  for mutate in[lambda a:a.update(expected_counts=[1]*5),lambda a:a['partitions'].pop(),lambda a:a['partitions'][1].update(index=0),lambda a:a['partitions'][0].update(end=str(START+5)),lambda a:a.update(ticket_start='001')]:
   a=audit();mutate(a)
   with self.assertRaises(ValueError):validate(a)
 def test_width_and_probe_separation(self):
  a=dict(schema=PROBE,start=str(START),end=str(START+100))
  self.assertEqual(len(validate(a,True)),1)
  with self.assertRaises(ValueError):validate(a)
  a['start']=str(1<<68);a['end']=str((1<<68)+100)
  with self.assertRaises(ValueError):validate(a,True)
 def test_pointwise_threshold_and_word(self):
  n=START;gap=minimum_gap(n);boundary=next_threshold_integer(gap)
  self.assertEqual(minimum_gap(boundary-1),gap);self.assertEqual(minimum_gap(boundary),gap+2)
  p=next_piece((11<<64)-19,(11<<64)+19,100)
  self.assertEqual(p.end,11<<64);self.assertEqual(p.minimum_gap,minimum_gap(p.start))
 def test_false_reply(self):
  t=(0,START,START+100,0,5,960);r=reply(t);self.assertEqual(validate_reply(r,t,0,False),1)
  for key,value in [('job_id',4),('end',str(START+99)),('affinity_verified',False),('count','101'),('canonical_source_manifest_sha256','f'*64),('implementation','independent_flint')]:
   v=dict(r);v[key]=value
   with self.assertRaises(ValueError):validate_reply(v,t,0,False)
 def test_ordered_frontier_duplicate_and_checkpoint_bound(self):
  s=Service.__new__(Service);s.workers={};s.completed={};s.committed=0;s.counts=[0];s.compute_seconds=0.;s.max_buffered=0;s.completed_piece_count=0;s.total=1000;s.probe=True;s.request=dict(schema=PROBE,start=str(START),end=str(START+1000));s.result=None;s.state='RUNNING';s.enabled=True;s.simd=False;s.saves=[];s.persist=lambda:s.saves.append((s.committed,list(s.counts)))
  class W:pass
  a,b=W(),W()
  for i,w in enumerate([a,b]):w.cpu=i;w.task=(i*100,START+i*100,START+(i+1)*100,0,i+1,960);w.positions=0;w.native_seconds=0.;s.workers[i]=w
  s.native_event(b,reply(b.task,1));self.assertEqual(s.committed,0);self.assertEqual(s.counts,[0])
  s.native_event(a,reply(a.task));self.assertEqual(s.committed,200);self.assertEqual(s.counts,[2]);self.assertEqual(s.saves,[(200,[2])]);self.assertEqual(len(s.completed),0)
  with self.assertRaises(ValueError):s.native_event(a,reply((0,START,START+100,0,1,960)))
 def test_no_partial_full_audit(self):
  s=Service.__new__(Service);s.workers={};s.completed={};s.committed=0;s.counts=[0];s.compute_seconds=0.;s.max_buffered=0;s.completed_piece_count=0;s.total=100;s.probe=False;s.result=None;s.simd=False;s.persist=lambda:None
  class W:pass
  w=W();w.cpu=0;w.task=(0,START,START+100,0,1,960);w.positions=0;w.native_seconds=0.;s.workers={0:w}
  with self.assertRaises(ValueError):s.native_event(w,reply(w.task))
 def test_eof_during_pending_mutation_is_not_lost(self):
  s=Service.__new__(Service);s.pending=dict(id=7,action='pause',generation=2);s.eof_requested=False
  s.command(dict(id='eof',action='close',drain=True));self.assertTrue(s.eof_requested);self.assertEqual(s.pending['id'],7)
 def test_fresh_budget_admission(self):
  from unittest.mock import patch
  class L:
   value=None
   next_value=None
   def sample(self):return(0,0)
   def reset(self):self.value=None
   def update(self):self.value=self.next_value;return self.value
  s=Service.__new__(Service);s.load=L();s.threads=2;s.inv={'available_cpus':list(range(12))}
  with patch('service.time.sleep')as sleep:
   self.assertFalse(s.admit_budget());sleep.assert_called_once_with(.26)
  s.load.next_value=20
  with patch('service.time.sleep'):self.assertTrue(s.admit_budget())
  s.load.next_value=70
  with patch('service.time.sleep'):self.assertFalse(s.admit_budget())
 def budget_fixture(self):
  from types import SimpleNamespace
  s=Service.__new__(Service);s.enabled=True;s.pending=None;s.request={'retained':True};s.result=None;s.threads=4;s.inv={'available_cpus':list(range(12))};s.budget_resume_since=None;s.budget_retry_after=0.;s.budget_refused=True;s.state='BUDGET_PAUSED';s.starts=0
  s.load=SimpleNamespace(value=20,update=lambda: s.load.value)
  s.admit_budget=lambda:True
  def start():s.starts+=1
  s.start_workers=start
  return s
 def test_guard_resumes_after_stable_background_only(self):
  from unittest.mock import patch
  s=self.budget_fixture()
  with patch('service.time.monotonic',return_value=0.):s.recover_budget()
  with patch('service.time.monotonic',return_value=1.99):s.recover_budget()
  self.assertEqual(s.starts,0);self.assertTrue(s.budget_refused)
  with patch('service.time.monotonic',return_value=2.):s.recover_budget()
  self.assertEqual(s.starts,1);self.assertFalse(s.budget_refused);self.assertEqual(s.state,'RUNNING')
 def test_guard_resumes_admissible_seven_thread_budget(self):
  from unittest.mock import patch
  s=self.budget_fixture();s.threads=7;s.load.value=14
  with patch('service.time.monotonic',return_value=0.):s.recover_budget()
  with patch('service.time.monotonic',return_value=2.):s.recover_budget()
  self.assertEqual(s.starts,1);self.assertFalse(s.budget_refused)
 def test_enabled_budget_can_wait_without_rejecting_the_ON_choice(self):
  s=self.budget_fixture();s.workers={};s.run_start=None;s.result=None;s.pending=dict(id=1,action='configure',generation=1,enabled=True,threads=7,affinity=[0,2,4,5,6,7,8]);s.admit_budget=lambda:False;s.stop_workers=lambda **kwargs:None;replies=[];s.reply=lambda command,*args,**kwargs:replies.append((args,kwargs));s.finish_pending()
  self.assertTrue(s.enabled);self.assertTrue(s.budget_refused);self.assertEqual(s.state,'BUDGET_PAUSED');self.assertEqual(replies,[((),{})]);self.assertEqual(s.starts,0)
 def test_guard_burst_resets_window_and_honors_OFF_close_result(self):
  from unittest.mock import patch
  s=self.budget_fixture()
  with patch('service.time.monotonic',return_value=0.):s.recover_budget()
  s.load.value=60
  with patch('service.time.monotonic',return_value=2.):s.recover_budget()
  self.assertIsNone(s.budget_resume_since);self.assertEqual(s.starts,0)
  s.load.value=20
  for mutation in[{'enabled':False},{'pending':{'action':'close'}},{'result':{'state':'COMPLETE'}}]:
   t=self.budget_fixture();t.budget_resume_since=0.
   for k,v in mutation.items():setattr(t,k,v)
   with patch('service.time.monotonic',return_value=10.):t.recover_budget()
   self.assertEqual(t.starts,0)
 def test_guard_uses_fresh_admission_and_preserves_closed_result(self):
  from unittest.mock import patch
  s=self.budget_fixture();s.budget_resume_since=0.;s.admit_budget=lambda:False
  with patch('service.time.monotonic',return_value=3.):s.recover_budget()
  self.assertEqual(s.starts,0);self.assertTrue(s.budget_refused)
  s.workers={};s.pending=None;s.run_start=None;s.result={'state':'AUDIT_COMPUTED_SOURCE_PROOF_PENDING'};s.enabled=False;s.stops=0
  def stop():s.stops+=1
  s.stop_workers=stop;s.finish_pending()
  self.assertEqual(s.state,s.result['state']);self.assertFalse(s.budget_refused);self.assertEqual(s.stops,1)
class LoadWindowTests(unittest.TestCase):
 def test_short_spike_uses_counter_window(self):
  from unittest.mock import patch
  from native_transport import Load
  s=Load.__new__(Load);values=iter([(0,0),(80,100),(160,200),(160,300),(240,400)])
  s.sample=lambda:next(values)
  with patch('native_transport.time.monotonic',return_value=0.):s.reset()
  observed=[]
  for t in(.25,.5,.75,1.):
   with patch('native_transport.time.monotonic',return_value=t):observed.append(s.update())
  self.assertAlmostEqual(observed[-1],40.);self.assertLess(max(observed),75.)
 def test_reset_removes_old_worker_load_before_admission(self):
  from unittest.mock import patch
  from native_transport import Load
  s=Load.__new__(Load);values=iter([(0,0),(0,100),(0,200),(80,300)]);s.sample=lambda:next(values)
  with patch('native_transport.time.monotonic',return_value=0.):s.reset()
  with patch('native_transport.time.monotonic',return_value=.25):self.assertEqual(s.update(),100.)
  with patch('native_transport.time.monotonic',return_value=1.):s.reset()
  with patch('native_transport.time.monotonic',return_value=1.26):self.assertAlmostEqual(s.update(),20.)
if __name__=='__main__':unittest.main()
