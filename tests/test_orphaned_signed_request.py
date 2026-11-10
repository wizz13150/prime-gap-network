from pathlib import Path
from types import SimpleNamespace
import json,sys,tempfile,threading,unittest

sys.path[:0]=[str(Path(__file__).resolve().parents[1]/'src/engine/runtime_host'),str(Path(__file__).resolve().parents[1]/'src/protocol')]
from horizon_ticket_request import retire_orphaned_signed_request


class RetiredSignedRequestTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)
        self.request=dict(schema='PGN_TICKET_REQUEST_RPC_V2',genesis='1'*64,request_id='2'*64,
                          anchor='3'*64,anchor_height=10,envelope='retain exact signed envelope')
        self.saved=dict(stage='REQUEST_SIGNED',sequence=7,request=self.request)
        self.journal=self.root/'controller.json';self.journal.write_text(json.dumps(self.saved))
        self.confirmations=-1;self.leases=[];self.reports=[];self.tip='4'*64;self.moving=False;self.calls=[]
        self.worker=SimpleNamespace(root=self.root,args=SimpleNamespace(genesis='1'*64),
             state_lock=threading.RLock(),rpc=SimpleNamespace(call=self.call))

    def call(self,name,args=None):
        self.calls.append(name)
        if name=='getblockheader':return dict(hash='3'*64,height=10,confirmations=self.confirmations)
        if name=='getpgnworkview':return dict(schema='PGN_WORK_VIEW_V1',kind='tickets',request_id='2'*64,
            state=dict(genesis='1'*64,bestblockhash=self.tip,height=40,leases=self.leases),reports=self.reports)
        if name=='getblockhash':return '5'*64
        if name=='getbestblockhash':return '6'*64 if self.moving else self.tip
        raise AssertionError('Unexpected signing/publication: '+name)

    def test_proven_orphan_retains_exact_signed_request_before_releasing_retry(self):
        self.assertTrue(retire_orphaned_signed_request(self.worker,self.journal,self.saved))
        state=json.loads(self.journal.read_bytes());self.assertEqual(state['stage'],'REQUEST_ORPHANED')
        retained=json.loads((self.root/state['retained_request']).read_bytes())
        self.assertEqual(retained['request'],self.request)
        self.assertEqual(state['sequence'],7)
        self.assertNotIn('signmessage',self.calls);self.assertNotIn('submitblock',self.calls)

    def test_canonical_present_or_moving_request_is_never_retired(self):
        for attr,value in [('confirmations',4),('leases',[{}]),('reports',[{}]),('moving',True)]:
            with self.subTest(attr=attr):
                previous=getattr(self,attr);setattr(self,attr,value)
                self.assertFalse(retire_orphaned_signed_request(self.worker,self.journal,self.saved))
                self.assertEqual(json.loads(self.journal.read_bytes()),self.saved)
                self.assertFalse((self.root/'retired-ticket-requests').exists());setattr(self,attr,previous)

    def test_transport_failure_does_not_mean_absence(self):
        def fail(*_):raise TimeoutError('temporary read failure')
        self.worker.rpc.call=fail
        with self.assertRaises(TimeoutError):retire_orphaned_signed_request(self.worker,self.journal,self.saved)
        self.assertEqual(json.loads(self.journal.read_bytes()),self.saved)

    def test_crash_after_receipt_before_controller_write_is_idempotent(self):
        self.assertTrue(retire_orphaned_signed_request(self.worker,self.journal,self.saved))
        self.journal.write_text(json.dumps(self.saved))
        self.assertTrue(retire_orphaned_signed_request(self.worker,self.journal,self.saved))
        self.assertEqual(len(list((self.root/'retired-ticket-requests').glob('*.json'))),1)


if __name__=='__main__':unittest.main()
