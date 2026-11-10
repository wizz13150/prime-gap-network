from pathlib import Path
from types import SimpleNamespace
import contextlib,io,json,sys,threading,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
from horizon_gpu_queue import ResidentGpuQueue
from horizon_network_worker import NetworkWorker
from horizon_mining_flow import MiningFlow

class Tests(unittest.TestCase):
    def test_second_off_gets_a_new_execution_thread_ack(self):
        enabled,stop,ack=threading.Event(),threading.Event(),threading.Event();count=[]
        def paused():count.append(1);ack.set()
        queue=ResidentGpuQueue(None,enabled,stop,None,None,paused,None)
        thread=threading.Thread(target=queue.run);thread.start()
        try:
            self.assertTrue(ack.wait(2));ack.clear()
            queue.request_pause_ack();self.assertTrue(ack.wait(2));self.assertEqual(len(count),2)
            self.assertFalse(enabled.is_set())
        finally:stop.set();thread.join(2);self.assertFalse(thread.is_alive())
    def test_no_pause_ack_until_active_job_reaches_physical_boundary(self):
        enabled,stop,started,finish,ack=(threading.Event()for _ in range(5));enabled.set()
        class Engine:
            def run_ticket(self,*_):started.set();self_test.assertTrue(finish.wait(2));return dict(state='PAUSED')
        self_test=self
        queue=ResidentGpuQueue(Engine(),enabled,stop,lambda _:None,lambda *_:None,ack.set,lambda e:None)
        queue.submit(dict(ticket_id='job'),dict(role='audit'),100)
        thread=threading.Thread(target=queue.run);thread.start()
        try:
            self.assertTrue(started.wait(2));enabled.clear();queue.request_pause_ack()
            self.assertFalse(ack.wait(.05));finish.set();self.assertTrue(ack.wait(2))
        finally:finish.set();stop.set();thread.join(2);self.assertFalse(thread.is_alive())
    def test_background_results_cannot_undo_a_verified_off(self):
        worker=NetworkWorker.__new__(NetworkWorker);worker.output_lock=threading.Lock()
        worker.running=threading.Event();worker.pause_acknowledged=threading.Event();worker.pause_acknowledged.set()
        for phase in ('Report ready','Submitting reports','Starting','Searching'):
            output=io.StringIO()
            with contextlib.redirect_stdout(output):worker.emit(dict(schema='PGN_HORIZON_MINING_VIEW_V1',phase=phase,positions_done='123'))
            value=json.loads(output.getvalue());self.assertEqual(value['phase'],'Paused');self.assertEqual(value['positions_done'],'123')
        worker.running.set();output=io.StringIO()
        with contextlib.redirect_stdout(output):worker.emit(dict(schema='PGN_HORIZON_MINING_VIEW_V1',phase='Searching'))
        self.assertEqual(json.loads(output.getvalue())['phase'],'Searching')
    def test_release_failure_or_new_on_cannot_manufacture_off_ack(self):
        for newer_on in (False,True):
            worker=SimpleNamespace(running=threading.Event(),pause_acknowledged=threading.Event())
            if newer_on:worker.running.set()
            def release():
                if not newer_on:raise RuntimeError('Native process is not released')
            worker.release_device=release;flow=MiningFlow.__new__(MiningFlow);flow.worker=worker
            if newer_on:flow.paused()
            else:
                with self.assertRaises(RuntimeError):flow.paused()
            self.assertFalse(worker.pause_acknowledged.is_set())
if __name__=='__main__':unittest.main()
