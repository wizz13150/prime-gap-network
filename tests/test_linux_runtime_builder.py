"""Check source-build inventory against the assembled files, without running them."""
from pathlib import Path
import hashlib
import importlib.util
import json
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
spec=importlib.util.spec_from_file_location('linux_runtime_builder',ROOT/'tools/build_linux_runtime.py')
builder=importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class LinuxRuntimeBuilderTests(unittest.TestCase):
    def test_source_declaration_drives_every_inventory_row(self):
        with tempfile.TemporaryDirectory()as folder:
            root=Path(folder)
            versions=dict(ui='2099.1.1.7',core='2099.1.1.3',gpu='2099.1.1.9',cpu='2099.1.1.2')
            declaration=root/'versions.json'
            declaration.write_text(json.dumps(versions),encoding='utf-8')
            versions=builder.load_versions(declaration)
            files={'PGN-Horizon':b'new UI','Core/pgn-core':b'new Core',
                   'EngineRuntime/network/horizon-engine-host':b'new host',
                   'EngineRuntime/cpu/horizon-cpu-controller':b'new CPU'}
            runtime=root/'runtime'
            for name,data in files.items():
                path=runtime/name
                path.parent.mkdir(parents=True,exist_ok=True)
                path.write_bytes(data)
            (runtime/'components.json').write_text('{"old":"inventory"}',encoding='utf-8')
            manifest=builder.write_inventory(runtime,versions)
            self.assertEqual(json.loads((runtime/'components.json').read_text()),manifest)
            self.assertEqual(manifest['release_version'],versions['ui'])
            self.assertEqual({x['id']:x['version']for x in manifest['components']},versions)
            prefixes=dict(ui='',core='Core/',gpu='EngineRuntime/',cpu='EngineRuntime/cpu/')
            observed={prefixes[c['id']]+f['path']:f for c in manifest['components']for f in c['files']}
            self.assertEqual(set(observed),set(files))
            for name,data in files.items():
                self.assertEqual(observed[name]['bytes'],len(data))
                self.assertEqual(observed[name]['sha256'],hashlib.sha256(data).hexdigest())

    def test_incomplete_or_invalid_declaration_cannot_label_a_build(self):
        with tempfile.TemporaryDirectory()as folder:
            path=Path(folder)/'versions.json'
            for change in ({'gpu':None},{'ui':'bad version'},{'core':''},{'extra':'1'}):
                versions=dict(builder.VERSIONS)
                versions.update(change)
                path.write_text(json.dumps(versions),encoding='utf-8')
                with self.subTest(change=change),self.assertRaises(ValueError):builder.load_versions(path)
            versions.pop('cpu')
            path.write_text(json.dumps(versions),encoding='utf-8')
            with self.assertRaises(ValueError):builder.load_versions(path)


if __name__=='__main__':unittest.main()
