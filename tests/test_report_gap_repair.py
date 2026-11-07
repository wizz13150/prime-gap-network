"""Pre-seal repair against real SessionStore projections, with synthetic geometry."""
from pathlib import Path
import copy,hashlib,json,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/shared_composite'),str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
from report_gap_repair import prepare
from pgn_session import SessionStore
from pgn_native_contract import fields
from pgn_threshold_policy import pin_range,verify_pin
from report_commitment import digest


def configuration(a,b,pin=None):
    pin=pin_range(a,b,954,count_override=954,detail_override=1200)if pin is None else pin
    return dict(start=str(a),end=str(b),bins=100,ticket_id='a'*64,**fields(a,b,pin=pin))


def indexed_report(cfg,pairs):
    """Use the production index/projector, rather than mirror repair projection."""
    with tempfile.TemporaryDirectory()as path:
        store=SessionStore.create(path,cfg,{'uuid':'NO-HARDWARE-REPORT-GEOMETRY'})
        try:
            for lo,hi in pairs:store.record_gap(lo,hi,'SYNTHETIC_REPORT_GEOMETRY')
            end=int(cfg['end']);store.accepted_progress(end-int(cfg['start']),0)
            body=dict(schema='PGN_HORIZON_COMPUTED_REPORT_V1',ticket_id=cfg['ticket_id'],
                start=cfg['start'],end=cfg['end'],positions=str(end-int(cfg['start'])),policy_sha256=cfg['threshold_policy']['sha256'],
                counts=[str(n)for n in store.gaps.policy_bin_counts('count',end)],
                best=[dict(gap=g,lower_prime=str(lo),upper_prime=str(lo+g))for g,_,lo in store.gaps.policy_top('detail',end,100)])
            return dict(body=body,sha256=digest(body),network_accepted=False)
        finally:store.close()


class ReportRepairTests(unittest.TestCase):
    def setUp(self):
        # Synthetic primes for projection/ownership logic only; no arithmetic claim.
        self.a=(1<<67)+1
        while self.a%30!=1:self.a+=2
        self.b=self.a+7001;self.first=self.a-30;self.last=self.a+10020
        self.false=self.a+1004
        self.factors=[(self.false,5,self.false//5)]
        self.primes={self.first,self.a,self.a+2010,self.a+4020,self.a+6030,self.last}
        self.oracle=lambda n:n in self.primes
        self.true=[(self.a,self.a+2010),(self.a+2010,self.a+4020),(self.a+4020,self.a+6030),(self.a+6030,self.last)]
        self.old=[(self.a,self.false),(self.false,self.a+2010),*self.true[1:]]
        self.cfg=configuration(self.a,self.b)

    def check_repair(self,cfg,old,new,facts=None):
        source=indexed_report(cfg,old);saved=copy.deepcopy(source)
        result=prepare(source,cfg,old,self.factors if facts is None else facts,self.first,self.last,self.oracle)
        self.assertEqual(result.report,indexed_report(cfg,new))
        self.assertEqual(source,saved);self.assertEqual(result.source_sha256,source['sha256'])
        self.assertFalse(result.complete_psp_coverage)
        changed=result.report;changed['body']['counts'][0]='999';self.assertNotEqual(changed,result.report)
        return result

    def test_false_separator_updates_counts_and_top_detail_under_pinned_roles(self):
        r=self.check_repair(self.cfg,self.old,self.true)
        self.assertEqual(len(r.removed),2);self.assertEqual(r.added,(self.true[0],))
        self.assertEqual(sum(map(int,r.report['body']['counts'])),4)

    def test_crossing_policy_cut_uses_reconstructed_lower_prime(self):
        pin=copy.deepcopy(self.cfg['threshold_policy']);left=pin['segments'][0];cut=self.a+1002
        left['end']=str(cut);right=dict(left,start=str(cut),end=str(self.b),count_gap=1200,detail_gap=1600)
        pin['segments']=[left,right]
        body={k:v for k,v in pin.items()if k!='sha256'}
        pin['sha256']=hashlib.sha256(json.dumps(body,sort_keys=True,separators=(',',':')).encode()).hexdigest().upper()
        verify_pin(pin,self.a,self.b)
        self.check_repair(configuration(self.a,self.b,pin),self.old,self.true)

    def test_exact_uneven100_bins_and_owned_upper_halo(self):
        # 7001 positions cannot be assigned with the rounded proportion shortcut.
        r=self.check_repair(self.cfg,self.old,self.true)
        expected=[0]*100
        bounds=[self.a+(self.b-self.a)*i//100 for i in range(101)]
        for lo,_ in self.true:expected[next(i for i in range(100)if bounds[i]<=lo<bounds[i+1])]+=1
        self.assertEqual(list(map(int,r.report['body']['counts'])),expected)
        self.assertGreater(self.true[-1][1],self.b)

    def test_primary_best100_is_not_the_full_index(self):
        pairs=[(self.a+3000*i,self.a+3000*i+1200)for i in range(130)]
        cfg=configuration(self.a,self.a+400000);report=indexed_report(cfg,pairs)
        self.assertEqual(len(report['body']['best']),100)
        best=[(int(g['lower_prime']),int(g['upper_prime']))for g in report['body']['best']]
        with self.assertRaisesRegex(ValueError,'incomplete'):
            prepare(report,cfg,best,[],self.first,self.a+500000,lambda n:True)

    def test_bad_digest_pin_and_stale_primary_do_not_produce_output(self):
        report=indexed_report(self.cfg,self.old)
        for what in ['digest','pin','index']:
            r=copy.deepcopy(report);cfg=copy.deepcopy(self.cfg);pairs=list(self.old)
            if what=='digest':r['sha256']='0'*64
            if what=='pin':cfg['threshold_policy']['sha256']='0'*64
            if what=='index':pairs=pairs[:-1]
            with self.subTest(what=what),self.assertRaises(ValueError):
                prepare(r,cfg,pairs,self.factors,self.first,self.last,self.oracle)

    def test_no_false_boundaries_is_identity_and_repeat_is_idempotent(self):
        r=self.check_repair(self.cfg,self.true,self.true,[])
        second=prepare(r.report,self.cfg,self.true,self.factors,self.first,self.last,self.oracle)
        self.assertEqual(second.report,r.report);self.assertFalse(second.added or second.removed)

    def test_invalid_factor_is_rejected_without_mutating_input(self):
        old=indexed_report(self.cfg,self.old);saved=copy.deepcopy(old)
        with self.assertRaises(ValueError):prepare(old,self.cfg,self.old,[(self.false,5,7)],self.first,self.last,self.oracle)
        self.assertEqual(old,saved)


if __name__=='__main__':unittest.main()
