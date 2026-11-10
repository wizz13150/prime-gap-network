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
        if name=='getblockhash':return 'e'*64 if args[0]==2 else 'c'*64
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


if __name__=='__main__':unittest.main()
