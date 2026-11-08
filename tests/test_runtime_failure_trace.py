from pathlib import Path
import errno,io,json,os,subprocess,sys,tempfile,unittest,zipfile
sys.path[:0]=[str(Path(__file__).resolve().parents[1]/'src/engine/runtime_host')]
from horizon_failure_trace import failure_context,failure_message
from horizon_diagnostics import export

class FailureTraceTests(unittest.TestCase):
 def test_actual_missing_file_survives_diagnostic_zip(self):
  with tempfile.TemporaryDirectory()as folder:
   root=Path(folder);missing=root/'engine components'/'missing worker.exe'
   try:missing.read_bytes()
   except FileNotFoundError as error:context=failure_context(error,bundle=root,state_root=root)
   self.assertEqual(context['causes'][0]['resource'],'bundle/engine components/missing worker.exe')
   self.assertEqual(context['causes'][0]['os_code'],errno.ENOENT)
   self.assertTrue(context['causes'][0]['frames'])
   self.assertEqual(json.loads((root/'runtime-failure.json').read_bytes()),context)
   archive=export(json.dumps(dict(schema='PGN_DIAGNOSTIC_CAPTURE_V1',samples=[dict(failure_context=context)])).encode())
   with zipfile.ZipFile(io.BytesIO(archive))as zipped:captured=json.loads(zipped.read('diagnostics.json'))
   self.assertIn('missing worker.exe',json.dumps(captured))
 def test_actual_missing_executable_identifies_entry(self):
  with tempfile.TemporaryDirectory()as folder:
   missing=Path(folder)/'missing-launcher.exe'
   try:subprocess.run([str(missing)],check=True)
   except OSError as error:context=failure_context(error)
   self.assertEqual(context['causes'][0]['resource'],'missing-launcher.exe')
 def test_current_process_phase_not_previous_process(self):
  with tempfile.TemporaryDirectory()as folder:
   root=Path(folder);phase=root/'runtime-startup.json'
   for pid,expected in [(os.getpid(),'verifying_core'),(-1,'controller_startup')]:
    phase.write_text(json.dumps(dict(schema='PGN_CONTROLLER_STARTUP_V1',pid=pid,phase='verifying_core')))
    self.assertEqual(failure_context(OSError(errno.ENOSPC,'disk full'),state_root=root)['step'],expected)
 def test_permission_import_and_caused_file_failure(self):
  for error in [PermissionError(errno.EACCES,'access denied','protected.dll'),ModuleNotFoundError('No module named dependency',name='dependency')]:
   value=failure_context(error);self.assertEqual(value['causes'][0]['error_type'],type(error).__name__)
  try:
   try:raise FileNotFoundError(errno.ENOENT,'missing',r'C:\Users\someone\EngineRuntime\worker.exe')
   except OSError as error:raise RuntimeError('initialization failed')from error
  except RuntimeError as error:value=failure_context(error)
  self.assertEqual(len(value['causes']),2);self.assertIn('worker.exe',failure_message(value));self.assertNotIn('someone',json.dumps(value))
 def test_bounded_trace_and_secrets(self):
  value=failure_context(RuntimeError('password=synthetic-secret\n'+'x'*10000))
  self.assertLess(len(json.dumps(value)),2048);self.assertNotIn('synthetic-secret',json.dumps(value))
  self.assertTrue(all(len(x['frames'])<=6 for x in value['causes']))
 def test_subprocess_arguments_never_enter_failure_context(self):
  for error in [subprocess.CalledProcessError(9,['worker.exe','--reward','SENSITIVE-ADDRESS']),subprocess.TimeoutExpired(['worker.exe','SENSITIVE-ADDRESS'],3)]:
   value=failure_context(error);self.assertNotIn('SENSITIVE-ADDRESS',json.dumps(value));self.assertEqual(value['causes'][0]['resource'],'worker.exe')
 def test_import_loader_retains_module_and_filename(self):
  value=failure_context(ImportError('DLL load failed',name='native_module',path=r'C:\Users\someone\runtime\native_module.pyd'))
  self.assertEqual(value['causes'][0]['resource'],'native_module.pyd');self.assertEqual(value['causes'][0]['module'],'native_module')
if __name__=='__main__':unittest.main()
