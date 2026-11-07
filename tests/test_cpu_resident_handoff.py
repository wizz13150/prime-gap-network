"""Completed normal quanta can hand off without recreating their workers."""
from pathlib import Path
from types import SimpleNamespace
import sys,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/cpu_auditor/shared_service'),str(ROOT/'src/protocol')]
from service import Service
from contracts import digest
class ResidentHandoffTests(unittest.TestCase):
 def owner(self):
  s=object.__new__(Service);s.request=dict(owned='finite-fixture');s.result=dict(state='CPU_WORK_SOURCE_PROOF_PENDING')
  s.workers={4:SimpleNamespace(task=None,ready=True)};s.pending=None;s.probe=False;s.enabled=True;s.keep_result_workers=True;s.run_start=None;s.budget_refused=False
  s.persist=lambda:None;s.reply=lambda *a,**k:None;return s
 def test_complete_owned_ack_keeps_resident_workers_and_on_intent(self):
  s=self.owner();workers=s.workers
  s.command(dict(id=1,action='ack_result',request_sha256=digest(s.request)))
  self.assertIs(s.workers,workers);self.assertTrue(s.enabled);self.assertIsNone(s.request);self.assertIsNone(s.result)
 def test_wrong_digest_or_unfinished_work_cannot_be_cleared(self):
  for changed in ('digest','inflight','partial'):
   s=self.owner();request=s.request
   sha=digest(request)
   if changed=='digest':sha='0'*64
   if changed=='inflight':s.workers[4].task=(1,)
   if changed=='partial':s.result=None
   with self.assertRaises(ValueError):s.command(dict(id=1,action='ack_result',request_sha256=sha))
   self.assertIs(s.request,request)
 def test_completion_keeps_workers_only_for_explicit_normal_policy(self):
  for keep in (False,True):
   s=self.owner();s.keep_result_workers=keep;stops=[];s.stop_workers=lambda:stops.append(True)
   s.finish_pending();self.assertEqual(bool(stops),not keep)
 def test_user_close_drains_residents_even_with_handoff_enabled(self):
  s=self.owner();s.pending=dict(id=1,action='close',drain=True);stops=[];s.stop_workers=lambda:stops.append(True)
  s.finish_pending();self.assertTrue(stops);self.assertFalse(s.enabled);self.assertTrue(s.closed)
 def test_residency_option_is_not_an_audit_probe_or_truthy_string(self):
  for action,value in (('execute','true'),('probe',True)):
   s=self.owner()
   with self.assertRaises(ValueError):s.command(dict(id=1,action=action,request={},keep_resident=value))
if __name__=='__main__':unittest.main()
