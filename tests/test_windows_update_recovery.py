"""Interrupted Windows profile restoration with inert code and owned handles.

No fixture PE is executed. Actual native process leases are tested separately.
"""
from contextlib import contextmanager
from pathlib import Path
import json,os,sys,unittest
from unittest.mock import patch
P=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(P/'tools'),str(P/'src/update')]
import horizon_windows_recovery as recovery
from horizon_component_install import _write,inventory
from test_component_install import InstallTests


class WindowsRecoveryTests(unittest.TestCase):
 def setUp(self):
  self.fixture=InstallTests();self.fixture.setUp();f=self.fixture
  self.candidate=f.store.prepare(f.client,['cpu']);self.previous=f.base;self.failed=Path(self.candidate['cohort'])
  self.runtime=f.user/'testnet/ui-runtime';self.runtime.mkdir(parents=True)
  self.profile=self.runtime/'wallet-core.json'
  self.original=json.dumps(dict(genesis_hash='5'*64,executable=str(f.base/'Core/pgn-core.exe'),wallet='KEEP',data_directory=str(f.user/'chain'))).encode()
  self.profile.write_bytes(b'{"candidate":"partial write"}')
  self.owner=self.runtime/'wallet-owned.json';self.digest=inventory(f.base)[1]['core/pgn-core.exe']['sha256']
  self.owner.write_text(json.dumps(dict(genesis_hash='5'*64,pid='200',birth='456',executable=str(f.base/'Core/pgn-core.exe'),executable_sha256=self.digest)))
  self.before=f.store.current()
  self.frame=dict(schema='PGN_WINDOWS_COHORT_LIFECYCLE_V1',state='rebinding_profiles',before=self.before,
   UI=dict(pid=100,birth='123',executable=str(f.base/'PGN-Horizon.exe')),
   Core=dict(pid=200,birth='456',executable=str(f.base/'Core/pgn-core.exe')),new_UI=None,
   data_root=str(f.user),application_root=str(f.root),explicit_root=True,minimized=True,
   profiles=[dict(path=str(self.profile),text=self.original.decode())])
  _write(f.store.root/'transaction.json',dict(schema='PGN_COHORT_ACTIVATION_V1',state='starting_candidate',before=self.before,candidate=self.candidate,restoration_completed=False))
  self.save_frame();self.live=set();self.locked=False;self.launches=[]
  parent=self
  class Process:
   def __init__(self,pid,executable,sha256,birth):self.pid=pid;self.executable=Path(executable);self.birth=birth
   def exited(self):return self.pid not in parent.live
   def identity(self):return dict(pid=self.pid,birth=self.birth,executable=str(self.executable))
   def close(self):pass
  @contextmanager
  def lease(*args):
   parent.assertFalse(parent.locked);parent.locked=True
   try:yield
   finally:parent.locked=False
  def launch(life,path):
   parent.assertFalse(parent.locked,'Launch while data lease is held would conflict with the GUI')
   parent.assertEqual(parent.profile.read_bytes(),parent.original);parent.assertFalse(parent.owner.exists())
   parent.launches.append(path);return True
  self.patches=[patch.object(recovery,'OwnedWindowsProcess',Process),patch.object(recovery,'ui_scope_lease',lease),patch.object(recovery.WindowsCohortLifecycle,'launch',launch)]
  for p in self.patches:p.start()
 def save_frame(self):_write(self.fixture.store.root/'windows-lifecycle.json',self.frame)
 def tearDown(self):
  for p in reversed(self.patches):p.stop()
  self.fixture.tearDown()
 def call(self):
  adapter=recovery.WindowsRecovery(self.fixture.store)
  try:return self.fixture.store.recover_previous(adapter)
  finally:adapter.close()
 def test_partial_rebind_restores_exact_profiles_retires_dead_receipt_and_preserves_original_frame(self):
  path=self.fixture.store.root/'windows-lifecycle.json';original_frame=path.read_bytes()
  result=self.call();self.assertEqual(result['cohort'],str(self.previous));self.assertEqual(self.profile.read_bytes(),self.original)
  self.assertEqual(self.launches,[self.previous]);self.assertEqual(path.read_bytes(),original_frame)
  self.assertEqual(self.call(),result);self.assertEqual(len(self.launches),1)
 def test_orphan_original_core_without_receipt_is_not_ignored_or_stopped(self):
  self.owner.unlink();self.live.add(200)
  with self.assertRaisesRegex(ValueError,'owned Core'):self.call()
  self.assertEqual(self.live,{200});self.assertFalse(self.launches)
 def test_foreign_current_core_is_refused_without_deleting_its_receipt(self):
  value=json.loads(self.owner.read_bytes());value['executable']=str(self.fixture.root/'foreign.exe');self.owner.write_text(json.dumps(value));before=self.owner.read_bytes()
  with self.assertRaisesRegex(ValueError,'recorded code scope'):self.call()
  self.assertEqual(self.owner.read_bytes(),before);self.assertFalse(self.launches)
 def test_wallet_file_cannot_be_injected_into_profile_restoration(self):
  self.frame['profiles'].append(dict(path=str(self.fixture.sentinel),text='{}'));self.save_frame()
  with self.assertRaisesRegex(ValueError,'only the recorded'):self.call()
 def test_different_genesis_and_data_scope_are_refused(self):
  self.frame['data_root']=str(self.fixture.root/'foreign-data');self.save_frame()
  with self.assertRaisesRegex(ValueError,'approved scope'):self.call()
 def test_existing_healthy_restored_gui_is_not_restarted_on_retry(self):
  self.live.add(300)
  identity=dict(pid=300,birth='789',executable=str(self.previous/'PGN-Horizon.exe'))
  _write(self.fixture.store.root/'recovery-windows-lifecycle.json',dict(before=self.before,state='healthy',new_UI=identity))
  state=dict(pgn_update_genesis='5'*64,pgn_update_data_root=self.fixture.user.as_posix(),pgn_core_link_state='ready')
  with patch.object(recovery,'owned_ipc',return_value=state):self.call()
  self.assertFalse(self.launches)
 def test_existing_unhealthy_restored_gui_is_not_reported_as_recovered(self):
  self.live.add(300)
  _write(self.fixture.store.root/'recovery-windows-lifecycle.json',dict(before=self.before,state='healthy',new_UI=dict(pid=300,birth='789',executable=str(self.previous/'PGN-Horizon.exe'))))
  with patch.object(recovery,'owned_ipc',return_value={}):
   with self.assertRaisesRegex(ValueError,'health is not confirmed'):self.call()
  self.assertFalse(self.launches)


@unittest.skipUnless(os.name=='nt','Windows byte-range lease')
class WindowsRecoveryLeaseTests(unittest.TestCase):
 def test_same_data_scope_refuses_second_writer_and_releases_cleanly(self):
  import tempfile
  with tempfile.TemporaryDirectory()as folder:
   with recovery.ui_scope_lease(Path(folder),'5'*64):
    with self.assertRaises(OSError):
     with recovery.ui_scope_lease(Path(folder),'5'*64):self.fail('Duplicate UI data writer')
   with recovery.ui_scope_lease(Path(folder),'5'*64):pass

if __name__=='__main__':unittest.main()
