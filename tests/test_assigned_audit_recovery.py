from pathlib import Path
from types import SimpleNamespace
import copy,json,sys,tempfile,threading,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
from horizon_assigned_audit_recovery import AssignedAuditRecovery
from report_commitment import audit_commitment

CHAIN='a'*64;ANCHOR='b'*64;ACTOR='0014'+'c'*40
def report(n):return dict(request_id=f'{n:064x}',ticket='d'*64,report_root='e'*64,miner_script='0014'+'f'*40)
def job(n):return dict(slot=0,attempt=0,state='assigned',actor=ACTOR,expired=False,
    ranges=[dict(index=i,start=str(1000+100*i),end=str(1100+100*i))for i in range(5)])
def page(offset,entries):return dict(schema='PGN_WORK_VIEW_V1',kind='audits',offset=offset,limit=100,
    state=dict(genesis=CHAIN,bestblockhash=ANCHOR,height=1000),reports=[report(n)for n in entries],
    audits=[dict(request_id=report(n)['request_id'],jobs=[job(n)])for n in entries])
class Rpc:
    def __init__(self):self.pages={0:page(0,[1,2])};self.tip=ANCHOR;self.calls=[]
    def call(self,method,args=None):
        self.calls.append((method,args))
        if method=='validateaddress':return dict(isvalid=True,scriptPubKey=ACTOR)
        if method=='getpgnworkview':return copy.deepcopy(self.pages[args[1]])
        if method=='getbestblockhash':return self.tip
        raise AssertionError(method)
class Tests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.rpc=Rpc()
        self.worker=SimpleNamespace(root=self.root,rpc=self.rpc,args=SimpleNamespace(genesis=CHAIN),
            network_lock=threading.RLock(),address=lambda:'own-address')
        self.recovery=AssignedAuditRecovery(self.worker)
        self.folders=[self.root/'service-audit-lanes'/str(i)/'audits' for i in range(2)]
        for folder in self.folders:folder.mkdir(parents=True)
    def test_missing_journals_recover_two_distinct_existing_assignments(self):
        first,_=self.recovery.claim(self.folders[0]);second,_=self.recovery.claim(self.folders[1])
        self.assertNotEqual(first['request_id'],second['request_id'])
        self.assertEqual(first['ranges'],job(1)['ranges']);self.assertEqual(first['counts'],[])
        self.assertEqual(json.loads((self.folders[0]/'current.json').read_bytes()),first)
        self.assertFalse(any(m in ('getpgnauditevent','signmessage','submitblock')for m,_ in self.rpc.calls))
    def test_conflicting_journal_is_never_overwritten(self):
        p=self.folders[0]/'current.json';p.write_text('{"request_id":"retained"}')
        with self.assertRaises(RuntimeError):self.recovery.claim(self.folders[0])
        self.assertEqual(p.read_text(),'{"request_id":"retained"}')
    def test_other_actor_or_expired_or_committed_not_recomputed(self):
        for key,value in [('actor','0014'+'1'*40),('expired',True),('state','committed')]:
            for row in self.rpc.pages[0]['audits']:row['jobs'][0]=dict(job(1),**{key:value})
            self.assertEqual(self.recovery.claim(self.folders[0]),(None,True))
            self.assertFalse((self.folders[0]/'current.json').exists())
    def test_changed_anchor_has_no_durable_effect(self):
        self.rpc.tip='9'*64
        self.assertEqual(self.recovery.claim(self.folders[0]),(None,False))
        self.assertFalse(self.recovery.cursor.exists());self.assertFalse((self.folders[0]/'current.json').exists())
    def test_other_genesis_is_rejected(self):
        self.rpc.pages[0]['state']['genesis']='1'*64
        with self.assertRaises(ValueError):self.recovery.claim(self.folders[0])
    def test_duplicate_and_malformed_ranges_rejected(self):
        for mutate in [lambda p:p['audits'].append(copy.deepcopy(p['audits'][0])),
                       lambda p:p['audits'][0]['jobs'][0]['ranges'][0].update(start='01'),
                       lambda p:p['audits'][0]['jobs'][0]['ranges'][1].update(index=0)]:
            self.rpc.pages[0]=page(0,[1,2]);mutate(self.rpc.pages[0])
            with self.assertRaises(ValueError):self.recovery.claim(self.folders[0])
            self.assertFalse((self.folders[0]/'current.json').exists())
    def test_bounded_cursor_survives_restart_and_wraps(self):
        self.rpc.pages[0]=page(0,range(1,101))
        for row in self.rpc.pages[0]['audits']:row['jobs'][0]['actor']='0014'+'1'*40
        self.rpc.pages[100]=page(100,[101])
        self.assertEqual(self.recovery.claim(self.folders[0]),(None,False))
        fresh=AssignedAuditRecovery(self.worker);a,_=fresh.claim(self.folders[0]);self.assertEqual(a['request_id'],f'{101:064x}')
        self.assertEqual(json.loads(fresh.cursor.read_bytes())['offset'],100)
    def test_own_report_cannot_be_recovered_as_independent(self):
        self.rpc.pages[0]['reports'][0]['miner_script']=ACTOR
        with self.assertRaises(ValueError):self.recovery.claim(self.folders[0])
    def test_core_authorized_self_assignment_recovers_exact_ranges(self):
        self.rpc.pages[0]['reports'][0]['miner_script']=ACTOR
        self.rpc.pages[0]['audits'][0]['jobs'][0]['actor_policy_accepted']=True
        audit,_=self.recovery.claim(self.folders[0])
        self.assertEqual(audit['request_id'],report(1)['request_id'])
        self.assertEqual(audit['ranges'],job(1)['ranges']);self.assertEqual(audit['counts'],[])
        self.assertNotIn('independent',audit)
    def test_policy_denial_and_non_boolean_permission_retain_journals(self):
        for accepted in (False,'true',1):
            self.rpc.pages[0]['audits'][0]['jobs'][0]['actor_policy_accepted']=accepted
            with self.assertRaises(ValueError):self.recovery.claim(self.folders[0])
            self.assertFalse((self.folders[0]/'current.json').exists())
    def legacy(self):
        r=report(1);v=dict(schema='PGN_HORIZON_ASSIGNED_AUDIT_V1',request_id=r['request_id'],
            ticket=r['ticket'],report_root=r['report_root'],slot=0,attempt=0,ranges=job(1)['ranges'],
            counts=[9,17,7,6,11],target_merit_q16=1821634)
        v['answer']=audit_commitment(v['ticket'],0,[p['index']for p in v['ranges']],
                                      [str(n)for n in v['counts']],'a'*64)
        p=self.root/'audit-lanes/0/audits/current.json';p.parent.mkdir(parents=True);p.write_text(json.dumps(v));return p,v
    def test_inactive_legacy_answer_is_migrated_not_treated_as_reserved(self):
        p,v=self.legacy();original=p.read_bytes();a,_=self.recovery.claim(self.folders[0])
        self.assertEqual(a['counts'],v['counts']);self.assertEqual(a['answer'],v['answer'])
        self.assertEqual(a['target_merit_q16'],v['target_merit_q16']);self.assertEqual(p.read_bytes(),original)
        self.assertEqual(a['legacy_answer_recovery']['source'],'audit-lanes/0/audits/current.json')
        self.assertFalse(any(m=='getpgnscientificwork'for m,_ in self.rpc.calls),'Mutable current Gold target must not alter a retained count answer')
    def test_corrupt_retained_answer_is_not_published_or_overwritten(self):
        p,v=self.legacy();v['answer']['commitment']='f'*64;p.write_text(json.dumps(v));before=p.read_bytes()
        with self.assertRaises(ValueError):self.recovery.claim(self.folders[0])
        self.assertEqual(p.read_bytes(),before);self.assertFalse((self.folders[0]/'current.json').exists())
    def test_legacy_range_mismatch_retains_both_originals(self):
        p,v=self.legacy();v['ranges'][0]['end']='1199';p.write_text(json.dumps(v));before=p.read_bytes()
        with self.assertRaises(ValueError):self.recovery.claim(self.folders[0])
        self.assertEqual(p.read_bytes(),before);self.assertFalse((self.folders[0]/'current.json').exists())
if __name__=='__main__':unittest.main()
