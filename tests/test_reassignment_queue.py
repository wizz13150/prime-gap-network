from copy import deepcopy
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
import hashlib,json,sys,tempfile,threading,unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
from horizon_report_outbox import ReportOutbox
from horizon_work_cycle import WorkCycleJournal
from horizon_reassignment_queue import ReassignmentQueue
from horizon_ticket_prefetch import TicketPrefetch
from horizon_mining_flow import MiningFlow
from report_commitment import canonical_bytes


class ReassignmentQueueTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        self.old=json.loads((ROOT/'tests/fixtures/network/issued-work.json').read_bytes())
        self.old.update(parent='b'*64,ticket_number='4',miner_script='0014'+'a'*40,reward_script='0014'+'c'*40)
        self.service=dict(schema='PGN_NATIVE_ASSIGNED_AUDIT_SERVICE_V1',enforced=True,
            request_id=self.old['request_id'],issuance_block='e'*64,issuance_height=2,credits=[])
        self.fresh=dict(self.old,ticket='6'*64,start=str(int(self.old['start'])+int(self.old['length'])),
            parent='a'*64,height=11,ticket_number='5',schema='PGN_NATIVE_SCIENTIFIC_WORK1',accepted=False,
            native_audit_service=dict(self.service,view_parent='a'*64))
        result=json.loads((ROOT/'tests/fixtures/network/computed-report.json').read_bytes())
        self.out=ReportOutbox(self.root)
        (self.root/'tickets'/self.old['ticket']).mkdir(parents=True)
        self.entry=self.out.put(self.old,result)
        self.report=self.root/'tickets'/self.old['ticket']/'network-report.json'
        self.report_bytes=self.report.read_bytes()
        self.cycles=WorkCycleJournal(self.root/'work-cycles',self.old['genesis']);self.addCleanup(lambda:self.cycles.close())
        self.cycles.prepare(self.old,native_service=self.service)
        self.cycles.advance(self.old['request_id'],'running');self.cycles.advance(self.old['request_id'],'completed')
        self.oldcycle=self.cycles.snapshot(self.old['request_id'])
        self.calls=[];self.old_confirmations=-1
        self.worker=SimpleNamespace(root=self.root,args=SimpleNamespace(genesis=self.old['genesis']),
                                    rpc=SimpleNamespace(call=self.call))
        self.flow=SimpleNamespace(worker=self.worker,outbox=self.out,cycles=self.cycles)
        self.queue=ReassignmentQueue(self.flow)

    def call(self,name,args=None):
        self.calls.append((name,deepcopy(args)))
        if name=='getpgnscientificwork':return deepcopy(self.fresh)
        if name=='getbestblockhash':return 'a'*64
        if name=='getblockhash':return {2:'e'*64,10:'a'*64}.get(args[0],'c'*64)
        if name=='getblockheader':
            return dict(hash=args[0],height=2 if args[0]=='e'*64 else 8,
                        confirmations=9 if args[0]=='e'*64 else self.old_confirmations)
        if name=='getpgnworkview':
            return dict(schema='PGN_WORK_VIEW_V1',kind='tickets',request_id=self.old['request_id'],reports=[],
                state=dict(genesis=self.old['genesis'],bestblockhash='a'*64,height=10,
                    leases=[dict(request=self.old['request_id'],state='assigned',issued=3,deadline=1000,
                        miner_script=self.old['miner_script'],reward_script=self.old['reward_script'],
                        start=self.fresh['start'],end=str(int(self.fresh['start'])+int(self.fresh['length'])))]))
        raise AssertionError('Unexpected RPC or mutation: '+name)

    def assert_retained(self):
        self.assertEqual(self.report.read_bytes(),self.report_bytes)
        self.assertEqual(self.cycles.history.retired_assignment(self.old['request_id'],self.old['ticket']),self.oldcycle)
        self.assertFalse(any(name in ('submitblock','signmessage')for name,_ in self.calls))

    def test_same_request_new_ticket_keeps_old_report_and_confirmed_service(self):
        self.assertTrue(self.queue.stage(self.entry))
        self.assertEqual(self.out._read(),[])
        self.assert_retained()
        cycle=self.cycles.snapshot(self.old['request_id'])
        self.assertEqual(cycle['stage'],'prepared');self.assertEqual(cycle['work']['ticket'],self.fresh['ticket'])
        self.assertEqual(cycle['native_service'],self.service)
        self.assertEqual(self.queue.next_work(),self.fresh)
        self.queue.adopted(self.fresh);self.assertIsNone(self.queue.next_work())

    def test_interrupted_cycle_replacement_rolls_forward_without_duplicating_work(self):
        original=self.cycles.replace_completed_assignment
        def interrupted(*args):original(*args);raise RuntimeError('Injected interruption after hot-cycle write')
        with patch.object(self.cycles,'replace_completed_assignment',side_effect=interrupted):
            with self.assertRaisesRegex(RuntimeError,'Injected'):self.queue.stage(self.entry)
        self.assertEqual(self.out.first(),self.entry)
        self.assertEqual(self.queue.next_work(),self.fresh)
        self.assertEqual(self.queue.next_work(),self.fresh)
        self.assertEqual(self.out._read(),[]);self.assert_retained()

    def test_interruption_after_retirement_before_hot_write_survives_restart(self):
        import horizon_work_cycle as module
        real=module.atomic_bytes
        def fail(path,data):
            if Path(path)==self.cycles.file:raise OSError('Injected failed hot write')
            return real(path,data)
        with patch.object(module,'atomic_bytes',side_effect=fail):
            with self.assertRaisesRegex(OSError,'Injected'):self.queue.stage(self.entry)
        self.assertEqual(self.out.first(),self.entry)
        self.cycles.close();self.cycles=WorkCycleJournal(self.root/'work-cycles',self.old['genesis'])
        self.flow.cycles=self.cycles
        self.assertEqual(self.queue.next_work(),self.fresh);self.assert_retained()

    def test_canonical_assignment_cannot_free_a_report_slot(self):
        self.old_confirmations=10
        with self.assertRaisesRegex(ValueError,'Canonical original'):self.queue.stage(self.entry)
        self.assertEqual(self.out.first(),self.entry);self.assertFalse(self.queue.index.exists())

    def test_forked_native_view_cannot_retire_completed_report(self):
        original=self.worker.rpc.call
        def forked(name,args=None):
            if name=='getblockhash'and args==[10]:return 'c'*64
            return original(name,args)
        self.worker.rpc.call=forked
        with self.assertRaisesRegex(RuntimeError,'changed branches'):self.queue.stage(self.entry)
        self.assertEqual(self.out.first(),self.entry)
        self.assertEqual(self.cycles.snapshot(self.old['request_id']),self.oldcycle)
        self.assertEqual(self.report.read_bytes(),self.report_bytes)
        self.assertFalse(self.queue.index.exists())
        self.assertFalse(any(name in ('submitblock','signmessage')for name,_ in self.calls))

    def test_retained_report_tampering_does_not_retire_a_cycle(self):
        self.report.write_bytes(self.report_bytes.replace(b'"ticket":',b'"bad_ticket":',1))
        with self.assertRaises((ValueError,KeyError)):self.queue.stage(self.entry)
        self.assertEqual(self.out.first(),self.entry)
        self.assertEqual(self.cycles.snapshot(self.old['request_id']),self.oldcycle)

    def test_recovered_prefetch_does_not_overwrite_an_unpublished_signed_request(self):
        self.queue.stage(self.entry)
        folder=self.root/'prepared-ticket';folder.mkdir()
        journal=folder/'controller.json';original=b'{"stage":"REQUEST_SIGNED","request":{"nonce":"preserve"}}\n';journal.write_bytes(original)
        prefetch=TicketPrefetch(SimpleNamespace(root=folder),threading.Event(),threading.Event(),threading.RLock(),
            recovery=self.queue.next_work,on_recovered=self.queue.adopted)
        prefetch.ready=self.queue.next_work();prefetch.ready_recovered=True
        adopted=[];self.assertEqual(prefetch.take(adopted.append,0),self.fresh)
        self.assertEqual(adopted,[self.fresh]);self.assertEqual(journal.read_bytes(),original)
        self.assertIsNone(self.queue.next_work());self.assert_retained()

    def test_restart_recovers_old_completed_assignment_before_starting_threads(self):
        flow=object.__new__(MiningFlow)
        flow.worker=self.worker;flow.cycles=self.cycles;flow.outbox=self.out
        flow.reassignments=ReassignmentQueue(flow)
        def changed(**_):raise ValueError('Saved ticket no longer matches its live computation lease')
        self.worker.assign=changed
        (self.root/'controller.json').write_bytes(canonical_bytes(dict(stage='ASSIGNED',work=self.old)))
        work,recovered=flow.startup_work()
        self.assertTrue(recovered);self.assertEqual(work,self.fresh)
        self.assertEqual(self.out._read(),[]);self.assert_retained()

    def test_actual_report_publisher_routes_ticket_mismatch_to_retained_handoff(self):
        from horizon_network_worker import validate_report_proposal
        flow=object.__new__(MiningFlow)
        flow.worker=self.worker;flow.cycles=self.cycles;flow.outbox=self.out
        flow.reassignments=ReassignmentQueue(flow)
        flow.recovered_report=None;flow.finalizer=None;flow.changed=threading.Event();flow.audit_lanes=[]
        flow.report_poller=SimpleNamespace(due=lambda:False)
        flow.report_ready=lambda entry:True
        self.worker.network_lock=threading.RLock();self.worker.quitting=threading.Event()
        def submit(_,work):
            validate_report_proposal(dict(accepted=False,genesis=work['genesis'],request_id=work['request_id'],
                ticket=self.fresh['ticket'],report_root=self.entry['report_root']),work,self.entry['report_root'])
        self.worker.submit=submit
        stage=flow.reassignments.stage
        def done(entry):
            result=stage(entry);self.worker.quitting.set();return result
        flow.reassignments.stage=done
        thread=threading.Thread(target=flow.publish_reports);thread.start();thread.join(4)
        if thread.is_alive():self.worker.quitting.set();thread.join(2);self.fail('Publisher did not finish retained handoff')
        self.assertEqual(flow.reassignments.next_work(),self.fresh)
        self.assertEqual(self.out._read(),[]);self.assert_retained()

    def partial_flow(self, stage='running', *, with_cycle=True):
        self.cycles.close()
        self.partial_serial=getattr(self,'partial_serial',0)+1
        self.cycles=WorkCycleJournal(self.root/('partial-cycles-'+str(self.partial_serial)),self.old['genesis'])
        if with_cycle:
            self.cycles.prepare(self.old,native_service=self.service)
            if stage=='running':self.cycles.advance(self.old['request_id'],'running')
        self.oldcycle=self.cycles.snapshot(self.old['request_id']) if with_cycle else None
        self.out._save([])
        session=self.report.parent/'session.json';session.write_bytes(b'{"state":"RUNNING","positions":"123456"}\n')
        self.session_bytes=session.read_bytes()
        flow=object.__new__(MiningFlow)
        flow.worker=self.worker;flow.cycles=self.cycles;flow.outbox=self.out
        flow.gpu=SimpleNamespace(active=None,jobs={},condition=threading.Condition())
        flow.audit_threads=[threading.Thread()]
        for name in ('report_thread','owner_report_thread','pressure_thread'):setattr(flow,name,threading.Thread())
        flow.reassignments=ReassignmentQueue(flow)
        return flow

    def test_restart_retires_partial_cycle_without_completing_or_retagging_results(self):
        flow=self.partial_flow()
        def changed(**_):raise ValueError('Saved ticket no longer matches its live computation lease')
        self.worker.assign=changed
        (self.root/'controller.json').write_bytes(canonical_bytes(dict(stage='ASSIGNED',work=self.old)))
        work,recovered=flow.startup_work()
        self.assertTrue(recovered);self.assertEqual(work,self.fresh)
        self.assert_retained()
        self.assertEqual(self.cycles.history.retired_assignment(self.old['request_id'],self.old['ticket'])['stage'],'running')
        self.assertEqual((self.report.parent/'session.json').read_bytes(),self.session_bytes)
        self.assertEqual(self.cycles.snapshot(self.old['request_id'])['stage'],'prepared')

    def test_startup_recovers_prepared_assignment_and_missing_cycle(self):
        for with_cycle in (True,False):
            with self.subTest(with_cycle=with_cycle):
                # Each case uses its own journal and immutable receipt folder.
                flow=self.partial_flow('prepared',with_cycle=with_cycle)
                flow.reassignments.folder=self.root/('restart-'+str(with_cycle))
                flow.reassignments.index=flow.reassignments.folder/'ready.json'
                self.assertTrue(flow.reassignments.stage_inactive(self.old))
                self.assertEqual(flow.reassignments.next_work(),self.fresh)
                flow.reassignments.adopted(self.fresh)
                self.assertEqual(self.report.read_bytes(),self.report_bytes)

    def test_partial_recovery_cannot_retire_a_queued_or_running_executor(self):
        flow=self.partial_flow();self.calls.clear()
        for active,jobs in ((object(),{}),(None,{'old':object()})):
            flow.gpu.active=active;flow.gpu.jobs=jobs
            with self.assertRaisesRegex(RuntimeError,'unstarted execution'):flow.reassignments.stage_inactive(self.old)
        self.assertEqual(self.calls,[])
        self.assertEqual(self.cycles.snapshot(self.old['request_id']),self.oldcycle)

    def test_partial_retirement_replays_after_interrupted_hot_journal_write(self):
        import horizon_work_cycle as module
        flow=self.partial_flow();real=module.atomic_bytes
        def fail(path,data):
            if Path(path)==self.cycles.file:raise OSError('Injected partial hot write')
            return real(path,data)
        with patch.object(module,'atomic_bytes',side_effect=fail):
            with self.assertRaisesRegex(OSError,'Injected'):flow.reassignments.stage_inactive(self.old)
        folder=self.cycles.file.parent
        self.cycles.close();self.cycles=WorkCycleJournal(folder,self.old['genesis']);flow.cycles=self.cycles
        self.assertEqual(flow.reassignments.next_work(),self.fresh);self.assert_retained()
        self.assertEqual((self.report.parent/'session.json').read_bytes(),self.session_bytes)

    def test_partial_session_change_after_recovery_intent_does_not_get_ignored(self):
        flow=self.partial_flow()
        with patch.object(flow.reassignments,'finish',side_effect=OSError('Injected before retirement')):
            with self.assertRaisesRegex(OSError,'Injected'):flow.reassignments.stage_inactive(self.old)
        (self.report.parent/'session.json').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError,'partial session changed'):flow.reassignments.next_work()
        self.assertEqual(self.cycles.snapshot(self.old['request_id']),self.oldcycle)

    def test_second_reorganization_before_adoption_keeps_both_retired_generations(self):
        self.assertTrue(self.queue.stage(self.entry))
        first=deepcopy(self.fresh);firstcycle=self.cycles.snapshot(first['request_id'])
        self.flow.gpu=SimpleNamespace(jobs={},condition=threading.Condition())
        self.fresh.update(ticket='9'*64,parent='d'*64,start=str(int(first['start'])+int(first['length'])),ticket_number='6')
        self.fresh['native_audit_service']['view_parent']='d'*64
        original=self.worker.rpc.call
        def current(name,args=None):
            if name=='getbestblockhash':return 'd'*64
            if name=='getblockhash'and args==[10]:return 'd'*64
            result=original(name,args)
            if name=='getpgnworkview':result['state']['bestblockhash']='d'*64
            return result
        self.worker.rpc.call=current
        self.assertEqual(self.queue.next_work(),self.fresh)
        self.assertEqual(self.cycles.history.retired_assignment(first['request_id'],first['ticket']),firstcycle)
        self.assert_retained()
        self.assertEqual(len(self.queue.read()),1)
        self.queue.adopted(self.fresh);self.assertIsNone(self.queue.next_work())

    def test_second_reorganization_cannot_replace_an_already_queued_range(self):
        self.assertTrue(self.queue.stage(self.entry))
        self.flow.gpu=SimpleNamespace(jobs={self.fresh['ticket']:object()},condition=threading.Condition())
        with self.assertRaisesRegex(RuntimeError,'already owned'):self.queue.stage_unstarted(self.fresh)

    def prepared_allocator(self, flow):
        folder=self.root/'prepared-ticket';folder.mkdir()
        saved=dict(stage='ASSIGNED',sequence=27,work=self.old)
        original=(json.dumps(saved,indent=2)+'\n').encode()
        (folder/'controller.json').write_bytes(original)
        def changed():raise ValueError('Saved ticket no longer matches its live computation lease')
        flow.allocator=SimpleNamespace(root=folder,assign=changed)
        return saved,original

    def test_actual_prefetch_recovers_orphaned_assigned_journal_while_execution_loop_lives(self):
        flow=self.partial_flow('prepared',with_cycle=False)
        saved,original=self.prepared_allocator(flow)
        enabled=threading.Event();enabled.set();stopping=threading.Event()
        prefetch=TicketPrefetch(flow.allocator,enabled,stopping,threading.RLock(),
            recovery=flow.reassignments.next_work,on_recovered=flow.reassignments.adopted,
            recover_allocator=flow.recover_prepared_assignment)
        prefetch.start()
        try:
            with prefetch.condition:
                self.assertTrue(prefetch.condition.wait_for(lambda:prefetch.ready is not None or prefetch.error is not None,4), 'No prefetch result')
            self.assertIsNone(prefetch.error);self.assertEqual(prefetch.ready,self.fresh)
            marker=json.loads((flow.allocator.root/'controller.json').read_bytes())
            self.assertEqual(marker['stage'],'ALLOCATION_REASSIGNED')
            record=flow.reassignments.record(flow.reassignments.read()[0])
            self.assertEqual(record['allocator_journal']['raw_utf8'].encode(),original)
            # A later unrelated signature must survive recovered-work adoption.
            newer=b'{"stage":"REQUEST_SIGNED","sequence":28,"request":{"nonce":"new"}}\n'
            (flow.allocator.root/'controller.json').write_bytes(newer)
            adopted=[];self.assertEqual(prefetch.take(adopted.append,0),self.fresh)
            self.assertEqual(adopted,[self.fresh]);self.assertEqual((flow.allocator.root/'controller.json').read_bytes(),newer)
            self.assertEqual(self.report.read_bytes(),self.report_bytes)
        finally:
            stopping.set();self.assertTrue(prefetch.close(3))

    def test_prepared_recovery_replays_after_cycle_write_before_allocator_retirement(self):
        flow=self.partial_flow('prepared',with_cycle=False)
        _,original=self.prepared_allocator(flow)
        with patch.object(flow.reassignments,'retire_allocator',side_effect=OSError('Injected before prepared retirement')):
            with self.assertRaisesRegex(OSError,'Injected'):flow.recover_prepared_assignment(ValueError('Saved ticket no longer matches its live computation lease'))
        self.assertEqual((flow.allocator.root/'controller.json').read_bytes(),original)
        self.assertEqual(flow.reassignments.next_work(),self.fresh)
        self.assertEqual(json.loads((flow.allocator.root/'controller.json').read_bytes())['stage'],'ALLOCATION_REASSIGNED')

    def test_unrelated_allocator_error_does_not_retire_any_journal(self):
        flow=self.partial_flow('prepared',with_cycle=False)
        _,original=self.prepared_allocator(flow)
        with self.assertRaisesRegex(ValueError,'Other error'):flow.recover_prepared_assignment(ValueError('Other error'))
        self.assertEqual((flow.allocator.root/'controller.json').read_bytes(),original)
        self.assertFalse(flow.reassignments.index.exists())


if __name__=='__main__':unittest.main()
