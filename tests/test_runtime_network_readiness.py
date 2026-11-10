import pathlib,sys,unittest,hashlib
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'src/engine/runtime_host'))
from horizon_sync_guard import synchronized,wait_for_network
from horizon_coordination import confirm_candidate,active_candidate,native_header_hash,validate_intent
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
 def test_native_scientific_header_identity_uses_riecoin_field_order(self):
  header=bytearray(HEADER);header[68:76]=(1793540000).to_bytes(8,'little');header[76:80]=(32*65536).to_bytes(4,'little')
  reordered=header[:68]+header[76:80]+header[68:76]+header[80:112]
  expected=hashlib.sha256(hashlib.sha256(reordered).digest()).digest()[::-1].hex()
  self.assertEqual(native_header_hash(header),expected)
  self.assertNotEqual(expected,hashlib.sha256(hashlib.sha256(header).digest()).digest()[::-1].hex())
  validate_intent(dict(hash=expected,parent='a'*64,block=(header+b'\0').hex()))
 def test_native_coordination_header_keeps_wire_order(self):
  header=bytearray(HEADER);header[68:76]=(1793540000).to_bytes(8,'little');header[76:80]=(0x207fffff).to_bytes(4,'little');header[80:112]=(2).to_bytes(32,'little')
  self.assertEqual(native_header_hash(header),hashlib.sha256(hashlib.sha256(header).digest()).digest()[::-1].hex())
 def test_exact_synced_chain_ready(self):self.assertTrue(synchronized(ReadyRpc(),1242,'a'*64))
 def test_paused_ready_network_allows_report_maintenance_without_compute(self):
  from types import SimpleNamespace
  from threading import Event
  running=Event();worker=SimpleNamespace(quitting=Event(),running=running,rpc=ReadyRpc(),args=SimpleNamespace(bootstrap_height=1242,bootstrap_hash='a'*64))
  self.assertTrue(wait_for_network(worker));self.assertFalse(running.is_set())
 def inbound_rpc(self,**peer_changes):
  rpc=ReadyRpc();original=rpc.call
  def call(method,params=None):
   if method=='getpeerinfo':return [dict(connection_type='inbound',inbound=True,pgn_announced_height=rpc.remote_height,pgn_syncing=rpc.native_syncing,**peer_changes)]
   return original(method,params)
  rpc.call=call;return rpc
 def test_synced_listening_seed_with_only_inbound_peer(self):
  self.assertTrue(synchronized(self.inbound_rpc(),1242,'a'*64))
 def test_uninitialized_inbound_does_not_disable_synced_seed(self):
  rpc=self.inbound_rpc();original=rpc.call
  def call(method,params=None):
   result=original(method,params)
   if method=='getpeerinfo':return [dict(connection_type='inbound',inbound=True),*result]
   return result
  rpc.call=call;self.assertTrue(synchronized(rpc,1242,'a'*64))
  rpc.ibd=True;self.assertFalse(synchronized(rpc,1242,'a'*64))
 def test_only_uninitialized_inbound_is_not_a_sync_witness(self):
  rpc=ReadyRpc();original=rpc.call
  rpc.call=lambda method,params=None: [dict(connection_type='inbound',inbound=True)] if method=='getpeerinfo'else original(method,params)
  self.assertFalse(synchronized(rpc,1242,'a'*64))
 def empty_header_seed(self,**changes):
  rpc=ReadyRpc();original=rpc.call
  peer=dict(connection_type='inbound',inbound=True,version=260600,inflight=[],
            bytessent_per_msg={'verack':33},bytesrecv_per_msg={'verack':33,'headers':22})
  peer.update(changes)
  rpc.call=lambda method,params=None:[peer]if method=='getpeerinfo'else original(method,params)
  return rpc
 def test_ready_seed_recovers_after_equal_tip_empty_headers_exchange(self):
  self.assertTrue(synchronized(self.empty_header_seed(),1242,'a'*64))
 def test_empty_headers_seed_still_requires_native_validation_and_checkpoint(self):
  for field,value in [('ibd',True),('headers',1243),('blocks',1241),('peers',0)]:
   with self.subTest(field=field):
    rpc=self.empty_header_seed();setattr(rpc,field,value);self.assertFalse(synchronized(rpc,1242,'a'*64))
  with self.assertRaisesRegex(ValueError,'checkpoint'):synchronized(self.empty_header_seed(),1242,'b'*64)
 def test_empty_headers_fallback_rejects_incomplete_handshake_and_malformed_hints(self):
  for changes in [dict(version=True),dict(version=0),dict(inflight=[1243]),dict(bytessent_per_msg={}),
                  dict(bytesrecv_per_msg={'verack':33}),dict(bytesrecv_per_msg={'verack':33,'headers':True}),
                  dict(pgn_announced_height=None),dict(pgn_syncing=None),dict(connection_type='manual')]:
   with self.subTest(changes=changes):self.assertFalse(synchronized(self.empty_header_seed(**changes),1242,'a'*64))
 def test_empty_headers_fallback_does_not_override_announced_pending_work(self):
  for height,syncing in [(1243,False),(1242,True)]:
   rpc=self.empty_header_seed(pgn_announced_height=height,pgn_syncing=syncing)
   self.assertFalse(synchronized(rpc,1242,'a'*64))
 def test_inbound_topology_cannot_override_core_ibd(self):
  rpc=self.inbound_rpc();rpc.ibd=True;self.assertFalse(synchronized(rpc,1242,'a'*64))
 def test_inbound_topology_preserves_native_and_checkpoint_guards(self):
  for field,value in [('headers',1243),('blocks',1241),('remote_height',1243),('native_syncing',True),('peers',0)]:
   with self.subTest(field=field):
    rpc=self.inbound_rpc();setattr(rpc,field,value);self.assertFalse(synchronized(rpc,1242,'a'*64))
  with self.assertRaisesRegex(ValueError,'checkpoint'):synchronized(self.inbound_rpc(),1242,'b'*64)
 def test_missing_blocks_headers_ibd_and_no_peer_wait(self):
  for field,value in [('blocks',1241),('headers',1243),('ibd',None),('ibd',1),('peers',0)]:
   with self.subTest(field=field):
    rpc=ReadyRpc();setattr(rpc,field,value);self.assertFalse(synchronized(rpc,1242,'a'*64))
 def test_old_tip_ibd_can_recover_only_after_native_download_and_checkpoint(self):
  rpc=ReadyRpc();rpc.ibd=True;self.assertTrue(synchronized(rpc,1242,'a'*64));self.assertTrue(rpc.ibd)
  rpc.native_syncing=True;self.assertFalse(synchronized(rpc,1242,'a'*64))
  rpc.native_syncing=False;rpc.remote_height=1243;self.assertFalse(synchronized(rpc,1242,'a'*64))
  rpc.remote_height=1242
  with self.assertRaisesRegex(ValueError,'checkpoint'):synchronized(rpc,1242,'b'*64)
 def test_every_manual_peer_requires_exact_native_fields(self):
  for field,value in [('pgn_announced_height',True),('pgn_announced_height',-1),('pgn_announced_height','1242'),('pgn_syncing',0),('pgn_syncing',None)]:
   rpc=ReadyRpc();original=rpc.call
   def call(method,params=None):
    if method=='getpeerinfo':
     good=dict(connection_type='manual',pgn_announced_height=1242,pgn_syncing=False)
     return [good,{**good,field:value}]
    return original(method,params)
   rpc.call=call;self.assertFalse(synchronized(rpc,1242,'a'*64))
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
