import pathlib,sys,tempfile,unittest
from unittest.mock import patch
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent))
from service import replace_checkpoint
class CheckpointReplaceTests(unittest.TestCase):
 def test_transient_windows_reader_keeps_exact_candidate(self):
  with tempfile.TemporaryDirectory()as d:
   src=pathlib.Path(d)/'new';dst=pathlib.Path(d)/'old';src.write_bytes(b'closed-prefix');dst.write_bytes(b'previous')
   import os
   real=os.replace;calls=[]
   def attempt(a,b):
    calls.append((a,b))
    if len(calls)<3:
     self.assertEqual(src.read_bytes(),b'closed-prefix');self.assertEqual(dst.read_bytes(),b'previous')
     e=PermissionError('reader denied replacement');e.winerror=5;raise e
    real(a,b)
   with patch('service.os.replace',side_effect=attempt),patch('service.time.sleep')as sleep:
    replace_checkpoint(src,dst)
   self.assertEqual(dst.read_bytes(),b'closed-prefix');self.assertFalse(src.exists());self.assertEqual(len(calls),3);self.assertEqual(sleep.call_count,2)
 def test_persistent_windows_conflict_stays_bounded(self):
  e=PermissionError('persistent conflict');e.winerror=32
  with patch('service.os.replace',side_effect=e)as attempt,patch('service.time.monotonic',side_effect=[0,0,2]),patch('service.time.sleep'):
   with self.assertRaises(PermissionError):replace_checkpoint('source','target')
  self.assertEqual(attempt.call_count,2)
 def test_other_failures_are_not_retried(self):
  for error in[PermissionError('real permissions'),OSError('disk full')]:
   with patch('service.os.replace',side_effect=error)as attempt,patch('service.time.sleep')as sleep:
    with self.assertRaises(type(error)):replace_checkpoint('source','target')
   self.assertEqual(attempt.call_count,1);sleep.assert_not_called()
if __name__=='__main__':unittest.main()
