"""Inactive candidate: real recovery catalogue, controlled canonical snapshots."""
from pathlib import Path
from types import SimpleNamespace
from copy import deepcopy
from unittest.mock import patch
import hashlib,json,sys,tempfile,threading,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
from horizon_network_audits import NetworkAudits,save
from horizon_audit_snapshot import AnchoredAuditStates
from report_commitment import audit_commitment,canonical_bytes

class BodyDeliveryTests(unittest.TestCase):
 def setUp(self):
  temp=tempfile.TemporaryDirectory();self.addCleanup(temp.cleanup);self.root=Path(temp.name)
  self.request='1'*64;self.genesis='f'*64
  self.audit=dict(request_id=self.request,ticket='2'*64,report_root='3'*64,slot=0,attempt=0,
      ranges=[dict(index=i,start=str(150000000000000000000+i*2000000000000),end=str(150000000000000000000+(i+1)*2000000000000))for i in range(5)],counts=[7]*5)
  self.audit['answer']=audit_commitment(self.audit['ticket'],0,list(range(5)),['7']*5,'4'*64)
  test=self
  class Rpc:
   tip='a'*64;height=10;published=False;policy='PGN_AUDIT_BODY_PUBLICATION_V1';bad_commitment=False
   def row(self):
    return dict(request_id=test.request,audited=False,owner_response_state='waiting_auditors',
      answer_publication_policy=self.policy,jobs=[dict(slot=0,attempt=0,state='committed',answer_published=self.published,
      commitment='0'*64 if self.bad_commitment else test.audit['answer']['commitment'])])
   def audit_snapshot(self,requests):
    return AnchoredAuditStates({test.request:self.row()},dict(genesis=test.genesis,hash=self.tip,height=self.height))
   def call(self,method,args=None,**kwargs):
    args=args or[]
    if method=='getbestblockhash':return self.tip
    if method=='getblockheader':return dict(height=self.height,hash=self.tip)
    if method=='getblockhash':return test.genesis if args[0]==0 else 'a'*64 if args[0]==10 else self.tip
    raise AssertionError(method)
  self.rpc=Rpc();self.calls=[];self.confirm=True
  self.worker=SimpleNamespace(root=self.root,rpc=self.rpc,source=SimpleNamespace(run_ticket=lambda *_:self.fail('A sealed body cannot compute again')),
      network_lock=threading.RLock(),running=threading.Event(),quitting=threading.Event(),args=SimpleNamespace(piece_limit=10**13))
  self.worker.running.set();self.service=NetworkAudits(self.worker)
  self.name=self.request+'-0-0';save(self.service.folder/(self.name+'.json'),self.audit);self.service.answers.register([self.name])
  def apply(action,request,slot,attempt,body):
   self.calls.append((action,request,slot,attempt,body))
   self.assertEqual(action,'publish');self.assertEqual(bytes.fromhex(body),canonical_bytes(self.audit['answer']['body']))
   if self.confirm:
    self.rpc.published=True;self.rpc.height=11;self.rpc.tip='b'*64
   return dict(available=True)
  self.service.apply=apply
 def waiting(self):
  with self.service.answers.database()as db:return db.execute('SELECT waiting FROM answers WHERE name=?',(self.name,)).fetchone()[0]
 def test_owner_absent_body_ACK_keeps_seal_without_Passed(self):
  raw=(self.service.folder/(self.name+'.json')).read_bytes();self.service.reveal_committed()
  self.assertEqual(self.waiting(),0);self.assertEqual(len(self.calls),1)
  self.assertEqual((self.service.folder/(self.name+'.json')).read_bytes(),raw)
  self.assertEqual(self.rpc.row()['jobs'][0]['state'],'committed');self.assertFalse(self.rpc.row()['audited'])
  self.service.reveal_committed();self.assertEqual(len(self.calls),1)
 def test_fork_removing_only_publication_reopens_exact_body(self):
  self.service.reveal_committed();self.assertEqual(self.waiting(),0)
  # Assignment+commit anchor10 remains canonical. Only publication11 is lost.
  self.rpc.tip='c'*64;self.rpc.published=False
  self.service.reveal_committed();self.assertEqual(len(self.calls),2);self.assertEqual(self.calls[0],self.calls[1]);self.assertEqual(self.waiting(),0)
 def test_successful_send_without_canonical_body_does_not_ACK(self):
  self.confirm=False;self.service.reveal_committed();self.assertEqual(self.waiting(),1)
 def test_crash_after_publication_before_ACK_recovers_without_resend(self):
  with patch.object(self.service.answers,'acknowledge',side_effect=OSError('Injected before ACK')):
   with self.assertRaises(OSError):self.service.reveal_committed()
  self.assertEqual(self.waiting(),1);self.assertTrue(self.rpc.published)
  restored=NetworkAudits(self.worker);restored.apply=lambda *_:self.fail('Canonical body must be adopted')
  restored.reveal_committed();self.assertEqual(self.waiting(),0);self.assertEqual(len(self.calls),1)
 def test_unknown_policy_or_mismatched_commitment_does_not_publish_or_ACK(self):
  self.rpc.policy='UNKNOWN_FUTURE_VERSION';self.service.reveal_committed();self.assertEqual(self.waiting(),1);self.assertFalse(self.calls)
  self.rpc.policy='PGN_AUDIT_BODY_PUBLICATION_V1';self.rpc.bad_commitment=True;self.rpc.published=True
  self.service.reveal_committed();self.assertEqual(self.waiting(),1);self.assertFalse(self.calls)
 def test_STOP_preserves_body_delivery_queue(self):
  self.worker.quitting.set();self.service.reveal_committed();self.assertEqual(self.waiting(),1);self.assertFalse(self.calls)

if __name__=='__main__':unittest.main()
