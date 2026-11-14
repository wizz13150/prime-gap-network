"""A retained answer keeps its canonical, wallet-owned signing identity."""
from pathlib import Path
from types import SimpleNamespace
import hashlib,sys,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
from horizon_network_audits import NetworkAudits

class RetainedSignerTests(unittest.TestCase):
 def setUp(self):
  self.body=b'{"retained":"same nonce"}';self.script='0014'+'ab'*20;self.calls=[];self.owned=True
  self.info_script=self.script;self.digest=hashlib.sha256(self.body).hexdigest()
  def call(method,args,**kw):
   self.calls.append((method,args,kw))
   if method=='getpgnauditstate':return [dict(jobs=[dict(slot=0,attempt=3,actor=self.script,commitment=self.digest)])]
   if method=='decodescript':return dict(address='old-owned-address')
   if method=='getaddressinfo':return dict(ismine=self.owned,scriptPubKey=self.info_script)
   if method=='getpgnauditevent':return dict(available=True,challenge='canonical challenge',envelope='00')
   if method=='signmessage':return 'owned-signature'
   raise AssertionError(method)
  self.service=object.__new__(NetworkAudits)
  self.service.worker=SimpleNamespace(rpc=SimpleNamespace(call=call),address=lambda:'new-current-address')
 def test_rotated_identity_uses_canonical_owned_signer(self):
  self.service.event('reveal','1'*64,0,3,self.body.hex())
  events=[a for m,a,_ in self.calls if m=='getpgnauditevent']
  self.assertEqual([a[1]for a in events],['old-owned-address']*2)
  self.assertEqual([a for m,a,_ in self.calls if m=='signmessage'],[['old-owned-address','canonical challenge']])
 def test_foreign_or_watch_only_key_is_never_used(self):
  self.owned=False
  with self.assertRaisesRegex(RuntimeError,'key is unavailable'):self.service.event('reveal','1'*64,0,3,self.body.hex())
  self.assertFalse(any(m in ('signmessage','getpgnauditevent')for m,_,_ in self.calls))
 def test_wallet_address_must_match_exact_canonical_script(self):
  self.info_script='0014'+'cd'*20
  with self.assertRaisesRegex(RuntimeError,'key is unavailable'):self.service.event('reveal','1'*64,0,3,self.body.hex())
  self.assertFalse(any(m=='signmessage'for m,_,_ in self.calls))
 def test_changed_body_is_rejected_before_wallet_access(self):
  with self.assertRaisesRegex(RuntimeError,'canonical commitment'):self.service.event('reveal','1'*64,0,3,b'other nonce'.hex())
  self.assertEqual([m for m,_,_ in self.calls],['getpgnauditstate'])
 def test_other_actions_keep_current_identity(self):
  self.service.event('commit','1'*64,0,3,'00')
  self.assertFalse(any(m in ('decodescript','getaddressinfo')for m,_,_ in self.calls))
  self.assertEqual([a[1]for m,a,_ in self.calls if m=='getpgnauditevent'],['new-current-address']*2)

if __name__=='__main__':unittest.main()
