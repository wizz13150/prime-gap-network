from pathlib import Path
import copy
import hashlib
import importlib.util
import json
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('component_manifest', ROOT / 'tools/generate_component_manifest.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class ComponentManifestTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        (self.root / 'program.bin').write_bytes(b'inert code fixture')
        self.spec = dict(release_version='0.1.0-dev', network='testnet', published=False,
                         components=[dict(id='ui', version='0.1.0-dev', root=str(self.root),
                                          files=['program.bin'])])

    def tearDown(self):
        self.temporary.cleanup()

    def test_declared_version_bound_to_exact_artifact_without_exporting_local_root(self):
        result = module.build(self.spec)
        self.assertNotIn(str(self.root), json.dumps(result))
        self.assertEqual(result['components'][0]['version'], '0.1.0-dev')
        self.assertEqual(result['components'][0]['files'][0]['sha256'], hashlib.sha256(b'inert code fixture').hexdigest())
        self.assertFalse(result['published'])

    def test_missing_version_is_not_guessed_from_program_name(self):
        del self.spec['components'][0]['version']
        with self.assertRaises(ValueError):
            module.build(self.spec)

    def test_mutable_wallet_or_credential_files_are_not_components(self):
        for path in ('wallet.dat', 'wallets/default', '.cookie', 'history.sqlite', 'secret.key'):
            self.spec['components'][0]['files'] = [path]
            with self.assertRaises(ValueError):
                module.build(self.spec)

    def test_path_traversal_or_ads_is_rejected(self):
        for path in ('../secret', '/absolute', 'C:/absolute', 'file:stream', 'dir\\file', 'a//b'):
            self.spec['components'][0]['files'] = [path]
            with self.assertRaises(ValueError):
                module.build(self.spec)

    def test_duplicate_component_or_case_aliased_path_is_rejected(self):
        self.spec['components'].append(copy.deepcopy(self.spec['components'][0]))
        with self.assertRaises(ValueError):
            module.build(self.spec)
        self.spec['components'].pop()
        self.spec['components'][0]['files'] = ['program.bin', 'PROGRAM.BIN']
        with self.assertRaises(ValueError):
            module.build(self.spec)

    def test_no_system_root_or_unknown_component(self):
        self.spec['components'][0]['root'] = self.root.anchor
        with self.assertRaises(ValueError):
            module.build(self.spec)
        self.spec['components'][0]['root'] = str(self.root)
        self.spec['components'][0]['id'] = 'wallet'
        with self.assertRaises(ValueError):
            module.build(self.spec)


if __name__ == '__main__':
    unittest.main()
