from pathlib import Path
import errno,json,sys,tempfile,threading,unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1];sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
from horizon_network_worker import NetworkWorker
class ControlOutputTests(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.worker=NetworkWorker.__new__(NetworkWorker);self.worker.root=Path(self.temp.name);self.worker.running=threading.Event();self.worker.running.set();self.worker.quitting=threading.Event();self.worker.output_lock=threading.Lock()
 def test_broken_pipe_requests_checkpoint_without_escaping_to_computation(self):
  with patch('builtins.print',side_effect=BrokenPipeError(errno.EPIPE,'Fixture closed pipe'))as output:
   self.assertFalse(self.worker.emit(dict(phase='Searching')));self.assertFalse(self.worker.emit(dict(phase='Paused')));self.assertEqual(output.call_count,1)
  self.assertFalse(self.worker.running.is_set());self.assertTrue(self.worker.quitting.is_set());self.assertEqual((self.worker.root/'pause.request').read_bytes(),b'control output disconnected\n')
 def test_semantic_serialization_error_is_not_hidden_as_disconnection(self):
  with self.assertRaises(TypeError):self.worker.emit(dict(invalid=object()))
  self.assertTrue(self.worker.running.is_set());self.assertFalse(self.worker.quitting.is_set());self.assertFalse((self.worker.root/'pause.request').exists())
 def test_checkpoint_write_failure_remains_visible(self):
  with patch('builtins.print',side_effect=BrokenPipeError()),patch('horizon_network_worker.atomic_bytes',side_effect=OSError(errno.ENOSPC,'Fixture full disk')):
   with self.assertRaises(OSError):self.worker.emit(dict(phase='Searching'))
  self.assertTrue(self.worker.quitting.is_set())
 def test_normal_output_does_not_change_running_state(self):
  with patch('builtins.print')as output:self.assertTrue(self.worker.emit(dict(phase='Searching')))
  self.assertTrue(self.worker.running.is_set());self.assertFalse(self.worker.quitting.is_set());self.assertEqual(json.loads(output.call_args.args[0])['phase'],'Searching')
if __name__=='__main__':unittest.main()
