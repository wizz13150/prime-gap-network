"""Actual Windows handle lifetime tests with owned inert Python children."""
from pathlib import Path
import hashlib,os,subprocess,sys,unittest
P=Path(__file__).resolve().parents[1];sys.path.insert(0,str(P/'src/update'))
from horizon_windows_lifecycle import OwnedWindowsProcess,ProcessAbsentError,LifecycleError

@unittest.skipUnless(os.name=='nt','Windows handle semantics')
class WindowsProcessLeaseTests(unittest.TestCase):
 def setUp(self):
  self.program=Path(getattr(sys,'_base_executable',sys.executable)).resolve();self.sha=hashlib.sha256(self.program.read_bytes()).hexdigest()
  self.child=subprocess.Popen([str(self.program),'-c','import sys; sys.stdin.buffer.read(1)'],stdin=subprocess.PIPE,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,creationflags=0x08000000)
 def tearDown(self):
  if self.child.poll()is None:
   self.child.stdin.write(b'x');self.child.stdin.close();self.child.wait(timeout=5)
 def test_exited_process_with_an_open_handle_is_absent_before_image_query(self):
  with OwnedWindowsProcess(self.child.pid,self.program,self.sha)as lease:
   self.child.stdin.write(b'x');self.child.stdin.close();self.child.wait(timeout=5);self.assertTrue(lease.exited())
   with self.assertRaises(ProcessAbsentError):OwnedWindowsProcess(self.child.pid,self.program,self.sha,lease.birth)
 def test_wrong_creation_time_is_refused_without_terminating_the_child(self):
  with self.assertRaisesRegex(LifecycleError,'creation time'):OwnedWindowsProcess(self.child.pid,self.program,self.sha,'0')
  self.assertIsNone(self.child.poll())
if __name__=='__main__':unittest.main()
