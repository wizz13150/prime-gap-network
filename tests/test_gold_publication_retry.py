"""Gold acceptance survives the submit/receipt crash boundary; no devices."""
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
import json,sys,tempfile,unittest
P=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(P/'src/engine/runtime_host'),str(P/'src/protocol')]
import horizon_network_worker as nw

class GoldPublicationRetryTests(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
  self.root=Path(self.temp.name);self.sent=0;self.confirmed={};self.fail_after_submit=False
  self.worker=nw.NetworkWorker.__new__(nw.NetworkWorker)
  self.worker.root=self.root;self.worker.args=SimpleNamespace(genesis='g'*64,proof_builder='unused')
  self.worker.address=lambda:'miner';self.worker.rpc=self
  self.sealed=dict(report_root='r'*64,body=dict(ticket='t'*64,computed=dict(best=[dict(gap=2000,lower_prime='150000000000000000001')])))
  self.folder=self.root/'tickets'/('t'*64)
  self.folder.mkdir(parents=True)
  self.addCleanup(patch.stopall)
  patch.object(nw,'build_gap_proof',return_value=dict(details_root='d'*64)).start()
  patch.object(nw,'assemble_gold_proof',return_value='proof').start()
 def call(self,method,parameters=None,**kw):
  if method=='getpgnscientificwork':return dict(target_numerator=28,target_denominator=1,challenge='sign')
  if method=='signmessage':return 'signature'
  if method=='getpgnscientifictemplate':return dict(work_verified=True,winner_atomic='123',block='bytes',hash='b'*64)
  if method=='submitblock':
   self.sent+=1;self.confirmed['b'*64]=1;return None
  if method=='getblockheader':
   if self.fail_after_submit and self.sent:
    self.fail_after_submit=False;raise OSError('crash after accepted submission')
   return dict(confirmations=self.confirmed.get(parameters[0],-1))
  raise AssertionError(method)
 def submit(self):return self.worker.submit_gold('q'*64,self.sealed,'d'*64)
 def test_retry_after_success_does_not_submit_twice(self):
  self.assertEqual(self.submit(),'accepted');self.assertEqual(self.submit(),'accepted');self.assertEqual(self.sent,1)
 def test_crash_after_acceptance_recovers_from_intent(self):
  self.fail_after_submit=True
  with self.assertRaises(OSError):self.submit()
  self.assertFalse((self.folder/'network-acceptance.json').exists())
  self.assertEqual(self.submit(),'accepted');self.assertEqual(self.sent,1)
 def test_orphaned_receipt_is_not_treated_as_active_acceptance(self):
  self.submit();self.confirmed['b'*64]=-1
  self.assertEqual(self.submit(),'accepted');self.assertEqual(self.sent,2)
 def test_foreign_request_receipt_is_rejected_before_broadcast(self):
  self.submit();p=self.folder/'network-acceptance.json';v=json.loads(p.read_bytes());v['request_id']='x'*64;p.write_text(json.dumps(v))
  with self.assertRaisesRegex(ValueError,'differs'):self.submit()
  self.assertEqual(self.sent,1)
 def test_invalid_proof_never_creates_publication_intent(self):
  with patch.object(nw,'build_gap_proof',return_value=dict(details_root='x'*64)):
   with self.assertRaisesRegex(ValueError,'does not match'):self.submit()
  self.assertEqual(self.sent,0);self.assertFalse((self.folder/'gold-publication-intent.json').exists())

if __name__=='__main__':unittest.main()
