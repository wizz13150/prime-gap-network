from pathlib import Path
from types import SimpleNamespace
import sys,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host')]
from horizon_audit_actor_transition import native_service_actors
from horizon_core_rpc import CoreRpcError

class ActorTransitionTests(unittest.TestCase):
    def worker(self,enforced=True):
        def call(method,args):
            self.assertEqual((method,args),('getpgnworkdutystatus',['real-miner']))
            return dict(schema='PGN_NATIVE_WORK_DUTY_STATUS_V1',genesis='a'*64,enforced=enforced)
        return SimpleNamespace(rpc=SimpleNamespace(call=call),address=lambda:'real-miner',args=SimpleNamespace(genesis='a'*64))
    def test_public_clients_do_not_change_or_add_an_rpc(self):
        self.assertFalse(native_service_actors(None,None))
    def test_service_enforced_uses_real_miner_not_lab_credits(self):
        self.assertTrue(native_service_actors(self.worker(),['lab1','lab2']))
    def test_old_core_keeps_existing_pilot_actors(self):
        self.assertFalse(native_service_actors(self.worker(False),['lab1','lab2']))
    def test_unverified_policy_cannot_authorize_transition(self):
        for bad in [None,1,'true']:
            with self.assertRaises(ValueError):native_service_actors(self.worker(bad),['lab1','lab2'])
        w=self.worker();w.args.genesis='b'*64
        with self.assertRaises(ValueError):native_service_actors(w,['lab1','lab2'])
    def test_only_missing_rpc_can_fall_back(self):
        w=self.worker()
        for code in [-32601,-28]:
            def call(*_,code=code):raise CoreRpcError(code,'fixture')
            w.rpc.call=call
            if code==-32601:self.assertFalse(native_service_actors(w,['lab1','lab2']))
            else:
                with self.assertRaises(CoreRpcError):native_service_actors(w,['lab1','lab2'])

if __name__=='__main__':unittest.main()
