"""Crash-safe finalization/handoff, using the real immutable ReportOutbox."""
from pathlib import Path
import copy,importlib.util,json,sys,tempfile,threading,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol'),str(ROOT/'src/engine/shared_composite')]
from horizon_preseal_queue import PresealQueue
from horizon_report_outbox import ReportOutbox
from report_commitment import digest
s=importlib.util.spec_from_file_location('repair_fixture',ROOT/'tests/test_report_gap_repair.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)


class PresealTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name);self.queue=PresealQueue(self.root);self.outbox=ReportOutbox(self.root)
        a=(1<<67)+1;cfg=m.configuration(a,a+10000)
        self.raw=m.indexed_report(cfg,[])
        self.work=dict(ticket=cfg['ticket_id'],request_id='b'*64,work_id='c'*64,genesis='d'*64,
            start=cfg['start'],length='10000',target_numerator='29',target_denominator='1')
        self.result=copy.deepcopy(self.raw)
        self.result['body']['counts'][0]='1'
        self.result['body']['best']=[dict(gap=1200,lower_prime=str(a),upper_prime=str(a+1200))]
        self.result['sha256']=digest(self.result['body'])

    def finalizer(self,work,raw):
        self.assertEqual(work,self.work);self.assertEqual(raw,self.raw)
        return copy.deepcopy(self.result)

    def file(self,name):return self.root/'tickets'/self.work['ticket']/name

    def test_success_corrects_before_sealing_and_retains_source(self):
        self.queue.put(self.work,self.raw)
        self.assertFalse(self.file('network-report.json').exists())
        self.assertTrue(self.queue.pump(self.finalizer,self.outbox))
        sealed=json.loads(self.file('network-report.json').read_bytes())
        self.assertEqual(sealed['body']['computed'],self.result['body'])
        self.assertEqual(json.loads(self.file('preseal-input.json').read_bytes())['computed'],self.raw)
        self.assertEqual(self.queue.entries(),[])

    def test_source_handoff_observer_runs_only_after_seal_and_replays_without_recomputation(self):
        self.queue.put(self.work,self.raw);seen=[]
        def acknowledge(work):
            self.assertEqual(work,self.work)
            sealed=json.loads(self.file('network-report.json').read_bytes())
            self.assertEqual(sealed['body']['computed'],self.result['body'])
            seen.append(work['ticket'])
            if len(seen)==1:raise OSError('Controlled crash before source reference acknowledgement')
        with self.assertRaises(OSError):self.queue.pump(self.finalizer,self.outbox,on_sealed=acknowledge)
        before=self.file('network-report.json').read_bytes()
        def forbidden(*_):raise AssertionError('Finalized source was recomputed')
        self.assertTrue(PresealQueue(self.root).pump(forbidden,self.outbox,on_sealed=acknowledge))
        self.assertEqual(self.file('network-report.json').read_bytes(),before)
        self.assertEqual(seen,[self.work['ticket']]*2)

    def test_pending_coverage_is_not_sealed(self):
        self.queue.put(self.work,self.raw)
        def not_ready(*args):raise RuntimeError('Complete complementary coverage pending')
        with self.assertRaises(RuntimeError):self.queue.pump(not_ready,self.outbox)
        self.assertFalse(self.file('network-report.json').exists());self.assertEqual(len(self.queue.entries()),1)
        self.assertFalse(self.file('preseal-final.json').exists())

    def test_resume_after_published_outbox_before_queue_ack_retains_nonce(self):
        self.queue.put(self.work,self.raw);old=self.outbox.put
        def crash(work,result):old(work,result);raise OSError('Simulated process interruption after durable seal')
        self.outbox.put=crash
        with self.assertRaises(OSError):self.queue.pump(self.finalizer,self.outbox)
        before=self.file('network-report.json').read_bytes();self.outbox.put=old
        reloaded=PresealQueue(self.root)
        def must_not_recompute(*args):raise AssertionError('Durable finalized output must be reused')
        self.assertTrue(reloaded.pump(must_not_recompute,self.outbox))
        self.assertEqual(before,self.file('network-report.json').read_bytes())
        self.assertEqual(len(self.outbox._read()),1)

    def test_pending_finalizer_does_not_hold_producer_queue_lock(self):
        self.queue.put(self.work,self.raw);started=threading.Event();release=threading.Event();errors=[]
        def slow(work,computed):started.set();self.assertTrue(release.wait(5));return self.result
        def pump():
            try:self.queue.pump(slow,self.outbox)
            except BaseException as e:errors.append(e)
        t=threading.Thread(target=pump);t.start()
        try:
            self.assertTrue(started.wait(3))
            work=copy.deepcopy(self.work);work.update(ticket='e'*64,request_id='f'*64)
            raw=copy.deepcopy(self.raw);raw['body']['ticket_id']=work['ticket'];raw['sha256']=digest(raw['body'])
            self.queue.put(work,raw);self.assertEqual(len(self.queue.entries()),2)
            self.assertFalse(self.queue.pump(self.finalizer,self.outbox))
        finally:release.set();t.join(5)
        self.assertFalse(t.is_alive());self.assertEqual(errors,[])
        self.assertEqual(len(self.queue.entries()),1)

    def test_changed_input_policy_and_foreign_outbox_fail_closed(self):
        self.queue.put(self.work,self.raw)
        changed=copy.deepcopy(self.raw);changed['body']['counts'][1]='1';changed['sha256']=digest(changed['body'])
        with self.assertRaises(ValueError):self.queue.put(self.work,changed)
        def policy_change(work,raw):raw['body']['policy_sha256']='0'*64;raw['sha256']=digest(raw['body']);return raw
        with self.assertRaisesRegex(ValueError,'identity or policy'):self.queue.pump(policy_change,self.outbox)
        self.assertFalse(self.file('network-report.json').exists())
        self.assertFalse(self.file('preseal-final.json').exists())
        other=ReportOutbox(self.root/'foreign')
        with self.assertRaises(ValueError):self.queue.pump(self.finalizer,other)
        self.assertTrue(self.queue.pump(self.finalizer,self.outbox))

    def test_callback_mutation_does_not_change_original_work(self):
        self.queue.put(self.work,self.raw)
        def mutate(work,raw):work['ticket']='e'*64;raw.clear();return self.result
        self.assertTrue(self.queue.pump(mutate,self.outbox))
        self.assertTrue(self.file('network-report.json').exists());self.assertFalse((self.root/'tickets'/('e'*64)).exists())

    def test_capacity_and_oversized_or_corrupt_files_preserve_pending(self):
        queue=PresealQueue(self.root,maximum=1);queue.put(self.work,self.raw)
        work=dict(self.work,request_id='e'*64,ticket='f'*64)
        raw=copy.deepcopy(self.raw);raw['body']['ticket_id']=work['ticket'];raw['sha256']=digest(raw['body'])
        with self.assertRaises(BufferError):queue.put(work,raw)
        self.assertFalse((self.root/'tickets'/work['ticket']/'preseal-input.json').exists())
        self.file('preseal-input.json').write_bytes(b' '*98305)
        with self.assertRaises(ValueError):queue.pump(self.finalizer,self.outbox)
        self.assertEqual(len(queue.entries()),1);self.assertFalse(self.file('network-report.json').exists())

    def test_legacy_commitment_is_never_rewritten(self):
        self.file('network-report.json').parent.mkdir(parents=True)
        self.outbox.put(self.work,self.raw);old=self.file('network-report.json').read_bytes()
        with self.assertRaisesRegex(ValueError,'migration'):self.queue.put(self.work,self.raw)
        self.assertEqual(self.file('network-report.json').read_bytes(),old)
        self.assertEqual(self.queue.entries(),[])


if __name__=='__main__':unittest.main()
