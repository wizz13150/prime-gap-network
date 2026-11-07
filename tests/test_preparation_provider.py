"""Plan/source/report coupling. Typed source fixtures do not claim execution.

The producer/queue have separate real-process tests. These tests exercise
identity loss, incomplete source admission and the actual retained-index path.
"""
from pathlib import Path
from contextlib import nullcontext
from types import SimpleNamespace
import copy
import json
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / p) for p in (
    'src/engine/runtime_host', 'src/engine/shared_composite', 'src/protocol', 'tests')]
from horizon_preparation_provider import PreparationProvider, PreparedSourcePlan, _decode
import horizon_preparation_provider as provider_module
from horizon_preparation_scopes import PreparationPlan, PlannedWork, LOW
from horizon_residual_bundle import ResidualBundle
from horizon_preparation_process import PreparationRecoveryRequired
from horizon_complement_feed import prepare, ComplementFeedBank
from horizon_engine_host import EngineService
from pgn_session import SessionStore
from pgn_threshold_policy import pin_range
from test_report_gap_repair import configuration, indexed_report


class PreparationProviderTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.a = (LOW // 30 + 100) * 30
        self.primes = {self.a - 7, self.a + 107, self.a + 299, self.a + 501}
        self.oracle = lambda n: n in self.primes  # Explicit synthetic halo fixture.
        self.provider = PreparationProvider(self.root / 'provider', sys.executable, self.oracle)
        self.work = dict(work_id='a' * 64, start=self.a, end=self.a + 100)

    def request(self):
        return self.provider.request([self.work], halo_limit=20)

    def test_idempotent_durable_request_preserves_plan_and_exact_work(self):
        identity, plan = self.request()
        reopened = PreparationProvider(self.root / 'provider', sys.executable, self.oracle)
        again, other = reopened.request([self.work], halo_limit=20)
        self.assertEqual((again, other), (identity, plan))
        self.assertEqual(len(reopened.queue.entries()), 1)
        self.assertEqual(reopened._input(identity)[0], plan)
        self.assertFalse(plan.document()['network_admitted'])

    def test_deferred_work_keeps_original_bounds_and_source_order(self):
        second = dict(work_id='b' * 64, start=self.a + 300, end=self.a + 500)
        identity, plan = self.provider.request([self.work, second], maximum_positions=200, halo_limit=20)
        self.assertEqual(plan.deferred, (second['work_id'],))
        self.assertEqual(self.provider.deferred(identity), [second])
        next_id, later = self.provider.request(self.provider.deferred(identity), halo_limit=20)
        self.assertNotEqual(next_id, identity)
        self.assertEqual(later.selected[0].work_id, second['work_id'])

    def test_changed_input_or_queue_reference_cannot_be_admitted(self):
        identity, _ = self.request()
        file = self.provider._folder(identity) / 'input.json'
        original = file.read_bytes()
        doc = json.loads(original)
        doc['requests'][0]['end'] = str(self.a + 101)
        file.write_text(json.dumps(doc), encoding='utf-8')
        with self.assertRaises(ValueError):
            self.provider._input(identity)
        file.write_bytes(original)
        file = self.provider._folder(identity) / 'job.json'
        doc = json.loads(file.read_bytes())
        doc['plan'] = 'f' * 64
        file.write_text(json.dumps(doc))
        with self.assertRaises(ValueError):
            self.provider._input(identity)

    def test_partial_source_and_wrong_scopes_produce_no_runtime(self):
        identity, plan = self.request()
        with self.assertRaises(PreparationRecoveryRequired):
            self.provider.prepared(identity, logical_cpu=4)
        wrong = ResidualBundle(10000000, ((self.a, self.a + 100),), ((),), 'a'*64, 'b'*64, ('c'*64,))
        with patch.object(self.provider.queue, 'ready', return_value=(wrong, {})):
            with self.assertRaises(ValueError):
                self.provider.prepared(identity, logical_cpu=4)
        matching = ResidualBundle(10000000, plan.scopes, ((),), 'a'*64, 'b'*64, ('c'*64,))
        with patch.object(self.provider.queue, 'ready', return_value=(matching, {})):
            bound = self.provider.prepared(identity, logical_cpu=4)
        self.assertEqual(bound.feeds.select(self.a, self.a+100).batch.start, plan.scopes[0][0])
        self.assertFalse(bound.bundle.whole95_coverage)

    def test_lost_work_bad_scope_index_and_unproved_sentinels_are_rejected(self):
        identity, plan = self.request()
        doc = plan.document()
        doc['selected'][0]['scope'] = False
        with self.assertRaises(ValueError):
            _decode(doc)
        self.provider.exact_prime = lambda _: False
        with self.assertRaises(ValueError):
            self.provider._input(identity)

    @staticmethod
    def brian_fixture():
        # Known EJ counterexample geometry. The oracle fixture names its two
        # neighbors; independent primality/product evidence lives in the native
        # counterexample corpus, not in this identity-coupling test.
        first = 160696461657079905863
        last = first + 956
        n, p, q = 160696461657079906273, 2142738313, 74995840921
        scope = (first // 30 * 30, last + 1)
        oracle = lambda value: value in (first, last)
        plan = PreparationPlan((PlannedWork('a'*64, first, last, first, last),), (), (scope,))
        bundle = ResidualBundle(10000000, (scope,), (((n,p,q),),), 'a'*64, 'b'*64, ('c'*64,))
        feed = prepare(bundle, 0, oracle)
        source = PreparedSourcePlan(plan, bundle, ComplementFeedBank((feed,)), oracle)
        cfg = configuration(first, last, pin_range(first, last, 954, count_override=954, detail_override=954))
        cfg.update(preseal_required=True, complement_feed_sha256=feed.fingerprint)
        return source, cfg, indexed_report(cfg, [])

    def test_refinement_recovers956_and_rejects_unbound_native_configuration(self):
        source, cfg, report = self.brian_fixture()
        result = source.refine(report, cfg, [])
        self.assertEqual([row['gap'] for row in result.report['body']['best']], [956])
        self.assertEqual(sum(map(int, result.report['body']['counts'])), 1)
        for changed in (dict(cfg, preseal_required=False), dict(cfg, ticket_id='f'*64),
                        dict(cfg, complement_feed_sha256='f'*64)):
            with self.assertRaises(ValueError):
                source.refine(report, changed, [])

    def test_actual_engine_finalizer_reads_complete_retained_index_before_sealing(self):
        source, cfg, report = self.brian_fixture()
        folder = self.root / 'tickets' / cfg['ticket_id']
        store = SessionStore.create(folder, cfg, dict(uuid='NO-HARDWARE-SOURCE-BINDING-TEST'))
        try:
            store.accepted_progress(int(cfg['end']) - int(cfg['start']), 0)
            store.closed('COMPLETE', 0, 0)
        finally:
            store.close()
        service = EngineService.__new__(EngineService)
        service.root = self.root
        service.complement = SimpleNamespace(prepared_source=source)
        service.archives = SimpleNamespace(use_ticket=lambda _: nullcontext())
        work = dict(ticket=cfg['ticket_id'], start=cfg['start'], length=str(int(cfg['end'])-int(cfg['start'])))
        corrected = service.finalize_prepared_report(work, report)
        self.assertEqual(corrected['body']['best'][0]['gap'], 956)
        marker=json.loads((folder/'source-finalization.json').read_bytes())
        self.assertEqual(marker['added'],[[cfg['start'],cfg['end']]])
        self.assertEqual(marker['corrected_report'],corrected)
        acknowledgements=[]
        service.complement.acknowledge_preparation=acknowledgements.append
        service.acknowledge_prepared_source(cfg['ticket_id'])
        self.assertEqual(acknowledgements,[cfg['ticket_id']])
        before=(folder/'source-finalization.json').read_bytes()
        (folder/'source-finalization.json').write_bytes(before+b' ')
        with self.assertRaises(ValueError):service.acknowledge_prepared_source(cfg['ticket_id'])
        (folder/'source-finalization.json').write_bytes(before)
        self.assertFalse((folder/'network-report.json').exists())
        def no_source(*_):raise AssertionError('Finalized source was scanned again')
        service.complement.prepared_source_for=no_source
        self.assertEqual(service.finalize_prepared_report(work,report),corrected)
        registered=[]
        service.complement.bind_configuration=lambda _:dict(cfg)
        service.complement.register_preparation=registered.extend
        with patch('horizon_engine_host.ticket_configuration',return_value=dict(cfg)):
            service.register_prepared_source([dict(controlled_ticket_fixture=True)])
        self.assertEqual(registered,[])
        wrong = dict(work, start=str(int(work['start'])+1))
        with self.assertRaises(ValueError):
            service.finalize_prepared_report(wrong, report)

    def test_complete_label_without_full_progress_is_not_finalizable(self):
        source, cfg, report = self.brian_fixture()
        folder = self.root / 'tickets' / cfg['ticket_id']
        store = SessionStore.create(folder, cfg, dict(uuid='INCOMPLETE-PROGRESS-FAULT-FIXTURE'))
        try:
            store.accepted_progress(1, 0)
            store.closed('COMPLETE', 0, 0)
        finally:
            store.close()
        service = EngineService.__new__(EngineService)
        service.root = self.root
        service.complement = SimpleNamespace(prepared_source=source)
        service.archives = SimpleNamespace(use_ticket=lambda _: nullcontext())
        work = dict(ticket=cfg['ticket_id'], start=cfg['start'], length=str(int(cfg['end'])-int(cfg['start'])))
        with self.assertRaises(ValueError):
            service.finalize_prepared_report(work, report)

    def completed_cache_fixture(self, identity, number=1):
        # Retention/journal contract fixture only. The adoption verifier is
        # mocked explicitly; this does not manufacture a real execution claim.
        plan,job,_=self.provider._input(identity)
        self.provider.queue._update(job,state='ready',attempt=number)
        attempt=self.provider.queue._job(job)/('attempt-'+str(number));(attempt/'output').mkdir(parents=True)
        for name in ['ready.json','producer-receipt.json','output/COMPLETE.json']:
            (attempt/name).write_text(json.dumps(dict(controlled_retention_fixture=True)))
        (attempt/'output/scope-0.factors').write_bytes(b'controlled cached factor bytes')
        bundle=ResidualBundle(10000000,plan.scopes,tuple(()for _ in plan.scopes),'a'*64,'b'*64,tuple('c'*64 for _ in plan.scopes))
        return job,bundle

    def test_completed_fourth_attempt_retires_after_three_cancelled_records(self):
        identity,plan=self.request();job,bundle=self.completed_cache_fixture(identity,4)
        for i in range(1,4):
            folder=self.provider.queue._job(job)/('attempt-'+str(i));folder.mkdir()
            (folder/'cancelled.json').write_bytes(b'controlled retention fixture only')
        self.provider.queue.acknowledge(job)
        with patch.object(provider_module,'run_preparation',return_value=(bundle,dict(controlled_retention_fixture=True)))as adopt:
            record=self.provider.retire(identity)
        self.assertEqual(adopt.call_args.args[1].name,'attempt-4')
        self.assertIn('job/attempt-3/cancelled.json',record['files'])
        self.assertFalse((self.provider.queue.jobs/job).exists())

    def test_terminal_cache_compaction_reopens_capacity_beyond64plans(self):
        for i in range(66):
            identity,plan=self.provider.request([dict(self.work,work_id='%064x'%i)],halo_limit=20)
            job,bundle=self.completed_cache_fixture(identity)
            self.provider.queue.acknowledge(job)
            with patch.object(provider_module,'run_preparation',return_value=(bundle,dict(controlled_retention_fixture=True))):
                record=self.provider.retire(identity)
            self.assertFalse(record['raw_factor_bytes_retained'])
            self.assertFalse((self.provider.plans/identity).exists())
            self.assertFalse((self.provider.queue.jobs/job).exists())
            self.assertEqual(self.provider._input(identity)[0],plan)
        self.assertEqual(list(self.provider.plans.iterdir()),[])
        self.assertEqual(self.provider.queue.entries(),[])

    def test_shared_job_survives_first_plan_retirement(self):
        first,plan=self.request();second,_=self.provider.request([dict(self.work,work_id='b'*64)],halo_limit=20)
        job,bundle=self.completed_cache_fixture(first)
        with patch.object(provider_module,'run_preparation',return_value=(bundle,dict(controlled_retention_fixture=True))):
            self.provider.retire(first)
            self.assertTrue((self.provider.queue.jobs/job).exists())
            self.provider.queue.acknowledge(job)
            self.provider.retire(second)
        self.assertFalse((self.provider.queue.jobs/job).exists())
        self.assertEqual(self.provider._input(first)[0],plan)

    def test_provider_restart_recovers_partial_retirement_from_compact_provenance(self):
        identity,plan=self.request();job,bundle=self.completed_cache_fixture(identity)
        self.provider.queue.acknowledge(job)
        unlink=Path.unlink;count=[0]
        def interrupted(path,*args,**kwargs):
            if self.provider.plans/identity in path.parents:
                count[0]+=1
                if count[0]==2:raise OSError('Controlled cache retirement crash')
            return unlink(path,*args,**kwargs)
        with patch.object(provider_module,'run_preparation',return_value=(bundle,{})),patch.object(Path,'unlink',new=interrupted):
            with self.assertRaises(OSError):self.provider.retire(identity)
        self.assertTrue(self.provider.retention.intent.exists())
        restarted=PreparationProvider(self.provider.root,sys.executable,self.oracle)
        self.assertFalse(restarted.retention.intent.exists())
        self.assertEqual(restarted._input(identity)[0],plan)
        self.assertFalse((restarted.queue.jobs/job).exists())


if __name__ == '__main__':
    unittest.main()
