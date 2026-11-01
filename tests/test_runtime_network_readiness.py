import pathlib,sys,unittest,hashlib
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'src/engine/runtime_host'))
from horizon_sync_guard import synchronized
from horizon_coordination import confirm_candidate,active_candidate
from horizon_core_rpc import CoreRpcError
HEADER=bytearray(112);HEADER[4:36]=bytes.fromhex('a'*64)[::-1]
EXACT_HASH=hashlib.sha256(hashlib.sha256(HEADER).digest()).digest()[::-1].hex()
EXACT_INTENT=dict(hash=EXACT_HASH,block=(HEADER+b'\x00').hex(),parent='a'*64)

class ReadyRpc:
 def __init__(self):self.blocks=1242;self.headers=1242;self.ibd=False;self.peers=1;self.checkpoint='a'*64;self.remote_height=1242;self.native_syncing=False
 def call(self,method,params=None):
  if method=='getblockchaininfo':return dict(blocks=self.blocks,headers=self.headers,initialblockdownload=self.ibd)
  if method=='getconnectioncount':return self.peers
  if method=='getpeerinfo':return [dict(connection_type='manual',pgn_announced_height=self.remote_height,pgn_syncing=self.native_syncing)]
  if method=='getblockhash':return self.checkpoint
  raise AssertionError(method)
class ReadinessTests(unittest.TestCase):
 def test_exact_synced_chain_ready(self):self.assertTrue(synchronized(ReadyRpc(),1242,'a'*64))
 def test_missing_blocks_headers_ibd_and_no_peer_wait(self):
  for field,value in [('blocks',1241),('headers',1243),('ibd',True),('ibd',None),('peers',0)]:
   with self.subTest(field=field):
    rpc=ReadyRpc();setattr(rpc,field,value);self.assertFalse(synchronized(rpc,1242,'a'*64))
 def test_foreign_checkpoint_rejected(self):
  with self.assertRaisesRegex(ValueError,'checkpoint'):synchronized(ReadyRpc(),1242,'b'*64)
 def test_native_local_headers_equal_blocks_does_not_hide_remote_ahead(self):
  rpc=ReadyRpc();rpc.remote_height=1300;self.assertFalse(synchronized(rpc,1242,'a'*64))
 def test_native_pending_body_still_waits(self):
  rpc=ReadyRpc();rpc.native_syncing=True;self.assertFalse(synchronized(rpc,1242,'a'*64))
 def test_unanchored_or_malformed_snapshot_refused(self):
  for height,hash_value in [(0,'a'*64),(True,'a'*64),(1,'invalid')]:
   with self.assertRaises(ValueError):synchronized(ReadyRpc(),height,hash_value)

class CandidateRpc:
 def __init__(self,mode='accept'):
  self.mode=mode;self.present=False;self.active=False;self.submits=[];self.hash=EXACT_HASH
 def call(self,method,params=None):
  if method=='getblockheader':
   if not self.present:raise CoreRpcError(-5,'Block not found')
   return dict(hash=self.hash,height=4,confirmations=1 if self.active else -1)
  if method=='getblockhash':return self.hash if self.active else 'c'*64
  if method=='submitblock':
   self.submits.append(params[0])
   if self.mode in ('accept','accepted_loss'):self.present=self.active=True
   if self.mode in ('unknown_loss','accepted_loss'):raise OSError('lost reply')
   if self.mode=='orphan':self.present=True
   return 'inconclusive'if self.mode=='inconclusive'else'duplicate-inconclusive'if self.mode=='duplicate'else None
  raise AssertionError(method)
class ConfirmationTests(unittest.TestCase):
 def setUp(self):self.intent=dict(EXACT_INTENT)
 def test_actual_active_acceptance(self):
  rpc=CandidateRpc();confirm_candidate(rpc,self.intent,seconds=0);self.assertEqual(rpc.submits,[self.intent['block']])
 def test_accepted_lost_reply_checked_without_resubmit(self):
  rpc=CandidateRpc('accepted_loss');confirm_candidate(rpc,self.intent,seconds=0);self.assertEqual(len(rpc.submits),1)
 def test_unknown_loss_retains_no_claim(self):
  rpc=CandidateRpc('unknown_loss')
  with self.assertRaisesRegex(RuntimeError,'unresolved'):confirm_candidate(rpc,self.intent,seconds=0)
  self.assertEqual(len(rpc.submits),1)
 def test_success_reply_or_duplicate_is_not_acceptance(self):
  for mode in ('orphan','duplicate','inconclusive'):
   rpc=CandidateRpc(mode)
   with self.assertRaisesRegex(RuntimeError,'not on the active'):confirm_candidate(rpc,self.intent,seconds=0)
   self.assertEqual(rpc.submits,[self.intent['block']])
 def test_already_active_no_duplicate_publication(self):
  rpc=CandidateRpc();rpc.active=rpc.present=True;confirm_candidate(rpc,self.intent,seconds=0);self.assertEqual(rpc.submits,[])
 def test_foreign_header_refused(self):
  rpc=CandidateRpc();rpc.present=True;rpc.hash='d'*64
  with self.assertRaises(ValueError):active_candidate(rpc,self.intent)
 def test_tampered_candidate_or_parent_is_not_recovered(self):
  for field,value in [('hash','b'*64),('parent','b'*64),('block',self.intent['block'][:-2]),('block','00'*131073)]:
   with self.subTest(field=field),self.assertRaises(ValueError):active_candidate(CandidateRpc(),dict(self.intent,**{field:value}))
if __name__=='__main__':unittest.main()
