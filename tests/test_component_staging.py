"""Closed adversarial tests for preparation; never starts a staged program."""
from copy import deepcopy
import importlib.util
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch
import zipfile

MODULE = Path(__file__).resolve().parents[1] / 'tools' / 'stage_component_bundle.py'
spec = importlib.util.spec_from_file_location('component_stage', MODULE)
stage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stage)


def pe():
    result = bytearray(256)
    result[:2] = b'MZ'
    result[60:64] = (128).to_bytes(4, 'little')
    result[128:134] = b'PE\0\0\x64\x86'
    return bytes(result)


class ComponentStageTests(unittest.TestCase):
    def test_dead_writer_releases_os_lock_and_its_partial_code_stage_is_recovered(self):
        self.make_zip()
        ready=self.root/'child-ready'
        script=('import sys,time;from pathlib import Path;sys.path.insert(0,sys.argv[1]);'
                'import stage_component_bundle as s;'
                'guard=s._lock(Path(sys.argv[2]));guard.__enter__();'
                'Path(sys.argv[3]).write_text("ready");time.sleep(30)')
        child=subprocess.Popen([sys.executable,'-c',script,str(MODULE.parent),str(self.cache),str(ready)],
                               stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        try:
            end=time.monotonic()+5
            while not ready.exists() and child.poll()is None and time.monotonic()<end:time.sleep(.02)
            self.assertTrue(ready.exists(),'Owned child did not acquire its lock')
            with self.assertRaises(OSError):
                with stage._lock(self.cache):self.fail('Second writer acquired the OS lock')
        finally:
            child.kill();child.communicate(timeout=5)
        partial=self.cache/('.pending-'+'a'*32);partial.mkdir();(partial/'partial.exe').write_bytes(b'INCOMPLETE CODE')
        folder,plan=self.prepare()
        self.assertFalse(partial.exists())
        stage.verify_stage(folder,plan['manifest_sha256'],self.context,self.context_hash)

    def test_exact_immutable_resources_remain_distinct_from_user_state(self):
        accepted = [('ui','data/reference/prime-gap-records.json'),('ui','README.txt'),('ui','TESTING.txt'),
                    ('ui','LICENSES/runtime/gcc-libs/COPYING.LIB'),
                    ('core','Core/core-template.json'),
                    ('gpu','EngineRuntime/bin/canonical-orders.bin'),
                    ('gpu','EngineRuntime/bin/prime-gap-worker-sm120-k64.exe.module.json'),
                    ('gpu','EngineRuntime/network/bootstrap.json'),
                    ('gpu','EngineRuntime/network/_internal/codex-rejected-cas.pem'),
                    ('gpu','EngineRuntime/vanity/runtime.json')]
        for component,path in accepted:
            stage._code_path('payload/'+component+'/'+path,component)
        for component,path in [('ui','data/wallet.dat'),('ui','data/reference/controller.json'),
                               ('ui','data/reference/prime-gap-records.json/wallet.dat'),
                               ('gpu','EngineRuntime/network/wallet-core.json'),
                               ('gpu','EngineRuntime/network/custom-ca.pem'),
                               ('gpu','EngineRuntime/bin/unknown.exe.module.json'),
                               ('cpu','data/reference/prime-gap-records.json'),
                               ('ui','LICENSES/keys/private.key'),('ui','wallet.txt'),
                               ('ui','README.json'),('ui','logs/README.txt')]:
            with self.subTest(component=component,path=path):
                with self.assertRaises(stage.StageError):
                    stage._code_path('payload/'+component+'/'+path,component)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='pgn-component-stage-')
        self.root = Path(self.temp.name)
        self.install = self.root / 'installation'
        self.install.mkdir()
        (self.install / 'wallet.dat').write_bytes(b'untouched private-state sentinel')
        self.context = dict(schema='PGN_COMPONENT_STAGE_CONTEXT_V1', network='testnet',
                            genesis_hash='5'*64, platform='windows-x86_64', compatibility_set='qa.cohort',
                            minimum_sequence=1, installed=[], protected_roots=[str(self.install)])
        self.context_path = self.root / 'context.json'
        self.write_context()
        self.cache = stage.init_cache(self.root / 'cache', self.context)
        self.payloads = {'payload/cpu/controller.exe': pe()}
        self.manifest = dict(schema='PGN_COMPONENT_STAGE_BUNDLE_V1', bundle_id='qa.cpu', sequence=2,
                             release_version='1.0.0', network='testnet', genesis_hash='5'*64,
                             platform='windows-x86_64', compatibility_set='qa.cohort', components=[])
        self.add_component('cpu')
        self.bundle = self.root / 'bundle.zip'

    def tearDown(self):
        self.assertEqual((self.install / 'wallet.dat').read_bytes(), b'untouched private-state sentinel')
        self.temp.cleanup()

    def write_context(self):
        self.context_path.write_text(json.dumps(self.context), encoding='utf-8')
        self.context, self.context_hash = stage.read_context(self.context_path)

    def add_component(self, component, dependencies=None):
        primary = f'payload/{component}/controller.exe'
        self.payloads[primary] = pe()
        self.manifest['components'].append(dict(id=component, version='1.0.0', abi='qa.abi.1',
            primary=primary, dependencies=dependencies or [], files=[dict(path=primary, bytes=len(pe()), sha256=stage.sha(pe()))]))

    def make_zip(self, extra=None, raw_manifest=None, attributes=None, compression=zipfile.ZIP_STORED):
        with zipfile.ZipFile(self.bundle, 'w', compression=compression) as archive:
            archive.writestr('component-bundle.json', raw_manifest or json.dumps(self.manifest).encode())
            for name, data in self.payloads.items():
                info = zipfile.ZipInfo(name)
                info.compress_type = compression
                info.create_system = 3
                info.external_attr = ((attributes or {}).get(name, stat.S_IFREG | 0o644)) << 16
                archive.writestr(info, data)
            for name, data in (extra or {}).items():
                archive.writestr(name, data)
        return stage.sha(self.bundle.read_bytes())

    def prepare(self, selected=('cpu',), digest=None, cancelled=lambda: False):
        return stage.stage_bundle(self.bundle, digest or stage.sha(self.bundle.read_bytes()),
                                  self.context, self.context_hash, self.cache, selected, cancelled)

    def reject_clean(self, message=None):
        before = sorted(p.name for p in self.cache.iterdir())
        with self.assertRaises(stage.StageError) as caught:
            self.prepare()
        if message:
            self.assertIn(message, str(caught.exception))
        self.assertEqual(sorted(p.name for p in self.cache.iterdir()), before)

    def test_prepare_verify_exact_and_never_activate(self):
        digest = self.make_zip()
        original = self.bundle.read_bytes()
        folder, plan = self.prepare()
        self.assertEqual(plan['archive_sha256'], digest)
        self.assertFalse(plan['activation_allowed'])
        self.assertFalse(plan['publisher_authenticated'])
        self.assertEqual(plan['state'], 'prepared_activation_blocked')
        self.assertEqual((folder/'payload/cpu/controller.exe').read_bytes(), pe())
        self.assertEqual(stage.verify_stage(folder, plan['manifest_sha256'], self.context, self.context_hash), plan)
        self.assertEqual(self.bundle.read_bytes(), original)
        self.reject_clean('already exists')

    def test_all_four_components_and_selection_dependency_closure(self):
        self.add_component('core')
        self.add_component('gpu')
        self.add_component('ui', [dict(id='cpu', version='1.0.0', abi='qa.abi.1')])
        self.make_zip()
        folder, plan = self.prepare(('ui',))
        self.assertEqual(plan['selected_with_dependencies'], ['cpu', 'ui'])
        self.assertFalse((folder/'payload/gpu').exists())
        self.assertFalse((folder/'payload/core').exists())
        self.assertEqual(stage.verify_stage(folder, plan['manifest_sha256'], self.context, self.context_hash), plan)

    def test_installed_dependency_and_missing_or_conflicting_dependency(self):
        self.manifest['components'][0]['dependencies'] = [dict(id='core', version='2.0', abi='core.rpc.1')]
        self.make_zip()
        self.reject_clean('dependency')
        self.context['installed'] = [dict(id='core', version='2.0', abi='wrong.abi')]
        self.write_context()
        self.reject_clean('dependency')
        self.context['installed'][0]['abi'] = 'core.rpc.1'
        self.write_context()
        _, plan = self.prepare()
        self.assertEqual(plan['selected_with_dependencies'], ['cpu'])

    def test_dependency_cycle(self):
        self.add_component('ui', [dict(id='cpu', version='1.0.0', abi='qa.abi.1')])
        self.manifest['components'][0]['dependencies'] = [dict(id='ui', version='1.0.0', abi='qa.abi.1')]
        self.make_zip()
        self.reject_clean('Cyclic')

    def test_chain_platform_and_cohort_mismatch(self):
        for key, value in [('network','main'),('genesis_hash','6'*64),('platform','linux-x86_64'),('compatibility_set','other.cohort')]:
            original = self.manifest[key]
            with self.subTest(key=key):
                self.manifest[key] = value
                self.make_zip()
                self.reject_clean('mismatch')
            self.manifest[key] = original

    def test_local_sequence_and_version_rollback_and_abi_conflict(self):
        self.context['minimum_sequence'] = 3
        self.write_context()
        self.make_zip()
        self.reject_clean('sequence')
        self.context['minimum_sequence'] = 1
        for version, abi in [('2.0.0','qa.abi.1'),('1.0.0','changed')]:
            self.context['installed'] = [dict(id='cpu', version=version, abi=abi)]
            self.write_context()
            self.reject_clean()

    def test_zip_slip_absolute_ads_reserved_names_case_aliases(self):
        for path in ('../escape.exe','/escape.exe','C:/escape.exe','payload/cpu/a\\escape.exe',
                     'payload/cpu/CON.exe','payload/cpu/Lpt9.dll','payload/cpu/a.exe:stream',
                     'payload/cpu/trailing.','payload/cpu/COM1.dll','payload/cpu/short~1.exe',
                     'payload/cpu/space .exe','payload/cpu/CONIN$.exe'):
            with self.subTest(path=path):
                self.make_zip(extra={path:pe()})
                self.reject_clean()
        self.payloads['payload/cpu/DLL/a.dll'] = pe()
        self.payloads['payload/cpu/dll/b.dll'] = pe()
        self.manifest['components'][0]['files'] += [dict(path=n,bytes=len(v),sha256=stage.sha(v)) for n,v in self.payloads.items() if '/controller.exe' not in n]
        self.make_zip()
        self.reject_clean('Case alias')

    def test_links_special_files_and_directories_in_zip(self):
        path = 'payload/cpu/controller.exe'
        for mode in (stat.S_IFLNK | 0o777, stat.S_IFIFO | 0o600, stat.S_IFDIR | 0o755):
            with self.subTest(mode=mode):
                self.make_zip(attributes={path:mode})
                self.reject_clean('special files')

    def test_manifest_inventory_size_digest_and_primary_mismatch(self):
        self.make_zip(extra={'payload/cpu/extra.dll':pe()})
        self.reject_clean('inventory')
        row = self.manifest['components'][0]['files'][0]
        for key,value in [('bytes',len(pe())+1),('sha256','0'*64)]:
            original = row[key]
            row[key] = value
            self.make_zip()
            self.reject_clean()
            row[key] = original
        self.manifest['components'][0]['primary'] = 'payload/cpu/missing.exe'
        self.make_zip()
        self.reject_clean('Primary')

    def test_wrong_outer_digest_and_architecture(self):
        self.make_zip()
        with self.assertRaisesRegex(stage.StageError,'Archive SHA256'):
            self.prepare(digest='0'*64)
        content = pe().replace(b'\x64\x86', b'\x4c\x01')
        self.payloads['payload/cpu/controller.exe'] = content
        self.manifest['components'][0]['files'][0]['sha256'] = stage.sha(content)
        self.make_zip()
        self.reject_clean('architecture')

    def test_duplicate_json_and_duplicate_zip_names(self):
        self.make_zip(raw_manifest=b'{"schema":1,"schema":2}')
        self.reject_clean('Duplicate JSON')
        self.make_zip()
        import warnings
        with warnings.catch_warnings():
            warnings.simplefilter('ignore', UserWarning)
            with zipfile.ZipFile(self.bundle, 'a') as archive:
                archive.writestr('payload/cpu/controller.exe',pe())
        self.reject_clean('Duplicate')

    def test_wallet_and_state_payload_rejected(self):
        for name in ('payload/cpu/wallets/a.exe','payload/cpu/data/a.exe','payload/cpu/a.db',
                     'payload/cpu/wallet.dat','payload/cpu/pid.json','payload/cpu/.cookie'):
            with self.subTest(name=name):
                row = dict(path=name,bytes=len(pe()),sha256=stage.sha(pe()))
                self.manifest['components'][0]['files'].append(row)
                self.payloads[name] = pe()
                self.make_zip()
                self.reject_clean()
                self.manifest['components'][0]['files'].pop()
                self.payloads.pop(name)

    def test_expansion_entry_count_metadata_and_quota_bounds(self):
        self.make_zip(extra={'payload/cpu/bomb.dll':b'0'*1000000},compression=zipfile.ZIP_DEFLATED)
        self.reject_clean('expansion')
        self.make_zip(extra={f'payload/cpu/f{i}.dll':b'1' for i in range(stage.MAX_ENTRIES)})
        self.reject_clean('entry bound')
        self.make_zip(raw_manifest=b' '* (stage.MAX_MANIFEST+1))
        self.reject_clean('bounded')
        name='payload/cpu/large.dll'
        self.payloads[name]=os.urandom(200000)
        self.manifest['components'][0]['files'].append(dict(path=name,bytes=200000,sha256=stage.sha(self.payloads[name])))
        self.make_zip()
        (self.cache/'retained.bin').write_bytes(b'1'*1950000)
        with patch.object(stage,'MAX_CACHE',2*1024*1024):
            self.reject_clean('quota')

    def test_protected_installation_and_unknown_cache_never_written(self):
        with self.assertRaisesRegex(stage.StageError,'overlaps'):
            stage.init_cache(self.install/'cache',self.context)
        self.assertFalse((self.install/'cache').exists())
        with self.assertRaisesRegex(stage.StageError,'overlaps'):
            stage.init_cache(self.install,self.context)
        self.make_zip()
        with self.assertRaises(stage.StageError):
            stage.stage_bundle(self.bundle,stage.sha(self.bundle.read_bytes()),self.context,self.context_hash,self.root,['cpu'])

    def test_cancel_before_and_during_extraction_and_atomic_publish_failure(self):
        self.make_zip()
        for predicate in (lambda:True,lambda:any(self.cache.glob('.pending-*'))):
            with self.assertRaises(stage.Cancelled):
                self.prepare(cancelled=predicate)
            self.assertEqual(sorted(p.name for p in self.cache.iterdir()),['.pgn-stage-cache','.writer.lock'])
        with patch.object(stage.os,'rename',side_effect=OSError('injected publication failure')):
            with self.assertRaises(OSError):
                self.prepare()
        self.assertEqual(sorted(p.name for p in self.cache.iterdir()),['.pgn-stage-cache','.writer.lock'])

    def test_cache_busy_and_existing_stage_preserved(self):
        self.make_zip()
        (self.cache/'.stage-lock').write_bytes(b'busy')
        self.reject_clean('busy')
        (self.cache/'.stage-lock').unlink()
        raw=json.dumps(self.manifest).encode()
        existing=self.cache/stage.sha(raw)[:20]
        existing.mkdir()
        (existing/'sentinel').write_bytes(b'preserve')
        self.reject_clean('already exists')
        self.assertEqual((existing/'sentinel').read_bytes(),b'preserve')

    @unittest.skipUnless(os.name == 'nt', 'Windows sharing-error recovery')
    def test_atomic_publication_recovers_only_bounded_windows_sharing_errors(self):
        self.make_zip();original=stage.os.rename;attempts=[]
        def transient(source,destination):
            attempts.append(1)
            if len(attempts)<3:
                error=PermissionError('simulated transient sharing conflict');error.winerror=32;raise error
            original(source,destination)
        with patch.object(stage.os,'rename',side_effect=transient),patch.object(stage.time,'sleep'):
            folder,plan=self.prepare()
        self.assertEqual(len(attempts),3)
        stage.verify_stage(folder,plan['manifest_sha256'],self.context,self.context_hash)

    @unittest.skipUnless(os.name == 'nt', 'Windows sharing-error recovery')
    def test_permanent_windows_denial_still_fails_cleanly_after_six_attempts(self):
        self.make_zip();error=PermissionError('simulated permanent denial');error.winerror=5
        with patch.object(stage.os,'rename',side_effect=error)as rename,patch.object(stage.time,'sleep'):
            with self.assertRaises(PermissionError):self.prepare()
        self.assertEqual(rename.call_count,6)
        self.assertEqual(sorted(p.name for p in self.cache.iterdir()),['.pgn-stage-cache','.writer.lock'])

    def test_cancel_after_a_real_payload_chunk_was_written(self):
        name='payload/cpu/controller.exe'
        data=pe()+os.urandom(stage.CHUNK)
        self.payloads[name]=data
        row=self.manifest['components'][0]['files'][0]
        row.update(bytes=len(data),sha256=stage.sha(data))
        self.make_zip()
        observed=[]
        def cancel_after_chunk():
            for directory in self.cache.glob('.pending-*'):
                file=directory/name
                if file.exists() and file.stat().st_size >= stage.CHUNK:
                    observed.append(file.stat().st_size)
                    return True
            return False
        with self.assertRaises(stage.Cancelled):
            self.prepare(cancelled=cancel_after_chunk)
        self.assertTrue(observed)
        self.assertEqual(sorted(p.name for p in self.cache.iterdir()),['.pgn-stage-cache','.writer.lock'])

    def test_directory_entry_count_is_bounded_before_zipfile_allocation(self):
        self.make_zip()
        raw=bytearray(self.bundle.read_bytes())
        offset=raw.rfind(b'PK\x05\x06')
        raw[offset+8:offset+12]=(1).to_bytes(2,'little')*2
        self.bundle.write_bytes(raw)
        with patch.object(stage.zipfile,'ZipFile',side_effect=AssertionError('must reject before ZipFile')):
            self.reject_clean('count mismatch')

    def test_insufficient_disk_space_fails_before_extraction(self):
        self.make_zip()
        from collections import namedtuple
        usage=namedtuple('usage','total used free')(100,99,1)
        with patch.object(stage.shutil,'disk_usage',return_value=usage):
            self.reject_clean('disk space')

    def assert_interrupted_write_recovers(self, error, message):
        self.make_zip()
        original_open = Path.open
        class Interrupted:
            def __init__(self, file): self.file = file
            def __getattr__(self, name): return getattr(self.file, name)
            def write(self, data):
                self.file.write(data[:7])
                raise error
        def open_file(path, mode='r', *args, **kwargs):
            file = original_open(path, mode, *args, **kwargs)
            if mode == 'xb' and path.name == 'controller.exe' and any(part.startswith('.pending-') for part in path.parts):
                return Interrupted(file)
            return file
        with patch.object(Path, 'open', open_file):
            self.reject_clean(message)
        self.assertFalse(any(p.name.startswith('.pending-') for p in self.cache.iterdir()))
        folder, plan = self.prepare()
        stage.verify_stage(folder, plan['manifest_sha256'], self.context, self.context_hash)

    def test_disk_fills_after_admission_retains_no_partial_stage_and_retries(self):
        import errno
        self.assert_interrupted_write_recovers(OSError(errno.ENOSPC, 'Injected disk full after first bytes'), 'Insufficient staging disk space')

    def test_write_permission_loss_is_explicit_and_retryable(self):
        import errno
        self.assert_interrupted_write_recovers(PermissionError(errno.EACCES, 'Injected permission loss'), 'could not be written')

    def test_cache_initialization_cancel_removes_partial_marker(self):
        original_open=Path.open
        class InterruptedWriter:
            def __init__(self,file): self.file=file
            def __enter__(self): return self
            def __exit__(self,*args): self.file.close()
            def write(self,data):
                self.file.write(data[:5])
                raise KeyboardInterrupt()
        new_cache=self.root/'interrupted-cache'
        with patch.object(stage.Path,'open',lambda p,*a,**k:InterruptedWriter(original_open(p,*a,**k))):
            with self.assertRaises(KeyboardInterrupt):
                stage.init_cache(new_cache,self.context)
        self.assertFalse(new_cache.exists())

    def test_verify_detects_payload_plan_context_extra_directory_tampering(self):
        self.make_zip()
        folder,plan=self.prepare()
        payload=folder/'payload/cpu/controller.exe'
        payload.write_bytes(b'X'*len(pe()))
        with self.assertRaisesRegex(stage.StageError,'payload changed'):
            stage.verify_stage(folder,plan['manifest_sha256'],self.context,self.context_hash)
        payload.write_bytes(pe())
        (folder/'empty-extra').mkdir()
        with self.assertRaisesRegex(stage.StageError,'directory'):
            stage.verify_stage(folder,plan['manifest_sha256'],self.context,self.context_hash)
        (folder/'empty-extra').rmdir()
        plan['activation_blockers']=[]
        (folder/'plan.json').write_text(json.dumps(plan))
        with self.assertRaisesRegex(stage.StageError,'plan changed'):
            stage.verify_stage(folder,plan['manifest_sha256'],self.context,self.context_hash)
        with self.assertRaisesRegex(stage.StageError,'context identity'):
            stage.verify_stage(folder,plan['manifest_sha256'],self.context,'0'*64)

    def test_linked_cache_ancestor_if_platform_supports_it(self):
        target=self.root/'real-directory'
        target.mkdir()
        link=self.root/'directory-link'
        try:
            os.symlink(target,link,target_is_directory=True)
        except OSError as exc:
            self.skipTest('OS symlink privilege unavailable: '+str(exc.winerror if hasattr(exc,'winerror') else exc.errno))
        with self.assertRaisesRegex(stage.StageError,'reparse'):
            stage.init_cache(link/'cache',self.context)

    def test_cli_prepare_verify_and_absent_activation_command(self):
        digest=self.make_zip()
        base=[sys.executable,'-I','-Xutf8',str(MODULE),'--context',str(self.context_path)]
        result=subprocess.run(base+['prepare',str(self.bundle),'--sha256',digest,'--cache',str(self.cache),'--component','cpu'],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        receipt=json.loads(result.stdout)
        result=subprocess.run(base+['verify-stage',receipt['stage'],'--manifest-sha256',receipt['plan']['manifest_sha256']],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertFalse(json.loads(result.stdout)['plan']['activation_allowed'])
        result=subprocess.run(base+['activate'],capture_output=True,text=True)
        self.assertEqual(result.returncode,2)


if __name__ == '__main__':
    unittest.main(verbosity=2)

