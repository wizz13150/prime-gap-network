"""OFF/quit are control effects before any failing durable write."""
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
import sys,threading,tempfile,unittest,json,errno
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
from horizon_network_worker import NetworkWorker
from horizon_engine_host import EngineService

class InputBoundary(Exception):pass
class StopStorageTests(unittest.TestCase):
    def check_off(self,action):
        with tempfile.TemporaryDirectory()as folder:
            worker=object.__new__(NetworkWorker);worker.root=Path(folder)
            worker.running=threading.Event();worker.running.set();worker.quitting=threading.Event()
            events=[];worker.message=lambda*a:events.append(('notice',a))
            worker.request_pause_ack=lambda:events.append(('ack',worker.running.is_set(),worker.quitting.is_set()))
            def commands():
                yield json.dumps(dict(action=action))+'\n'
                raise InputBoundary()
            def full(*a):
                events.append(('write',worker.running.is_set(),worker.quitting.is_set()))
                raise OSError(errno.ENOSPC,'injected full disk')
            with patch('horizon_network_worker.sys.stdin',commands()),patch('horizon_network_worker.save',full),patch('horizon_network_worker.atomic_bytes',full):
                with self.assertRaises(InputBoundary):worker.controls()
            self.assertFalse(worker.running.is_set())
            self.assertEqual(worker.quitting.is_set(),action=='quit')
            self.assertTrue(events);self.assertEqual(events[0],('ack',False,action=='quit'))
            self.assertTrue(all(not e[1]for e in events if e[0]=='write'))
    def test_stop_wins_when_both_control_files_cannot_be_written(self):self.check_off('stop')
    def test_quit_wins_when_both_control_files_cannot_be_written(self):self.check_off('quit')

    def test_engine_observes_OFF_without_any_pause_file(self):
        with tempfile.TemporaryDirectory()as folder:
            engine=object.__new__(EngineService);engine.root=Path(folder);engine.complement=None
            running=threading.Event();stopping=threading.Event();running.set()
            engine.bind_run_control(running,stopping)
            self.assertFalse(engine.pause_requested());running.clear()
            self.assertTrue(engine.pause_requested());self.assertFalse((engine.root/'pause.request').exists())
            running.set();self.assertFalse(engine.pause_requested());stopping.set();self.assertTrue(engine.pause_requested())

    def test_native_stop_file_failure_terminates_only_bound_job_and_keeps_evidence(self):
        with tempfile.TemporaryDirectory()as folder:
            engine=object.__new__(EngineService);engine.root=Path(folder)
            evidence=engine.root/'checkpoint.bin';evidence.write_bytes(b'exact retained prefix')
            calls=[];job=SimpleNamespace(terminate=lambda:calls.append('owned job terminated'))
            with patch('horizon_engine_host.atomic_bytes',side_effect=OSError(errno.ENOSPC,'full disk')):
                with self.assertRaises(OSError):engine.request_native_pause(engine.root/'stop.request',job)
            self.assertEqual(calls,['owned job terminated']);self.assertEqual(evidence.read_bytes(),b'exact retained prefix')
            self.assertEqual(list(engine.root.iterdir()),[evidence])

if __name__=='__main__':unittest.main()
