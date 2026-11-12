from pathlib import Path
import copy,json,sys,tempfile,threading,unittest
from types import SimpleNamespace
from unittest.mock import patch
P=Path(__file__).resolve().parent;Q=P.parent
sys.path[:0]=[str(P),str(Q/'src/engine/runtime_host'),str(Q/'src/protocol')]
from horizon_work_cycle import WorkCycleJournal
from horizon_cycle_retirement import observe_inactive,retain_inactive,reconcile_cpu_capacity

class Rpc:
    def __init__(self,cycle,genesis):
        self.view=dict(schema='PGN_WORK_VIEW_V1',kind='tickets',request_id=cycle['work']['request_id'],reports=[],state=dict(genesis=genesis,bestblockhash='b'*64,height=999,leases=[]));self.tip='b'*64
    def call(self,method,args=None):
        return copy.deepcopy(self.view)if method=='getpgnworkview'else self.tip

class Tests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.root=Path(self.tmp.name)
        snapshot=dict(schema='PGN_WORK_CYCLES_V1',genesis='a'*64,cycles=[])
        for number in range(1,5):
            work=dict(request_id=f'{number:064x}',ticket=f'{number+100:064x}',ticket_number=str(number),start='150000000000000000000',length='200000000000000',target_numerator='1846001',target_denominator='65536')
            native=dict(schema='PGN_NATIVE_ASSIGNED_AUDIT_SERVICE_V1',enforced=True,request_id=work['request_id'],issuance_block='b'*64,issuance_height=1,credits=[])
            from horizon_native_work_duty import tasks
            snapshot['cycles'].append(dict(work=work,stage='running'if number%2 else'prepared',audits=tasks(native,work),native_service=native,hold=''))
        self.genesis=snapshot['genesis'];self.folder=self.root/'work-cycles';self.folder.mkdir();(self.folder/'cycles.json').write_text(json.dumps(snapshot))
        self.j=WorkCycleJournal(self.folder,self.genesis);self.addCleanup(lambda:self.j.close())
        self.cycle=copy.deepcopy(self.j.value['cycles'][0]);self.request=self.cycle['work']['request_id'];self.rpc=Rpc(self.cycle,self.genesis)
        self.source=self.root/'retained-source.json';self.source.write_bytes(b'original exact scientific frontier,never rewritten')
    def proof(self):return observe_inactive(self.rpc,self.cycle,self.genesis)
    def retain(self,proof=None,protected=()):return retain_inactive(self.j,self.request,self.proof()if proof is None else proof,protected_requests=protected,expected_cycle=self.cycle)
    def test_actual_four_cycle_fixture_admits_fifth_without_false_completion(self):
        self.assertEqual(len(self.j.value['cycles']),4);old=self.source.read_bytes()
        self.assertTrue(self.retain());self.assertFalse(self.retain())
        work=dict(self.cycle['work'],genesis=self.genesis,request_id='e'*64,ticket='f'*64,ticket_number='20000')
        self.assertTrue(self.j.prepare(work));self.assertEqual(self.source.read_bytes(),old)
        with self.j.history.database()as db:
            row=db.execute('SELECT receipt FROM inactive_native_cycles WHERE request=?',(self.request,)).fetchone()
        saved=json.loads(row[0]);self.assertEqual(saved['cycle'],self.cycle);self.assertEqual(saved['cycle']['stage'],'running')
    def test_existing_lease_is_never_retired(self):
        self.rpc.view['state']['leases']=[dict(request=self.request,state='assigned')];self.assertIsNone(self.proof())
    def test_report_is_never_retired(self):
        self.rpc.view['reports']=[dict(request=self.request)];self.assertIsNone(self.proof())
    def test_missing_lease_or_report_field_is_not_absence(self):
        for key in ('leases','reports'):
            rpc=Rpc(self.cycle,self.genesis);del (rpc.view['state']if key=='leases'else rpc.view)[key]
            with self.assertRaises(ValueError):observe_inactive(rpc,self.cycle,self.genesis)
    def test_other_request_or_chain_refused(self):
        for obj,key,value in [(self.rpc.view,'request_id','c'*64),(self.rpc.view['state'],'genesis','c'*64)]:
            old=obj[key];obj[key]=value
            with self.assertRaises(ValueError):self.proof()
            obj[key]=old
    def test_moving_core_anchor_returns_no_evidence(self):
        self.rpc.tip='c'*64;self.assertIsNone(self.proof())
    def test_protected_executor_cycle_refused(self):
        with self.assertRaises(RuntimeError):self.retain(protected={self.request})
        self.assertEqual(len(self.j.value['cycles']),4)
    def test_changed_snapshot_refused(self):
        self.j.hold(self.request,'other operation')
        with self.assertRaises(ValueError):self.retain()
    def test_wrong_proof_ticket_refused(self):
        proof=self.proof();proof['previous_ticket']='d'*64
        with self.assertRaises(ValueError):self.retain(proof)
    def test_crash_after_archive_before_hot_write_replays_at_new_anchor(self):
        before=self.j.file.read_bytes()
        with patch('horizon_cycle_retirement.atomic_bytes',side_effect=OSError('power loss')):
            with self.assertRaises(OSError):self.retain()
        self.assertEqual(self.j.file.read_bytes(),before);self.j.close();self.j=WorkCycleJournal(self.folder,self.genesis)
        self.rpc.view['state']['bestblockhash']='c'*64;self.rpc.tip='c'*64
        self.assertTrue(self.retain());self.assertEqual(self.source.read_bytes(),b'original exact scientific frontier,never rewritten')
    def test_mutated_archived_payload_refused(self):
        with patch('horizon_cycle_retirement.atomic_bytes',side_effect=OSError('power loss')):
            with self.assertRaises(OSError):self.retain()
        with self.j.history.database()as db:
            raw=json.loads(db.execute('SELECT receipt FROM inactive_native_cycles').fetchone()[0]);raw['cycle']['work']['ticket_number']='9'
            db.execute('UPDATE inactive_native_cycles SET receipt=?',(json.dumps(raw).encode(),))
        with self.assertRaises(ValueError):self.retain()
    def flow(self,*,cpu=True):
        return SimpleNamespace(cycles=self.j,gpu=SimpleNamespace(condition=threading.Condition(),jobs={}),worker=SimpleNamespace(cpu_only=cpu,rpc=self.rpc,network_lock=threading.RLock(),args=SimpleNamespace(genesis=self.genesis)))
    def incoming(self):return dict(self.cycle['work'],genesis=self.genesis,request_id='e'*64,ticket='f'*64,ticket_number='20000')
    def test_preparation_integration_frees_exactly_one_slot(self):
        flow=self.flow();self.assertTrue(reconcile_cpu_capacity(flow,self.incoming()));self.assertEqual(len(self.j.value['cycles']),3)
        self.j.prepare(self.incoming());self.assertEqual(len(self.j.value['cycles']),4)
    def test_gpu_flow_does_not_reconcile_cpu_history(self):
        before=self.j.file.read_bytes();self.assertFalse(reconcile_cpu_capacity(self.flow(cpu=False),self.incoming()));self.assertEqual(before,self.j.file.read_bytes())
    def test_scheduler_claim_during_rpc_keeps_cycle(self):
        flow=self.flow();rpc=self.rpc.call
        def changed(method,args=None):
            reply=rpc(method,args)
            if method=='getbestblockhash':flow.gpu.jobs['owned']=SimpleNamespace(context=dict(work_cycle=self.request))
            return reply
        self.rpc.call=changed
        # Other fixtures cannot obtain evidence from this exact-request fake.
        with self.assertRaises(ValueError):reconcile_cpu_capacity(flow,self.incoming())
        self.assertEqual(self.j.snapshot(self.request),self.cycle)
    def test_reconciliation_not_attempted_when_capacity_exists(self):
        self.retain();self.rpc.call=lambda *a:(_ for _ in ()).throw(AssertionError('unnecessary RPC'))
        self.assertFalse(reconcile_cpu_capacity(self.flow(),self.incoming()))
    def test_completed_cache_entry_uses_existing_eviction_without_rpc(self):
        self.j.advance(self.request,'completed');self.rpc.call=lambda *a:(_ for _ in ()).throw(AssertionError('unnecessary RPC'))
        self.assertFalse(reconcile_cpu_capacity(self.flow(),self.incoming()));self.assertTrue(self.j.prepare(self.incoming()))

if __name__=='__main__':unittest.main(verbosity=2)
