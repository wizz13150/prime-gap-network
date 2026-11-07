from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
import contextlib,errno,io,json,os,subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1];sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
from pgn_session import SessionControllerLock,SessionControllerBusy
if len(sys.argv)>2 and sys.argv[1]=='--hold':
 lock=SessionControllerLock(Path(sys.argv[2]));print('ready',flush=True);sys.stdin.readline();lock.release();raise SystemExit(0)
import horizon_network_worker as worker
class ControllerBusyTests(unittest.TestCase):
 def test_real_other_process_owner_is_not_removed_and_recovery_works_after_release(self):
  with tempfile.TemporaryDirectory()as temporary:
   folder=Path(temporary);(folder/'.controller.lock').write_bytes(b'0');before=(folder/'.controller.lock').read_bytes();child=subprocess.Popen([sys.executable,'-I','-X','utf8',__file__,'--hold',str(folder)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
   try:
    self.assertEqual(child.stdout.readline().strip(),'ready')
    with self.assertRaises(SessionControllerBusy)as caught:SessionControllerLock(folder)
    self.assertEqual(caught.exception.code,'SESSION_CONTROLLER_BUSY');self.assertEqual((folder/'.controller.lock').stat().st_size,len(before));self.assertIsNone(child.poll());child.stdin.write(chr(10));child.stdin.flush();child.wait(timeout=3)
    lock=SessionControllerLock(folder);lock.release();self.assertEqual((folder/'.controller.lock').read_bytes(),before)
   finally:
    if child.poll()is None:child.stdin.close();child.wait(timeout=3)
    child.stdout.close();child.stderr.close()
 def test_non_contention_lock_error_remains_a_real_error(self):
  with tempfile.TemporaryDirectory()as temporary:
   name='msvcrt.locking'if os.name=='nt'else'fcntl.flock'
   with patch(name,side_effect=OSError(errno.EINVAL,'Invalid lock fixture')):
    with self.assertRaises(OSError):SessionControllerLock(Path(temporary))
 def emission(self,error):
  args=SimpleNamespace(proof_builder=Path('unused'),bundle=Path('unused'));output=io.StringIO()
  with patch('argparse.ArgumentParser.parse_args',return_value=args),patch.object(worker,'NetworkWorker',side_effect=error),contextlib.redirect_stdout(output):self.assertEqual(worker.main(),1)
  return json.loads(output.getvalue())
 def test_only_typed_lock_contention_is_waiting(self):
  value=self.emission(SessionControllerBusy('Existing writer'));self.assertEqual(value['phase'],'Waiting for controller');self.assertEqual(value['error_code'],'SESSION_CONTROLLER_BUSY')
 def test_text_that_looks_like_busy_cannot_hide_a_semantic_failure(self):
  value=self.emission(RuntimeError('Waiting for the existing controller to release this session'));self.assertEqual(value['phase'],'Needs attention');self.assertNotIn('error_code',value)
if __name__=='__main__':unittest.main()
