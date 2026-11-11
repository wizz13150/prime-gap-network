"""Real reference-TUF acceptance through the bounded UI/helper protocol."""
from pathlib import Path
from copy import deepcopy
from unittest.mock import patch
import contextlib,errno,io,json,sys,unittest
P=Path(__file__).resolve().parents[1];sys.path[:0]=[str(P/'tools'),str(P/'src/update')]
import horizon_update_cli as cli
from test_component_install import InstallTests
import test_authenticated_updates as fixtures
from horizon_update_client import digest

class CliTests(unittest.TestCase):
 def setUp(self):
  self.fixture=fixtures.UpdateTests();self.fixture.setUp();f=self.fixture
  self.repositories={'testnet':dict(root=f.bootstrap.decode(),root_sha256=digest(f.bootstrap),genesis_hash='5'*64,compatibility_set='qa.cohort',metadata_url=f.base,targets_url=f.target)}
  self.request=dict(schema='PGN_UPDATE_REQUEST_V1',action='check',context=f.context,selected=['cpu'],cache=str(f.root/'cli-cache'))
 def tearDown(self):self.fixture.tearDown()
 def call(self):return cli.run(self.request,repositories=self.repositories,fetcher=self.fixture.transport)
 def test_authenticated_available_version_and_preparation(self):
  result=self.call();self.assertTrue(result['publisher_authenticated']);self.assertEqual(result['components'][0]['version'],'1.0.0')
  self.request['action']='prepare';result=self.call();self.assertTrue(result['publisher_authenticated']);self.assertFalse(result['installation_performed'])
 def test_request_cannot_inject_another_repository_or_trust_key(self):
  for key in ('metadata_url','root','command'):
   self.request[key]='attacker'
   with self.assertRaises(ValueError):self.call()
   del self.request[key]
  self.assertFalse(Path(self.request['cache']).exists())
 def test_wrong_chain_cannot_use_another_trust_root(self):
  self.request['context']=deepcopy(self.request['context']);self.request['context']['genesis_hash']='6'*64
  with self.assertRaisesRegex(ValueError,'No trusted'):self.call()
  self.assertFalse(Path(self.request['cache']).exists())
 def test_arbitrary_operations_duplicates_and_relative_cache_are_refused(self):
  for action in ('execute','delete','install'):
   self.request['action']=action
   with self.assertRaises(ValueError):self.call()
  self.request['action']='check';self.request['selected']=['cpu','cpu']
  with self.assertRaises(ValueError):self.call()
 def test_authenticated_install_runs_only_the_owned_lifecycle_adapter(self):
  base=InstallTests();base.setUp();calls=[];parent=self
  try:
   self.request.update(action='install',base_cohort=str(base.base),installation_store=str(self.fixture.root/'cli-install'),activation={'fixture':'owned-lifecycle'})
   self.request['context']=deepcopy(self.request['context']);self.request['context']['installed']=deepcopy(base.context['installed'])
   component=self.fixture.manifest['components'][0];component['primary']='payload/cpu/EngineRuntime/cpu/horizon-cpu-controller.exe';component['files'][0]['path']=component['primary'];component['abi']='qa.1';self.fixture.publish()
   class Lifecycle:
    def __init__(self,store,original,activation):
     calls.append('constructed');self.store=store
     with parent.assertRaises(OSError):
      with fixtures.locked_cache(store.root):parent.fail('Lifecycle journal writer is outside activation lock')
    def drain(self):calls.append('drained');return True
    def start_and_verify(self,path):calls.append('health_verified');return True
    def restore_previous(self,*args):raise AssertionError('Unexpected restore')
    def close(self):calls.append('handles_closed')
   result=cli.run(self.request,repositories=self.repositories,fetcher=self.fixture.transport,lifecycle_factory=Lifecycle)
   self.assertTrue(result['publisher_authenticated']);self.assertTrue(result['installation_committed']);self.assertEqual(result['selected'],['cpu']);self.assertEqual(calls,['constructed','drained','health_verified','handles_closed'])
   cache=Path(self.request['cache'])
   self.assertFalse(list(cache.glob('s-*')),'Installed code retained disposable extractions')
   self.assertFalse(list(cache.glob('*/targets/*')),'Installed code retained downloaded archives')
   self.assertTrue(list(cache.glob('*/metadata/root.json')),'Cleanup removed trusted TUF state')
   self.assertEqual(result['warning'],'')
  finally:base.tearDown()
  self.request['selected']=['cpu'];self.request['cache']='relative'
  with self.assertRaises(ValueError):self.call()
 def test_loader_failure_never_constructs_or_drains_live_lifecycle(self):
  base=InstallTests();base.setUp()
  try:
   self.request.update(action='install',base_cohort=str(base.base),installation_store=str(self.fixture.root/'preflight-install'),activation={'fixture':'not used'})
   self.request['context']=deepcopy(self.request['context']);self.request['context']['installed']=deepcopy(base.context['installed'])
   component=self.fixture.manifest['components'][0];component['primary']='payload/cpu/EngineRuntime/cpu/horizon-cpu-controller.exe';component['files'][0]['path']=component['primary'];component['abi']='qa.1';self.fixture.publish()
   with patch('horizon_runtime_preflight.candidate_runtime_preflight',side_effect=ValueError('Loader refused before shutdown'))as check,patch('horizon_windows_install.WindowsCohortLifecycle')as lifecycle:
    with self.assertRaisesRegex(ValueError,'before shutdown'):self.call()
   check.assert_called_once();lifecycle.assert_not_called()
  finally:base.tearDown()
 def test_failed_final_progress_write_keeps_authenticated_install_success(self):
  base=InstallTests();base.setUp();calls=[]
  try:
   self.request.update(action='install',base_cohort=str(base.base),installation_store=str(self.fixture.root/'progress-full-install'),activation={'fixture':'owned-lifecycle'})
   self.request['context']=deepcopy(self.request['context']);self.request['context']['installed']=deepcopy(base.context['installed'])
   component=self.fixture.manifest['components'][0];component['primary']='payload/cpu/EngineRuntime/cpu/horizon-cpu-controller.exe';component['files'][0]['path']=component['primary'];component['abi']='qa.1';self.fixture.publish()
   class Lifecycle:
    def __init__(self,*args):pass
    def drain(self):calls.append('drained');return True
    def start_and_verify(self,path):calls.append('healthy');return True
    def restore_previous(self,*args):raise AssertionError('Committed runtime must not be rolled back')
    def close(self):calls.append('closed')
   def full(stage,*args,**kwargs):
    if stage=='committed':raise OSError(errno.ENOSPC,'Injected full progress volume')
   with patch.object(cli.progress,'report',side_effect=full):
    result=cli.run(self.request,repositories=self.repositories,fetcher=self.fixture.transport,lifecycle_factory=Lifecycle)
   self.assertTrue(result['publisher_authenticated']);self.assertTrue(result['installation_committed']);self.assertTrue(result['installation_performed'])
   self.assertIn('Update installed',result['warning']);self.assertEqual(calls,['drained','healthy','closed'])
  finally:base.tearDown()
 def test_main_emits_true_committed_result_if_result_file_is_full(self):
  folder=Path(self.request['cache'])/'sessions'/('1'*32);folder.mkdir(parents=True);source=folder/'request.json';request=dict(self.request,action='install');source.write_text(json.dumps(request));target=folder/'result.json';output=io.StringIO();write=cli._write
  result=dict(schema='PGN_UPDATE_INSTALL_RESULT_V1',publisher_authenticated=True,installation_performed=True,installation_committed=True)
  def full(path,value):
   if value.get('schema')=='PGN_UPDATE_INSTALL_RESULT_V1':raise OSError(errno.ENOSPC,'Full result volume')
   return write(path,value)
  with patch.object(sys,'argv',['horizon-updater','--request-file',str(source),'--result-file',str(target)]),patch.object(cli,'run',return_value=result),patch.object(cli,'_write',side_effect=full),contextlib.redirect_stdout(output):
   code=cli.main()
  saved=json.loads(output.getvalue());self.assertEqual(code,0);self.assertTrue(saved['installation_committed']);self.assertTrue(saved['installation_performed']);self.assertFalse(saved['result_file_written']);self.assertIn('Update installed',saved['warning'])
 def test_main_emits_original_error_if_error_result_file_is_full(self):
  folder=Path(self.request['cache'])/'sessions'/('2'*32);folder.mkdir(parents=True);source=folder/'request.json';request=dict(self.request,action='install');source.write_text(json.dumps(request));target=folder/'result.json';output=io.StringIO();write=cli._write
  def full(path,value):
   if value.get('schema')=='PGN_UPDATE_HELPER_ERROR_V1':raise OSError(errno.ENOSPC,'Full result volume')
   return write(path,value)
  with patch.object(sys,'argv',['horizon-updater','--request-file',str(source),'--result-file',str(target)]),patch.object(cli,'run',side_effect=ValueError('Original verification failure')),patch.object(cli,'_write',side_effect=full),contextlib.redirect_stdout(output):
   code=cli.main()
  saved=json.loads(output.getvalue());self.assertEqual(code,1);self.assertFalse(saved['installation_committed']);self.assertEqual(saved['error'],'Original verification failure');self.assertFalse(saved['result_file_written'])
 def test_recovery_is_offline_locked_and_closes_its_native_handles(self):
  base=InstallTests();base.setUp();calls=[];parent=self
  try:
   candidate=base.store.prepare(base.client,['cpu'])
   from horizon_component_install import _write
   _write(base.store.root/'transaction.json',dict(schema='PGN_COHORT_ACTIVATION_V1',state='starting_candidate',before=base.store.current(),candidate=candidate,restoration_completed=False))
   request=dict(schema='PGN_UPDATE_REQUEST_V1',action='recover',context=base.context,cache=str(base.root/'recovery-cache'),installation_store=str(base.store.root))
   class Recovery:
    def __init__(self,store):
     calls.append('constructed')
     with parent.assertRaises(OSError):
      with fixtures.locked_cache(store.root):parent.fail('Recovery is outside the install-store lock')
    def __call__(self,*paths):calls.append('health_verified');return True
    def close(self):calls.append('handles_closed')
   def no_network(*args):parent.fail('Offline recovery requested a download')
   result=cli.run(request,repositories=self.repositories,fetcher=no_network,recovery_factory=Recovery)
   self.assertTrue(result['recovery_performed']);self.assertEqual(calls,['constructed','health_verified','handles_closed'])
   self.assertFalse(Path(request['cache']).exists())
  finally:base.tearDown()
if __name__=='__main__':unittest.main()
