"""CPU computation is durable; scientific publication remains a separate gate."""
from pathlib import Path
import copy,json,sys,tempfile,threading,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
from horizon_cpu_source import CpuOnlyNormalSource,CpuScientificSourcePending
from horizon_cpu_cycle import cycle_request,digest
from pgn_native_contract import fields
from pgn_session import SessionControllerBusy

LO=151208600000000000000
def ticket():
    return dict(schema='PGN_HORIZON_ENGINE_TICKET_V1',ticket_id='a'*64,start=str(LO),end=str(LO+20000),
        target_merit_q16=29*65536,threshold_policy=fields(LO,LO+20000)['threshold_policy'])
def cycle():
    return dict(work=dict(request_id='b'*64,ticket='a'*64),hold=None,stage='running',
        audits=[dict(stage='unavailable'),dict(stage='unavailable')])
def context():return dict(role='search',work_cycle='b'*64)

class Endpoint:
    def __init__(self):self.request=None;self.enabled=False;self.completed=False;self.acks=0;self.lose_ack=False;self.bad_records=False;self.progress=0;self.auto_finish=True
    def configure(self,enabled,*args):self.enabled=enabled;return dict(status=self.status())
    def pause(self):self.enabled=False;return dict(status=self.status())
    def status(self):
        if self.enabled and self.request:
            self.progress=int(self.request['end'])-int(self.request['start']) if self.auto_finish else 100
            self.completed=self.auto_finish
        return dict(request_sha256=digest(self.request)if self.request else None,result_ready=self.completed,
            completed_positions=str(self.progress),compute_started=self.enabled and not self.completed,
            positions_per_second=1234.5,mining_rates=dict(tests_per_second=42.5,candidate_tests='17'))
    def call(self,action,**kw):
        if action=='status':return dict(status=self.status())
        if action=='execute':
            if self.request:raise AssertionError('Unacknowledged result replaced')
            self.request=kw['request'];self.progress=0;self.completed=False;return dict(status=self.status())
        if action=='evidence':
            lo=int(self.request['start'])+1
            return dict(evidence=dict(request=self.request,request_sha256=digest(self.request),complete=self.completed,
                counts100=['1']+['0']*99,best=[]if self.bad_records else[dict(lower_prime=str(lo),upper_prime=str(lo+1000),gap=1000)],candidate_tests='17'))
        if action=='ack_result':
            assert kw['request_sha256']==digest(self.request)
            self.request=None;self.progress=0;self.completed=False;self.acks+=1
            if self.lose_ack:self.lose_ack=False;raise OSError('Lost acknowledgement after native commit')
            return dict(status=self.status())
        raise AssertionError(action)

class CpuSourceTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.events=[];self.observed=[];self.endpoint=Endpoint();self.source=None
    def open(self):
        self.source=CpuOnlyNormalSource(self.root,self.events.append,quantum=10000,observer=self.observed.append)
        self.source.clients['CPU/TEST/0']=self.endpoint
        self.source.options['CPU/TEST/0']=dict(enabled=True,threads=1,affinity=[0])
        self.source.bind_cycle(ticket(),context(),cycle())
        return self.source
    def tearDown(self):
        if self.source:self.source.close()
    def test_complete_records_reprojected_and_no_implicit_publication(self):
        s=self.open();r=s.run_ticket(ticket(),10000)
        self.assertEqual(r['body']['positions'],'20000');self.assertEqual(sum(map(int,r['body']['counts'])),2)
        self.assertEqual(len(r['body']['best']),2);self.assertEqual(self.endpoint.acks,2)
        self.assertFalse(r['network_accepted']);self.assertTrue(r['scientific_finalization_pending'])
        with self.assertRaises(CpuScientificSourcePending):s.finalize_report({},r)
    def test_ack_lost_after_commit_is_not_recomputed_or_double_counted(self):
        s=self.open();self.endpoint.lose_ack=True
        with self.assertRaises(OSError):s.run_ticket(ticket(),10000)
        s.close();self.source=None;s=self.open();r=s.run_ticket(ticket(),10000)
        self.assertEqual(self.endpoint.acks,2);self.assertEqual(sum(map(int,r['body']['counts'])),2)
    def test_pause_cold_preserves_same_owned_quantum(self):
        s=self.open();self.endpoint.auto_finish=False
        def pause(event):self.source.options['CPU/TEST/0']['enabled']=False
        s.observer=pause;r=s.run_ticket(ticket(),10000);self.assertEqual(r['state'],'PAUSED')
        saved=copy.deepcopy(self.endpoint.request);self.assertEqual(self.endpoint.acks,0)
        s.close();self.source=None;self.endpoint.auto_finish=True;s=self.open();r=s.run_ticket(ticket(),10000)
        self.assertEqual(saved['start'],ticket()['start']);self.assertEqual(sum(map(int,r['body']['counts'])),2)
    def test_other_grant_cannot_reuse_saved_frontier(self):
        s=self.open();s.run_ticket(ticket(),10000)
        other=cycle();other['work']['request_id']='c'*64;s.bind_cycle(ticket(),dict(role='search',work_cycle='c'*64),other)
        with self.assertRaisesRegex(ValueError,'another normal-cycle'):s.run_ticket(ticket(),10000)
    def test_missing_records_never_acknowledged(self):
        s=self.open();self.endpoint.bad_records=True
        with self.assertRaisesRegex(ValueError,'complete primary'):s.run_ticket(ticket(),10000)
        self.assertEqual(self.endpoint.acks,0);self.assertIsNotNone(self.endpoint.request)
    def test_search_cannot_bypass_unfinished_audits(self):
        c=cycle();c['audits'][0]=dict(stage='assigned')
        with self.assertRaisesRegex(ValueError,'two normal audit'):cycle_request(ticket(),context(),c)
    def test_second_writer_cannot_take_the_cpu_source(self):
        self.open()
        with self.assertRaises(SessionControllerBusy):CpuOnlyNormalSource(self.root)
    def test_no_gpu_or_remote_configuration_is_required(self):
        s=self.open();self.assertFalse(hasattr(s,'gpu'));self.assertFalse(hasattr(s,'manager'))
        self.assertIsNone(s.finalizer)
    def test_rate_units_are_numeric_and_selected_on_survives_startup(self):
        s=self.open();_,state=s._state(ticket(),s.grant)
        status=self.endpoint.status();s._present(ticket(),state,'CPU/TEST/0',status)
        self.assertTrue(self.events[-1]['resource_enabled']);self.assertEqual(s.current_frame['phase'],'Starting')
        status['compute_started']=True;s._present(ticket(),state,'CPU/TEST/0',status)
        self.assertEqual(s.current_frame['speed_rate'],1234.5);self.assertEqual(s.current_frame['compute_tests_rate'],42.5)
    def test_global_off_does_not_execute_a_quantum(self):
        s=self.open();s.running=threading.Event()
        self.assertEqual(s.run_ticket(ticket(),10000)['state'],'PAUSED');self.assertIsNone(self.endpoint.request)
    def test_ticket_threshold_cannot_be_replaced(self):
        s=self.open();bad=ticket();bad['threshold_policy']['sha256']='0'*64
        with self.assertRaises(ValueError):s.run_ticket(bad,10000)

if __name__=='__main__':unittest.main()
