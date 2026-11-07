"""Shared preparation routing only; synthetic empty feeds are not coverage proofs."""
from pathlib import Path
import json,sys,unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/engine/shared_composite'),str(ROOT/'src/protocol'),str(ROOT/'tests')]
from horizon_complement_feed import ComplementFeed,ComplementFeedBank
from psp_batch import RecoveryBatch
import horizon_complement_runtime as backend
import test_complement_runtime as runtime_fixture


class FeedBankTests(unittest.TestCase):
    def setUp(self):
        self.fixture=runtime_fixture.ComplementRuntimeTests();self.fixture.setUp();self.addCleanup(self.fixture.doCleanups)
        self.a=self.fixture.feed
        self.start=self.a.batch.start+self.a.batch.length+3000
        self.start-=self.start%30
        self.b=ComplementFeed(RecoveryBatch(self.start,3000,b''),'b'*64,())
        self.bank=ComplementFeedBank((self.b,self.a))

    def test_bank_is_bounded_sorted_and_immutable(self):
        self.assertEqual(self.bank.feeds,(self.a,self.b))
        with self.assertRaises(AttributeError):self.bank.feeds=()
        for value in [(),(self.a,)*65,[self.a],(object(),)]:
            with self.subTest(value=type(value)),self.assertRaises(ValueError):ComplementFeedBank(value)

    def test_overlap_and_duplicate_scope_are_rejected(self):
        for pair in [(self.a,self.a),(self.b,ComplementFeed(RecoveryBatch(self.start+30,3000,b''),'c'*64,()))]:
            with self.assertRaisesRegex(ValueError,'overlap'):ComplementFeedBank(pair)

    def test_selection_preserves_exact_feed_identity(self):
        self.assertIs(self.bank.select(self.start,self.start+3000),self.b)
        self.assertIs(self.bank.select(self.a.batch.start,self.a.batch.start+30),self.a)
        for a,b in [(self.start-1,self.start+30),(self.start,self.start+3001),
                    (self.a.batch.start,self.start+30),(True,30),(self.start,self.start)]:
            with self.subTest(bounds=(a,b)),self.assertRaises(ValueError):self.bank.select(a,b)

    def test_actual_runtime_binds_and_resumes_the_selected_scope(self):
        f=self.fixture
        runtime=backend.ComplementRuntime(f.worker,f.table,self.bank,f.root/'bank-cache',86)
        command=[str(f.worker),'--start',str(self.start),'--length','3000']
        cfg=dict(start=str(self.start),end=str(self.start+3000))
        self.assertEqual(runtime.bind_configuration(cfg)['complement_feed_sha256'],self.b.fingerprint)
        first=runtime.launch(command,0);resumed=runtime.launch(command,30)
        path=lambda args:Path(args[args.index('--composite-page-directory')+1])
        for args,start in [(first,self.start),(resumed,self.start+30)]:
            m=json.loads((path(args)/'manifest.json').read_bytes())
            self.assertEqual(m['feed'],self.b.fingerprint);self.assertEqual(m['owned_start'],str(start))
        self.assertEqual(runtime.launch(command,30),resumed)
        self.assertEqual(runtime.workers(),[(64,f.worker.resolve())])
        with self.assertRaises(ValueError):runtime.bind_configuration(dict(cfg,end=str(self.start+3001)))


if __name__=='__main__':unittest.main()
