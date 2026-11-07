"""Deferred source routing and audit count barriers. No GPU execution."""
from pathlib import Path
from types import SimpleNamespace
import copy
import os
import json
import sys
import tempfile
import threading
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/p)for p in ['src/engine/runtime_host','src/engine/shared_composite','src/protocol','tests']]
import horizon_complement_runtime as runtime
import test_complement_runtime as native_fixture
import test_preparation_provider as provider_fixture
from horizon_network_audits import NetworkAudits
from horizon_preparation_process import PreparationRecoveryRequired
from test_report_gap_repair import indexed_report
from horizon_engine_host import ticket_configuration
from report_commitment import digest
from horizon_engine_capabilities import qualified_presentation_identity
from pgn_native_contract import PROTOCOL


class DeferredRuntimeTests(unittest.TestCase):
    def setUp(self):
        self.fixture=native_fixture.ComplementRuntimeTests()
        self.fixture.setUp();self.addCleanup(self.fixture.doCleanups)
        self.calls=[]
        def pending(configuration):
            self.calls.append(configuration)
            raise PreparationRecoveryRequired('Source is still being prepared')
        self.backend=runtime.DeferredComplementRuntime(self.fixture.worker,self.fixture.table,86,pending)

    def test_dense_piece_bound_does_not_widen_inline_residual_page_bound(self):
        self.assertEqual(self.backend.maximum_piece,4*10**12)
        self.assertEqual(runtime.ComplementRuntime.maximum_piece,10**12)

    def test_dense_launch_never_waits_for_residual_or_invents_factor_pages(self):
        f=self.fixture;cfg=dict(start=f.command[2],end=str(int(f.command[2])+1000))
        bound=self.backend.bind_configuration(cfg)
        self.assertTrue(bound['preseal_required'])
        self.assertNotIn('complement_feed_sha256',bound)
        for command in [self.backend.plan(f.command),self.backend.launch(f.command,0),self.backend.launch(f.command,30)]:
            self.assertIn('--composite-small-factors',command)
            self.assertNotIn('--composite-page-directory',command)
        self.assertEqual(self.calls,[])
        with self.assertRaises(PreparationRecoveryRequired):self.backend.prepared_source_for(bound)
        self.assertEqual(len(self.calls),1)

    def test_deferred_primary_is_repaired_under_its_qualified_dense_contract(self):
        source,cfg,report=provider_fixture.PreparationProviderTests.brian_fixture()
        bound=self.backend.bind_configuration(cfg)
        bound.pop('complement_feed_sha256',None)
        self.assertEqual(source.refine(report,bound,[]).report['body']['best'][0]['gap'],956)
        for key in ['complement_primary_image_sha256','complement_primary_source_sha256','complement_canonical_table_sha256']:
            with self.subTest(key=key),self.assertRaises(ValueError):source.refine(report,dict(bound,**{key:'f'*64}),[])

    def test_unsupported_domain_progress_or_changed_image_is_rejected(self):
        f=self.fixture
        for cfg in [dict(start='3',end='30'),dict(start=str(1<<66),end=str(10**21+1))]:
            with self.assertRaises(ValueError):self.backend.bind_configuration(cfg)
        for progress in [True,-1,1000]:
            with self.assertRaises(ValueError):self.backend.launch(f.command,progress)
        f.hashes[f.worker.resolve()]='f'*64
        with self.assertRaises(ValueError):self.backend.launch(f.command,0)

    def test_readonly_presentation_requires_the_same_deferred_primary_contract(self):
        source,cfg,report=provider_fixture.PreparationProviderTests.brian_fixture()
        cfg=self.backend.bind_configuration(cfg);cfg.pop('complement_feed_sha256',None)
        identity=dict(worker_sha256=self.backend.image_sha,worker_source_sha256=self.backend.source_sha,
                      profile_k=64,native_protocol=PROTOCOL)
        # These artifacts are qualified for Windows only; Linux must refuse them.
        self.assertEqual(qualified_presentation_identity(cfg,identity),os.name=='nt')
        for changed in [dict(cfg,preseal_required=False),dict(cfg,complement_canonical_table_sha256='f'*64),
                        dict(cfg,complement_primary_source_sha256='f'*64)]:
            self.assertFalse(qualified_presentation_identity(changed,identity))


class AuditFinalizationBarrierTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name);self.running=threading.Event();self.running.set()
        self.source=SimpleNamespace(run_ticket=self.computed,finalize_report=self.finalize)
        self.worker=SimpleNamespace(root=self.root,running=self.running,source=self.source,args=SimpleNamespace(piece_limit=10**12))
        self.audit=NetworkAudits(self.worker,before_commit=lambda _:False)
        a=160696461657079905863;b=a+956
        self.input=dict(request_id='b'*64,ticket='c'*64,report_root='d'*64,slot=0,attempt=0,
                        ranges=[dict(index=0,start=str(a),end=str(b))],counts=[],target_merit_q16=29*65536)
        (self.audit.folder/'current.json').write_text(json.dumps(self.input))
        self.audit.state=lambda _:{'jobs':[dict(slot=0,attempt=0,state='assigned')]}
        self.calls=0

    def computed(self,job,_):
        return indexed_report(ticket_configuration(job),[])

    def finalize(self,work,raw):
        # Synthetic corrected count exercises ordering only. Arithmetic repair
        # is tested independently, including the actual retained200T corpus.
        self.calls+=1;corrected=copy.deepcopy(raw);corrected['body']['counts'][0]='7'
        corrected['sha256']=digest(corrected['body']);return corrected

    def test_only_finalized_counts_reach_the_audit_journal(self):
        self.assertEqual(self.audit.process_one(),'paused')
        saved=json.loads((self.audit.folder/'current.json').read_bytes())
        self.assertEqual(saved['counts'],[7]);self.assertEqual(self.calls,1)
        self.assertNotIn('answer',saved)

    def test_pending_source_does_not_append_or_commit_raw_counts(self):
        def pending(*_):raise PreparationRecoveryRequired('Controlled source pending')
        self.source.finalize_report=pending
        with self.assertRaises(PreparationRecoveryRequired):self.audit.process_one()
        saved=json.loads((self.audit.folder/'current.json').read_bytes())
        self.assertEqual(saved['counts'],[]);self.assertNotIn('answer',saved)

    def test_pause_does_not_invoke_finalizer(self):
        self.source.run_ticket=lambda *_:dict(state='PAUSED')
        self.assertEqual(self.audit.process_one(),'paused');self.assertEqual(self.calls,0)

    def test_all_five_issued_partitions_are_registered_before_first_compute(self):
        a=int(self.input['ranges'][0]['start'])
        self.input['ranges']=[dict(index=i,start=str(a+i*10000),end=str(a+i*10000+956))for i in range(5)]
        (self.audit.folder/'current.json').write_text(json.dumps(self.input))
        registered=[]
        self.source.register_preparation=lambda jobs:registered.extend(jobs)
        original=self.source.run_ticket
        def compute(job,limit):
            self.assertEqual(len(registered),5)
            self.assertIn(job,registered)
            return original(job,limit)
        self.source.run_ticket=compute
        self.assertEqual(self.audit.process_one(),'paused')
        self.assertEqual(json.loads((self.audit.folder/'current.json').read_bytes())['counts'],[7]*5)

    def test_source_acknowledgement_replays_after_durable_count_without_gpu_recomputation(self):
        calls=[]
        def acknowledge(identity):
            saved=json.loads((self.audit.folder/'current.json').read_bytes())
            self.assertEqual(saved['counts'],[7]);calls.append(identity)
            if len(calls)==1:raise OSError('Controlled crash after count before source release')
        self.source.acknowledge_preparation=acknowledge
        with self.assertRaises(OSError):self.audit.process_one()
        self.source.run_ticket=lambda *_:(_ for _ in()).throw(AssertionError('Already finalized partition was recomputed'))
        self.assertEqual(self.audit.process_one(),'paused')
        self.assertEqual(len(calls),2);self.assertEqual(calls[0],calls[1])


if __name__=='__main__':unittest.main()
