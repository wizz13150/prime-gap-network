"""Loader checks use mocks; inert test PE files must never be executed."""
from pathlib import Path
from unittest.mock import patch
import os,subprocess,sys,tempfile,unittest
P=Path(__file__).resolve().parents[1];sys.path[:0]=[str(P/'tools'),str(P/'src/update')]
import horizon_runtime_preflight as runtime

class PreflightTests(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
  self.files={p.casefold():dict(sha256='a'*64)for p in ('EngineRuntime/network/horizon-engine-host.exe','UpdateRuntime/horizon-updater.exe')}
  self.inventory=patch.object(runtime,'inventory',return_value=({},self.files,'b'*64,0));self.inventory.start()
 def tearDown(self):self.inventory.stop();self.temp.cleanup()
 def test_loader_modes_no_work_and_bootstrap_environment_removed(self):
  with patch.dict(os.environ,{'PGN_PRIVATE_OWNER_BOOTSTRAP':'must not execute'}),patch.object(runtime.subprocess,'run',return_value=subprocess.CompletedProcess([],0))as run:
   result=runtime.candidate_runtime_preflight(self.root)
  self.assertTrue(result['PASS']);self.assertFalse(result['mining_started']);self.assertEqual(run.call_count,2)
  self.assertEqual([x.args[0][-1]for x in run.call_args_list],['--help','--self-test'])
  for call in run.call_args_list:
   self.assertNotIn('PGN_PRIVATE_OWNER_BOOTSTRAP',call.kwargs['env']);self.assertEqual(call.kwargs['env']['PYINSTALLER_RESET_ENVIRONMENT'],'1');self.assertEqual(call.kwargs['timeout'],20)
 def test_failed_first_loader_stops_before_second(self):
  with patch.object(runtime.subprocess,'run',return_value=subprocess.CompletedProcess([],1))as run:
   with self.assertRaisesRegex(ValueError,'loader failed before update'):runtime.candidate_runtime_preflight(self.root)
  self.assertEqual(run.call_count,1)
 def test_windows_path_failure_is_reported_before_live_shutdown(self):
  with patch.object(runtime.subprocess,'run',side_effect=OSError(206,'Path too long')):
   with self.assertRaisesRegex(ValueError,'cannot start before update'):runtime.candidate_runtime_preflight(self.root)
 def test_timeout_is_bounded_and_reported(self):
  with patch.object(runtime.subprocess,'run',side_effect=subprocess.TimeoutExpired('fixture',20)):
   with self.assertRaisesRegex(ValueError,'TimeoutExpired'):runtime.candidate_runtime_preflight(self.root)
if __name__=='__main__':unittest.main()
