"""Actual Linux pidfd, native data lease, install/restart and rollback.

The Qt child is an inert fixture using the production data lease, not Horizon
or Core. Authentication has separate reference-TUF tests. Wallet bytes are a
sentinel and are never passed to lifecycle code.
"""
from copy import deepcopy
from pathlib import Path
import hashlib,json,os,shutil,subprocess,sys,tempfile,time,unittest,zipfile
from unittest.mock import patch
P=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(P/'tools'),str(P/'src/update')]
import stage_component_bundle as stage
from build_component_bundle import COMPONENT_PREFIX,LINUX_PRIMARY
from horizon_component_install import CohortStore,inventory,_write
from horizon_linux_install import LinuxCohortLifecycle
from horizon_linux_lifecycle import OwnedLinuxProcess,owned_ipc
from horizon_linux_recovery import LinuxRecovery,ui_scope_lease


@unittest.skipUnless(sys.platform=='linux','Actual Linux native lifecycle')
class LinuxInstallTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build=tempfile.TemporaryDirectory(prefix='pgn-linux-update-test-build-')
        cls.image=Path(cls.build.name)/'native-fixture'
        flags=subprocess.check_output(['pkg-config','--cflags','--libs','Qt6Core'],text=True).split()
        subprocess.run(['g++','-std=c++17','-fPIC','-I'+str(P/'src'),str(P/'tests/fixtures/linux_update_process.cpp'),'-o',str(cls.image),*flags],check=True,timeout=60)

    @classmethod
    def tearDownClass(cls):cls.build.cleanup()

    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='pgn-linux-update-test-')
        self.root=Path(self.temp.name);self.base=self.root/'code';self.base.mkdir()
        self.data=self.root/'data';self.data.mkdir();self.control=self.root/'control';self.control.mkdir()
        self.wallet=self.data/'wallet.dat';self.wallet.write_bytes(b'KEEP EXACT USER BYTES')
        self.environment=patch.dict(os.environ,PGN_TEST_UPDATE_CONTROL=str(self.control),PGN_TEST_UPDATE_ROOT=str(self.base),PGN_TEST_UPDATE_DATA=str(self.data),PGN_TEST_UPDATE_GENESIS='5'*64)
        self.environment.start();self.life=None;self.recovery=None;self.processes=[]
        image=self.image.read_bytes();components=[]
        for identity,name in LINUX_PRIMARY.items():
            path=self.base/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(image);path.chmod(0o755)
            components.append(dict(id=identity,version='1.0.0',files=[dict(path=name[len(COMPONENT_PREFIX[identity]):],bytes=len(image),sha256=stage.sha(image))]))
        launcher=self.base/'launch-horizon.sh';launcher.write_bytes(b'#!/bin/sh\nexit 0\n');launcher.chmod(0o755)
        next(c for c in components if c['id']=='ui')['files'].append(dict(path=launcher.name,bytes=launcher.stat().st_size,sha256=stage.sha(launcher.read_bytes())))
        _write(self.base/'components.json',dict(schema='PGN_COMPONENT_MANIFEST_V1',network='testnet',platform='linux-x86_64',release_version='1.0.0',published=True,components=components))
        self.context=dict(schema='PGN_COMPONENT_STAGE_CONTEXT_V1',network='testnet',genesis_hash='5'*64,platform='linux-x86_64',compatibility_set='qa.linux',minimum_sequence=1,installed=[dict(id=x,version='1.0.0',abi='qa.1')for x in LINUX_PRIMARY],protected_roots=[str(self.base),str(self.data)])
        self.context,self.context_sha=stage.context_from_bytes(json.dumps(self.context).encode())
        self.store=CohortStore.initialize(self.root/'installation',self.base,self.context,self.context_sha)
        name='payload/cpu/'+LINUX_PRIMARY['cpu'];updated=image+b'NEW CPU CODE'
        manifest=dict(schema='PGN_COMPONENT_STAGE_BUNDLE_V1',bundle_id='qa.linux',sequence=2,release_version='2.0.0',network='testnet',genesis_hash='5'*64,platform='linux-x86_64',compatibility_set='qa.linux',components=[dict(id='cpu',version='2.0.0',abi='qa.1',primary=name,dependencies=[],files=[dict(path=name,bytes=len(updated),sha256=stage.sha(updated))])])
        archive=self.root/'update.zip'
        with zipfile.ZipFile(archive,'w')as z:z.writestr('component-bundle.json',json.dumps(manifest));z.writestr(name,updated)
        cache=stage.init_cache(self.root/'stage',self.context)
        folder,plan=stage.stage_bundle(archive,stage.sha(archive.read_bytes()),self.context,self.context_sha,cache,['cpu'])
        receipt=dict(publisher_authenticated=True,context_sha256=self.context_sha,stage=str(folder),manifest_sha256=plan['manifest_sha256'],selection=['cpu'])
        client=type('PreparedClient',(),dict(context=self.context,context_sha256=self.context_sha,prepare=lambda ignored,selected:deepcopy(receipt)))()
        self.candidate=self.store.prepare(client,['cpu']);self.new=Path(self.candidate['cohort'])
        self.runtime=self.data/'testnet/ui-runtime';self.runtime.mkdir(parents=True)
        self.profile=self.runtime/'wallet-core.json'
        _write(self.profile,dict(genesis_hash='5'*64,executable=str(self.base/LINUX_PRIMARY['core']),wallet='KEEP',data_directory=str(self.data/'chain')))
        self.profile_bytes=self.profile.read_bytes()
        process=subprocess.Popen([str(self.base/LINUX_PRIMARY['ui'])]);self.processes.append(process)
        self.wait_state(process.pid)
        with OwnedLinuxProcess(process.pid,self.base/LINUX_PRIMARY['ui'],stage.sha(image)) as own:birth=own.birth
        self.activation=dict(schema='PGN_LINUX_UPDATE_ACTIVATION_V1',ui_pid=process.pid,ui_birth=birth,application_root=str(self.base),data_root=str(self.data))
        popen=subprocess.Popen
        def track(*args,**kwargs):
            child=popen(*args,**kwargs);self.processes.append(child);return child
        self.spawn_patch=patch('horizon_cohort_lifecycle.subprocess.Popen',side_effect=track)
        self.spawn_patch.start()

    def wait_state(self,pid):
        deadline=time.monotonic()+5
        while not(self.control/('state.'+str(pid))).exists():
            if time.monotonic()>deadline:self.fail('Native fixture did not become ready')
            time.sleep(.01)

    def tearDown(self):
        for path in self.control.glob('state.*'):(self.control/path.name.replace('state.','stop.')).write_text('stop')
        for process in self.processes:
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=5)
        # Launched children are owned by the lifecycle; request their test exit.
        for life in (self.life,self.recovery.life if self.recovery else None):
            if life and life.new_ui:
                deadline=time.monotonic()+5
                while not life.new_ui.exited() and time.monotonic()<deadline:time.sleep(.01)
                self.assertTrue(life.new_ui.exited())
        if self.recovery:self.recovery.close()
        if self.life:self.life.close()
        self.assertEqual(self.wallet.read_bytes(),b'KEEP EXACT USER BYTES')
        self.spawn_patch.stop();self.environment.stop();self.temp.cleanup()

    def lifecycle(self):
        self.life=LinuxCohortLifecycle(self.store,self.base,self.activation);return self.life

    def test_native_install_drains_restarts_rebinds_and_retains_execute_modes(self):
        life=self.lifecycle()
        self.assertTrue(self.store.activate(self.candidate,life.drain,life.start_and_verify,life.restore_previous))
        self.assertTrue(life.ui.exited());self.assertFalse(life.new_ui.exited())
        self.assertEqual(self.store.current()['cohort'],str(self.new))
        self.assertEqual(json.loads(self.profile.read_bytes())['executable'],str(self.new/LINUX_PRIMARY['core']))
        for row in inventory(self.new)[0]['components']:
            self.assertTrue((self.new/LINUX_PRIMARY[row['id']]).stat().st_mode&0o100)
        self.assertTrue((self.new/'launch-horizon.sh').stat().st_mode&0o100)
        self.assertEqual(subprocess.run([str(self.new/'launch-horizon.sh')],timeout=5).returncode,0)
        with self.assertRaises(BlockingIOError):
            with ui_scope_lease(self.data,'5'*64):self.fail('Two data owners')

    def test_restart_clears_snap_paths_and_preserves_the_users_qt_backend(self):
        with patch.dict(os.environ,SNAP='/snap/test/1',SNAP_NAME='test',
                        LD_LIBRARY_PATH='/snap/test/1/lib:/usr/lib',QT_QPA_PLATFORM='offscreen'):
            environment=LinuxCohortLifecycle.launch_environment()
        self.assertNotIn('SNAP',environment);self.assertNotIn('SNAP_NAME',environment)
        self.assertNotIn('LD_LIBRARY_PATH',environment)
        self.assertEqual(environment['QT_QPA_PLATFORM'],'offscreen')

    def test_core_profile_in_user_config_is_rebound_separately_from_worker_data(self):
        configured=self.root/'config/wallet/testnet'/('5'*64)
        configured.mkdir(parents=True)
        self.profile.replace(configured/'wallet-core.json');self.profile=configured/'wallet-core.json'
        process=self.processes[0];state_path=self.control/('state.'+str(process.pid))
        state=json.loads(state_path.read_bytes());state['pgn_update_core_runtime_directory']=str(configured);_write(state_path,state)
        life=self.lifecycle()
        self.assertTrue(life.had_core);self.assertEqual(life.core_runtime,configured)
        self.assertTrue(self.store.activate(self.candidate,life.drain,life.start_and_verify,life.restore_previous))
        self.assertEqual(json.loads(self.profile.read_bytes())['executable'],str(self.new/LINUX_PRIMARY['core']))
        self.assertFalse((self.runtime/'wallet-core.json').exists())

    def test_rollback_reads_core_owner_from_the_configured_directory(self):
        configured=self.root/'config/wallet/testnet'/('5'*64)
        configured.mkdir(parents=True)
        self.profile.replace(configured/'wallet-core.json');self.profile=configured/'wallet-core.json'
        state_path=self.control/('state.'+str(self.processes[0].pid))
        state=json.loads(state_path.read_bytes());state['pgn_update_core_runtime_directory']=str(configured);_write(state_path,state)
        life=self.lifecycle()
        self.assertTrue(self.store.activate(self.candidate,life.drain,life.start_and_verify,life.restore_previous))
        candidate_pid=life.new_ui.pid
        _write(configured/'wallet-owned.json',dict(genesis_hash='5'*64,pid=candidate_pid,
            executable=str(life.new_ui.executable),executable_sha256=stage.sha(life.new_ui.executable.read_bytes()),birth=life.new_ui.birth))
        opened=[];native_own=life.own_process
        def observe(*args):
            opened.append(args[0]);return native_own(*args)
        life.own_process=observe
        life.restore_previous(self.base,self.new)
        self.assertIn(candidate_pid,opened)
        self.assertEqual(life.new_ui.executable,self.base/LINUX_PRIMARY['ui'])

    def test_failed_candidate_restarts_previous_with_exact_profile_bytes(self):
        os.environ['PGN_TEST_UPDATE_FAIL_IMAGE']=str(self.new/LINUX_PRIMARY['ui'])
        life=self.lifecycle()
        try:
            with self.assertRaises(RuntimeError):self.store.activate(self.candidate,life.drain,life.start_and_verify,life.restore_previous)
        finally:os.environ.pop('PGN_TEST_UPDATE_FAIL_IMAGE',None)
        self.assertEqual(self.store.current()['cohort'],str(self.base));self.assertEqual(self.profile.read_bytes(),self.profile_bytes)
        self.assertFalse(life.new_ui.exited());self.assertEqual(life.new_ui.executable,self.base/LINUX_PRIMARY['ui'])

    def test_interrupted_rebind_recovers_once_without_restarting_healthy_recovery(self):
        life=self.lifecycle();before=self.store.current();life.drain();life.rebind_profiles(self.new)
        _write(self.store.root/'transaction.json',dict(schema='PGN_COHORT_ACTIVATION_V1',state='starting_candidate',before=before,candidate=self.candidate,restoration_completed=False))
        raw=life.journal.read_bytes();self.recovery=LinuxRecovery(self.store)
        result=self.store.recover_previous(self.recovery)
        self.assertEqual(result['cohort'],str(self.base));self.assertEqual(self.profile.read_bytes(),self.profile_bytes)
        self.assertEqual(life.journal.read_bytes(),raw);pid=self.recovery.life.new_ui.pid
        self.assertEqual(self.store.recover_previous(self.recovery),result)
        self.assertEqual(self.recovery.life.new_ui.pid,pid)


if __name__=='__main__':unittest.main()
