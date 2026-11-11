from contextlib import nullcontext
"""Cohort transactions only. Inert PE fixtures; no application is executed.

TUF authentication has separate reference-library signature tests. Here the
prepared-client fixture supplies a real verified stage to isolate installation,
selection, old-version conservation, and lifecycle failure behavior.
"""
from copy import deepcopy
from pathlib import Path
import errno,hashlib,json,sys,tempfile,unittest,zipfile
from unittest.mock import patch
P=Path(__file__).resolve().parents[1];sys.path[:0]=[str(P/'tools'),str(P/'src/update')]
import stage_component_bundle as stage
import horizon_component_install as install
from test_component_staging import pe
from build_component_bundle import COMPONENT_PREFIX,PRIMARY


class InstallTests(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name);self.base=self.root/'original-code';self.base.mkdir();self.user=self.root/'user-state';self.user.mkdir();self.sentinel=self.user/'wallet.dat';self.sentinel.write_bytes(b'NEVER TOUCHED')
  components=[]
  for identity in ('ui','core','gpu','cpu'):
   path=self.base/PRIMARY[identity];path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(pe());prefix=COMPONENT_PREFIX[identity];rows=[dict(path=PRIMARY[identity][len(prefix):],bytes=len(pe()),sha256=stage.sha(pe()))]
   if identity=='cpu':
    old=path.parent/'obsolete.dll';old.write_bytes(b'OLD');rows.append(dict(path='obsolete.dll',bytes=3,sha256=stage.sha(b'OLD')))
   components.append(dict(id=identity,version='1.0.0',files=rows))
  (self.base/'components.json').write_text(json.dumps(dict(schema='PGN_COMPONENT_MANIFEST_V1',network='testnet',release_version='1.0.0',published=True,components=components)))
  self.context=dict(schema='PGN_COMPONENT_STAGE_CONTEXT_V1',network='testnet',genesis_hash='5'*64,platform='windows-x86_64',compatibility_set='qa.cohort',minimum_sequence=1,installed=[dict(id=x,version='1.0.0',abi='qa.1')for x in ('ui','core','gpu','cpu')],protected_roots=[str(self.base),str(self.user)])
  c=self.root/'context.json';c.write_text(json.dumps(self.context));self.context,self.context_sha=stage.read_context(c)
  self.store=install.CohortStore.initialize(self.root/'installation-store',self.base,self.context,self.context_sha)
  data=pe()+b'CPU VERSION TWO';name='payload/cpu/'+PRIMARY['cpu'];self.manifest=dict(schema='PGN_COMPONENT_STAGE_BUNDLE_V1',bundle_id='qa.install',sequence=2,release_version='2.0.0',network='testnet',genesis_hash='5'*64,platform='windows-x86_64',compatibility_set='qa.cohort',components=[dict(id='cpu',version='2.0.0',abi='qa.1',primary=name,dependencies=[],files=[dict(path=name,bytes=len(data),sha256=stage.sha(data))])])
  archive=self.root/'update.zip'
  with zipfile.ZipFile(archive,'w')as z:z.writestr('component-bundle.json',json.dumps(self.manifest));z.writestr(name,data)
  self.cache=stage.init_cache(self.root/'staging',self.context)
  folder,plan=stage.stage_bundle(archive,stage.sha(archive.read_bytes()),self.context,self.context_sha,self.cache,['cpu'])
  self.receipt=dict(publisher_authenticated=True,context_sha256=self.context_sha,stage=str(folder),manifest_sha256=plan['manifest_sha256'],selection=['cpu'])
  self.client=type('PreparedClient',(),dict(context=self.context,context_sha256=self.context_sha,prepared=lambda ignored,selected,**kwargs:nullcontext(deepcopy(self.receipt))))()
  self.original=install.inventory(self.base)[2]
 def tearDown(self):
  self.assertEqual(self.sentinel.read_bytes(),b'NEVER TOUCHED');self.assertEqual(install.inventory(self.base)[2],self.original);self.temp.cleanup()
 def test_component_selection_preserves_other_code_and_removes_obsolete_selected_files(self):
  candidate=self.store.prepare(self.client,['cpu']);new=Path(candidate['cohort']);value,files,_,_=install.inventory(new)
  self.assertFalse((new/'EngineRuntime/cpu/obsolete.dll').exists());self.assertEqual((new/PRIMARY['ui']).read_bytes(),pe());self.assertEqual({c['id']:c['version']for c in value['components']},{'ui':'1.0.0','core':'1.0.0','gpu':'1.0.0','cpu':'2.0.0'});self.assertEqual(self.store.current()['cohort'],str(self.base))
  self.assertEqual(self.store.prepare(self.client,['cpu']),candidate)
 def test_authenticated_stage_lease_spans_every_source_copy(self):
  from contextlib import contextmanager
  held=[];copied=[];copy=install.shutil.copyfile
  @contextmanager
  def prepared(selected,**kwargs):
   held.append(True)
   try:yield deepcopy(self.receipt)
   finally:held.pop()
  def copying(source,target):
   self.assertTrue(held,'The cache lease ended before source consumption')
   copied.append(str(source));return copy(source,target)
  with patch.object(self.client,'prepared',side_effect=prepared),patch.object(install.shutil,'copyfile',side_effect=copying):
   self.store.prepare(self.client,['cpu'])
  self.assertTrue(copied);self.assertFalse(held)
 def test_subsequent_install_can_protect_its_selected_code_inside_the_owned_store(self):
  candidate=self.store.prepare(self.client,['cpu'])
  self.assertTrue(self.store.activate(candidate,lambda:True,lambda path:True,lambda *args:None))
  context=deepcopy(self.context);context['protected_roots'].append(candidate['cohort'])
  reopened=install.CohortStore(self.store.root,context,'unused-at-this-read-boundary')
  self.assertEqual(reopened.current()['cohort'],candidate['cohort'])
  with self.assertRaisesRegex(ValueError,'overlaps'):stage._cache_path(self.store.root,context)
 def test_wallet_or_unselected_code_nested_in_install_store_remains_forbidden(self):
  candidate=self.store.prepare(self.client,['cpu'])
  for path in (self.store.root,self.store.root/'wallets',Path(candidate['cohort'])):
   context=deepcopy(self.context);context['protected_roots'].append(str(path))
   with self.assertRaisesRegex(ValueError,'Only selected immutable code'):install.CohortStore(self.store.root,context,'unused')
 def second_generation(self):
  first=self.store.prepare(self.client,['cpu']);self.assertTrue(self.store.activate(first,lambda:True,lambda path:True,lambda *args:None))
  context=deepcopy(self.context)
  next(c for c in context['installed']if c['id']=='cpu')['version']='2.0.0'
  context['protected_roots'].append(first['cohort'])
  context,context_sha=stage.context_from_bytes(json.dumps(context,sort_keys=True,separators=(',',':')).encode())
  cache=stage.init_cache(self.root/'second-staging',context);archive=self.root/'update.zip'
  folder,plan=stage.stage_bundle(archive,stage.sha(archive.read_bytes()),context,context_sha,cache,['cpu'])
  receipt=dict(publisher_authenticated=True,context_sha256=context_sha,stage=str(folder),manifest_sha256=plan['manifest_sha256'],selection=['cpu'])
  client=type('SecondPreparedClient',(),dict(context=context,context_sha256=context_sha,prepared=lambda ignored,selected,**kwargs:nullcontext(deepcopy(receipt))))()
  return Path(first['cohort']),install.CohortStore(self.store.root,context,context_sha),client
 def test_only_owned_immutable_cohorts_share_identical_code_across_updates(self):
  first,store,client=self.second_generation();second=Path(store.prepare(client,['cpu'])['cohort'])
  self.assertFalse(install.os.path.samefile(self.base/PRIMARY['core'],first/PRIMARY['core']))
  for identity in ('ui','core','gpu','cpu'):self.assertTrue(install.os.path.samefile(first/PRIMARY[identity],second/PRIMARY[identity]))
  self.assertEqual(install.inventory(first)[2],install.inventory(second)[2])
 def test_unsupported_hardlinks_fall_back_to_independent_verified_code(self):
  first,store,client=self.second_generation()
  with patch.object(install.os,'link',side_effect=OSError('Filesystem does not support hardlinks')):
   second=Path(store.prepare(client,['cpu'])['cohort'])
  for identity in ('ui','core','gpu','cpu'):self.assertFalse(install.os.path.samefile(first/PRIMARY[identity],second/PRIMARY[identity]))
  self.assertEqual(install.inventory(first)[2],install.inventory(second)[2])
 def test_storage_quota_charges_shared_file_identity_once_and_independent_copies_separately(self):
  first,store,client=self.second_generation();store.prepare(client,['cpu'])
  paths=[p for p in store.root.rglob('*')if p.is_file()];logical=sum(p.stat().st_size for p in paths);unique={}
  for path in paths:
   info=path.stat();unique[(info.st_dev,info.st_ino)]=info.st_size
  physical=sum(unique.values());self.assertLess(physical,logical)
  with patch.object(install,'MAX_STORE',physical):store._quota()
  with patch.object(install,'MAX_STORE',physical-1):
   with self.assertRaisesRegex(ValueError,'quota'):store._quota()
 def test_inconclusive_link_creation_never_overwrites_an_existing_code_alias(self):
  first,store,client=self.second_generation();real=install.os.link
  def uncertain(source,target):real(source,target);raise OSError('Ambiguous filesystem result')
  before=install.inventory(first)[2]
  with patch.object(install.os,'link',side_effect=uncertain):
   with self.assertRaisesRegex(ValueError,'Unresolved code link'):store.prepare(client,['cpu'])
  self.assertEqual(install.inventory(first)[2],before);self.assertFalse(list((store.root/'cohorts').glob('.pending-*')))
 def test_shutdown_refusal_keeps_old_selector_and_does_not_start_candidate(self):
  candidate=self.store.prepare(self.client,['cpu']);calls=[]
  self.assertFalse(self.store.activate(candidate,lambda:False,lambda path:calls.append(path),lambda *args:None));self.assertFalse(calls);self.assertEqual(self.store.current()['cohort'],str(self.base))
 def test_failed_start_restores_old_runtime_and_selector(self):
  candidate=self.store.prepare(self.client,['cpu']);restores=[]
  self.assertFalse(self.store.activate(candidate,lambda:True,lambda path:False,lambda *args:restores.append(args)));self.assertEqual(restores,[(self.base,Path(candidate['cohort']))]);self.assertEqual(self.store.current()['cohort'],str(self.base))
 def test_exception_during_start_restores_old_runtime(self):
  candidate=self.store.prepare(self.client,['cpu']);restores=[]
  def fail(path):raise RuntimeError('Startup failed')
  with self.assertRaisesRegex(RuntimeError,'Startup failed'):self.store.activate(candidate,lambda:True,fail,lambda *args:restores.append(args))
  self.assertEqual(len(restores),1);self.assertEqual(self.store.current()['cohort'],str(self.base))
 def test_partial_shutdown_exception_attempts_one_restore(self):
  candidate=self.store.prepare(self.client,['cpu']);restores=[];starts=[]
  def fail():raise RuntimeError('Core closed but final UI acknowledgement failed')
  with self.assertRaisesRegex(RuntimeError,'acknowledgement failed'):self.store.activate(candidate,fail,lambda p:starts.append(p),lambda *args:restores.append(args))
  self.assertFalse(starts);self.assertEqual(len(restores),1);self.assertEqual(self.store.current()['cohort'],str(self.base));self.assertEqual(json.loads((self.store.root/'transaction.json').read_bytes())['state'],'shutdown_failed')
 def test_disk_full_before_shutdown_never_stops_the_runtime(self):
  candidate=self.store.prepare(self.client,['cpu']);calls=[];write=install._write
  def full(path,value):
   if Path(path).name=='transaction.json':raise OSError(errno.ENOSPC,'Injected full journal volume')
   return write(path,value)
  with patch.object(install,'_write',side_effect=full):
   with self.assertRaises(OSError):self.store.activate(candidate,lambda:calls.append('drain'),lambda p:calls.append('start'),lambda *a:calls.append('restore'))
  self.assertEqual(calls,[]);self.assertEqual(self.store.current()['cohort'],str(self.base))
 def test_disk_full_after_shutdown_restores_once_even_if_failure_journal_is_full(self):
  candidate=self.store.prepare(self.client,['cpu']);calls=[];write=install._write
  def full(path,value):
   if Path(path).name=='transaction.json'and value.get('state')in ('starting_candidate','activation_failed'):
    raise OSError(errno.ENOSPC,'Injected full journal volume')
   return write(path,value)
  def drain():calls.append('drain');return True
  with patch.object(install,'_write',side_effect=full):
   with self.assertRaises(OSError):self.store.activate(candidate,drain,lambda p:calls.append('start'),lambda *a:calls.append('restore'))
  self.assertEqual(calls,['drain','restore']);self.assertEqual(self.store.current()['cohort'],str(self.base))
  self.assertEqual(json.loads((self.store.root/'transaction.json').read_bytes())['state'],'prepared')
  self.assertTrue(Path(candidate['cohort']).is_dir());self.assertEqual(self.sentinel.read_bytes(),b'NEVER TOUCHED')
 def test_transient_journal_failure_after_shutdown_preserves_retryable_restoration(self):
  candidate=self.store.prepare(self.client,['cpu']);calls=[];write=install._write
  def full(path,value):
   if Path(path).name=='transaction.json'and value.get('state')=='starting_candidate':
    raise OSError(errno.ENOSPC,'Injected transient full journal volume')
   return write(path,value)
  def drain():calls.append('drain');return True
  with patch.object(install,'_write',side_effect=full):
   with self.assertRaises(OSError):self.store.activate(candidate,drain,lambda p:calls.append('start'),lambda *a:calls.append('restore'))
  self.assertEqual(calls,['drain','restore']);self.store.require_resolved_transaction()
  saved=json.loads((self.store.root/'transaction.json').read_bytes())
  self.assertEqual(saved['state'],'activation_failed');self.assertTrue(saved['restoration_completed'])
 def test_restore_failure_is_not_retried_inside_same_transaction(self):
  candidate=self.store.prepare(self.client,['cpu']);restores=[]
  def fail_restore(*args):restores.append(args);raise RuntimeError('Previous UI refused startup')
  with self.assertRaisesRegex(RuntimeError,'refused startup'):self.store.activate(candidate,lambda:True,lambda p:False,fail_restore)
  self.assertEqual(len(restores),1);self.assertEqual(self.store.current()['cohort'],str(self.base))
 def test_full_final_receipt_preserves_committed_installation_and_never_restarts_it(self):
  candidate=self.store.prepare(self.client,['cpu']);calls=[];write=install._write
  def full(path,value):
   if Path(path).name=='transaction.json'and value.get('state')=='committed':
    raise OSError(errno.ENOSPC,'Injected terminal receipt full')
   return write(path,value)
  def drain():calls.append('drain');return True
  def start(path):calls.append('start');return True
  with patch.object(install,'_write',side_effect=full):
   self.assertTrue(self.store.activate(candidate,drain,start,lambda *a:calls.append('restore')))
  self.assertEqual(calls,['drain','start']);self.assertEqual(self.store.current()['cohort'],candidate['cohort'])
  self.assertIn('Update installed',self.store.commit_warning);self.store.require_resolved_transaction()
  reopened=install.CohortStore(self.store.root,self.context,self.context_sha)
  self.assertEqual(reopened.recover_previous(lambda *a:self.fail('Committed runtime must not be restarted')),self.store.current())
  self.assertEqual(json.loads((self.store.root/'transaction.json').read_bytes())['state'],'starting_candidate')
 def test_incomplete_final_receipt_cannot_authorise_a_changed_selector(self):
  candidate=self.store.prepare(self.client,['cpu']);write=install._write
  def full(path,value):
   if Path(path).name=='transaction.json'and value.get('state')=='committed':raise OSError(errno.ENOSPC,'Full')
   return write(path,value)
  with patch.object(install,'_write',side_effect=full):
   self.assertTrue(self.store.activate(candidate,lambda:True,lambda path:True,lambda *a:None))
  selected=self.store.current();selected['sequence']+=1;install._write(self.store.root/'current.json',selected)
  with self.assertRaises(install.RecoveryRequired):self.store.require_resolved_transaction()
 def test_success_is_published_only_after_health_confirmation(self):
  candidate=self.store.prepare(self.client,['cpu']);calls=[]
  def start(path):
   self.assertEqual(self.store.current()['cohort'],str(self.base));calls.append('health_verified');return True
  self.assertTrue(self.store.activate(candidate,lambda:True,start,lambda *args:self.fail('Unexpected rollback')))
  self.assertEqual(calls,['health_verified']);self.assertEqual(self.store.current()['cohort'],candidate['cohort']);self.assertEqual(self.store.current()['previous']['cohort'],str(self.base))
  with self.assertRaisesRegex(stage.StageError,'changed since'):self.store.activate(candidate,lambda:True,start,lambda *args:None)
 def test_changed_candidate_fails_before_shutdown(self):
  candidate=self.store.prepare(self.client,['cpu']);(Path(candidate['cohort'])/PRIMARY['cpu']).write_bytes(b'ALTERED');calls=[]
  with self.assertRaises(stage.StageError):self.store.activate(candidate,lambda:calls.append(1),lambda path:True,lambda *args:None)
  self.assertFalse(calls)
 def test_reused_cohort_cannot_replace_signed_data_with_another_valid_inventory(self):
  candidate=self.store.prepare(self.client,['cpu']);folder=Path(candidate['cohort']);f=folder/PRIMARY['cpu'];f.write_bytes(b'ANOTHER VALIDLY HASHED LOCAL FILE');inventory=json.loads((folder/'components.json').read_bytes())
  c=next(c for c in inventory['components']if c['id']=='cpu');c['files'][0].update(bytes=f.stat().st_size,sha256=stage.sha(f.read_bytes()));(folder/'components.json').write_text(json.dumps(inventory))
  with self.assertRaisesRegex(stage.StageError,'authenticated files'):self.store.prepare(self.client,['cpu'])
 def test_selector_publication_failure_restores_old_runtime(self):
  candidate=self.store.prepare(self.client,['cpu']);restores=[];original=install.os.replace
  def fail_current(source,target):
   if Path(target).name=='current.json':raise PermissionError('Selector locked')
   return original(source,target)
  with patch.object(install.os,'replace',side_effect=fail_current):
   with self.assertRaisesRegex(PermissionError,'locked'):self.store.activate(candidate,lambda:True,lambda path:True,lambda *args:restores.append(args))
  self.assertEqual(len(restores),1);self.assertEqual(self.store.current()['cohort'],str(self.base))
 def test_incomplete_copy_is_cleaned_without_changing_selector(self):
  with patch.object(install.shutil,'copyfile',side_effect=OSError('Disk write failed')):
   with self.assertRaisesRegex(OSError,'Disk write failed'):self.store.prepare(self.client,['cpu'])
  self.assertFalse(list((self.store.root/'cohorts').glob('.pending-*')));self.assertEqual(self.store.current()['cohort'],str(self.base))
 def test_interrupted_attempt_keeps_recovery_journal_and_never_downloads_again(self):
  candidate=self.store.prepare(self.client,['cpu'])
  for state in ('prepared','starting_candidate','activation_failed','shutdown_failed'):
   install._write(self.store.root/'transaction.json',dict(schema='PGN_COHORT_ACTIVATION_V1',state=state,before=self.store.current(),candidate=candidate,restoration_completed=False))
   original=(self.store.root/'transaction.json').read_bytes()
   with patch.object(self.client,'prepared',side_effect=AssertionError('Must preserve recovery before downloading')):
    with self.assertRaisesRegex(ValueError,'requires recovery'):self.store.prepare(self.client,['cpu'])
   self.assertEqual((self.store.root/'transaction.json').read_bytes(),original)
 def test_interruption_blocks_callbacks_and_preserves_native_backups(self):
  candidate=self.store.prepare(self.client,['cpu']);calls=[]
  install._write(self.store.root/'transaction.json',dict(schema='PGN_COHORT_ACTIVATION_V1',state='starting_candidate',before=self.store.current(),candidate=candidate))
  backup=self.store.root/'windows-lifecycle.json';backup.write_bytes(b'EXACT PRIOR ROLLBACK FRAME')
  with self.assertRaisesRegex(ValueError,'requires recovery'):
   self.store.activate(candidate,lambda:calls.append('drain'),lambda p:calls.append('start'),lambda *a:calls.append('restore'))
  self.assertFalse(calls);self.assertEqual(backup.read_bytes(),b'EXACT PRIOR ROLLBACK FRAME')
 def test_verified_restoration_allows_retry_but_failed_restoration_does_not(self):
  candidate=self.store.prepare(self.client,['cpu'])
  self.assertFalse(self.store.activate(candidate,lambda:True,lambda path:False,lambda *args:None))
  self.assertTrue(json.loads((self.store.root/'transaction.json').read_bytes())['restoration_completed'])
  self.assertEqual(self.store.prepare(self.client,['cpu']),candidate)
  def refused(*args):raise RuntimeError('Previous startup failed')
  with self.assertRaisesRegex(RuntimeError,'Previous startup failed'):
   self.store.activate(candidate,lambda:True,lambda path:False,refused)
  self.assertFalse(json.loads((self.store.root/'transaction.json').read_bytes())['restoration_completed'])
  with self.assertRaisesRegex(ValueError,'requires recovery'):self.store.prepare(self.client,['cpu'])
 def test_committed_receipt_must_match_actual_selector(self):
  candidate=self.store.prepare(self.client,['cpu']);self.assertTrue(self.store.activate(candidate,lambda:True,lambda path:True,lambda *args:None))
  self.store.require_resolved_transaction()
  current=json.loads((self.store.root/'current.json').read_bytes());current['cohort']=str(self.base);current['inventory_sha256']=self.original
  install._write(self.store.root/'current.json',current)
  with self.assertRaisesRegex(ValueError,'requires recovery'):self.store.require_resolved_transaction()
 def interrupted(self):
  candidate=self.store.prepare(self.client,['cpu']);before=self.store.current()
  install._write(self.store.root/'transaction.json',dict(schema='PGN_COHORT_ACTIVATION_V1',state='starting_candidate',before=before,candidate=candidate,restoration_completed=False))
  return candidate,before
 def test_recovery_restores_known_code_and_keeps_antirollback_floor(self):
  candidate,before=self.interrupted();calls=[]
  def restore(old,failed):calls.append((old,failed));return True
  recovered=self.store.recover_previous(restore)
  self.assertEqual(calls,[(self.base,Path(candidate['cohort']))]);self.assertEqual(recovered['cohort'],str(self.base));self.assertEqual(recovered['sequence'],2)
  self.store.require_resolved_transaction();self.assertEqual(json.loads((self.store.root/'recovery.json').read_bytes())['state'],'completed')
 def test_recovery_refusal_keeps_original_transaction_and_selector(self):
  self.interrupted();journal=self.store.root/'transaction.json';original=journal.read_bytes();before=self.store.current()
  with self.assertRaisesRegex(ValueError,'not confirmed'):self.store.recover_previous(lambda *args:False)
  self.assertEqual(journal.read_bytes(),original);self.assertEqual(self.store.current(),before)
 def test_recovery_selector_retry_does_not_repeat_runtime_restart(self):
  self.interrupted();calls=[];original=install._write
  def fail(path,value):
   if path.name=='current.json':raise PermissionError('Temporary selector sharing failure')
   return original(path,value)
  def restore(*args):calls.append('restored');return True
  with patch.object(install,'_write',side_effect=fail):
   with self.assertRaises(PermissionError):self.store.recover_previous(restore)
  self.assertEqual(json.loads((self.store.root/'recovery.json').read_bytes())['state'],'runtime_verified')
  self.store.recover_previous(restore);self.assertEqual(calls,['restored'])
 def test_recovery_rejects_changed_candidate_before_platform_actions(self):
  candidate,_=self.interrupted();(Path(candidate['cohort'])/PRIMARY['cpu']).write_bytes(b'CHANGED');calls=[]
  with self.assertRaisesRegex(ValueError,'fingerprint|size'):self.store.recover_previous(lambda *args:calls.append('bad'))
  self.assertFalse(calls)
 def test_recovery_after_pointer_publication_can_restore_previous(self):
  candidate,before=self.interrupted()
  install._write(self.store.root/'current.json',dict(schema='PGN_ACTIVE_COHORT_V1',network='testnet',genesis_hash='5'*64,cohort=candidate['cohort'],inventory_sha256=candidate['inventory_sha256'],sequence=2,previous=before))
  self.assertEqual(self.store.recover_previous(lambda *args:True)['cohort'],str(self.base))
 def test_duplicate_recovery_does_not_restart_an_already_restored_runtime(self):
  self.interrupted();calls=[]
  def restore(*args):calls.append('restart');return True
  first=self.store.recover_previous(restore);second=self.store.recover_previous(restore)
  self.assertEqual(first,second);self.assertEqual(calls,['restart'])
 def test_unknown_recovery_state_cannot_invoke_platform_actions(self):
  self.interrupted();path=self.store.root/'transaction.json';value=json.loads(path.read_bytes());value.update(state='newer_unknown_state',restoration_completed=True);install._write(path,value)
  with self.assertRaisesRegex(ValueError,'requires recovery'):self.store.require_resolved_transaction()
  calls=[]
  with self.assertRaisesRegex(ValueError,'Known interrupted'):self.store.recover_previous(lambda *args:calls.append('bad'))
  self.assertFalse(calls)
class LifecycleObservationTests(unittest.TestCase):
 def test_one_observation_timeout_is_a_lifecycle_error_not_a_failed_process(self):
  import subprocess
  from types import SimpleNamespace
  import horizon_lifecycle as lifecycle
  owner=SimpleNamespace(pid=17,executable=Path('fixture-ui'),exited=lambda:False)
  with patch.object(lifecycle.subprocess,'run',side_effect=subprocess.TimeoutExpired(['fixture-ui'],8)):
   with self.assertRaises(lifecycle.LifecycleError):lifecycle.owned_ipc(owner,'--inspect-owned')

 def test_delayed_authenticated_startup_can_recover_after_an_observation_timeout(self):
  import subprocess
  from types import SimpleNamespace
  import horizon_lifecycle as lifecycle
  import horizon_cohort_lifecycle as native
  owner=SimpleNamespace(pid=17,executable=Path('cohort/PGN-Horizon.exe'),exited=lambda:False,close=lambda:None)
  life=native.NativeCohortLifecycle.__new__(native.NativeCohortLifecycle)
  life.store=SimpleNamespace(context=dict(network='testnet',genesis_hash='b'*64))
  life.ui_relative='PGN-Horizon.exe';life.explicit_root=False;life.minimized=False
  life.data=Path('data');life.had_core=True;life.launch_environment=lambda:{};life.launch_options=lambda:{}
  life.own_process=lambda *args:owner;life.ipc=lifecycle.owned_ipc;events=[];life.record=events.append
  healthy=dict(pgn_update_genesis='b'*64,pgn_update_data_root='data',pgn_core_link_state='ready')
  responses=[subprocess.TimeoutExpired(['fixture-ui'],8),subprocess.CompletedProcess([],0,json.dumps(healthy).encode(),b'')]
  with patch.object(native,'inventory',return_value=(None,{'pgn-horizon.exe':{'sha256':'a'*64}},None,None)),patch.object(native.subprocess,'Popen',return_value=SimpleNamespace(pid=17)),patch.object(lifecycle.subprocess,'run',side_effect=responses)as observer,patch.object(native.time,'sleep'):
   self.assertTrue(life.launch(Path('cohort')))
  self.assertEqual(observer.call_count,2)
  self.assertEqual(events,['verifying_startup','healthy'])

if __name__=='__main__':unittest.main()
