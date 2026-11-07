"""Profile conservation/refusal tests. Inert executables and fake process handles.

The real Windows UI/Core drain and same-profile restart are qualified separately
with actual native handles. These tests never run the inert PE fixtures.
"""
from pathlib import Path
import json,sys,unittest
from unittest.mock import patch
P=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(P/'tools'),str(P/'src/update')]
import horizon_windows_install as native
from test_component_install import InstallTests


class FakeProcess:
 def __init__(self,pid,executable,sha256,birth=None):self.pid=pid;self.executable=Path(executable);self.birth=str(birth or '123')
 def identity(self):return dict(pid=self.pid,birth=self.birth,executable=str(self.executable))
 def exited(self):return False
 def close(self):pass


class WindowsProfileTests(unittest.TestCase):
 def setUp(self):
  self.fixture=InstallTests();self.fixture.setUp();f=self.fixture
  self.runtime=f.user/'testnet/ui-runtime';self.runtime.mkdir(parents=True)
  self.activation=dict(schema='PGN_WINDOWS_UPDATE_ACTIVATION_V1',ui_pid=100,ui_birth='123',application_root=str(f.base),data_root=str(f.user))
  self.state=dict(pgn_update_application_root=f.base.as_posix(),pgn_update_data_root=f.user.as_posix(),pgn_update_genesis='5'*64,pgn_update_explicit_root=False,window_minimized=True)
  self.profile=dict(genesis_hash='5'*64,executable=str(f.base/'Core/pgn-core.exe'),executable_sha256='unchanged-by-test',data_directory=str(f.user/'chain'),wallet='KEEP-ME',rpc_port=1234)
  (self.runtime/'wallet-core.json').write_text(json.dumps(self.profile,indent=2)+'\n')
  self.stream=dict(program=str(f.base/'EngineRuntime/network/horizon-engine-host.exe'),arguments=['--bundle',str(f.base/'EngineRuntime'),'--state-root',str(f.user/'mining'),'--remote','C:/OTHER/Horizon/engine.exe'])
  (self.runtime/'worker-stream.json').write_text(json.dumps(self.stream))
  self.backup={p:p.read_bytes()for p in self.runtime.iterdir()}
  self.candidate=f.store.prepare(f.client,['cpu'])
  self.patches=[patch.object(native,'OwnedWindowsProcess',FakeProcess),patch.object(native,'owned_ipc',return_value=self.state)]
  for p in self.patches:p.start()
 def tearDown(self):
  for p in self.patches:p.stop()
  self.fixture.tearDown()
 def lifecycle(self):return native.WindowsCohortLifecycle(self.fixture.store,self.fixture.base,self.activation)
 def test_code_paths_rebind_and_user_data_remote_paths_remain_exact(self):
  lifecycle=self.lifecycle();candidate=Path(self.candidate['cohort'])
  try:
   lifecycle.rebind_profiles(candidate)
   profile=json.loads((self.runtime/'wallet-core.json').read_bytes());stream=json.loads((self.runtime/'worker-stream.json').read_bytes())
   self.assertEqual(profile['executable'],str(candidate/'Core/pgn-core.exe'))
   for key in ('data_directory','wallet','rpc_port','genesis_hash'):self.assertEqual(profile[key],self.profile[key])
   self.assertEqual(stream['program'],str(candidate/'EngineRuntime/network/horizon-engine-host.exe'));self.assertEqual(stream['arguments'][1],str(candidate/'EngineRuntime'));self.assertEqual(stream['arguments'][3],str(self.fixture.user/'mining'));self.assertEqual(stream['arguments'][-1],'C:/OTHER/Horizon/engine.exe')
   self.assertEqual(json.loads(lifecycle.journal.read_bytes())['state'],'rebinding_profiles')
   lifecycle.restore_previous(self.fixture.base,candidate)
   self.assertEqual({p:p.read_bytes()for p in self.runtime.iterdir()},self.backup)
  finally:lifecycle.close()
 def test_foreign_ui_data_root_is_refused_before_profile_changes(self):
  self.state['pgn_update_data_root']=str(self.fixture.root/'another-wallet')
  with self.assertRaisesRegex(ValueError,'actual owned UI'):self.lifecycle()
  self.assertEqual({p:p.read_bytes()for p in self.runtime.iterdir()},self.backup)
 def test_foreign_core_binary_is_refused_before_any_profile_changes(self):
  extra=self.fixture.root/'foreign.exe';extra.write_bytes(b'UNRELATED')
  self.profile['executable']=str(extra);path=self.runtime/'wallet-core.json';path.write_text(json.dumps(self.profile));before={p:p.read_bytes()for p in self.runtime.iterdir()}
  lifecycle=self.lifecycle()
  try:
   with self.assertRaisesRegex(ValueError,'unrelated installation'):lifecycle.rebind_profiles(Path(self.candidate['cohort']))
   self.assertEqual({p:p.read_bytes()for p in self.runtime.iterdir()},before)
  finally:lifecycle.close()
 def test_only_verified_exited_core_receipt_is_retired_and_rollback_restores_exact_bytes(self):
  f=self.fixture;path=self.runtime/'wallet-owned.json'
  owner=dict(genesis_hash='5'*64,pid='200',birth='456',executable=str(f.base/'Core/pgn-core.exe'),executable_sha256='fixture')
  path.write_text(json.dumps(owner,indent=2));raw=path.read_bytes();before={p:p.read_bytes()for p in self.runtime.iterdir()};lifecycle=self.lifecycle();candidate=Path(self.candidate['cohort'])
  try:
   with self.assertRaisesRegex(ValueError,'process is alive'):lifecycle.rebind_profiles(candidate)
   self.assertEqual({p:p.read_bytes()for p in self.runtime.iterdir()},before)
   lifecycle.core.exited=lambda:True;lifecycle.rebind_profiles(candidate)
   self.assertFalse(path.exists());journal=json.loads(lifecycle.journal.read_bytes());self.assertTrue(any(x['path']==str(path)and x['text'].encode()==raw for x in journal['profiles']))
   lifecycle.ui.exited=lambda:True
   with patch.object(lifecycle,'launch',return_value=True)as start:lifecycle.restore_previous(f.base,candidate);start.assert_called_once_with(f.base)
   self.assertEqual({p:p.read_bytes()for p in self.runtime.iterdir()},before)
  finally:lifecycle.close()
if __name__=='__main__':unittest.main()
