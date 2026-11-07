"""Actual publication method, immutable local reports, controlled branch changes."""
from pathlib import Path
from types import SimpleNamespace
import copy,json,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
from horizon_network_worker import NetworkWorker,save
from report_commitment import seal_report


class PublicationBindingTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        self.work=json.loads((ROOT/'tests/fixtures/network/issued-work.json').read_bytes())
        raw=json.loads((ROOT/'tests/fixtures/network/computed-report.json').read_bytes())
        self.sealed=seal_report(raw,self.work,nonce='d'*64)
        self.file=self.root/'tickets'/self.work['ticket']/'network-report.json';self.file.parent.mkdir(parents=True)
        save(self.file,self.sealed);self.original=self.file.read_bytes()
        self.calls=[];self.writes=[];self.marked=[];self.mutate_first=None;self.mutate_second=None;self.confirmed=[]
        outer=self
        class Rpc:
            def call(self,method,args=None,**_):
                outer.calls.append(method)
                if method=='getpgnreports':return copy.deepcopy(outer.confirmed)
                if method=='signmessage':return 'fixture-signature'
                if method=='getpgnreportcommit':
                    v=dict(genesis=outer.work['genesis'],request_id=outer.work['request_id'],ticket=outer.work['ticket'],
                        report_root=outer.sealed['report_root'],parent='a'*64,report_id='b'*64,challenge='controlled',accepted=False)
                    mutate=outer.mutate_second if len(args)==3 else outer.mutate_first
                    if mutate:mutate(v)
                    if len(args)==3:v['envelope']='01'
                    return v
                raise AssertionError(method)
        self.worker=NetworkWorker.__new__(NetworkWorker);self.worker.root=self.root;self.worker.rpc=Rpc()
        self.worker.address=lambda:'fixture-miner';self.worker.mark_report_confirmed=lambda *a:self.marked.append(a)
        def ledger(envelopes):
            self.writes.append(envelopes);self.confirmed=[dict(request_id=self.work['request_id'],
                ticket=self.work['ticket'],report_root=self.sealed['report_root'])]
        self.worker.ledger=ledger

    def test_exact_proposal_is_signed_published_and_acknowledged_once(self):
        self.worker.submit(None,work=self.work)
        self.assertEqual(self.calls.count('signmessage'),1);self.assertEqual(len(self.writes),1)
        self.assertEqual(len(self.marked),1);self.assertEqual(self.file.read_bytes(),self.original)

    def test_already_confirmed_report_recovers_lost_ack_without_resigning(self):
        self.confirmed=[dict(request_id=self.work['request_id'],ticket=self.work['ticket'],report_root=self.sealed['report_root'])]
        self.worker.submit(None,work=self.work)
        self.assertEqual(self.calls,['getpgnreports']);self.assertFalse(self.writes);self.assertEqual(len(self.marked),1)

    def test_changed_ticket_or_context_is_rejected_before_signing_or_publication(self):
        cases=dict(genesis='e'*64,request_id='e'*64,ticket='e'*64,report_root='e'*64,accepted=True,
                   parent='bad',report_id='bad',challenge='')
        for key,value in cases.items():
            with self.subTest(field=key):
                self.calls.clear();self.mutate_first=lambda v,k=key,x=value:v.__setitem__(k,x)
                with self.assertRaises(ValueError):self.worker.submit(None,work=self.work)
                self.assertNotIn('signmessage',self.calls);self.assertFalse(self.writes)
                self.assertEqual(self.file.read_bytes(),self.original)

    def test_branch_or_challenge_change_during_signing_retains_report_without_ledger_write(self):
        for key in ('parent','report_id','challenge'):
            with self.subTest(field=key):
                self.calls.clear();self.mutate_second=lambda v,k=key:v.__setitem__(k,'e'*64)
                with self.assertRaises(RuntimeError):self.worker.submit(None,work=self.work)
                self.assertEqual(self.calls.count('signmessage'),1);self.assertFalse(self.writes)
                self.assertFalse(self.marked);self.assertEqual(self.file.read_bytes(),self.original)

    def test_corrupt_or_misbound_local_report_never_reaches_rpc(self):
        for field in ('hash','context'):
            with self.subTest(field=field):
                value=copy.deepcopy(self.sealed)
                if field=='hash':value['body']['computed']['counts'][0]='999'
                else:
                    from report_commitment import digest
                    value['body']['issue_context']='e'*64;value['report_root']=digest(value['body'])
                save(self.file,value);saved=self.file.read_bytes()
                with self.assertRaises(ValueError):self.worker.submit(None,work=self.work)
                self.assertFalse(self.calls);self.assertFalse(self.writes);self.assertEqual(self.file.read_bytes(),saved)

    def test_current_confirmation_for_other_ticket_is_not_adopted(self):
        self.confirmed=[dict(request_id=self.work['request_id'],ticket='e'*64,report_root=self.sealed['report_root'])]
        with self.assertRaises(ValueError):self.worker.submit(None,work=self.work)
        self.assertEqual(self.calls,['getpgnreports']);self.assertFalse(self.writes);self.assertFalse(self.marked)


if __name__=='__main__':unittest.main()
