import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('native_cuda_builder', ROOT / 'tools/build_native_cuda.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
SOURCE = ROOT / 'src/engine/prime_gap_native'


class NativeCudaBuilderTests(unittest.TestCase):
    def fixture(self):
        temporary = tempfile.TemporaryDirectory(prefix='pgn-native-auth-')
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name).resolve()
        self.assertTrue(root.is_absolute() and root.name.startswith('pgn-native-auth-'))
        source = root / 'source'
        source.mkdir()
        # Existing inputs are immutable; a tamper is installed atomically into
        # the private fixture so no write can reach a source hardlink.
        for path in SOURCE.iterdir():
            if path.is_file():
                try:
                    os.link(path, source / path.name)
                except OSError:
                    shutil.copy2(path, source / path.name)
        return root, source

    def replace(self, target, data):
        private = target.with_suffix(target.suffix + '.private')
        private.write_bytes(data)
        private.replace(target)

    def test_current_published_inputs_authenticate(self):
        self.assertEqual(builder.authenticate_native_source(SOURCE), builder.SOURCE_ID)

    def test_changed_source_is_rejected(self):
        _, source = self.fixture()
        target = source / 'search_pipeline.cuh'
        self.replace(target, target.read_bytes() + b'\n')
        with self.assertRaisesRegex(ValueError, 'Native input authentication failed: search_pipeline.cuh'):
            builder.authenticate_native_source(source)

    def test_changed_manifest_is_rejected(self):
        _, source = self.fixture()
        target = source / 'SCIENTIFIC-SOURCE-INPUTS.json'
        self.replace(target, target.read_bytes() + b' ')
        with self.assertRaisesRegex(ValueError, 'manifest differs'):
            builder.authenticate_native_source(source)

    def test_dry_run_keeps_proof_authentication_and_unqualified_outputs(self):
        root, _ = self.fixture()
        proof = root / 'proof'
        proof.mkdir()
        payload = b'isolated dry-run fixture, never linked or executed'
        (proof / 'fixture.a').write_bytes(payload)
        manifest = {'libraries': [{'file': 'fixture.a', 'bytes': len(payload),
                                  'sha256': hashlib.sha256(payload).hexdigest()}]}
        (proof / 'manifest.json').write_text(json.dumps(manifest))
        output = root / 'output'
        args = [sys.executable, '-I', str(ROOT / 'tools/build_native_cuda.py'),
                '--output', str(output), '--proof-runtime', str(proof),
                '--architectures', '86', '--dry-run']
        result = subprocess.run(args, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        summary = json.loads(result.stdout)
        self.assertFalse(summary['qualified'])
        self.assertTrue(summary['dry_run'])
        self.assertEqual(len(summary['outputs']), 1)
        self.assertEqual(list(output.iterdir()), [])
        (proof / 'fixture.a').write_bytes(b'tampered')
        result = subprocess.run(args, capture_output=True, text=True, timeout=30)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Exact proof library authentication failed', result.stderr)


if __name__ == '__main__':
    unittest.main()
