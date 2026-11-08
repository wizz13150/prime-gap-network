"""An orphaned physical candidate must not pin fresh authorized publication."""
from pathlib import Path
from types import SimpleNamespace
import hashlib,json,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
from horizon_network_worker import NetworkWorker,save
from horizon_coordination import native_header_hash
from horizon_core_rpc import CoreRpcError

class CoordinationRebaseTests(unittest.TestCase):
 def test_orphan_is_preserved_and_fresh_current_operation_is_confirmed(self):
  with tempfile.TemporaryDirectory()as temporary:
   root=Path(temporary);parent='1'*64;tip='2'*64;old=bytearray(113);old[4:36]=bytes.fromhex(parent)[::-1];old[80]=2
   prior=dict(parent=parent,block=old.hex(),hash=native_header_hash(old),operation_sha256='3'*64,confirmed=False)
   save(root/'coordination-intent.json',prior);fresh=bytearray(old);fresh[4:36]=bytes.fromhex(tip)[::-1];seen=[];accepted=set()
   class Rpc:
    def call(self,method,args=None):
     if method=='getbestblockhash':return tip
     if method=='getpgnbootstraptemplate':return dict(block=fresh.hex(),parent=tip,numeric_target='f'*64)
     if method=='getblockheader':
      if args[0]==prior['hash']:return dict(hash=args[0],height=4,confirmations=-1)
      if args[0]in accepted:return dict(hash=args[0],height=6,confirmations=1)
      raise CoreRpcError(-5,'unknown')
     if method=='submitblock':
      seen.append(args[0]);accepted.add(native_header_hash(bytes.fromhex(args[0])));return None
     if method=='getblockhash':return next(iter(accepted))
     raise AssertionError(method)
   worker=NetworkWorker.__new__(NetworkWorker);worker.root=root;worker.rpc=Rpc();worker.args=SimpleNamespace(pilot_coordination=True)
   result=worker.ledger(['aa']);new=json.loads((root/'coordination-intent.json').read_bytes())
   self.assertEqual(json.loads((root/'coordination-history'/f"{prior['hash']}.json").read_bytes()),prior)
   self.assertEqual([fresh.hex()],seen);self.assertEqual(new['hash'],result['block_hash']);self.assertTrue(new['confirmed'])
   self.assertNotEqual(prior['hash'],new['hash'])
   # Retrying the same action after actual confirmation is idempotent.
   self.assertEqual(result,worker.ledger(['aa']));self.assertEqual(1,len(seen))

if __name__=='__main__':unittest.main()
