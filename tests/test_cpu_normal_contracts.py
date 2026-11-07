from pathlib import Path
import copy,importlib.util,json,sys,tempfile,unittest,time,threading
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol'),str(ROOT/'src/engine/cpu_auditor/shared_service')]
import cycle_work
import service
from contracts import SOURCE,COVERAGE,digest
from horizon_cpu_cycle import cycle_request,MixedNormalSource,CpuOnlyNormalSource
from horizon_work_cycle import WorkCycleJournal
from horizon_cpu_bridge import LogicalMinerCpuResources,GuiCpuClient
from types import SimpleNamespace

LO=1<<67
def ticket():return dict(ticket_id='b'*64,start=str(LO),end=str(LO+10000))
def work():return dict(genesis='f'*64,request_id='a'*64,ticket='b'*64,ticket_number='1',start=str(LO),length='10000',target_numerator='32',target_denominator='1')
def context(role='search',position=0):
    return dict(role=role,work_cycle='a'*64,audit_position=position,audit_request_id=('c'if position==0 else'd')*64,audit_slot=position,audit_attempt=0)
def assigned(position):return dict(stage='assigned',request=('c'if position==0 else'd')*64,slot=position,attempt=0)
def record(lo=LO+1,gap=1000):return dict(lower_prime=str(lo),upper_prime=str(lo+gap),gap=gap)
def request():return dict(schema=cycle_work.SCHEMA,work_cycle='a'*64,normal_ticket='b'*64,stage='search',**ticket())

class FakeClient:
    def __init__(self,complete=True,mismatch=False):self.request=None;self.enabled=False;self.complete=complete;self.calls=[];self.mismatch=mismatch;self.executor=None
    def call(self,action,**fields):
        self.calls.append(action)
        if action=='status':return dict(status=dict(request_sha256=digest(self.request)if self.request else None))
        if action=='execute':self.request=fields['request'];return dict(status={})
        if action=='evidence':
            return dict(evidence=dict(request=self.request,request_sha256=digest(self.request),complete=self.complete,executor=self.executor,
                counts100=['1'if self.mismatch else'0']+['0']*99,best=[],completed_positions='10000'if self.complete else'100',
                frontiers=cycle_work.frontier(cycle_work.partitions(self.request),10000 if self.complete else 100),
                production_admitted=False,network_accepted=False))
        if action in('ack_result','discard'):self.request=None;return dict(status={})
        raise AssertionError(action)
    def configure(self,*args):self.enabled=args[0];self.calls.append(('configure',args[0]));return dict(status=dict(enabled=self.enabled))
    def pause(self):self.enabled=False;self.calls.append('pause');return dict(status={})
    def close(self):self.calls.append('close')

class FakeGpu:
    def __init__(self,paused=False):self.paused=paused;self.calls=0
    def run_ticket(self,ticket,piece):
        self.calls+=1
        if self.paused:return dict(state='PAUSED',ticket_id=ticket['ticket_id'])
        body=dict(counts=['0']*100,best=[])
        return dict(body=body,sha256=digest(body),network_accepted=False)
    def close(self):pass

class ContractTests(unittest.TestCase):
    def test_owned_work_has100_exact_bins_and_rejects_other_domain(self):
        parts=cycle_work.partitions(request());self.assertEqual(len(parts),100)
        self.assertEqual(sum(int(x['end'])-int(x['start'])for x in parts),10000)
        bad=request();bad['start']=str(1<<66)
        with self.assertRaisesRegex(ValueError,'Owned68'):cycle_work.partitions(bad)
        bad=request();bad['stage']='audit_only'
        with self.assertRaises(ValueError):cycle_work.partitions(bad)
        bad=request();bad['end']=str(LO+10001)
        with self.assertRaisesRegex(ValueError,'equal partitions'):cycle_work.partitions(bad)
    def test_best_merges_without_duplicate_or_unowned_gap(self):
        rows=[record(LO+1,1200),record(LO+3,1000)]
        self.assertEqual(cycle_work.records(rows,LO,LO+100,2,900),rows)
        self.assertEqual(cycle_work.merge_best(rows,rows),rows)
        with self.assertRaises(ValueError):cycle_work.records([record(LO-1)],LO,LO+100,1,900)
        many=[record(LO+2*i+1,1000+2*i)for i in range(150)]
        self.assertEqual(len(cycle_work.merge_best([],many)),100)
    def test_partial_frontier_is_explicit(self):
        rows=cycle_work.frontier(cycle_work.partitions(request()),150)
        self.assertTrue(rows[0]['complete']);self.assertFalse(rows[1]['complete']);self.assertEqual(rows[1]['covered_end'],str(LO+150));self.assertEqual(rows[2]['covered_end'],rows[2]['start'])
    def test_native_result_binds_records_frontier_affinity_and_counters(self):
        task=(0,LO,LO+100,0,1,900)
        r=dict(type='result',schema='PGN_CPU_SHARED_OWNED_PIECE_V2',state='COMPLETE',backend='cpu_shared_gpu_lineage68',job_id=1,
            start=str(LO),end=str(LO+100),positions='100',minimum_gap=900,pinned_cpu=2,affinity_verified=True,
            canonical_source_manifest_sha256=SOURCE,coverage_contract=COVERAGE,production_admitted=False,audit_only=False,
            implementation='shared_scalar64',sieve_layout='canonical_tiled',count='1',seconds=.01,candidate_tests=3,best=[record()],
            owned_frontier=dict(start=str(LO),end=str(LO+100),gap_owner='lower_prime',right_crossing_verified=True),records_verification=cycle_work.RECORDS)
        self.assertEqual(service.validate_reply(r,task,2,False),1)
        for key,value in [('pinned_cpu',3),('best',[]),('owned_frontier',{}),('production_admitted',True)]:
            bad=copy.deepcopy(r);bad[key]=value
            with self.assertRaises(ValueError,msg=key):service.validate_reply(bad,task,2,False)
    def test_standalone_audit_action_is_refused(self):
        obj=object.__new__(service.Service)
        with self.assertRaisesRegex(ValueError,'Exact command'):obj.command(dict(id=1,action='open',request={}))
    def test_out_of_order_native_pieces_do_not_credit_unowned_frontier(self):
        obj=object.__new__(service.Service);obj.simd=False;obj.completed={};obj.committed=0;obj.counts=[0]*100;obj.best=[];obj.tests=0
        obj.compute_seconds=0.;obj.completed_piece_count=0;obj.total=10000;obj.max_buffered=0;obj.persist=lambda:None;obj.remember_pages=lambda w,p:None
        first=SimpleNamespace(cpu=2,task=(0,LO,LO+100,0,1,900),positions=0,native_seconds=0.)
        second=SimpleNamespace(cpu=3,task=(100,LO+100,LO+200,1,2,900),positions=0,native_seconds=0.)
        obj.workers={2:first,3:second}
        def reply(w):
            offset,lo,hi,part,seq,gap=w.task
            return dict(type='result',schema='PGN_CPU_SHARED_OWNED_PIECE_V2',state='COMPLETE',backend='cpu_shared_gpu_lineage68',job_id=seq,start=str(lo),end=str(hi),positions=str(hi-lo),minimum_gap=gap,pinned_cpu=w.cpu,affinity_verified=True,canonical_source_manifest_sha256=SOURCE,coverage_contract=COVERAGE,production_admitted=False,audit_only=False,implementation='shared_scalar64',sieve_layout='canonical_tiled',count='1',seconds=.01,candidate_tests=3,best=[record(lo+1)],owned_frontier=dict(start=str(lo),end=str(hi),gap_owner='lower_prime',right_crossing_verified=True),records_verification=cycle_work.RECORDS)
        obj.native_event(second,reply(second));self.assertEqual(obj.committed,0);self.assertEqual(obj.best,[]);self.assertEqual(obj.tests,0)
        obj.native_event(first,reply(first));self.assertEqual(obj.committed,200);self.assertEqual(obj.tests,6);self.assertEqual(obj.counts[:2],[1,1]);self.assertEqual(len(obj.best),2)
    def test_cpu_only_admission_remains_closed(self):
        with self.assertRaisesRegex(ValueError,'fixed24h'):CpuOnlyNormalSource().run_ticket(ticket(),100)
    def test_cpu_bridge_output_loss_requests_no_unbounded_wait(self):
        client=GuiCpuClient('CPU/ALEX/0',dict(host='ALEX'),lambda value:False,timeout=.5)
        began=time.monotonic()
        with self.assertRaisesRegex(OSError,'disconnected'):client.call('status')
        self.assertLess(time.monotonic()-began,.1)
    def test_checkpoint_with100_best_records_remains_bounded_and_resumes(self):
        with tempfile.TemporaryDirectory()as directory:
            obj=object.__new__(service.Service);obj.cp=Path(directory)/'checkpoint.json';obj.identity=lambda:dict(control_version=2)
            obj.request=request();obj.probe=False;obj.parts=cycle_work.partitions(obj.request);obj.total=10000;obj.committed=10000
            obj.counts=[1]*100;obj.compute_seconds=1.;obj.completed_piece_count=100;obj.tests=1000
            obj.best=sorted([record(LO+100*i+1)for i in range(100)],key=lambda r:(-r['gap'],int(r['lower_prime'])))
            obj.result=dict(request_sha256=digest(obj.request),counts=['1']*100,state='CPU_WORK_SOURCE_PROOF_PENDING')
            obj.persist();self.assertLess(obj.cp.stat().st_size,32768)
            restored=object.__new__(service.Service);restored.cp=obj.cp;restored.identity=obj.identity;restored.restore()
            self.assertEqual(restored.best,obj.best);self.assertEqual(restored.tests,1000);self.assertEqual(restored.committed,10000)

class CycleTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name);self.journal=WorkCycleJournal(self.root/'cycles','f'*64);self.journal.prepare(work())
    def tearDown(self):self.journal.close();self.tmp.cleanup()
    def ready(self):
        for p in range(2):self.journal.attach('a'*64,p,dict(stage='unavailable',anchor='e'*64,reason='no_eligible_independent_work'),permission_verified=True)
        self.journal.advance('a'*64,'running');return self.journal.snapshot('a'*64)
    def test_exact_audit1_audit2_search_order(self):
        self.journal.attach('a'*64,0,assigned(0));self.journal.attach('a'*64,1,assigned(1))
        cycle=self.journal.snapshot('a'*64)
        cycle_request(ticket(),context('audit',0),cycle)
        with self.assertRaises(ValueError):cycle_request(ticket(),context('audit',1),cycle)
        with self.assertRaises(ValueError):cycle_request(ticket(),context(),cycle)
        self.journal.published('a'*64,0,'c'*64,0,0,'e'*64)
        cycle_request(ticket(),context('audit',1),self.journal.snapshot('a'*64))
        wrong=context('audit',1);wrong['audit_attempt']=1
        with self.assertRaisesRegex(ValueError,'another'):cycle_request(ticket(),wrong,self.journal.snapshot('a'*64))
        self.journal.published('a'*64,1,'d'*64,1,0,'e'*64);self.journal.advance('a'*64,'running')
        self.assertEqual(cycle_request(ticket(),context(),self.journal.snapshot('a'*64))['stage'],'search')
    def test_cpu_on_does_not_allocate_standalone_work(self):
        cpu=FakeClient();source=MixedNormalSource(FakeGpu(),cpu,self.root,affinity=[2]);source.configure_cpu(True,1,[2])
        source.run_ticket(ticket(),100)
        self.assertNotIn('execute',cpu.calls)
    def test_mixed_complete_report_is_gpu_owned_and_cpu_matches(self):
        cpu=FakeClient();gpu=FakeGpu();source=MixedNormalSource(gpu,cpu,self.root,affinity=[2])
        source.bind_cycle(ticket(),context(),self.ready());source.configure_cpu(True,1,[2]);r=source.run_ticket(ticket(),100);source.wait_cpu()
        self.assertEqual(r['sha256'],digest(r['body']));self.assertEqual(cpu.calls.count('execute'),1);self.assertIn('ack_result',cpu.calls)
        saved=json.loads((self.root/'tickets'/('b'*64)/'cpu-cycle-evidence.json').read_bytes())
        self.assertTrue(saved['full_result_compared']);self.assertFalse(saved['cpu_publication_admitted']);self.assertEqual(saved['publication_source'],'qualified_gpu')
    def test_partial_is_evidence_and_never_full_report(self):
        cpu=FakeClient(complete=False);source=MixedNormalSource(FakeGpu(),cpu,self.root,affinity=[2]);source.bind_cycle(ticket(),context(),self.ready());source.configure_cpu(True,1,[2]);source.run_ticket(ticket(),100);source.wait_cpu()
        saved=json.loads((self.root/'tickets'/('b'*64)/'cpu-cycle-evidence.json').read_bytes())
        self.assertFalse(saved['full_result_compared']);self.assertFalse(saved['cpu']['complete']);self.assertIn('discard',cpu.calls)
    def test_paused_gpu_retains_cpu_ownership_for_resume(self):
        cpu=FakeClient(complete=False);gpu=FakeGpu(paused=True);source=MixedNormalSource(gpu,cpu,self.root,affinity=[2]);source.bind_cycle(ticket(),context(),self.ready());source.configure_cpu(True,1,[2]);source.run_ticket(ticket(),100);source.wait_cpu()
        self.assertIsNotNone(cpu.request);self.assertNotIn('discard',cpu.calls)
        gpu.paused=False;source.run_ticket(ticket(),100);source.wait_cpu();self.assertEqual(cpu.calls.count('execute'),1)
    def test_full_cpu_mismatch_cannot_reach_network_handoff(self):
        cpu=FakeClient(mismatch=True);source=MixedNormalSource(FakeGpu(),cpu,self.root,affinity=[2]);source.bind_cycle(ticket(),context(),self.ready());source.configure_cpu(True,1,[2])
        report=source.run_ticket(ticket(),100);source.wait_cpu()
        self.assertIn('disagrees',source.last_cpu_error);self.assertFalse(source.enabled);self.assertIn('body',report)
        self.assertIsNotNone(cpu.request)
    def test_provisional_gpu_report_is_not_a_false_cpu_qualification_reference(self):
        cpu=FakeClient(mismatch=True);gpu=FakeGpu();gpu.scientific_raw_report_provisional=True
        source=MixedNormalSource(gpu,cpu,self.root,affinity=[2]);source.bind_cycle(ticket(),context(),self.ready())
        source.configure_cpu(True,1,[2]);source.run_ticket(ticket(),100);source.wait_cpu()
        self.assertIsNone(source.last_cpu_error)
        saved=json.loads((self.root/'tickets'/('b'*64)/'cpu-cycle-evidence.json').read_bytes())
        self.assertFalse(saved['full_result_compared']);self.assertFalse(saved['cpu_publication_admitted'])
        self.assertEqual(saved['publication_source'],'gpu_pending_scientific_finalization')
    def test_gpu_never_waits_for_cpu_boundary(self):
        release=threading.Event();draining=threading.Event()
        class SlowCpu(FakeClient):
            def pause(self):draining.set();release.wait(2);return super().pause()
        cpu=SlowCpu();source=MixedNormalSource(FakeGpu(),cpu,self.root,affinity=[2]);source.bind_cycle(ticket(),context(),self.ready());source.configure_cpu(True,1,[2])
        start=time.monotonic();report=source.run_ticket(ticket(),100);elapsed=time.monotonic()-start
        self.assertLess(elapsed,.2);self.assertIn('body',report);self.assertTrue(draining.wait(1));self.assertTrue(source.cpu_thread.is_alive())
        release.set();source.wait_cpu()
    def test_permission_hold_refuses_cpu_grant(self):
        self.journal.hold_permission('a'*64,'audit_permission_disabled')
        with self.assertRaisesRegex(ValueError,'live normal'):cycle_request(ticket(),context(),self.journal.snapshot('a'*64))
    def test_two_hosts_use_distinct_gui_owned_services_and_workspaces(self):
        devices={'CPU/ALEX/0':dict(host='ALEX',processor_group=0),'CPU/WIZZ/0':dict(host='WIZZ',processor_group=0)}
        backends={device:FakeClient()for device in devices};seen=[]
        for device in devices:backends[device].executor=devices[device]
        def emit(frame):
            if frame['schema']!='PGN_HORIZON_CPU_COMMAND_V1':return
            device=frame['device_id'];command=frame['command'];backend=backends[device];seen.append((device,command['action']))
            if command['action']=='configure':reply=backend.configure(command['enabled'],command['threads'],command['affinity'])
            elif command['action']=='pause':reply=backend.pause()
            else:reply=backend.call(command['action'],**{k:v for k,v in command.items()if k!='action'})
            reply['ok']=True;fleet.accept_cpu_reply(device,frame['bridge_id'],reply)
            if command['action']=='execute':
                directory=self.root/device.replace('/','_');directory.mkdir(exist_ok=True);(directory/'owned-request.json').write_text(json.dumps(backend.request))
        fleet=LogicalMinerCpuResources(FakeGpu(),self.root,emit);fleet.bind_cycle(ticket(),context(),self.ready())
        for device,inventory in devices.items():fleet.configure_cpu(device,inventory,True,1,[2])
        report=fleet.run_ticket(ticket(),100)
        for lane in fleet.lanes.values():lane.wait_cpu()
        self.assertIn('body',report);self.assertEqual(len(fleet.lanes),2)
        for device in devices:
            self.assertEqual(seen.count((device,'execute')),1)
            saved=json.loads((self.root/device.replace('/','_')/'owned-request.json').read_bytes())
            self.assertEqual(saved['work_cycle'],'a'*64);self.assertEqual(saved['normal_ticket'],'b'*64)
        self.assertNotEqual(fleet.lanes['CPU/ALEX/0'].cpu,fleet.lanes['CPU/WIZZ/0'].cpu)
        with self.assertRaisesRegex(ValueError,'endpoint host'):fleet.configure_cpu('CPU/WIZZ/0',devices['CPU/ALEX/0'],True,1,[2])
        with self.assertRaises(ValueError):fleet.accept_cpu_reply('CPU/UNKNOWN/0','1',dict(ok=True))

if __name__=='__main__':unittest.main()
