"""Real stage/inventory transactions; inert programs are never executed here.

Native PID/image/IPC ownership is exercised independently by the packaged probe.
These tests do not substitute an authenticated publisher signature test.
"""
from contextlib import nullcontext
from copy import deepcopy
from pathlib import Path
import json,sys,unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'src/update'),str(ROOT/'tests')]
import stage_component_bundle as stage
import horizon_component_install as install
import test_component_install as fixture
from build_component_bundle import PRIMARY

class RunningBaseTests(unittest.TestCase):
 def setUp(self):
  fixture.InstallTests.setUp(self)
  self.fresh=self.root/'fresh-package';install.shutil.copytree(self.base,self.fresh)
  v=json.loads((self.fresh/'components.json').read_bytes())
  core=next(c for c in v['components']if c['id']=='core');core['version']='1.1.0'
  raw=(self.fresh/PRIMARY['core']).read_bytes()+b'NEW CORE';(self.fresh/PRIMARY['core']).write_bytes(raw)
  core['files'][0].update(bytes=len(raw),sha256=stage.sha(raw))
  install._write(self.fresh/'components.json',v)
  self.context=deepcopy(self.context);next(c for c in self.context['installed']if c['id']=='core')['version']='1.1.0'
  self.context['protected_roots'].append(str(self.fresh));self.refresh_context()
 def refresh_context(self):
  self.context,self.context_sha=stage.context_from_bytes(json.dumps(self.context,sort_keys=True,separators=(',',':')).encode())
  self.store=install.CohortStore(self.store.root,self.context,self.context_sha)
  cache=stage.init_cache(self.root/('stage-'+self.context_sha[:12]),self.context)
  archive=self.root/'update.zip'
  folder,plan=stage.stage_bundle(archive,stage.sha(archive.read_bytes()),self.context,self.context_sha,cache,['cpu'])
  self.receipt=dict(publisher_authenticated=True,context_sha256=self.context_sha,stage=str(folder),manifest_sha256=plan['manifest_sha256'],selection=['cpu'])
  self.client=type('PreparedClient',(),dict(context=self.context,context_sha256=self.context_sha,prepared=lambda ignored,selected,**kwargs:nullcontext(deepcopy(self.receipt))))()
 def tearDown(self):fixture.InstallTests.tearDown(self)
 def prepare(self,verifier=None):return self.store.prepare(self.client,['cpu'],base_cohort=self.fresh,verify_running_base=verifier)
 def assert_new_core(self,candidate):
  value,_,_,_=install.inventory(Path(candidate['cohort']))
  self.assertEqual({c['id']:c['version']for c in value['components']},{'ui':'1.0.0','core':'1.1.0','gpu':'1.0.0','cpu':'2.0.0'})
  self.assertEqual((Path(candidate['cohort'])/PRIMARY['core']).read_bytes(),(self.fresh/PRIMARY['core']).read_bytes())
 def test_stale_selector_reproduces_old_installed_version_rejection(self):
  with self.assertRaisesRegex(ValueError,'Installed versions changed'):self.store.prepare(self.client,['cpu'])
 def test_download_preserves_new_unselected_core_without_changing_selector(self):
  before=self.store.current();candidate=self.prepare();self.assert_new_core(candidate)
  self.assertEqual(self.store.current(),before)
  with self.assertRaisesRegex(ValueError,'requires owned installation'):self.store.activate(candidate,lambda:True,lambda p:True,lambda *a:None)
 def test_explicit_owned_install_adopts_actual_base_and_commits(self):
  seen=[];candidate=self.prepare(lambda p:seen.append(p));self.assertEqual(seen,[self.fresh]);self.assert_new_core(candidate)
  self.assertEqual(self.store.current()['cohort'],str(self.fresh));self.assertEqual(self.store.current()['sequence'],1)
  self.assertTrue(self.store.activate(candidate,lambda:True,lambda p:True,lambda *a:None))
  self.assertEqual(self.store.current()['previous']['cohort'],str(self.fresh))
 def test_failed_candidate_restores_actual_fresh_base(self):
  candidate=self.prepare(lambda p:None);seen=[]
  self.assertFalse(self.store.activate(candidate,lambda:True,lambda p:False,lambda *a:seen.append(a)))
  self.assertEqual(seen,[(self.fresh,Path(candidate['cohort']))]);self.assertEqual(self.store.current()['cohort'],str(self.fresh))
 def test_unknown_ownership_changes_nothing(self):
  before=self.store.current()
  def reject(p):raise ValueError('Exact UI identity rejected')
  with self.assertRaisesRegex(ValueError,'UI identity'):self.prepare(reject)
  self.assertEqual(self.store.current(),before);self.assertFalse((self.store.root/'cohorts').exists())
 def test_unresolved_transaction_is_never_discarded(self):
  journal=self.store.root/'transaction.json';value=dict(schema='PGN_COHORT_ACTIVATION_V1',state='draining',before=self.store.current())
  install._write(journal,value)
  with self.assertRaises(install.RecoveryRequired):self.prepare(lambda p:self.fail('must not inspect/adopt'))
  self.assertEqual(json.loads(journal.read_bytes()),value)
 def test_resolved_transaction_does_not_become_spurious_recovery_after_adoption(self):
  journal=self.store.root/'transaction.json';install._write(journal,dict(schema='PGN_COHORT_ACTIVATION_V1',state='committed',candidate=self.store.current()))
  candidate=self.prepare(lambda p:None);self.assertFalse(journal.exists())
  self.assertTrue(self.store.activate(candidate,lambda:True,lambda p:True,lambda *a:None))
 def test_baseline_write_failure_remains_retryable(self):
  journal=self.store.root/'transaction.json';install._write(journal,dict(schema='PGN_COHORT_ACTIVATION_V1',state='committed',candidate=self.store.current()))
  before=self.store.current();write=install._write
  def full(path,value):
   if Path(path).name=='current.json':raise OSError('injected disk full')
   return write(path,value)
  with patch.object(install,'_write',side_effect=full):
   with self.assertRaisesRegex(OSError,'disk full'):self.prepare(lambda p:None)
  self.assertEqual(self.store.current(),before);self.assertFalse(journal.exists());self.assert_new_core(self.prepare(lambda p:None))
 def test_component_downgrade_is_rejected_before_ownership_or_mutation(self):
  v=json.loads((self.fresh/'components.json').read_bytes());next(c for c in v['components']if c['id']=='core')['version']='0.9.0';install._write(self.fresh/'components.json',v)
  next(c for c in self.context['installed']if c['id']=='core')['version']='0.9.0';self.refresh_context();before=self.store.current()
  with self.assertRaisesRegex(ValueError,'downgrade'):self.prepare(lambda p:self.fail('must not adopt downgrade'))
  self.assertEqual(self.store.current(),before)
 def test_inventory_tampering_is_rejected(self):
  (self.fresh/PRIMARY['core']).write_bytes(b'CORRUPT')
  with self.assertRaisesRegex(ValueError,'size/path differs|fingerprint'):self.prepare(lambda p:self.fail('must not adopt tampered files'))
 def test_stage_authentication_context_is_preserved(self):
  self.receipt['context_sha256']='0'*64
  with self.assertRaisesRegex(ValueError,'Authenticated preparation'):self.prepare(lambda p:self.fail('must not adopt unauthenticated stage'))
 def test_wrong_network_is_rejected(self):
  v=json.loads((self.fresh/'components.json').read_bytes());v['network']='main';install._write(self.fresh/'components.json',v)
  with self.assertRaisesRegex(ValueError,'network or platform'):self.prepare(lambda p:self.fail('must not adopt wrong network'))
 def missing_previous(self):
  old=self.store.current();old=dict(old,cohort=str(self.root/'removed-former-code'))
  install._write(self.store.root/'current.json',old)
  self.store=install.CohortStore(self.store.root,self.context,self.context_sha,running_base=self.fresh)
  return old
 def test_missing_former_code_cannot_be_used_by_the_legacy_or_recovery_path(self):
  self.missing_previous()
  with self.assertRaisesRegex(ValueError,'Missing'):install.CohortStore(self.store.root,self.context,self.context_sha)
 def test_missing_former_code_download_uses_complete_fresh_base_but_cannot_activate(self):
  before=self.missing_previous();candidate=self.prepare();self.assert_new_core(candidate)
  self.assertEqual(self.store.current(),before)
  with self.assertRaisesRegex(ValueError,'requires owned installation'):self.store.activate(candidate,lambda:self.fail('must not drain'),lambda p:True,lambda *a:None)
 def test_missing_former_code_explicit_owned_install_restores_the_actual_base_on_failure(self):
  self.missing_previous();seen=[];candidate=self.prepare(lambda p:seen.append(p));self.assertEqual(seen,[self.fresh]);self.assert_new_core(candidate)
  restores=[];self.assertFalse(self.store.activate(candidate,lambda:True,lambda p:False,lambda *a:restores.append(a)))
  self.assertEqual(restores,[(self.fresh,Path(candidate['cohort']))]);self.assertEqual(self.store.current()['cohort'],str(self.fresh))
 def test_missing_former_code_never_ignores_unresolved_activation(self):
  before=self.missing_previous();journal=self.store.root/'transaction.json';value=dict(schema='PGN_COHORT_ACTIVATION_V1',state='draining',before=before);install._write(journal,value)
  with self.assertRaises(install.RecoveryRequired):self.prepare(lambda p:self.fail('must not adopt unresolved operation'))
  self.assertEqual(json.loads(journal.read_bytes()),value)
 def test_missing_former_code_never_accepts_an_incorrect_installed_context(self):
  self.missing_previous();context=deepcopy(self.context);next(c for c in context['installed']if c['id']=='core')['version']='9.9.9'
  with self.assertRaisesRegex(ValueError,'installed component context'):install.CohortStore(self.store.root,context,'not-used',running_base=self.fresh)
 def test_existing_corrupt_former_code_is_not_treated_as_missing(self):
  before=(self.base/PRIMARY['core']).read_bytes()
  try:
   (self.base/PRIMARY['core']).write_bytes(b'CORRUPT')
   with self.assertRaisesRegex(ValueError,'size/path differs|fingerprint'):install.CohortStore(self.store.root,self.context,self.context_sha,running_base=self.fresh)
  finally:(self.base/PRIMARY['core']).write_bytes(before)

if __name__=='__main__':unittest.main()
