"""Real owned non-CUDA children; private lease registry and unique fake identity."""
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch
import uuid

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'src/engine/runtime_host'))
import pgn_owned as owned


class FiniteDiagnosticOutput(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory(prefix='pgn-finite-diagnostic-');self.root=Path(self.temp.name)
  self.identity='NO-CUDA-TEST-'+uuid.uuid4().hex;self.children=[];self.original=owned.create_owned
  def factory(command,environment,gpu_uuid,**kwargs):
   child=self.original(command,environment,gpu_uuid,registry_root=self.root/'registry',**kwargs)
   self.children.append(child);return child
  self.factory=patch.object(owned,'create_owned',side_effect=factory);self.factory.start()
 def tearDown(self):
  self.factory.stop()
  for child in self.children:
   self.assertIsNotNone(child.returncode,'Owned child left alive')
   if os.name=='nt':self.assertIsNone(child.job,'Owned Job not drained/closed')
   self.assertTrue(child.stdout.closed,'Owned read handle left open')
  lease=owned.GpuLease(self.identity,self.root/'registry').acquire();lease.release()
  self.temp.cleanup()
 def execute(self,source,sink,**kwargs):
  return owned.run_checked([sys.executable,'-I','-c',source],dict(os.environ),self.identity,self.root,sink,**kwargs)
 def test_exact_bytes_and_limit_are_preserved(self):
  sink=io.BytesIO();payload=b'\x00\xff\r\n'+b'x'*4092
  code,receipt=self.execute("import os;os.write(1,"+repr(payload)+")",sink,maximum_output_bytes=len(payload))
  self.assertEqual(code,0);self.assertEqual(sink.getvalue(),payload);self.assertEqual(receipt['output_bytes'],len(payload))
  record=json.loads((self.root/'owner-identity.json').read_text())
  self.assertEqual(record['kind'],'FINITE_DIAGNOSTIC');self.assertEqual(record['pid'],receipt['pid'])
 def test_overflow_fails_even_when_child_exits_successfully(self):
  sink=io.BytesIO()
  with self.assertRaisesRegex(ValueError,'byte budget'):
   self.execute("import os;os.write(1,b'x'*4097)",sink,maximum_output_bytes=4096)
  self.assertEqual(len(sink.getvalue()),4096)
 def test_noisy_child_is_stopped_with_disk_output_capped(self):
  began=time.monotonic();log=self.root/'worker.log'
  with log.open('wb')as sink,self.assertRaisesRegex(ValueError,'byte budget'):
   self.execute("import os\nwhile True:os.write(1,b'x'*65536)",sink,maximum_output_bytes=65536,timeout=10)
  self.assertEqual(log.stat().st_size,65536);self.assertLess(time.monotonic()-began,5)
 def test_silent_child_timeout_still_closes_owned_job(self):
  with self.assertRaises(subprocess.TimeoutExpired):self.execute("import time;time.sleep(30)",io.BytesIO(),timeout=.25)
 def test_sink_error_is_not_accepted_or_waited_until_timeout(self):
  class Failed(io.BytesIO):
   def write(self,data):raise OSError('injected full disk')
  began=time.monotonic()
  with self.assertRaisesRegex(OSError,'full disk'):
   self.execute("import os,time;os.write(1,b'hello');time.sleep(30)",Failed(),timeout=10)
  self.assertLess(time.monotonic()-began,5)
 def test_nonzero_exit_keeps_error_bytes(self):
  sink=io.BytesIO();code,receipt=self.execute("import os,sys;os.write(2,b'controlled error');sys.exit(7)",sink)
  self.assertEqual(code,7);self.assertEqual(sink.getvalue(),b'controlled error')
 def test_invalid_budget_never_creates_child(self):
  for value in(0,-1,True,2*1024*1024+1):
   with self.assertRaises(ValueError):self.execute('pass',io.BytesIO(),maximum_output_bytes=value)
  self.assertFalse(self.children)


if __name__=='__main__':unittest.main()
