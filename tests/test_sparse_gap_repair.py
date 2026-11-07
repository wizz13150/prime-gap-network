"""Independent complete sieve oracle, ownership/halo and atomic refusal cases."""
from pathlib import Path
from dataclasses import FrozenInstanceError
import itertools,sys,tempfile,unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'src/engine/shared_composite'))
import sparse_gap_repair as repair

def sieve(limit):
    flags=[True]*(limit+1);flags[:2]=[False,False]
    for p in range(2,limit+1):
        if flags[p]:
            for n in range(p*p,limit+1,p):flags[n]=False
    return flags

FLAGS=sieve(500)
PRIMES=[n for n in range(5,500)if FLAGS[n]]
def proof(n):return FLAGS[n]
def factors(n):
    for p in range(2,n):
        if n%p==0:return(n,p,n//p)
    raise AssertionError('Fixture is not composite')
def gaps(values,a,b,minimum):
    return tuple((lo,hi)for lo,hi in zip(values,values[1:])if a<=lo<b and hi-lo>=minimum)

class SparseRepairTests(unittest.TestCase):
    def test_exhaustive_false_separator_subsets_against_independent_complete_sieve(self):
        extras=[121,143,169,187,209,221,247,253,289,299]
        self.assertTrue(all(not FLAGS[n]for n in extras))
        comparisons=0
        for bits in itertools.product((False,True),repeat=len(extras)):
            false=[n for n,present in zip(extras,bits)if present]
            accepted=sorted(PRIMES+false)
            for a,b in[(101,311),(102,310)]:
                for minimum in(2,14,24):
                    old=gaps(accepted,a,b,minimum);expected=gaps(PRIMES,a,b,minimum)
                    result=repair.repair(old,list(map(factors,false)),a,b,minimum,5,499,proof)
                    self.assertEqual(result.gaps,expected,(false,a,b,minimum))
                    self.assertEqual(set(result.added),set(expected)-set(old))
                    self.assertEqual(set(result.removed),set(old)-set(expected))
                    # Integer interval boundaries may create uneven bin widths.
                    for bins in(1,100):
                        got=[0]*bins;want=[0]*bins
                        bounds=[a+(b-a)*i//bins for i in range(bins+1)]
                        for lo,hi in result.gaps:
                            got[next(i for i in range(bins)if bounds[i]<=lo<bounds[i+1])]+=1
                        for lo,hi in expected:
                            want[next(i for i in range(bins)if bounds[i]<=lo<bounds[i+1])]+=1
                        self.assertEqual(got,want)
                    comparisons+=1
        self.assertEqual(comparisons,6144)

    def test_multiple_false_boundaries_coalesce_once_and_repair_is_idempotent(self):
        facts=[(25,5,5),(27,3,9),(25,5,5)]
        result=repair.repair([(23,25),(25,27),(27,29)],facts,23,29,2,19,31,proof)
        self.assertEqual(result.gaps,((23,29),));self.assertEqual(result.reconstructed,((23,29),))
        again=repair.repair(result.gaps,facts,23,29,2,19,31,proof)
        self.assertEqual(again.gaps,result.gaps);self.assertFalse(again.added or again.removed)
        self.assertFalse(result.complete_psp_coverage)
        with self.assertRaises((FrozenInstanceError,AttributeError)):result.complete_psp_coverage=True

    def test_false_boundary_beyond_owned_end_recovers_owned_true_gap(self):
        result=repair.repair([],[(25,5,5),(27,3,9)],23,25,6,19,31,proof)
        self.assertEqual(result.gaps,((23,29),))
        with self.assertRaises(ValueError):repair.repair([],[(25,5,5)],23,25,6,19,25,proof)

    def test_true_lower_prime_outside_ownership_removes_wrong_fragments(self):
        result=repair.repair([(25,27),(27,29)],[(25,5,5),(27,3,9)],25,29,2,19,31,proof)
        self.assertEqual(result.gaps,());self.assertEqual(len(result.removed),2)

    def test_budget_refusal_is_atomic_and_does_not_change_inputs(self):
        old=[(23,25),(25,29)];facts=[(25,5,5)]
        with self.assertRaises(repair.RepairBudgetExceeded):
            repair.repair(old,facts,23,29,2,19,31,proof,maximum_oracle_calls=3)
        self.assertEqual(old,[(23,25),(25,29)]);self.assertEqual(facts,[(25,5,5)])

    def test_inconsistent_primary_gap_cannot_cross_a_proved_prime(self):
        with self.assertRaises(ValueError):repair.repair([(23,31)],[(25,5,5)],23,29,2,19,37,proof)

    def test_invalid_factor_coverage_integer_or_oracle_rejected(self):
        cases=[dict(factors=[(25,5,7)]),dict(factors=[(31,1,31)]),dict(factors=[(25,True,25)]),
               dict(factors=[(49,7,7)]),dict(covered_start=20),dict(covered_end=30),dict(owned_start=True),
               dict(primary=[(23,29),(23,29)]),dict(primary=[(23,27),(25,29)]),dict(exact_prime=lambda n:1)]
        for case in cases:
            args=dict(primary=[],factors=[(25,5,5)],owned_start=23,owned_end=29,minimum_gap=2,covered_start=19,covered_end=31,exact_prime=proof)
            args.update(case)
            with self.subTest(case=case),self.assertRaises(ValueError):repair.repair(**args)

    def test_item_envelope_stops_without_consuming_unbounded_iterator(self):
        consumed=[]
        def endless():
            for i in range(100):consumed.append(i);yield(25,5,5)
        with patch.object(repair,'MAX_ITEMS',2),self.assertRaises(repair.RepairBudgetExceeded):
            repair.repair([],endless(),23,29,2,19,31,proof)
        self.assertEqual(len(consumed),3)

    def test_actual_session_uneven_bin_edge_uses_interval_ownership(self):
        # Synthetic geometry only: no primality, mining or admission claim.
        sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
        from pgn_session import SessionStore
        from pgn_native_contract import fields
        a=1<<66;b=a+101
        cfg=dict(start=str(a),end=str(b),bins=100,**fields(a,b,954))
        with tempfile.TemporaryDirectory()as folder:
            store=SessionStore.create(folder,cfg,{'uuid':'NO-HARDWARE-BIN-GEOMETRY'})
            try:
                self.assertTrue(store.record_gap(a+33,a+1033,'SYNTHETIC_PROJECTION_GEOMETRY'))
                store.accepted_progress(101,0);expected=[0]*100;expected[33]=1
                self.assertEqual(store.gaps.policy_bin_counts('count',b),expected)
                self.assertEqual((33*100)//101,32)  # This shortcut is wrong here.
                sequence=store.sequence
            finally:store.close()
            store=SessionStore.load(folder)
            try:
                self.assertEqual(store.gaps.policy_bin_counts('count',b),expected)
                self.assertEqual(store.sequence,sequence)
            finally:store.close()

if __name__=='__main__':unittest.main()
