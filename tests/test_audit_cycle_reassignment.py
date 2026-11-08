"""Prepared-cycle orphan replay on actual clients/journals; controlled RPC, no credit or GPU."""
from pathlib import Path
from types import SimpleNamespace
import copy,json,sys,tempfile,threading,unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
from horizon_network_audits import NetworkAudits,save
from horizon_mining_flow import MiningFlow
from horizon_work_cycle import WorkCycleJournal
from horizon_coordination import native_header_hash
from report_commitment import audit_commitment

def intent(serial):
    header=bytearray(112);header[4:36]=bytes.fromhex('a'*64)[::-1];header[80]=2;header[82]=serial
    block=bytes(header)+b'\1'
    return dict(parent='a'*64,block=block.hex(),hash=native_header_hash(block),operation_sha256='f'*64)

class ReassignmentTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.work='d'*64;self.request='1'*64
        self.old=intent(1);self.new=intent(2)
        self.ranges=[dict(index=i,start=str(150000000000000000000+i*2000000000000),end=str(150000000000000000000+(i+1)*2000000000000))for i in range(5)]
        self.cycles=WorkCycleJournal(self.root/'cycles','9'*64);self.addCleanup(lambda:self.cycles.close())
        self.cycles.prepare(dict(genesis='9'*64,request_id=self.work,ticket='e'*64,ticket_number='42',start='150000000000000000000',length='200000000000000',target_numerator='32',target_denominator='1'))
        self.cycles.attach(self.work,0,dict(stage='assigned',request=self.request,slot=0,attempt=0))
        self.cycles.attach(self.work,1,dict(stage='assigned',request='4'*64,slot=1,attempt=0))
        self.cycles.published(self.work,0,self.request,0,0,self.old['hash'])
        outer=self
        class Rpc:
            def __init__(self):self.stage='assigned';self.canonical=False;self.missing=False;self.moving=False;self.tip_reads=0;self.header_reads=0;self.tip='a'*64
            def call(self,method,args=None,wallet=False):
                args=args or []
                if method=='getbestblockhash':
                    self.tip_reads+=1
                    return 'b'*64 if self.moving and self.tip_reads>1 else self.tip
                if method=='getblockheader':
                    self.header_reads+=1
                    if self.missing:raise OSError('Controlled header read unavailable')
                    if args[0]==outer.old['hash']:return dict(hash=args[0],height=7,confirmations=3 if self.canonical else -1)
                    if args[0]=='8'*64:return dict(hash=args[0],height=8,confirmations=2)
                    return dict(hash=args[0],height=10 if args[0]==outer.new['hash'] else 9,confirmations=1)
                if method=='getblockhash':return outer.old['hash'] if self.canonical and args[0]==7 else outer.new['hash'] if args[0]==10 else '8'*64 if args[0]==8 else 'a'*64 if args[0]==9 else 'c'*64
                if method=='getpgnauditstate':return [dict(request_id=outer.request,jobs=[dict(slot=0,attempt=0,state=self.stage,commitment=outer.answer['commitment'])])]
                if method=='getpgnauditevent':
                    if args[0]=='assign' and self.stage=='committed':return dict(available=False,unavailable_reason='no_eligible_independent_work',genesis='9'*64,parent=self.tip)
                    return dict(available=True,challenge='fixture',envelope=args[0],request_id=outer.request,slot=0,attempt=0,ranges=outer.ranges)
                if method=='signmessage':return 'fixture-signature'
                if method=='getpgnreports':return [dict(ticket='2'*64,report_root='3'*64)]
                raise AssertionError(method)
        self.rpc=Rpc();self.compute=0;self.publications=0
        def compute(*args):self.compute+=1;raise AssertionError('A sealed answer must not be computed again')
        def ledger(envelopes,*,on_intent=None):
            if envelopes==['assign']:
                self.rpc.stage='assigned'
                return dict(block_hash='a'*64)
            self.publications+=1
            if on_intent:on_intent(self.new)
            self.rpc.stage='committed'
            self.rpc.tip=self.new['hash']
            return dict(block_hash=self.new['hash'])
        running=threading.Event();running.set()
        worker=SimpleNamespace(root=self.root,rpc=self.rpc,source=SimpleNamespace(run_ticket=compute),network_lock=threading.RLock(),
                               args=SimpleNamespace(piece_limit=64000000000000),running=running,quitting=threading.Event(),address=lambda:'fixture',ledger=ledger)
        self.flow=object.__new__(MiningFlow);self.flow.worker=worker;self.flow.cycles=self.cycles;self.flow.gpu=SimpleNamespace(wake=lambda:None);self.flow.recovered_report=None
        self.audits=NetworkAudits(worker,on_assigned=lambda a:self.flow.bind_audit_cycle(0,a),
            on_committed=lambda a,b:self.flow.audit_committed(0,a,b),before_commit=lambda a:self.flow.before_audit_commit(0,a),
            on_reassigned=lambda a,b:self.flow.reopen_audit_cycle(0,a,b),reassignment_needed=lambda a:self.flow.audit_reassignment_needed(0,a))
        self.flow.audit_lanes=[self.audits]
        self.answer=audit_commitment('2'*64,0,list(range(5)),['1']*5,'6'*64)
        self.audit=dict(schema='PGN_HORIZON_ASSIGNED_AUDIT_V1',request_id=self.request,ticket='2'*64,report_root='3'*64,slot=0,attempt=0,
                        ranges=self.ranges,counts=[1]*5,target_merit_q16=32*65536,answer=self.answer,publication_intent=self.old,work_cycle=self.work)
        self.name=self.request+'-0-0';self.current=self.audits.folder/'current.json';self.sealed=self.audits.folder/(self.name+'.json')
        save(self.current,self.audit);save(self.sealed,self.audit);self.audits.answers.register([self.name])
    def test_current_journal_orphan_republishes_exact_answer_without_compute(self):
        self.assertEqual(self.audits.process_one(),'committed')
        task=self.cycles.snapshot(self.work)['audits'][0]
        self.assertEqual(task['block'],self.new['hash']);self.assertEqual(task['stage'],'committed')
        self.assertFalse(self.cycles.may_search(self.work));self.assertEqual((self.compute,self.publications),(0,1))
        saved=json.loads(self.sealed.read_bytes());self.assertEqual(saved['answer'],self.answer);self.assertEqual(saved['counts'],[1]*5)
    def test_archive_only_orphan_recovers_after_current_file_was_removed(self):
        self.current.unlink();self.test_current_journal_orphan_republishes_exact_answer_without_compute()
    def test_crash_after_local_reopen_resumes_idempotently(self):
        def crash(a,b):self.flow.reopen_audit_cycle(0,a,b);raise RuntimeError('Controlled crash after durable reopen')
        self.audits.on_reassigned=crash
        with self.assertRaisesRegex(RuntimeError,'Controlled crash'):self.audits.process_one()
        self.assertEqual(self.cycles.snapshot(self.work)['audits'][0]['stage'],'assigned')
        self.cycles.close();self.cycles=WorkCycleJournal(self.root/'cycles','9'*64);self.flow.cycles=self.cycles
        self.audits.on_reassigned=lambda a,b:self.flow.reopen_audit_cycle(0,a,b)
        self.test_current_journal_orphan_republishes_exact_answer_without_compute()
    def refused(self,kind,pattern):
        before=self.cycles.snapshot(self.work)
        with self.assertRaisesRegex(kind,pattern):self.audits.process_one()
        self.assertEqual(self.cycles.snapshot(self.work),before);self.assertEqual((self.compute,self.publications),(0,0))
    def test_canonical_confirmation_never_replaced(self):
        self.rpc.canonical=True;self.refused(RuntimeError,'Canonical')
    def test_missing_header_is_not_an_orphan_proof(self):
        self.rpc.missing=True;self.refused(OSError,'unavailable')
    def test_moving_tip_does_not_change_local_service_accounting(self):
        self.rpc.moving=True;self.refused(RuntimeError,'Chain changed')
    def test_different_active_attempt_cannot_reopen_old_confirmation(self):
        self.audit['attempt']=1;save(self.current,self.audit);self.refused(RuntimeError,'assignment changed')
    def test_running_search_replays_without_discarding_search_state(self):
        self.cycles.published(self.work,1,'4'*64,1,0,'8'*64);self.cycles.advance(self.work,'running')
        before=self.cycles.snapshot(self.work)
        self.test_current_journal_orphan_republishes_exact_answer_without_compute()
        after=self.cycles.snapshot(self.work)
        self.assertEqual(after['stage'],before['stage']);self.assertEqual(after['audits'][1],before['audits'][1])
        self.assertEqual(after['hold'],'');self.assertEqual((self.compute,self.publications),(0,1))
    def test_unrelated_hold_remains_held(self):
        self.cycles.hold(self.work,'Operator inspection required');self.refused(RuntimeError,'Unrelated retained hold')
    def test_previous_reorg_hold_can_be_reconciled(self):
        self.cycles.hold(self.work,'Audit confirmation changed branch; retained work needs reconciliation')
        self.test_current_journal_orphan_republishes_exact_answer_without_compute()
    def test_cached_count_tampering_cannot_reuse_answer(self):
        self.current.unlink();self.audit['counts'][0]=9;save(self.sealed,self.audit)
        self.refused(ValueError,'committed counts')
    def test_cached_answer_tampering_is_refused_by_catalogue_digest(self):
        self.current.unlink();self.audit['answer']['body']['counts'][0]='9';save(self.sealed,self.audit)
        self.refused(ValueError,'changed after registration')
    def test_reassigned_ranges_must_match_sealed_work(self):
        self.current.unlink();self.audit['ranges']=copy.deepcopy(self.ranges);self.audit['ranges'][0]['end']='150000000000000000002';save(self.sealed,self.audit)
        self.refused(ValueError,'differs from the reassigned')
    def test_incomplete_cached_counts_are_refused(self):
        self.current.unlink();self.audit['counts'].pop();save(self.sealed,self.audit);self.refused(ValueError,'incomplete counts')
    def test_wrong_old_block_cannot_reopen_journal(self):
        before=self.cycles.snapshot(self.work)
        with self.assertRaisesRegex(ValueError,'retained block'):self.cycles.reopen_audit(self.work,0,self.request,0,0,'7'*64)
        self.assertEqual(self.cycles.snapshot(self.work),before)
    def test_ordinary_unconfirmed_publication_needs_no_orphan_proof(self):
        self.cycles.reopen_audit(self.work,0,self.request,0,0,self.old['hash']);self.rpc.missing=True
        # Publication confirmation itself still needs the new block's header.
        original=self.rpc.call
        def headers(method,args=None,**kw):
            if method=='getblockheader' and args[0]==self.old['hash']:raise AssertionError('Unconfirmed old block was checked as a fulfilled service')
            if method=='getblockheader':return dict(hash=args[0],height=10,confirmations=1)
            return original(method,args,**kw)
        self.rpc.call=headers;self.test_current_journal_orphan_republishes_exact_answer_without_compute()

if __name__=='__main__':unittest.main()
