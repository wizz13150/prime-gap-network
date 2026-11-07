"""Planner invariants with controlled oracles, not claims about synthetic primes."""
from pathlib import Path
import copy,sys,unittest
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'src/engine/runtime_host'))
from horizon_preparation_scopes import plan_preparation,PreparationHaloPending,LOW


class PreparationScopesTests(unittest.TestCase):
    def setUp(self):self.start=(LOW//30+100)*30
    def work(self,i,a,b):return dict(work_id=('%064x'%i),start=a,end=b)
    def test_halos_and_class30_padding_are_charged_to_budget(self):
        a=self.start;work=[self.work(1,a,a+100)];original=copy.deepcopy(work)
        oracle=lambda n:n in(a-7,a+107)
        p=plan_preparation(work,oracle,halo_limit=20,maximum_positions=1000)
        self.assertEqual(work,original);self.assertEqual(p.scopes,((a-30,a+108),))
        self.assertEqual(p.covered_positions,138);self.assertEqual(p.selected[0].left_prime,a-7)
        self.assertEqual(p.selected[0].right_prime,a+107)
        small=plan_preparation(work,oracle,halo_limit=20,maximum_positions=100)
        self.assertEqual(small.selected,());self.assertEqual(small.deferred,(work[0]['work_id'],))

    def test_shared_and_adjacent_halos_merge_without_reordering_work(self):
        a=self.start;work=[self.work(2,a+80,a+180),self.work(1,a,a+100)]
        p=plan_preparation(work,lambda n:n in(a-7,a+79,a+107,a+181),halo_limit=20)
        self.assertEqual([w.work_id for w in p.selected],[x['work_id']for x in work])
        self.assertEqual(p.scopes,((a-30,a+182),));self.assertEqual(p.covered_positions,212)

    def test_deferred_work_is_retained_and_smaller_work_can_use_spare_capacity(self):
        a=self.start;work=[self.work(1,a,a+100),self.work(2,a+300,a+500),self.work(3,a+700,a+730)]
        allowed={a-1,a+101,a+299,a+501,a+699,a+731}
        p=plan_preparation(work,lambda n:n in allowed,halo_limit=3,maximum_positions=220)
        self.assertEqual([w.work_id for w in p.selected],[work[0]['work_id'],work[2]['work_id']])
        self.assertEqual(p.deferred,(work[1]['work_id'],));self.assertLessEqual(p.covered_positions,220)

    def test_bad_ids_bounds_and_unbounded_inputs_reject_before_oracle(self):
        a=self.start;good=self.work(1,a,a+30)
        def forbidden(_):raise AssertionError('Malformed work reached prime oracle')
        for value in [[],[good]*65,[good,good],[dict(good,start=True)],
                      [dict(good,work_id='bad')],[dict(good,end=10**21+1)],iter([good])]:
            with self.subTest(value=type(value)),self.assertRaises(ValueError):plan_preparation(value,forbidden)
        for field,value in [('maximum_positions',True),('maximum_positions',10**15+1),('halo_limit',-1)]:
            with self.assertRaises(ValueError):plan_preparation([good],forbidden,**{field:value})

    def test_missing_or_nonexact_halo_never_produces_ready_work(self):
        work=[self.work(1,self.start,self.start+30)]
        with self.assertRaises(PreparationHaloPending):plan_preparation(work,lambda _:False,halo_limit=8)
        with self.assertRaises(ValueError):plan_preparation(work,lambda _:1,halo_limit=8)
        edge=self.work(2,10**21-30,10**21)
        with self.assertRaises(PreparationHaloPending):plan_preparation([edge],lambda _:True,halo_limit=8)

    def test_durable_document_preserves_integer_precision_and_is_not_admission(self):
        a=self.start;work=[self.work(1,a,a+100)]
        p=plan_preparation(work,lambda n:n in(a-7,a+107),halo_limit=20)
        d=p.document();self.assertEqual(d['selected'][0]['start'],str(a))
        self.assertEqual(d['scopes'][0],dict(start=str(a-30),end=str(a+108)))
        self.assertEqual(d['selected'][0]['scope'],0);self.assertEqual(d['covered_positions'],'138')
        self.assertFalse(d['producer_executed']);self.assertFalse(d['network_admitted'])
        d['selected'].clear();self.assertEqual(len(p.document()['selected']),1)


if __name__=='__main__':unittest.main()
