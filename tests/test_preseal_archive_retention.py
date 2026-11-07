"""Completed source indexes survive asynchronous finalization and restarts."""
from pathlib import Path
from contextlib import nullcontext
from types import SimpleNamespace
import copy,hashlib,json,sys,threading,unittest
from unittest.mock import Mock,patch
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol'),str(ROOT/'tests')]
import test_ticket_archive as fixture
import horizon_ticket_archive as archive
from horizon_engine_host import EngineService
from horizon_preseal_queue import PresealQueue
from horizon_report_outbox import ReportOutbox
from report_commitment import digest


class PresealArchiveTests(unittest.TestCase):
    def setUp(self):
        self.f=fixture.TicketArchiveTests();self.f.setUp();self.addCleanup(self.f.tearDown)
        self.folder=self.f.folder;self.root=self.folder.parent.parent
        self.f.cfg['preseal_required']=True
        self.f.original=archive.canonical(self.f.meta)
        (self.folder/'session.json').write_bytes(self.f.original)
        self.work=dict(ticket=self.folder.name,request_id='b'*64,work_id='c'*64,genesis='d'*64,
            start=self.f.cfg['start'],length='200000000000000',target_numerator='29',target_denominator='1')
        self.queue=PresealQueue(self.root);self.outbox=ReportOutbox(self.root)

    def compact(self):return self.f.compact()

    def queue_input(self):self.queue.put(self.work,self.f.report)

    def test_complete_before_callback_and_restart_sweep_retains_full_index(self):
        # No preseal file exists yet: EngineService has returned COMPLETE but
        # MiningFlow's completion callback has not queued the report.
        for _ in range(2):
            outcome=self.compact();self.assertFalse(outcome['compacted'])
            self.assertIn('finalization pending',outcome['reason']);self.f.intact()

    def test_slow_finalizer_never_exposes_source_to_archive(self):
        self.queue_input();entered=threading.Event();release=threading.Event();errors=[]
        def finalize(work,raw):entered.set();self.assertTrue(release.wait(3));return raw
        def pump():
            try:self.queue.pump(finalize,self.outbox)
            except BaseException as exc:errors.append(exc)
        thread=threading.Thread(target=pump);thread.start()
        try:
            self.assertTrue(entered.wait(2));self.assertFalse(self.compact()['compacted']);self.f.intact()
        finally:release.set();thread.join(3)
        self.assertFalse(thread.is_alive());self.assertEqual(errors,[])
        self.assertTrue(self.compact()['compacted'])

    def test_finalized_but_not_sealed_is_retained_after_shutdown(self):
        self.queue_input();stop=threading.Event()
        def finalize(work,raw):stop.set();return raw
        with self.assertRaises(InterruptedError):self.queue.pump(finalize,self.outbox,cancelled=stop.is_set)
        self.assertTrue((self.folder/'preseal-final.json').exists())
        self.assertFalse(self.compact()['compacted']);self.f.intact()
        reloaded=PresealQueue(self.root)
        def no_recompute(*_):raise AssertionError('Restart discarded durable finalization')
        self.assertTrue(reloaded.pump(no_recompute,self.outbox))
        self.assertTrue(self.compact()['compacted'])

    def test_changed_projection_retains_full_index_after_durable_seal(self):
        self.queue_input()
        def repair(work,raw):
            raw['body']['counts'][0]='4';raw['sha256']=digest(raw['body']);return raw
        self.assertTrue(self.queue.pump(repair,self.outbox));self.assertEqual(self.queue.entries(),[])
        outcome=self.compact();self.assertFalse(outcome['compacted'])
        self.assertIn('archival reconciliation',outcome['reason']);self.f.intact()

    def test_corrupt_input_binding_cannot_release_retention(self):
        self.queue_input();self.queue.pump(lambda w,r:r,self.outbox)
        path=self.folder/'preseal-final.json';body=json.loads(path.read_bytes());body['input_sha256']='0'*64
        path.write_text(json.dumps(body))
        with self.assertRaisesRegex(ValueError,'input binding'):self.compact()
        self.f.intact()

    def test_wrong_sealed_request_cannot_release_retention(self):
        self.queue_input();self.queue.pump(lambda w,r:r,self.outbox)
        path=self.folder/'network-report.json';body=json.loads(path.read_bytes());body['body']['request_id']='0'*64
        body['report_root']=digest(body['body']);path.write_text(json.dumps(body))
        with self.assertRaises(ValueError):self.compact()
        self.f.intact()

    def test_invalid_flag_cannot_disable_retention(self):
        self.f.cfg['preseal_required']=0
        with self.assertRaisesRegex(ValueError,'retention policy'):self.compact()
        self.f.intact()

    def test_queue_readers_wait_for_the_inflight_atomic_publication(self):
        self.queue_input()
        for owner,read in ((self.queue,self.queue.entries),(self.outbox,self.outbox._read)):
            started=threading.Event();returned=threading.Event();errors=[]
            def reader():
                started.set()
                try:read()
                except BaseException as exc:errors.append(exc)
                finally:returned.set()
            with owner.lock:
                thread=threading.Thread(target=reader);thread.start()
                self.assertTrue(started.wait(2))
                self.assertFalse(returned.wait(.05),'Reader bypassed the active publication lock')
            thread.join(2);self.assertTrue(returned.is_set());self.assertEqual(errors,[])

    def test_source_pins_retention_before_any_result_or_archive_offer(self):
        source=EngineService.__new__(EngineService);source.root=self.root
        source.require_report_finalization();seen=[]
        source.archives=SimpleNamespace(use_ticket=lambda f:nullcontext(),offer=lambda f:seen.append('offer'))
        def compute(folder,cfg,maximum):
            self.assertIs(cfg['preseal_required'],True);seen.append('compute');return self.f.report
        source._run_reserved_ticket=compute
        with patch('horizon_engine_host.ticket_configuration',return_value=dict(self.f.cfg,preseal_required=False)):
            self.assertEqual(source.run_ticket({}),self.f.report)
        self.assertEqual(seen,['compute','offer'])


if __name__=='__main__':unittest.main()
