from pathlib import Path
import json,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'src/engine/runtime_host'))
from horizon_lab_audit_actors import load_lab_audit_actors

class Rpc:
    def __init__(self):self.chain='test';self.owned=True;self.calls=[]
    def call(self,name,args=None,**kw):
        self.calls.append((name,args,kw))
        if name=='getblockchaininfo':return {'chain':self.chain}
        if name=='getaddressinfo':return {'ismine':self.owned}
        raise AssertionError('Unexpected network mutation '+name)

class LabActorsTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.file=Path(self.temp.name)/'actors.json';self.rpc=Rpc();self.value={'schema':'PGN_CONTROLLED_LAB_AUDIT_ACTORS_V1','genesis':'a'*64,'scope':'controlled-testnet-laboratory','independent_economic_auditors':False,'actors':['tpgn'+'A'*30,'tpgn'+'B'*30]};self.write()
    def write(self):self.file.write_text(json.dumps(self.value))
    def load(self,pilot=True):return load_lab_audit_actors(self.file,self.rpc,'a'*64,pilot_coordination=pilot)
    def test_absence_does_not_read_rpc_or_change_normal_identity(self):
        self.assertIsNone(load_lab_audit_actors(None,self.rpc,'a'*64,pilot_coordination=False));self.assertEqual(self.rpc.calls,[])
    def test_explicit_two_wallet_owned_actors_without_network_mutations(self):
        self.assertEqual(self.load(),tuple(self.value['actors']));self.assertEqual([x[0]for x in self.rpc.calls],['getblockchaininfo','getaddressinfo','getaddressinfo'])
    def test_mainnet_and_missing_pilot_rejected(self):
        with self.assertRaises(ValueError):self.load(False)
        self.rpc.chain='main'
        with self.assertRaises(ValueError):self.load()
    def test_wrong_chain_scope_or_false_independence_rejected(self):
        for field,value in [('genesis','b'*64),('scope','public'),('independent_economic_auditors',True)]:
            original=self.value[field];self.value[field]=value;self.write()
            with self.assertRaises(ValueError):self.load()
            self.value[field]=original
    def test_duplicate_or_unsignable_actor_rejected(self):
        self.value['actors'][1]=self.value['actors'][0];self.write()
        with self.assertRaises(ValueError):self.load()
        self.value['actors'][1]='tpgn'+'B'*30;self.write();self.rpc.owned=False
        with self.assertRaises(ValueError):self.load()

if __name__=='__main__':unittest.main()
