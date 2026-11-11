"""Own-Core receipt validation and local-cycle conservation; controlled RPC."""
from pathlib import Path
import copy,sys,tempfile,unittest
import threading
from types import SimpleNamespace
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/p)for p in ('src/engine/runtime_host','src/protocol','tests')]
import horizon_native_work_duty as duty
from horizon_core_rpc import CoreRpcError
from horizon_work_cycle import WorkCycleJournal
from test_work_cycle import work


def receipt(w,count=2,block='e'*64):
    return dict(schema='PGN_NATIVE_ASSIGNED_AUDIT_SERVICE_V1',enforced=True,request_id=w['request_id'],
        issuance_block=block,issuance_height=16,
        credits=[dict(request_id=f'{300+int(w["request_id"],16)*2+i:064x}',slot=i,attempt=0,commitment=f'{500+i:064x}')for i in range(count)])


class Rpc:
    def __init__(self,w,count=2):
        self.w=dict(w,accepted=False,parent='f'*64,height=21)
        self.w['native_audit_service']=dict(receipt(w,count),view_parent=self.w['parent'])
        self.tip=self.w['parent'];self.canonical=True;self.view_canonical=True;self.old_confirmations=-1
    def call(self,method,args=None):
        if method=='getpgnscientificwork':return copy.deepcopy(self.w)
        if method=='getbestblockhash':return self.tip
        if method=='getblockhash':
            if args==[self.w['height']-1]:return self.w['parent']if self.view_canonical else'2'*64
            return self.w['native_audit_service']['issuance_block']if self.canonical else'1'*64
        if method=='getblockheader':
            block=self.w['native_audit_service']['issuance_block']
            return dict(hash=args[0],height=16,confirmations=5 if args[0]==block and self.canonical else self.old_confirmations)
        raise AssertionError(method)


class NativeDutyTests(unittest.TestCase):
    def test_zero_one_and_two_consumed_credits_fill_exactly_two_terminal_stages(self):
        for n in range(3):
            w=work();rpc=Rpc(w,n);value=duty.observe(rpc,w,w['genesis']);stages=duty.tasks(value,w)
            self.assertEqual(len(stages),2);self.assertEqual(sum(x['stage']=='committed'for x in stages),n)
            self.assertEqual(sum(x['stage']=='unavailable'for x in stages),2-n)
    def test_saved_claim_is_not_authority_and_fresh_wrong_range_is_rejected(self):
        w=dict(work(),native_audit_service=receipt(work()));rpc=Rpc(w);rpc.w['start']='160000000000000000000'
        with self.assertRaises(ValueError):duty.observe(rpc,w,w['genesis'])
    def test_retarget_does_not_change_the_confirmed_service_consumer(self):
        w=work();rpc=Rpc(w);expected=duty.observe(rpc,w,w['genesis'])
        # Actual cold-resume regression: retained117485/4096, current233443/8192.
        for numerator,denominator in [('233443','8192'),('467201','16384')]:
            rpc.w.update(target_numerator=numerator,target_denominator=denominator)
            self.assertEqual(duty.observe(rpc,w,w['genesis'],expected),expected)
        for field in ('request_id','ticket','start','length'):
            changed=Rpc(w);changed.w[field]='1'if field in('start','length')else'1'*64
            with self.subTest(field=field),self.assertRaises(ValueError):duty.observe(changed,w,w['genesis'])
    def test_old_pre_activation_work_resumes_after_a_difficulty_change(self):
        w=work();rpc=Rpc(w)
        rpc.w.update(target_numerator='233443',target_denominator='8192')
        rpc.w['native_audit_service']['enforced']=False;rpc.w['native_audit_service']['credits']=[]
        self.assertIsNone(duty.observe(rpc,w,w['genesis']))
    def test_branch_change_or_orphaned_issuance_cannot_authorize_search(self):
        for change in ('view_canonical','canonical'):
            rpc=Rpc(work());setattr(rpc,change,False)
            with self.assertRaises(RuntimeError):duty.observe(rpc,work(),work()['genesis'])
    def test_descendant_blocks_during_service_read_do_not_stop_cold_resume(self):
        w=work();rpc=Rpc(w);expected=duty.observe(rpc,w,w['genesis'])
        rpc.tip='9'*64
        self.assertEqual(duty.observe(rpc,w,w['genesis'],expected),expected)
    def test_changed_consumer_self_audit_duplicate_and_noninteger_fields_are_rejected(self):
        values=[];base=receipt(work())
        x=copy.deepcopy(base);x['credits'][0]['request_id']=work()['request_id'];values.append(x)
        x=copy.deepcopy(base);x['credits'][1]=x['credits'][0];values.append(x)
        x=copy.deepcopy(base);x['credits'][0]['attempt']=True;values.append(x)
        x=copy.deepcopy(base);x['credits'][0]['slot']=2;values.append(x)
        x=copy.deepcopy(base);x['request_id']='b'*64;values.append(x)
        for value in values:
            with self.subTest(value=value),self.assertRaises(ValueError):duty.context(value,work())
    def test_only_explicit_missing_rpc_allows_legacy_gate(self):
        class Missing:
            def __init__(self,code):self.code=code
            def call(self,*_):raise CoreRpcError(self.code,'controlled transport')
        self.assertIsNone(duty.gate(Missing(-32601),'signer','a'*64))
        with self.assertRaises(CoreRpcError):duty.gate(Missing(-28),'signer','a'*64)
    def test_gate_requires_audits_before_a_new_request_is_signed(self):
        class Gate:
            def call(self,*_):return dict(schema='PGN_NATIVE_WORK_DUTY_STATUS_V1',genesis='a'*64,
                bestblockhash='e'*64,enforced=True,may_request=False,status='audit_work_required')
        with self.assertRaises(duty.NativeAuditServiceRequired):duty.gate(Gate(),'signer','a'*64)

    def test_real_allocator_holds_before_signing_or_publishing_new_and_cached_requests(self):
        from types import SimpleNamespace
        from horizon_network_worker import NetworkWorker,save
        class Gate:
            def __init__(self):self.calls=[]
            def verify(self):return {}
            def call(self,method,args=None,**_):
                self.calls.append(method)
                if method=='listwallets':return ['fixture']
                if method=='getpgnstate':return dict(genesis='a'*64,leases=[])
                if method=='getpgnworkdutystatus':
                    return dict(schema='PGN_NATIVE_WORK_DUTY_STATUS_V1',genesis='a'*64,
                        bestblockhash='e'*64,enforced=True,may_request=False,status='audit_work_required')
                raise AssertionError('Unexpected write or signing RPC: '+method)
        for cached in (False,True):
            with self.subTest(cached=cached),tempfile.TemporaryDirectory()as path:
                w=NetworkWorker.__new__(NetworkWorker);w.root=Path(path);w.rpc=Gate();w.sequence=0
                w.args=SimpleNamespace(genesis='a'*64,wallet='fixture');w.address=lambda:'fixture-signer'
                w.ledger=lambda *_:self.fail('Gate must hold before publication')
                if cached:save(w.root/'controller.json',dict(stage='REQUEST_SIGNED',sequence=1,
                    request=dict(request_id='b'*64,envelope='00')))
                before=(w.root/'controller.json').read_bytes()if cached else None
                with self.assertRaises(duty.NativeAuditServiceRequired):w.assign()
                self.assertNotIn('signmessage',w.rpc.calls);self.assertNotIn('getpgnticketrequest',w.rpc.calls)
                if cached:self.assertEqual((w.root/'controller.json').read_bytes(),before)
                else:self.assertFalse((w.root/'controller.json').exists())
    def test_receipt_replacement_requires_a_genuinely_orphaned_old_block(self):
        w=work();previous=receipt(w,block='d'*64);rpc=Rpc(w)
        self.assertEqual(duty.observe(rpc,w,w['genesis'],previous),receipt(w))
        rpc.old_confirmations=2
        with self.assertRaises(ValueError):duty.observe(rpc,w,w['genesis'],previous)
    def test_native_cycle_is_ready_without_scheduling_two_more_audits(self):
        with tempfile.TemporaryDirectory()as path,WorkCycleJournal(path,'a'*64)as j:
            w=work();j.prepare(w,native_service=receipt(w));self.assertTrue(j.may_search(w['request_id']))
            self.assertIsNone(j.pending(0));self.assertIsNone(j.pending(1))
            self.assertIsNone(j.bound_audit(receipt(w)['credits'][0]['request_id'],0,0,0))
    def test_native_cold_history_preserves_consuming_request_without_lane_replay(self):
        with tempfile.TemporaryDirectory()as path:
            j=WorkCycleJournal(path,'a'*64)
            try:
                for i in range(1,6):
                    w=work(i);j.prepare(w,native_service=receipt(w));j.advance(w['request_id'],'running');j.advance(w['request_id'],'completed')
                before=j.snapshot(work(1)['request_id']);self.assertIn('native_service',before)
                self.assertIsNone(j.bound_audit(receipt(work(1))['credits'][0]['request_id'],0,0,0))
            finally:j.close()
            with WorkCycleJournal(path,'a'*64)as reopened:
                self.assertEqual(reopened.snapshot(work(1)['request_id']),before)
                self.assertFalse(reopened.prepare(work(1),native_service=receipt(work(1))))
    def test_missing_native_context_cannot_retag_a_started_cycle_as_legacy(self):
        with tempfile.TemporaryDirectory()as path,WorkCycleJournal(path,'a'*64)as j:
            w=work();j.prepare(w,native_service=receipt(w));j.advance(w['request_id'],'running')
            with self.assertRaises(ValueError):j.prepare(w)

    def test_flow_rechecks_cold_native_context_even_when_saved_work_flag_is_missing(self):
        from horizon_mining_flow import MiningFlow
        with tempfile.TemporaryDirectory()as path:
            w=work();rpc=Rpc(w)
            with WorkCycleJournal(path,w['genesis'])as j:
                j.prepare(w,native_service=receipt(w));j.advance(w['request_id'],'running')
                before=j.snapshot(w['request_id'])
            with WorkCycleJournal(path,w['genesis'])as j:
                flow=MiningFlow.__new__(MiningFlow);flow.cycles=j
                flow.worker=SimpleNamespace(rpc=rpc,args=SimpleNamespace(genesis=w['genesis']),network_lock=threading.RLock())
                rpc.canonical=False
                with self.assertRaises(RuntimeError):flow.prepare_cycle(w)
                self.assertEqual(j.snapshot(w['request_id']),before)
                rpc.canonical=True;self.assertFalse(flow.prepare_cycle(w))

    def test_native_reorg_repair_rebinds_only_orphaned_consumer_and_keeps_completed_range(self):
        from horizon_mining_flow import MiningFlow
        with tempfile.TemporaryDirectory()as path,WorkCycleJournal(path,'a'*64)as j:
            w=work();old=receipt(w,block='d'*64);rpc=Rpc(w)
            j.prepare(w,native_service=old);j.advance(w['request_id'],'running');j.advance(w['request_id'],'completed')
            j.hold(w['request_id'],WorkCycleJournal.reorg_hold);before=j.snapshot(w['request_id'])
            flow=MiningFlow.__new__(MiningFlow);flow.cycles=j
            flow.worker=SimpleNamespace(rpc=rpc,args=SimpleNamespace(genesis=w['genesis']),network_lock=threading.RLock())
            rpc.old_confirmations=2
            with self.assertRaises(ValueError):flow.refresh_cycle_confirmations(w['request_id'])
            self.assertEqual(j.snapshot(w['request_id']),before)
            rpc.old_confirmations=-1;self.assertTrue(flow.refresh_cycle_confirmations(w['request_id']))
            after=j.snapshot(w['request_id']);self.assertEqual(after['stage'],'completed');self.assertFalse(after['hold'])
            self.assertEqual(after['native_service'],receipt(w));self.assertTrue(j.ready(after))
            self.assertIsNone(j.pending(0));self.assertIsNone(j.pending(1))


if __name__=='__main__':unittest.main()
