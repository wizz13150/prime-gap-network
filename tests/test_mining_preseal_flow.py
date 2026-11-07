"""Real durable report queues on MiningFlow's independent finalization lane."""
from pathlib import Path
from types import SimpleNamespace
import copy,importlib.util,json,sys,tempfile,threading,time,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/engine/shared_composite'),str(ROOT/'src/protocol')]
from horizon_mining_flow import MiningFlow
from horizon_preseal_queue import PresealQueue
from horizon_report_outbox import ReportOutbox
from report_commitment import digest
s=importlib.util.spec_from_file_location('preseal_fixture',ROOT/'tests/test_preseal_queue.py');f=importlib.util.module_from_spec(s);s.loader.exec_module(f)


class MiningPresealFlowTests(unittest.TestCase):
    def setUp(self):
        self.fixture=f.PresealTests();self.fixture.setUp();self.addCleanup(self.fixture.doCleanups)
        self.root=self.fixture.root;self.raw=self.fixture.raw;self.work=self.fixture.work
        self.flow=MiningFlow.__new__(MiningFlow)
        self.flow.worker=SimpleNamespace(root=self.root,quitting=threading.Event(),network_notice='')
        self.flow.preseal=PresealQueue(self.root);self.flow.outbox=ReportOutbox(self.root)
        self.flow.changed=threading.Event();self.flow.gpu=SimpleNamespace(timings=[],wake=lambda:None)
        self.flow.finalizer=lambda work,raw:raw

    def job(self,work):return SimpleNamespace(context=dict(role='search',work=work,legacy_assignment=True))

    def second(self):
        work=dict(self.work,ticket='e'*64,request_id='f'*64)
        raw=copy.deepcopy(self.raw);raw['body']['ticket_id']=work['ticket'];raw['sha256']=digest(raw['body'])
        return work,raw

    def test_gpu_completion_only_persists_input_and_does_not_finalize(self):
        self.flow.finalizer=lambda *_:(_ for _ in()).throw(AssertionError('GPU callback called finalizer'))
        self.flow.completed(self.job(self.work),self.raw)
        self.assertEqual(len(self.flow.preseal.entries()),1);self.assertIsNone(self.flow.outbox.first())
        self.assertFalse(self.fixture.file('network-report.json').exists())

    def test_slow_finalizer_does_not_hold_gpu_completion_or_next_report(self):
        entered=threading.Event();release=threading.Event();calls=[]
        def finalize(work,raw):
            calls.append(work['request_id']);entered.set();self.assertTrue(release.wait(3));return raw
        self.flow.finalizer=finalize;self.flow.completed(self.job(self.work),self.raw)
        thread=threading.Thread(target=self.flow.finalize_reports);thread.start()
        try:
            self.assertTrue(entered.wait(2));work,raw=self.second()
            self.flow.completed(self.job(work),raw)
            self.assertEqual(len(self.flow.preseal.entries()),2);self.assertIsNone(self.flow.outbox.first())
            release.set();deadline=time.monotonic()+3
            # Outbox publication precedes durable queue acknowledgement. Wait
            # for both boundaries rather than racing the valid middle state.
            while (len(self.flow.outbox._read())<2 or self.flow.preseal.entries())and time.monotonic()<deadline:time.sleep(.01)
            self.assertEqual(len(self.flow.outbox._read()),2);self.assertEqual(self.flow.preseal.entries(),[])
            self.assertEqual(calls,[self.work['request_id'],work['request_id']])
        finally:
            release.set();self.flow.worker.quitting.set();thread.join(3)
        self.assertFalse(thread.is_alive())

    def test_shutdown_keeps_finalized_result_unsealed_and_restart_reuses_it(self):
        self.flow.completed(self.job(self.work),self.raw)
        def finalizer(work,raw):self.flow.worker.quitting.set();return raw
        self.flow.finalizer=finalizer;self.flow.finalize_reports()
        self.assertTrue(self.fixture.file('preseal-final.json').exists())
        self.assertFalse(self.fixture.file('network-report.json').exists())
        queue=PresealQueue(self.root)
        def no_recompute(*_):raise AssertionError('Restart must retain finalized bytes')
        self.assertTrue(queue.pump(no_recompute,self.flow.outbox))
        self.assertEqual(self.flow.outbox.first()['work']['request_id'],self.work['request_id'])

    def test_full_finalization_queue_holds_only_new_searches(self):
        self.flow.preseal=PresealQueue(self.root,maximum=1)
        self.flow.completed(self.job(self.work),self.raw)
        self.assertFalse(self.flow.may_start_work(self.job(self.work)))
        self.flow.cycles=SimpleNamespace()
        audit=SimpleNamespace(context=dict(role='audit',work_cycle=None))
        self.assertTrue(self.flow.may_start_work(audit))
        work,raw=self.second()
        with self.assertRaises(BufferError):self.flow.completed(self.job(work),raw)
        self.assertEqual(len(self.flow.preseal.entries()),1)


if __name__=='__main__':unittest.main()
