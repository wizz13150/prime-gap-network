from pathlib import Path
from types import SimpleNamespace
import json,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
from horizon_coordination import confirm_candidate,CoordinationParentChanged,native_header_hash
from horizon_core_rpc import CoreRpcError
from horizon_network_worker import NetworkWorker

class ParentChangeTests(unittest.TestCase):
 def fixture(self):
  raw=bytearray(113);raw[4:36]=bytes.fromhex('1'*64)[::-1];raw[80]=2
  intent=dict(block=raw.hex(),hash=native_header_hash(raw),parent='1'*64)
  class RPC:
   tip='1'*64;active=False;submissions=0;move_on_submit=False
   def call(self,method,args=None):
    if method=='getbestblockhash':return self.tip
    if method=='getblockheader':
     if self.active:return dict(hash=args[0],height=1,confirmations=1)
     raise CoreRpcError(-5,'unknown candidate')
    if method=='getblockhash':return intent['hash']
    if method=='submitblock':
     self.submissions+=1
     if self.move_on_submit:self.tip='2'*64;return 'duplicate'
     self.active=True;return None
    raise AssertionError(method)
  return intent,RPC()
 def test_parent_change_before_submit_preserves_candidate_and_never_sends(self):
  intent,rpc=self.fixture();before=dict(intent);rpc.tip='2'*64
  with self.assertRaises(CoordinationParentChanged):confirm_candidate(rpc,intent,rebase=True,sleep=lambda _:self.fail('obsolete candidate waited'))
  self.assertEqual(rpc.submissions,0);self.assertEqual(intent,before)
 def test_parent_change_after_ambiguous_duplicate_does_not_wait_or_resubmit(self):
  intent,rpc=self.fixture();rpc.move_on_submit=True
  with self.assertRaises(CoordinationParentChanged):confirm_candidate(rpc,intent,rebase=True,sleep=lambda _:self.fail('obsolete duplicate waited'))
  self.assertEqual(rpc.submissions,1)
 def test_active_candidate_remains_accepted_when_tip_advanced(self):
  intent,rpc=self.fixture();rpc.tip='2'*64;rpc.active=True
  confirm_candidate(rpc,intent,rebase=True);self.assertEqual(rpc.submissions,0)
 def test_same_parent_normal_publication_still_requires_active_inclusion(self):
  intent,rpc=self.fixture();confirm_candidate(rpc,intent,rebase=True)
  self.assertEqual(rpc.submissions,1);self.assertTrue(rpc.active)
 def test_worker_journal_survives_race_and_next_authorized_operation_rebases(self):
  intent,rpc=self.fixture();rpc.move_on_submit=True
  with tempfile.TemporaryDirectory(prefix='pgn-parent-rebase-')as temporary:
   root=Path(temporary).resolve();self.assertTrue(root.name.startswith('pgn-parent-rebase-'))
   original=rpc.call
   def call(method,args=None):
    if method=='getpgnbootstraptemplate':
     raw=bytearray(113);raw[4:36]=bytes.fromhex(rpc.tip)[::-1];raw[80]=2
     return dict(block=raw.hex(),parent=rpc.tip,numeric_target='f'*64)
    if method=='getblockhash':return rpc.accepted
    if method=='submitblock'and not rpc.move_on_submit:
     rpc.submissions+=1;rpc.accepted=native_header_hash(bytes.fromhex(args[0]));rpc.active=True;return None
    return original(method,args)
   rpc.call=call;worker=NetworkWorker.__new__(NetworkWorker);worker.root=root;worker.rpc=rpc;worker.args=SimpleNamespace(pilot_coordination=True)
   with self.assertRaises(CoordinationParentChanged):worker.ledger(['old-Core-authorized-envelope'])
   saved=json.loads((root/'coordination-intent.json').read_bytes());self.assertNotIn('confirmed',saved)
   rpc.move_on_submit=False
   receipt=worker.ledger(['fresh-Core-authorized-envelope'])
   retained=root/'coordination-history'/(saved['hash']+'.json')
   self.assertEqual(json.loads(retained.read_bytes()),saved)
   self.assertNotEqual(receipt['block_hash'],saved['hash'])
   self.assertTrue(json.loads((root/'coordination-intent.json').read_bytes())['confirmed'])
 def test_worker_unknown_confirmation_yields_without_waiting_in_publication_lane(self):
  from horizon_coordination import PublicationPending
  import horizon_network_worker as module
  from unittest.mock import patch
  intent,rpc=self.fixture()
  original=rpc.call
  def call(method,args=None):
   if method=='getpgnbootstraptemplate':return dict(block=intent['block'],parent=intent['parent'],numeric_target='f'*64)
   if method=='submitblock':rpc.submissions+=1;return 'inconclusive'
   return original(method,args)
  rpc.call=call
  actual=module.confirm_candidate
  def one_step(rpc,value,**kwargs):
   self.assertEqual(kwargs.get('seconds'),0)
   return actual(rpc,value,**kwargs,sleep=lambda _:self.fail('publication lane slept'))
  with tempfile.TemporaryDirectory()as temporary:
   worker=NetworkWorker.__new__(NetworkWorker);worker.root=Path(temporary);worker.rpc=rpc
   worker.args=SimpleNamespace(pilot_coordination=True)
   with patch.object(module,'confirm_candidate',one_step),self.assertRaises(PublicationPending):worker.ledger(['authorized-envelope'])
   saved=json.loads((worker.root/'coordination-intent.json').read_bytes())
   self.assertNotIn('confirmed',saved)
   self.assertEqual(rpc.submissions,1)
   # A lost acknowledgement can become visible on the next observation.
   # The exact candidate is reconciled before any additional send.
   rpc.active=True
   receipt=worker.ledger(['authorized-envelope'])
   self.assertEqual(receipt['block_hash'],saved['hash'])
   self.assertEqual(rpc.submissions,1)

if __name__=='__main__':unittest.main()
