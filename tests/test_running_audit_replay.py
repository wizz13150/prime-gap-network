"""Started/completed-cycle recovery: actual client/journal, controlled chain and zero GPU."""
from pathlib import Path
from types import SimpleNamespace
import copy,json,sys,threading,unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tests'),str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
import test_audit_cycle_reassignment as fixture
from horizon_work_cycle import WorkCycleJournal
from horizon_network_audits import NetworkAudits,save
from horizon_coordination import native_header_hash
from report_commitment import audit_commitment

class RunningReplayTests(unittest.TestCase):
    def setUp(self):
        self.f=fixture.ReassignmentTests();self.f.setUp();self.addCleanup(self.f.doCleanups)
        self.second_old=fixture.intent(4);self.second_new=fixture.intent(3)
        self.second_canonical=True;self.second_stage='assigned';self.second_publications=0
        self.f.flow.gpu.condition=threading.Condition()
        original=self.f.rpc.call
        def rpc(method,args=None,**kwargs):
            args=args or []
            if method=='getblockheader' and args[0]==self.second_old['hash']:
                return dict(hash=args[0],height=8,confirmations=2 if self.second_canonical else -1)
            if method=='getblockheader' and args[0]==self.second_new['hash']:
                return dict(hash=args[0],height=11,confirmations=1)
            if method=='getblockhash' and args[0]==8:return self.second_old['hash'] if self.second_canonical else '8'*64
            if method=='getblockhash' and args[0]==11:return self.second_new['hash']
            if method=='getpgnauditstate' and args[2]=='4'*64:
                return [dict(request_id='4'*64,jobs=[dict(slot=1,attempt=0,state=self.second_stage,commitment=self.answer2['commitment'])])]
            return original(method,args,**kwargs)
        self.f.rpc.call=rpc
        self.f.cycles.published(self.f.work,1,'4'*64,1,0,self.second_old['hash'])
        self.f.cycles.advance(self.f.work,'running')
        self.entry=dict(work=dict(request_id=self.f.work),report_root='7'*64)
    def complete(self):self.f.cycles.advance(self.f.work,'completed')
    def held(self):return self.f.cycles.snapshot(self.f.work)['hold']==WorkCycleJournal.reorg_hold
    def second_lane(self):
        f=self.f;root=f.root/'second-lane';root.mkdir()
        def ledger(envelopes,*,on_intent=None):
            self.second_publications+=1
            if on_intent:on_intent(self.second_new)
            self.second_stage='committed';f.rpc.tip=self.second_new['hash']
            return dict(block_hash=self.second_new['hash'])
        w=SimpleNamespace(**vars(f.flow.worker));w.root=root;w.ledger=ledger
        audits=NetworkAudits(w,on_assigned=lambda a:f.flow.bind_audit_cycle(1,a),
            on_committed=lambda a,b:f.flow.audit_committed(1,a,b),before_commit=lambda a:f.flow.before_audit_commit(1,a),
            on_reassigned=lambda a,b:f.flow.reopen_audit_cycle(1,a,b),reassignment_needed=lambda a:f.flow.audit_reassignment_needed(1,a))
        f.flow.audit_lanes.append(audits)
        self.answer2=audit_commitment('5'*64,0,list(range(5)),['2']*5,'7'*64)
        value=dict(schema='PGN_HORIZON_ASSIGNED_AUDIT_V1',request_id='4'*64,ticket='5'*64,report_root='6'*64,slot=1,attempt=0,
            ranges=f.ranges,counts=[2]*5,target_merit_q16=32*65536,answer=self.answer2,publication_intent=self.second_old,work_cycle=f.work)
        name='4'*64+'-1-0';save(audits.folder/'current.json',value);save(audits.folder/(name+'.json'),value);audits.answers.register([name])
        return audits,value
    def test_completed_cycle_replays_without_reopening_its_search(self):
        self.complete();self.assertTrue(self.f.flow.report_ready(self.entry))
        self.assertEqual(self.f.audits.process_one(),'committed')
        self.assertEqual(self.f.cycles.snapshot(self.f.work)['stage'],'completed')
        self.assertFalse(self.held());self.assertTrue(self.f.flow.report_ready(self.entry))
        self.assertEqual((self.f.compute,self.f.publications),(0,1))
    def test_both_orphans_keep_report_until_second_exact_replay(self):
        self.complete();self.second_canonical=False
        self.assertEqual(self.f.audits.process_one(),'committed')
        self.assertTrue(self.held());self.assertFalse(self.f.flow.report_ready(self.entry))
        audits,value=self.second_lane();self.assertEqual(audits.process_one(),'committed')
        self.assertFalse(self.held());self.assertTrue(self.f.flow.report_ready(self.entry))
        self.assertEqual((self.f.compute,self.f.publications,self.second_publications),(0,1,1))
        result=json.loads((audits.folder/('4'*64+'-1-0.json')).read_bytes())
        self.assertEqual(result['answer'],self.answer2);self.assertEqual(result['counts'],[2]*5)
    def test_second_orphan_waits_for_canonical_first_service(self):
        self.complete();self.second_canonical=False
        audits,_=self.second_lane()
        timer=threading.Timer(.15,self.f.flow.worker.quitting.set);timer.start()
        try:self.assertEqual(audits.process_one(),'paused')
        finally:timer.cancel();timer.join()
        self.assertEqual(self.second_publications,0);self.assertEqual(self.f.compute,0)
        self.assertTrue(self.held());self.assertFalse(self.f.flow.report_ready(self.entry))
    def test_cold_restart_after_first_replay_preserves_second_obligation(self):
        self.complete();self.second_canonical=False;self.f.audits.process_one()
        self.f.cycles.close();self.f.cycles=WorkCycleJournal(self.f.root/'cycles','9'*64);self.f.flow.cycles=self.f.cycles
        self.assertTrue(self.held());self.assertFalse(self.f.flow.report_ready(self.entry))
        audits,_=self.second_lane();audits.process_one()
        self.assertFalse(self.held());self.assertTrue(self.f.flow.report_ready(self.entry));self.assertEqual(self.f.compute,0)
        raw=json.loads((self.f.root/'cycles/cycles.json').read_bytes())
        self.assertEqual(raw['schema'],'PGN_WORK_CYCLES_V1')
        self.assertEqual(set(raw['cycles'][0]),{'work','stage','audits','hold'})
    def test_transport_failure_after_local_replacement_is_idempotent(self):
        self.complete();original=self.f.flow.refresh_cycle_confirmations
        self.f.flow.refresh_cycle_confirmations=lambda request:(_ for _ in ()).throw(OSError('Controlled post-publication read failure'))
        with self.assertRaisesRegex(OSError,'post-publication'):self.f.audits.process_one()
        self.assertEqual(self.f.cycles.snapshot(self.f.work)['audits'][0]['block'],self.f.new['hash'])
        self.assertTrue(self.held());self.assertTrue(self.f.current.is_file())
        self.f.flow.refresh_cycle_confirmations=original
        self.assertEqual(self.f.audits.process_one(),'none')
        self.assertFalse(self.held());self.assertFalse(self.f.current.exists());self.assertEqual((self.f.compute,self.f.publications),(0,1))
    def test_changed_sibling_reference_invalidates_hold_release_snapshot(self):
        self.complete();self.f.cycles.hold(self.f.work,WorkCycleJournal.reorg_hold);self.f.rpc.canonical=True
        prior=self.f.rpc.call;tip_reads=0
        def mutate(method,args=None,**kwargs):
            nonlocal tip_reads
            value=prior(method,args,**kwargs)
            if method=='getbestblockhash':
                tip_reads+=1
                if tip_reads==2:self.f.cycles.replace_audit_confirmation(self.f.work,1,'4'*64,1,0,self.second_old['hash'],self.second_new['hash'])
            return value
        self.f.rpc.call=mutate
        self.assertFalse(self.f.flow.refresh_cycle_confirmations(self.f.work));self.assertTrue(self.held())
        self.f.rpc.call=prior
        self.assertTrue(self.f.flow.refresh_cycle_confirmations(self.f.work));self.assertFalse(self.held())
    def test_moving_tip_never_releases_report(self):
        self.complete();self.f.cycles.hold(self.f.work,WorkCycleJournal.reorg_hold);self.f.rpc.canonical=True;self.f.rpc.moving=True
        self.assertFalse(self.f.flow.refresh_cycle_confirmations(self.f.work));self.assertTrue(self.held())
        self.assertFalse(self.f.flow.report_ready(self.entry))
    def test_unrelated_hold_does_not_trigger_confirmation_reads(self):
        self.complete();self.f.cycles.hold(self.f.work,'Wallet operation needs review')
        before=self.f.rpc.header_reads
        self.assertFalse(self.f.flow.refresh_cycle_confirmations(self.f.work))
        self.assertEqual(self.f.rpc.header_reads,before);self.assertFalse(self.f.flow.report_ready(self.entry))
    def test_reconsidered_original_confirmations_release_without_new_publication(self):
        self.complete();self.f.cycles.hold(self.f.work,WorkCycleJournal.reorg_hold);self.f.rpc.canonical=True
        self.assertTrue(self.f.flow.refresh_cycle_confirmations(self.f.work))
        self.assertTrue(self.f.flow.report_ready(self.entry));self.assertEqual((self.f.compute,self.f.publications),(0,0))
    def test_repeated_confirmed_callback_after_release_is_noop(self):
        self.complete();self.f.audits.process_one()
        saved=json.loads(self.f.sealed.read_bytes());before=self.f.cycles.snapshot(self.f.work)
        self.f.flow.audit_committed(0,saved,self.f.new['hash'])
        self.assertEqual(self.f.cycles.snapshot(self.f.work),before);self.assertFalse(self.held())
    def test_wrong_attempt_cannot_use_idempotent_confirmation_path(self):
        self.complete();self.f.audits.process_one()
        saved=json.loads(self.f.sealed.read_bytes());saved['attempt']=1;before=self.f.cycles.snapshot(self.f.work)
        with self.assertRaisesRegex(ValueError,'another retained attempt'):self.f.flow.audit_committed(0,saved,self.f.new['hash'])
        self.assertEqual(self.f.cycles.snapshot(self.f.work),before)
    def test_report_publisher_retains_held_entry_without_submitting_it(self):
        self.complete();self.f.cycles.hold(self.f.work,WorkCycleJournal.reorg_hold)
        submitted=[];ack=[];flow=self.f.flow
        flow.outbox=SimpleNamespace(first=lambda:copy.deepcopy(self.entry),acknowledge=lambda *args:ack.append(args))
        flow.changed=threading.Event();flow.worker.audits=SimpleNamespace();flow.worker.submit=lambda *args,**kw:submitted.append(kw)
        class Poller:
            def __init__(self,*args):pass
            def due(self):return False
        timer=threading.Timer(.2,flow.worker.quitting.set);timer.start()
        try:
            with patch('horizon_report_poll.ReportPoller',Poller):flow.publish_reports()
        finally:timer.cancel();timer.join()
        self.assertEqual(submitted,[]);self.assertEqual(ack,[]);self.assertTrue(self.held())
    def test_running_report_cannot_publish_before_completed_transition(self):
        self.assertFalse(self.f.flow.report_ready(self.entry));self.complete();self.assertTrue(self.f.flow.report_ready(self.entry))
    def test_report_waiting_for_network_lane_rechecks_new_hold(self):
        self.complete();flow=self.f.flow;waiting=threading.Event();submitted=[]
        def first():return copy.deepcopy(self.entry)
        flow.outbox=SimpleNamespace(first=first,acknowledge=lambda *args:None)
        flow.changed=threading.Event();flow.worker.audits=SimpleNamespace();flow.worker.submit=lambda *args,**kw:submitted.append(kw)
        class Poller:
            def __init__(self,*args):pass
            def due(self):return False
        class Lane:
            def __init__(self):self.lock=threading.RLock()
            def __enter__(self):waiting.set();self.lock.acquire();return self
            def __exit__(self,*args):self.lock.release()
        lane=Lane();flow.worker.network_lock=lane
        # Skip the unrelated periodic read; synchronize the exact contention
        # boundary between selecting a report and acquiring the network lane.
        with patch.object(flow.cycles,'reorg_held',return_value=[]),patch('horizon_report_poll.ReportPoller',Poller):
            lane.lock.acquire()
            thread=threading.Thread(target=flow.publish_reports);thread.start()
            try:
                self.assertTrue(waiting.wait(1))
                flow.cycles.hold(self.f.work,WorkCycleJournal.reorg_hold)
            finally:lane.lock.release()
            flow.worker.quitting.set();flow.changed.set();thread.join(2)
        self.assertFalse(thread.is_alive());self.assertEqual(submitted,[]);self.assertTrue(self.held())

if __name__=='__main__':unittest.main()
